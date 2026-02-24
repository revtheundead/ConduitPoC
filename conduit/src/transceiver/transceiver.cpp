// SPDX-License-Identifier: MIT
// Conduit - Transceiver (implementation)

#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/serial.hpp>
#include <conduit/logging/logger.hpp>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace conduit::transceiver {

// ============================================================================
// Construction / Destruction
// ============================================================================

Transceiver::Transceiver(TransceiverConfig config)
    : config_(std::move(config)) {
    if (config_.message_log.enabled) {
        message_log_ = std::make_unique<MessageLog>(config_.message_log);
    }

    // Materialize any config-driven peers
    auto result = materialize_config_peers();
    if (!result) {
        throw std::runtime_error(
            "Failed to materialize config peers: " +
            result.error().format_short());
    }
}

Transceiver::~Transceiver() {
    stop();
}

// ============================================================================
// Transport factory
// ============================================================================

std::shared_ptr<transport::ITransport>
Transceiver::make_transport(const TransportConfig& cfg) {
    return std::visit([](const auto& c) -> std::shared_ptr<transport::ITransport> {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, transport::UdpConfig>) {
            return std::make_shared<transport::UdpTransport>(c);
        } else if constexpr (std::is_same_v<T, transport::TcpClientConfig>) {
            return std::make_shared<transport::TcpClientTransport>(c);
        } else if constexpr (std::is_same_v<T, transport::TcpServerConfig>) {
            return std::make_shared<transport::TcpServerTransport>(c);
        } else if constexpr (std::is_same_v<T, transport::SerialConfig>) {
            return std::make_shared<transport::SerialTransport>(c);
        } else {
            static_assert(sizeof(T) == 0, "Unhandled transport config type");
        }
    }, cfg);
}

bool Transceiver::is_multi_peer_config(const TransportConfig& cfg) {
    return std::visit([](const auto& c) -> bool {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, transport::UdpConfig>) {
            // Multi-peer when no remote_address (receiver mode)
            return c.remote_address.empty();
        } else if constexpr (std::is_same_v<T, transport::TcpServerConfig>) {
            return true;
        } else {
            return false;  // TcpClient, Serial are single-peer
        }
    }, cfg);
}

VoidResult Transceiver::materialize_config_peers() {
    for (auto& peer_cfg : config_.peers) {
        auto transport = make_transport(peer_cfg.transport);

        if (is_multi_peer_config(peer_cfg.transport)) {
            // Multi-peer: use session factory overload
            auto result = add_peer(
                peer_cfg.name,
                peer_cfg.session_factory,
                std::move(transport));
            if (!result) return std::unexpected(result.error());
        } else {
            // Single-peer: create one session instance
            auto session = peer_cfg.session_factory();
            if (!session) {
                return std::unexpected(CONDUIT_ERROR(
                    ErrorCode::InvalidArgument,
                    std::format("Session factory returned null for peer '{}'",
                                peer_cfg.name)));
            }
            auto result = add_peer(
                peer_cfg.name,
                std::move(session),
                std::move(transport));
            if (!result) return std::unexpected(result.error());
        }
    }
    return {};
}

// ============================================================================
// Peer management
// ============================================================================

