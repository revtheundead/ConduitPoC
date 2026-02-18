// SPDX-License-Identifier: MIT
// Conduit - Transceiver (Main User-Facing Class)

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/net/connection_state.hpp>
#include <conduit/queue/bounded_queue.hpp>
#include <conduit/traits/codec_traits.hpp>
#include <conduit/traits/session_traits.hpp>
#include <conduit/transceiver/error_event.hpp>
#include <conduit/transceiver/handler.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/transceiver/message_log.hpp>
#include <conduit/transceiver/peer.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/transceiver/transport/itransport.hpp>
#include <conduit/transceiver/transceiver_config.hpp>
#include <conduit/transceiver/transceiver_stats.hpp>
#include <any>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace conduit::transceiver {

// ============================================================================
// Transceiver: Orchestrator wiring protocol sessions to transport I/O
//
// Users register typed message handlers, send typed messages, and never touch
// raw bytes. Works with any arbitrary BMDL-defined protocol.
// ============================================================================

class Transceiver {
public:
    explicit Transceiver(TransceiverConfig config = {});
    ~Transceiver();

    Transceiver(const Transceiver&) = delete;
    Transceiver& operator=(const Transceiver&) = delete;
    Transceiver(Transceiver&&) = delete;
    Transceiver& operator=(Transceiver&&) = delete;

    // ========================================================================
    // Peer management (call before start())
    // ========================================================================

    // Add a single-peer transport (TCP client, serial).
    [[nodiscard]] Result<PeerId> add_peer(
        std::string name,
        std::unique_ptr<traits::ISession> session,
        std::shared_ptr<transport::ITransport> transport);

    // Add a multi-peer transport (TCP server) with a session factory.
    [[nodiscard]] Result<PeerId> add_peer(
        std::string name,
        SessionFactory session_factory,
        std::shared_ptr<transport::ITransport> transport);

    // Convenience: get the sole peer when only one exists.
    [[nodiscard]] Result<PeerId> sole_peer() const;

    // Lookup a peer by name. Returns the PeerId for the first peer with the
    // given name, or an error if not found.
    [[nodiscard]] Result<PeerId> peer(std::string_view name) const;

    // ========================================================================
    // Handler registration
    // ========================================================================

    // Register handler for a specific peer.
    template<traits::Message T>
    void on(PeerId peer, std::function<void(const T&)> cb) {
        handlers_.register_handler(peer, ErasedHandler(std::move(cb)));
    }

    // Register handler for sole peer (convenience).
    template<traits::Message T>
    void on(std::function<void(const T&)> cb) {
        handlers_.register_handler_all(ErasedHandler(std::move(cb)));
    }

    // Unregister handler for a specific peer and message type.
    template<traits::Message T>
    bool remove_handler(PeerId peer) {
        return handlers_.unregister_handler(peer, T::TYPE_ID);
    }

    // Unregister global handler for a message type.
    template<traits::Message T>
    bool remove_handler() {
        return handlers_.unregister_handler_all(T::TYPE_ID);
    }

    // Install a MessageHandler for a specific peer.
    void set_handler(PeerId peer, MessageHandler handler);

    // Install a MessageHandler for the sole peer (convenience, requires exactly 1 peer).
    void set_handler(MessageHandler handler);

    // Register connection state change callback. Returns an ID for removal.
    [[nodiscard]] CallbackId on_state_change(ConnectionStateCallback cb);

    // Remove a previously registered state change callback.
    bool remove_state_change(CallbackId id);

    // Register error callback. Fires for decode errors, queue drops, handler
    // exceptions/timeouts, and session factory failures. Returns an ID for removal.
    [[nodiscard]] CallbackId on_error(ErrorCallback cb);

    // Remove a previously registered error callback.
    bool remove_error_callback(CallbackId id);

    // ========================================================================
    // Send
    // ========================================================================

    // Send typed message to a specific peer.
    template<traits::Message T>
    VoidResult send(PeerId peer, const T& msg) {
        return send_impl(peer, T::TYPE_ID, std::any(msg));
    }

    // Send typed message to sole peer (convenience).
    template<traits::Message T>
    VoidResult send(const T& msg) {
        auto peer_result = sole_peer();
        if (!peer_result) {
            return std::unexpected(peer_result.error());
        }
        return send<T>(*peer_result, msg);
    }

    // Send multiple messages of the same type in a single frame (batch).
    // Only works with array-payload protocols. Returns BatchNotSupported otherwise.
    template<traits::Message T>
    VoidResult send_batch(PeerId peer, std::span<const T> messages) {
        std::vector<std::any> payloads;
        payloads.reserve(messages.size());
        for (const auto& m : messages)
            payloads.emplace_back(m);
        return send_batch_impl(peer, T::TYPE_ID, payloads);
    }