Result<PeerId> Transceiver::add_peer(
    std::string name,
    std::unique_ptr<traits::ISession> session,
    std::shared_ptr<transport::ITransport> transport) {

    CONDUIT_ENSURE(!running_, ErrorCode::AlreadyRunning,
                   "Cannot add peers while transceiver is running");
    CONDUIT_ENSURE(session != nullptr, ErrorCode::InvalidArgument,
                   "Session must not be null");
    CONDUIT_ENSURE(transport != nullptr, ErrorCode::InvalidArgument,
                   "Transport must not be null");
    CONDUIT_ENSURE(!transport->is_multi_peer(), ErrorCode::InvalidArgument,
                   std::format("Single-session add_peer rejects multi-peer transport "
                               "for peer '{}'; use the SessionFactory overload", name));

    auto id = next_peer_id();
    auto ctx = std::make_unique<PeerContext>();
    ctx->id = id;
    ctx->name = name;

    // Stream-oriented transports get a framer
    if (transport->is_stream_oriented()) {
        ctx->framer = std::make_unique<StreamFramer>(*session);
    }

    ctx->session = std::move(session);
    ctx->transport = transport;

    {
        std::unique_lock lock(peers_mutex_);
        CONDUIT_ENSURE(name_to_peer_.find(ctx->name) == name_to_peer_.end(),
                       ErrorCode::InvalidArgument,
                       std::format("Duplicate peer name '{}'", ctx->name));
        peers_.push_back(std::move(ctx));
        peer_map_[id.value()] = peers_.back().get();
        name_to_peer_.emplace(std::move(name), id);

        // Track transport for lifecycle (under lock to protect transports_)
        if (std::find(transports_.begin(), transports_.end(), transport)
            == transports_.end()) {
            transports_.push_back(std::move(transport));
        }
    }

    return id;
}

Result<PeerId> Transceiver::add_peer(
    std::string name,
    SessionFactory session_factory,
    std::shared_ptr<transport::ITransport> transport) {

    CONDUIT_ENSURE(!running_, ErrorCode::AlreadyRunning,
                   "Cannot add peers while transceiver is running");
    CONDUIT_ENSURE(session_factory != nullptr, ErrorCode::InvalidArgument,
                   "Session factory must not be null");
    CONDUIT_ENSURE(transport != nullptr, ErrorCode::InvalidArgument,
                   "Transport must not be null");
    CONDUIT_ENSURE(transport->is_multi_peer(), ErrorCode::InvalidArgument,
                   "Session factory overload requires a multi-peer transport");

    auto id = next_peer_id();

    MultiPeerEntry entry;
    entry.base_id = id;
    entry.name = name;
    entry.session_factory = std::move(session_factory);
    entry.transport = transport;

    {
        std::unique_lock lock(peers_mutex_);
        CONDUIT_ENSURE(name_to_peer_.find(name) == name_to_peer_.end(),
                       ErrorCode::InvalidArgument,
                       std::format("Duplicate peer name '{}'", name));
        multi_peer_entries_.push_back(std::move(entry));
        name_to_peer_.emplace(std::move(name), id);

        // Track transport for lifecycle (under lock to protect transports_)
        if (std::find(transports_.begin(), transports_.end(), transport)
            == transports_.end()) {
            transports_.push_back(std::move(transport));
        }
    }

    return id;
}

Result<PeerId> Transceiver::sole_peer() const {
    std::shared_lock lock(peers_mutex_);

    // Only count actual peers, not multi-peer transport registrations.
    // A MultiPeerEntry is a listener/factory — not a connected peer.
    if (peers_.size() == 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::PeerNotFound, "No peers registered"));
    }
    if (peers_.size() > 1) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::MultiplePeers,
                          "Multiple peers exist; specify PeerId explicitly"));
    }

    return peers_.front()->id;
}

Result<PeerId> Transceiver::peer(std::string_view name) const {
    std::shared_lock lock(peers_mutex_);
    auto it = name_to_peer_.find(std::string(name));
    if (it == name_to_peer_.end()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::PeerNotFound,
                          std::format("No peer named '{}'", name)));
    }
    return it->second;
}

// ============================================================================
// Handler installation
// ============================================================================

void Transceiver::set_handler(PeerId peer, MessageHandler handler) {
    handlers_.install_handler(peer, handler);
}

void Transceiver::set_handler(MessageHandler handler) {
    handlers_.install_handler(handler);
}

// ============================================================================
// State change callback
// ============================================================================

CallbackId Transceiver::on_state_change(ConnectionStateCallback cb) {
    std::lock_guard lock(state_cb_mutex_);
    auto id = CallbackId{next_callback_id_.fetch_add(1)};
    state_callbacks_.push_back({id, std::move(cb)});
    return id;
}

bool Transceiver::remove_state_change(CallbackId id) {
    std::lock_guard lock(state_cb_mutex_);
    auto it = std::find_if(state_callbacks_.begin(), state_callbacks_.end(),
        [id](const StateCallbackEntry& e) { return e.id == id; });
    if (it == state_callbacks_.end()) return false;
    state_callbacks_.erase(it);
    return true;
}

CallbackId Transceiver::on_error(ErrorCallback cb) {
    std::lock_guard lock(error_cb_mutex_);
    auto id = CallbackId{next_callback_id_.fetch_add(1)};
    error_callbacks_.push_back({id, std::move(cb)});
    return id;
}

bool Transceiver::remove_error_callback(CallbackId id) {
    std::lock_guard lock(error_cb_mutex_);
    auto it = std::find_if(error_callbacks_.begin(), error_callbacks_.end(),
        [id](const ErrorCallbackEntry& e) { return e.id == id; });
    if (it == error_callbacks_.end()) return false;
    error_callbacks_.erase(it);
    return true;
}

void Transceiver::fire_error(PeerId peer, Error error) {
    std::string name, endpoint;
    {
        std::shared_lock lock(peers_mutex_);
        if (auto* ctx = find_peer(peer)) {
            name = ctx->name;
            endpoint = ctx->remote_endpoint;
        }
    }
    fire_error_event(ErrorEvent{peer, std::move(name), std::move(endpoint), std::move(error)});
}

void Transceiver::fire_error_event(ErrorEvent event) {
    std::vector<ErrorCallbackEntry> cbs;
    {
        std::lock_guard lock(error_cb_mutex_);
        cbs = error_callbacks_;
    }
    for (auto& entry : cbs) {
        try {
            entry.cb(event);
        } catch (const std::exception& e) {
            LOG_ERRORF("Error callback threw: {}", e.what());
        } catch (...) {
            LOG_ERROR("Error callback threw unknown exception");
        }
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

VoidResult Transceiver::start() {
    CONDUIT_ENSURE(!running_, ErrorCode::AlreadyRunning,
                   "Transceiver is already running");

    double bp = config_.rx_queue.back_pressure_threshold;
    CONDUIT_ENSURE(bp >= 0.0 && bp <= 1.0, ErrorCode::InvalidArgument,
                   std::format("back_pressure_threshold must be in [0.0, 1.0], got {}", bp));

    // Create dispatch queue
    dispatch_queue_ = std::make_unique<queue::BoundedQueue<InboundMessage>>(
        config_.rx_queue.capacity, config_.rx_queue.drop_policy);

    running_ = true;

    // Start worker threads BEFORE transports so messages have consumers
    size_t thread_count = config_.worker.thread_count;
    if (thread_count == 0) thread_count = 1;
    workers_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }

    // Start all transports
    for (auto& transport : transports_) {
        transport::TransportCallbacks cb;

        cb.on_data_received = [this](PeerId peer, std::span<const uint8_t> data) {
            handle_data_received(peer, data);
        };
        cb.on_peer_connected = [this, tp = transport.get()](std::string endpoint) -> PeerId {
            return handle_peer_connected(tp, std::move(endpoint));
        };
        cb.on_peer_disconnected = [this](PeerId peer) {
            handle_peer_disconnected(peer);
        };
        cb.on_state_changed = [this](PeerId peer, net::ConnectionState state) {
            handle_state_changed(peer, state);
        };

        auto result = transport->start(std::move(cb));
        if (!result) {
            // Stop already-started transports on failure
            stop();
            return result;
        }
    }

    return {};
}

void Transceiver::stop() {
    if (!running_.exchange(false)) {
        // Wasn't running, but still clean up queue/workers from partial start
        if (dispatch_queue_) {
            dispatch_queue_->close();
        }
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();

        for (auto& transport : transports_) {
            transport->stop();
        }
        return;
    }

    // Stop all transports FIRST so no new messages arrive
    for (auto& transport : transports_) {
        transport->stop();
    }

    // Close dispatch queue (unblocks workers after they drain remaining messages)
    if (dispatch_queue_) {
        dispatch_queue_->close();
    }

    // Wait for workers with optional timeout
    auto timeout = config_.shutdown_timeout;
    if (timeout.count() > 0) {
        std::unique_lock lock(shutdown_mutex_);
        bool drained = shutdown_cv_.wait_for(lock, timeout,
            [this] { return active_workers_.load(std::memory_order_acquire) == 0; });
        if (!drained) {
            LOG_WARNF("Shutdown timeout ({}ms): {} workers still active, waiting for completion",
                      timeout.count(), active_workers_.load(std::memory_order_acquire));
        }
    }

    // Always join worker threads — never detach, as detached threads would
    // access destroyed members (dispatch_queue_, handlers_, stats_, etc.)
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

bool Transceiver::is_running() const noexcept {
    return running_.load();
}

// ============================================================================
// Query
// ============================================================================

net::ConnectionState Transceiver::peer_state(PeerId peer) const {
    std::shared_lock lock(peers_mutex_);
    auto* ctx = find_peer(peer);
    if (!ctx) return net::ConnectionState::Disconnected;
    return ctx->state.load();
}

size_t Transceiver::peer_count() const {
    std::shared_lock lock(peers_mutex_);
    return peers_.size();
}

std::vector<PeerId> Transceiver::peer_ids() const {
    std::shared_lock lock(peers_mutex_);
    std::vector<PeerId> ids;
    ids.reserve(peers_.size());
    for (const auto& ctx : peers_) {
        ids.push_back(ctx->id);
    }
    return ids;
}

// ============================================================================
// Send pipeline
// ============================================================================

VoidResult Transceiver::send_impl(PeerId peer, uint64_t type_id,
                                  const std::any& payload) {
    CONDUIT_ENSURE(running_, ErrorCode::NotRunning,
                   "Transceiver is not running");

    std::shared_lock lock(peers_mutex_);
    auto* ctx = find_peer(peer);
    CONDUIT_ENSURE(ctx != nullptr, ErrorCode::PeerNotFound,
                   std::format("Peer {} not found", peer.value()));

    // Copy transport shared_ptr while lock is held to prevent
    // use-after-free if peer is erased by I/O thread after unlock.
    auto transport = ctx->transport;

    // Block sending receive-only message types
    if (ctx->session->is_receive_only(type_id)) {
        auto name = ctx->session->type_name(type_id);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::DirectionViolation,
                          std::format("Cannot send receive-only message type '{}'", name)));
    }

    // Encode via session under per-peer lock (session is not thread-safe)
    std::vector<uint8_t> encoded;
    std::string log_content;
    std::string log_type_name;
    std::string log_protocol;
    std::string log_peer_name;
    std::string log_remote;
    std::string_view log_transport;
    {
        std::lock_guard ctx_lock(ctx->ctx_mutex);
        CONDUIT_TRY_ASSIGN(auto, enc,
                           ctx->session->encode_wrap(type_id, payload));
        encoded = std::move(enc.bytes);

        if (message_log_) {
            log_type_name = std::string(ctx->session->type_name(type_id));
            if (config_.message_log.include_message_content) {
                log_content = ctx->session->format_outbound(
                    type_id, payload, enc.auto_fields);
            }
            log_protocol = std::string(ctx->session->protocol_name());
            log_peer_name = ctx->name;
            log_remote = ctx->remote_endpoint;
            log_transport = ctx->transport->transport_type();
        }
    }

    lock.unlock();

    if (message_log_) {
        message_log_->log_send(log_peer_name, log_remote, log_type_name,
                               encoded.size(), log_content, log_protocol,
                               log_transport);
    }

    // Send via transport
    stats_.bytes_sent.fetch_add(encoded.size(), std::memory_order_relaxed);
    return transport->send(peer, encoded);
}