    // Send batch to sole peer (convenience).
    template<traits::Message T>
    VoidResult send_batch(std::span<const T> messages) {
        auto peer_result = sole_peer();
        if (!peer_result)
            return std::unexpected(peer_result.error());
        return send_batch<T>(*peer_result, messages);
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    [[nodiscard]] VoidResult start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Query
    // ========================================================================

    [[nodiscard]] net::ConnectionState peer_state(PeerId peer) const;
    [[nodiscard]] size_t peer_count() const;
    [[nodiscard]] std::vector<PeerId> peer_ids() const;
    [[nodiscard]] const TransceiverStats& stats() const noexcept { return stats_; }

private:
    // Internal peer context
    struct PeerContext {
        PeerId id;
        std::string name;
        std::string remote_endpoint;  // "ip:port" for TCP/UDP connections
        std::unique_ptr<traits::ISession> session;
        std::unique_ptr<StreamFramer> framer;
        std::shared_ptr<transport::ITransport> transport;
        std::atomic<net::ConnectionState> state{net::ConnectionState::Disconnected};
        std::mutex ctx_mutex;  // Protects session + framer access
    };

    // Multi-peer transport entry (TCP server)
    struct MultiPeerEntry {
        PeerId base_id;  // The ID returned from add_peer
        std::string name;
        SessionFactory session_factory;
        std::shared_ptr<transport::ITransport> transport;
        uint32_t next_child{1};  // Sequential child counter for naming
    };

    // Create a transport from a TransportConfig variant
    static std::shared_ptr<transport::ITransport> make_transport(const TransportConfig& cfg);

    // Determine if a transport config produces a multi-peer transport
    static bool is_multi_peer_config(const TransportConfig& cfg);

    // Materialize peers from config_.peers
    VoidResult materialize_config_peers();

    // Send implementation
    VoidResult send_impl(PeerId peer, uint64_t type_id, const std::any& payload);
    VoidResult send_batch_impl(PeerId peer, uint64_t type_id,
                               std::span<const std::any> payloads);

    // Transport callbacks (fire on I/O thread)
    void handle_data_received(PeerId peer, std::span<const uint8_t> data);
    PeerId handle_peer_connected(transport::ITransport* transport,
                                 std::string remote_endpoint);
    void handle_peer_disconnected(PeerId peer);
    void handle_state_changed(PeerId peer, net::ConnectionState state);

    // Worker thread loop
    void worker_loop();

    // Allocate next peer ID
    PeerId next_peer_id();

    // Find peer context
    PeerContext* find_peer(PeerId id);
    const PeerContext* find_peer(PeerId id) const;

    // Find which multi-peer entry owns a transport
    MultiPeerEntry* find_multi_peer_entry(transport::ITransport* transport);

    TransceiverConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> next_peer_id_{1};

    mutable std::shared_mutex peers_mutex_;
    std::vector<std::unique_ptr<PeerContext>> peers_;
    std::unordered_map<uint32_t, PeerContext*> peer_map_;  // PeerId value → PeerContext*
    std::vector<MultiPeerEntry> multi_peer_entries_;

    // Name -> PeerId mapping (for config-driven peers and add_peer)
    std::unordered_map<std::string, PeerId> name_to_peer_;

    // Set of transports (for lifecycle management)
    std::vector<std::shared_ptr<transport::ITransport>> transports_;

    HandlerRegistry handlers_;
    TransceiverStats stats_;

    struct StateCallbackEntry {
        CallbackId id;
        ConnectionStateCallback cb;
    };
    std::vector<StateCallbackEntry> state_callbacks_;
    std::mutex state_cb_mutex_;
    std::atomic<uint32_t> next_callback_id_{1};

    struct ErrorCallbackEntry {
        CallbackId id;
        ErrorCallback cb;
    };
    std::vector<ErrorCallbackEntry> error_callbacks_;
    std::mutex error_cb_mutex_;

    // Fire error event to all registered error callbacks.
    // fire_error: looks up peer context — must NOT be called while holding peers_mutex_.
    // fire_error_event: fires a pre-constructed event — safe to call from any context.
    void fire_error(PeerId peer, Error error);
    void fire_error_event(ErrorEvent event);

    std::unique_ptr<MessageLog> message_log_;

    std::unique_ptr<queue::BoundedQueue<InboundMessage>> dispatch_queue_;
    std::vector<std::thread> workers_;

    // Graceful shutdown support
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::atomic<int> active_workers_{0};
};

} // namespace conduit::transceiver