VoidResult Transceiver::send_batch_impl(PeerId peer, uint64_t type_id,
                                        std::span<const std::any> payloads) {
    CONDUIT_ENSURE(running_, ErrorCode::NotRunning,
                   "Transceiver is not running");
    CONDUIT_ENSURE(!payloads.empty(), ErrorCode::InvalidArgument,
                   "Batch must contain at least one message");

    std::shared_lock lock(peers_mutex_);
    auto* ctx = find_peer(peer);
    CONDUIT_ENSURE(ctx != nullptr, ErrorCode::PeerNotFound,
                   std::format("Peer {} not found", peer.value()));

    auto transport = ctx->transport;

    if (ctx->session->is_receive_only(type_id)) {
        auto name = ctx->session->type_name(type_id);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::DirectionViolation,
                          std::format("Cannot send receive-only message type '{}'", name)));
    }

    std::vector<uint8_t> encoded;
    std::string log_content;
    std::string log_type_name;
    std::string log_protocol;
    std::string log_peer_name;
    std::string log_remote;
    std::string_view log_transport;
    {
        std::lock_guard ctx_lock(ctx->ctx_mutex);
        CONDUIT_TRY_ASSIGN(auto, enc,
                           ctx->session->encode_batch(type_id, payloads));
        encoded = std::move(enc.bytes);

        if (message_log_) {
            log_type_name = std::string(ctx->session->type_name(type_id));
            if (config_.message_log.include_message_content) {
                for (size_t i = 0; i < payloads.size(); ++i) {
                    if (i > 0) log_content += '\n';
                    log_content += ctx->session->format_outbound(
                        type_id, payloads[i], enc.auto_fields);
                }
            }
            log_protocol = std::string(ctx->session->protocol_name());
            log_peer_name = ctx->name;
            log_remote = ctx->remote_endpoint;
            log_transport = ctx->transport->transport_type();
        }
    }

    lock.unlock();

    if (message_log_) {
        message_log_->log_send(log_peer_name, log_remote, log_type_name,
                               encoded.size(), log_content, log_protocol,
                               log_transport);
    }

    stats_.bytes_sent.fetch_add(encoded.size(), std::memory_order_relaxed);
    return transport->send(peer, encoded);
}

// ============================================================================
// Receive pipeline (I/O thread callbacks)
// ============================================================================

void Transceiver::handle_data_received(PeerId peer,
                                       std::span<const uint8_t> data) {
    stats_.bytes_received.fetch_add(data.size(), std::memory_order_relaxed);

    // Decode under locks, but collect messages and push to queue AFTER
    // releasing ctx_mutex.  This prevents deadlock when DropPolicy::Block
    // causes push() to block while a worker's handler sends to the same peer
    // (which needs ctx_mutex for encode).
    std::vector<InboundMessage> to_enqueue;

    struct RecvLogEntry {
        std::string peer_name;
        std::string remote_endpoint;
        std::string type_name;
        size_t byte_count;
        std::string content;
        std::string protocol;
        std::string transport;
    };
    std::vector<RecvLogEntry> log_entries;

    // Collect error events under lock; fire them after releasing all locks.
    std::vector<ErrorEvent> deferred_errors;

    {
        std::shared_lock lock(peers_mutex_);
        auto* ctx = find_peer(peer);
        if (!ctx) return;

        std::lock_guard ctx_lock(ctx->ctx_mutex);

        // Collect logging metadata once (shared across all messages in this callback)
        std::string log_peer_name;
        std::string log_remote;
        std::string log_protocol;
        std::string log_transport_str;
        if (message_log_) {
            log_peer_name = ctx->name;
            log_remote = ctx->remote_endpoint;
            log_protocol = std::string(ctx->session->protocol_name());
            log_transport_str = std::string(ctx->transport->transport_type());
        }

        auto log_decoded_messages = [&](const std::vector<traits::DecodedMessage>& msgs,
                                        size_t frame_bytes) {
            if (!message_log_) return;
            for (const auto& msg : msgs) {
                RecvLogEntry entry;
                entry.peer_name = log_peer_name;
                entry.remote_endpoint = log_remote;
                entry.type_name = std::string(msg.type_name);
                entry.byte_count = frame_bytes;
                if (config_.message_log.include_message_content) {
                    entry.content = ctx->session->format_message(msg.type_id, msg.payload);
                }
                entry.protocol = log_protocol;
                entry.transport = log_transport_str;
                log_entries.push_back(std::move(entry));
            }
        };

        // Helper to defer an error event (safe under lock — no callback invocation)
        auto defer_error = [&](Error err) {
            deferred_errors.push_back(ErrorEvent{peer, ctx->name, ctx->remote_endpoint, std::move(err)});
        };

        if (ctx->framer) {
            // Stream transport: extract frames via framer
            auto frames_result = ctx->framer->push_data(data);
            if (!frames_result) {
                stats_.decode_errors.fetch_add(1, std::memory_order_relaxed);
                LOG_ERRORF("Framing error: peer='{}' remote={} proto={} error={}",
                         ctx->name, ctx->remote_endpoint,
                         ctx->session->protocol_name(),
                         frames_result.error().format_short());
                defer_error(frames_result.error().with_context("framing"));
                ctx->framer->reset();
                // Fall through to fire deferred errors after lock release
            } else {
                for (auto& frame : *frames_result) {
                    auto decoded = ctx->session->decode_frame(frame);
                    if (!decoded) {
                        stats_.decode_errors.fetch_add(1, std::memory_order_relaxed);
                        LOG_ERRORF("Decode error: peer='{}' remote={} proto={} frame_bytes={} error={}",
                                 ctx->name, ctx->remote_endpoint,
                                 ctx->session->protocol_name(),
                                 frame.size(),
                                 decoded.error().format_short());
                        defer_error(decoded.error().with_context("decode"));
                        continue;
                    }
                    log_decoded_messages(*decoded, frame.size());
                    for (auto& msg : *decoded) {
                        stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
                        to_enqueue.push_back(InboundMessage{peer, std::move(msg)});
                    }
                }
            }
        } else {
            // Datagram transport: entire data span is one frame
            auto decoded = ctx->session->decode_frame(data);
            if (!decoded) {
                stats_.decode_errors.fetch_add(1, std::memory_order_relaxed);
                LOG_ERRORF("Decode error: peer='{}' remote={} proto={} frame_bytes={} error={}",
                         ctx->name, ctx->remote_endpoint,
                         ctx->session->protocol_name(),
                         data.size(),
                         decoded.error().format_short());
                defer_error(decoded.error().with_context("decode"));
                // Fall through to fire deferred errors after lock release
            } else {
                log_decoded_messages(*decoded, data.size());
                for (auto& msg : *decoded) {
                    stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
                    to_enqueue.push_back(InboundMessage{peer, std::move(msg)});
                }
            }
        }
    }

    // Fire deferred error events outside all locks
    for (auto& evt : deferred_errors) {
        fire_error_event(std::move(evt));
    }

    // Log outside all locks
    for (const auto& entry : log_entries) {
        message_log_->log_recv(entry.peer_name, entry.remote_endpoint,
                               entry.type_name, entry.byte_count,
                               entry.content, entry.protocol, entry.transport);
    }

    // Push to queue outside all locks — safe to block here
    for (auto& inbound : to_enqueue) {
        auto type_id = inbound.decoded.type_id;
        if (!dispatch_queue_->push(std::move(inbound))) {
            stats_.messages_dropped.fetch_add(1, std::memory_order_relaxed);
            LOG_WARNF("Dispatch queue dropped message type_id={} from peer {}",
                      type_id, peer.value());
            fire_error(peer, Error(ErrorCode::QueueFull,
                std::format("dispatch queue dropped message type_id={}", type_id)));
        }
    }

    // Back-pressure: pause transport if queue fill exceeds threshold
    double threshold = config_.rx_queue.back_pressure_threshold;
    if (threshold > 0.0 && dispatch_queue_) {
        double fill = static_cast<double>(dispatch_queue_->size()) /
                      static_cast<double>(config_.rx_queue.capacity);
        if (fill >= threshold) {
            std::shared_lock lock(peers_mutex_);
            auto* ctx = find_peer(peer);
            if (ctx) ctx->transport->pause();
        }
    }
}

PeerId Transceiver::handle_peer_connected(transport::ITransport* transport,
                                          std::string remote_endpoint) {
    // Single-peer transport: return the pre-existing PeerId
    if (!transport->is_multi_peer()) {
        std::unique_lock lock(peers_mutex_);
        for (auto& ctx : peers_) {
            if (ctx->transport.get() == transport) {
                ctx->remote_endpoint = std::move(remote_endpoint);
                return ctx->id;
            }
        }
        return PeerId{};
    }

    // Multi-peer transport: create a new PeerContext and insert atomically
    // under a single unique_lock to prevent race between lookup and insert.
    PeerId id;
    {
        std::unique_lock lock(peers_mutex_);
        auto* entry = find_multi_peer_entry(transport);
        if (!entry) {
            LOG_WARN("Peer connected on unknown multi-peer transport");
            return PeerId{};
        }

        id = next_peer_id();
        auto session = entry->session_factory();
        if (!session) {
            LOG_ERROR("Session factory returned null");
            std::string name = entry->name;
            lock.unlock();  // Release lock before firing error callback
            fire_error_event(ErrorEvent{PeerId{}, std::move(name), {},
                Error(ErrorCode::InternalError, "session factory returned null")});
            return PeerId{};
        }

        auto ctx = std::make_unique<PeerContext>();
        ctx->id = id;
        ctx->name = std::format("{}/{}", entry->name, entry->next_child++);
        ctx->remote_endpoint = std::move(remote_endpoint);

        if (transport->is_stream_oriented()) {
            ctx->framer = std::make_unique<StreamFramer>(*session);
        }

        ctx->session = std::move(session);
        ctx->transport = entry->transport;
        ctx->state.store(net::ConnectionState::Connected);

        peers_.push_back(std::move(ctx));
        peer_map_[id.value()] = peers_.back().get();
        name_to_peer_[peers_.back()->name] = id;
    }

    handle_state_changed(id, net::ConnectionState::Connected);
    return id;
}

void Transceiver::handle_peer_disconnected(PeerId peer) {
    // Set disconnected state + remove dynamic peer atomically under one lock
    bool is_dynamic_peer = false;
    {
        std::unique_lock lock(peers_mutex_);
        auto* ctx = find_peer(peer);
        if (ctx) {
            ctx->state.store(net::ConnectionState::Disconnected);
        }

        auto it = std::find_if(peers_.begin(), peers_.end(),
            [peer](const auto& c) { return c->id == peer; });
        if (it != peers_.end()) {
            // Check if this is a dynamic peer (has a matching multi-peer entry)
            auto* entry = find_multi_peer_entry((*it)->transport.get());
            if (entry) {
                is_dynamic_peer = true;
                // Also clean up name_to_peer_ for this dynamic peer
                for (auto nit = name_to_peer_.begin(); nit != name_to_peer_.end(); ++nit) {
                    if (nit->second == peer) {
                        name_to_peer_.erase(nit);
                        break;
                    }
                }
                peer_map_.erase(peer.value());
                peers_.erase(it);
            }
        }
    }

    // Only clean up per-peer handlers for dynamic peers (e.g. TCP server clients).
    // Static peers (TCP client) keep their handlers across reconnects.
    if (is_dynamic_peer) {
        handlers_.remove_peer(peer);
    }

    // Notify callbacks outside all locks
    std::vector<StateCallbackEntry> cbs;
    {
        std::lock_guard lock(state_cb_mutex_);
        cbs = state_callbacks_;
    }
    for (auto& entry : cbs) {
        try {
            entry.cb(peer, net::ConnectionState::Disconnected);
        } catch (const std::exception& e) {
            LOG_ERRORF("State callback threw: {}", e.what());
        } catch (...) {
            LOG_ERROR("State callback threw unknown exception");
        }
    }
}

void Transceiver::handle_state_changed(PeerId peer,
                                       net::ConnectionState state) {
    {
        std::shared_lock lock(peers_mutex_);
        auto* ctx = find_peer(peer);
        if (ctx) {
            // Reset session and framer on (re)connect to clear stale state
            // BEFORE making Connected state visible, so no thread can
            // send on a stale session.
            if (state == net::ConnectionState::Connected) {
                std::lock_guard ctx_lock(ctx->ctx_mutex);
                ctx->session->reset();
                if (ctx->framer) {
                    ctx->framer->reset();
                }
            }

            ctx->state.store(state);
        }
    }

    std::vector<StateCallbackEntry> cbs;
    {
        std::lock_guard lock(state_cb_mutex_);
        cbs = state_callbacks_;
    }

    for (auto& entry : cbs) {
        try {
            entry.cb(peer, state);
        } catch (const std::exception& e) {
            LOG_ERRORF("State change callback threw: {}", e.what());
        } catch (...) {
            LOG_ERROR("State change callback threw unknown exception");
        }
    }
}

// ============================================================================
// Worker thread
// ============================================================================

void Transceiver::worker_loop() {
    active_workers_.fetch_add(1, std::memory_order_acquire);

    while (true) {
        auto msg = dispatch_queue_->pop();
        if (!msg) break;  // Queue closed and drained

        auto before = std::chrono::steady_clock::now();

        auto result = handlers_.dispatch(
            msg->peer, msg->decoded.type_id, msg->decoded.payload);

        // Handler timeout warning
        auto handler_timeout = config_.worker.handler_timeout;
        if (handler_timeout.count() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - before;
            if (elapsed > handler_timeout) {
                stats_.handler_timeouts.fetch_add(1, std::memory_order_relaxed);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                LOG_WARNF("Handler for type_id={} took {}ms (timeout={}ms)",
                          msg->decoded.type_id, ms, handler_timeout.count());
                fire_error(msg->peer, Error(ErrorCode::Timeout,
                    std::format("handler exceeded timeout for type_id={} ({}ms > {}ms)",
                        msg->decoded.type_id, ms, handler_timeout.count())));
            }
        }

        switch (result) {
        case DispatchResult::Handled:
            stats_.messages_dispatched.fetch_add(1, std::memory_order_relaxed);
            break;
        case DispatchResult::Error:
            stats_.handler_errors.fetch_add(1, std::memory_order_relaxed);
            fire_error(msg->peer, Error(ErrorCode::InternalError,
                std::format("handler threw exception for type_id={}", msg->decoded.type_id)));
            break;
        case DispatchResult::NotFound:
            LOG_DEBUGF("Unhandled message type_id={} from peer {}",
                      msg->decoded.type_id, msg->peer.value());
            break;
        }

        // Back-pressure: resume all transports if queue fill dropped below threshold.
        // Multiple transports may have been paused, so resume all of them.
        // Use multiplicative hysteresis (80% of threshold) to avoid resume
        // being impossible when threshold < 0.1.
        double threshold = config_.rx_queue.back_pressure_threshold;
        if (threshold > 0.0 && dispatch_queue_) {
            double fill = static_cast<double>(dispatch_queue_->size()) /
                          static_cast<double>(config_.rx_queue.capacity);
            if (fill < threshold * 0.8) {
                std::shared_lock lock(peers_mutex_);
                for (auto& t : transports_) {
                    t->resume();
                }
            }
        }
    }

    active_workers_.fetch_sub(1, std::memory_order_release);
    shutdown_cv_.notify_all();
}

// ============================================================================
// Internal helpers
// ============================================================================

PeerId Transceiver::next_peer_id() {
    uint32_t id = next_peer_id_.fetch_add(1);
    if (id == 0) {
        LOG_WARN("PeerId counter wrapped around, skipping reserved ID 0");
        id = next_peer_id_.fetch_add(1);
    }
    return PeerId{id};
}

Transceiver::PeerContext* Transceiver::find_peer(PeerId id) {
    auto it = peer_map_.find(id.value());
    return it != peer_map_.end() ? it->second : nullptr;
}

const Transceiver::PeerContext* Transceiver::find_peer(PeerId id) const {
    auto it = peer_map_.find(id.value());
    return it != peer_map_.end() ? it->second : nullptr;
}

Transceiver::MultiPeerEntry*
Transceiver::find_multi_peer_entry(transport::ITransport* transport) {
    for (auto& entry : multi_peer_entries_) {
        if (entry.transport.get() == transport) return &entry;
    }
    return nullptr;
}

} // namespace conduit::transceiver
