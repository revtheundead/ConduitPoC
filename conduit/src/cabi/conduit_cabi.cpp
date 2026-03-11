// SPDX-License-Identifier: MIT
// Conduit Full Transceiver C ABI Implementation

#ifndef CONDUIT_CABI_EXPORTS
#define CONDUIT_CABI_EXPORTS
#endif

#include <conduit/cabi/conduit_cabi.h>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/transport/serial.hpp>
#include <conduit/logging/logger.hpp>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// ============================================================================
// Exception guards for extern "C" functions.
// C++ exceptions must never propagate through a C ABI boundary (undefined
// behaviour).  These macros catch all exceptions and return a safe fallback.
// ============================================================================

#define CABI_TRY try {

#define CABI_CATCH_ERR                                                      \
    } catch (const std::exception& e) {                                     \
        LOG_ERROR(std::string("CABI: ") + e.what());                        \
        return CONDUIT_XCVR_ERR_UNKNOWN;                                    \
    } catch (...) { return CONDUIT_XCVR_ERR_UNKNOWN; }

#define CABI_CATCH_VOID                                                     \
    } catch (const std::exception& e) {                                     \
        LOG_ERROR(std::string("CABI: ") + e.what());                        \
    } catch (...) { }

#define CABI_CATCH_RET(val)                                                 \
    } catch (const std::exception& e) {                                     \
        LOG_ERROR(std::string("CABI: ") + e.what());                        \
        return (val);                                                       \
    } catch (...) { return (val); }

// ============================================================================
// Session registry
// ============================================================================

namespace {

struct XcvrSessionRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::function<void*()>> factories;

    static XcvrSessionRegistry& instance() {
        static XcvrSessionRegistry reg;
        return reg;
    }
};

conduit_xcvr_error_t map_xcvr_error(const conduit::Error& err) {
    using EC = conduit::ErrorCode;
    switch (err.code()) {
        case EC::InvalidArgument:
        case EC::InvalidConfig:
            return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
        case EC::AlreadyRunning:
            return CONDUIT_XCVR_ERR_ALREADY_RUNNING;
        case EC::NotRunning:
            return CONDUIT_XCVR_ERR_NOT_RUNNING;
        case EC::PeerNotFound:
            return CONDUIT_XCVR_ERR_PEER_NOT_FOUND;
        case EC::BatchNotSupported:
            return CONDUIT_XCVR_ERR_BATCH_NOT_SUPPORTED;
        case EC::DirectionViolation:
            return CONDUIT_XCVR_ERR_SEND_FAILED;
        default:
            if (err.is_decode_error()) return CONDUIT_XCVR_ERR_ENCODE_FAILED;
            if (err.is_encode_error()) return CONDUIT_XCVR_ERR_ENCODE_FAILED;
            if (err.is_connection_error()) return CONDUIT_XCVR_ERR_SEND_FAILED;
            return CONDUIT_XCVR_ERR_UNKNOWN;
    }
}

// Wrapper around Transceiver with C ABI state
struct TransceiverWrapper {
    TransceiverWrapper() = default;

    conduit::transceiver::Transceiver xcvr;
    uint32_t next_cb_id = 1;

    // Track the raw catch-all that dispatches to C callbacks
    bool raw_catch_all_installed = false;

    // Cached type_id -> type_name mapping (populated during add_peer)
    std::unordered_map<uint64_t, std::string> type_names;

    // Message callback entries
    struct MsgCbEntry {
        conduit_callback_id id;
        uint64_t type_id;  // 0 means any-message
        conduit_msg_callback_t callback;
        void* user_data;
    };
    std::mutex msg_cb_mutex;
    std::vector<MsgCbEntry> msg_callbacks;

    // Look up cached type name for a type_id (empty string if not found)
    const char* lookup_type_name(uint64_t type_id) const {
        auto it = type_names.find(type_id);
        return (it != type_names.end()) ? it->second.c_str() : "";
    }

    // Install a raw catch-all that dispatches to registered C callbacks.
    // Uses the raw-bytes-aware path so callbacks receive actual message data.
    void ensure_raw_catch_all() {
        if (raw_catch_all_installed) return;
        raw_catch_all_installed = true;

        xcvr.handlers().set_raw_catch_all(
            [this](conduit::transceiver::PeerId peer,
                   uint64_t type_id,
                   const std::any& /*payload*/,
                   std::span<const uint8_t> raw) {
                const char* type_name = lookup_type_name(type_id);
                std::lock_guard lock(msg_cb_mutex);
                for (auto& entry : msg_callbacks) {
                    if (entry.type_id == 0 || entry.type_id == type_id) {
                        entry.callback(
                            peer.value(), type_id, type_name,
                            raw.data(), raw.size(),
                            entry.user_data);
                    }
                }
            });
    }
};

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" {

CONDUIT_CABI_API conduit_transceiver_t* conduit_create(void) {
    CABI_TRY
    auto* wrapper = new (std::nothrow) TransceiverWrapper{};
    if (!wrapper) return nullptr;
    return reinterpret_cast<conduit_transceiver_t*>(wrapper);
    CABI_CATCH_RET(nullptr)
}

CONDUIT_CABI_API void conduit_destroy(conduit_transceiver_t* xcvr) {
    if (!xcvr) return;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    try { if (wrapper->xcvr.is_running()) wrapper->xcvr.stop(); }
    catch (...) { (void)0; /* Swallow all exceptions during teardown */ }
    delete wrapper;
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_start(conduit_transceiver_t* xcvr) {
    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.start();
    if (!result) return map_xcvr_error(result.error());
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API void conduit_stop(conduit_transceiver_t* xcvr) {
    if (!xcvr) return;
    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    wrapper->xcvr.stop();
    CABI_CATCH_VOID
}

CONDUIT_CABI_API int conduit_is_running(const conduit_transceiver_t* xcvr) {
    if (!xcvr) return 0;
    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.is_running() ? 1 : 0;
    CABI_CATCH_RET(0)
}

// ============================================================================
// Peer management
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_add_peer(
    conduit_transceiver_t* xcvr,
    const char* name,
    conduit_session_name_t session_name,
    const conduit_transport_config_t* transport,
    conduit_peer_id* out_peer_id) {

    if (!xcvr || !name || !session_name || !transport || !out_peer_id)
        return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    // Look up session factory
    std::function<void*()> factory;
    {
        auto& reg = XcvrSessionRegistry::instance();
        std::lock_guard lock(reg.mutex);
        auto it = reg.factories.find(session_name);
        if (it == reg.factories.end())
            return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
        factory = it->second;
    }

    // Create a test session to cache type names
    auto* raw_session = factory();
    if (!raw_session) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    std::unique_ptr<conduit::traits::ISession> session(
        static_cast<conduit::traits::ISession*>(raw_session));

    // Cache type_id -> type_name from the session before it's moved
    for (auto id : session->leaf_type_ids()) {
        auto tname = session->type_name(id);
        if (!tname.empty()) {
            wrapper->type_names[id] = std::string(tname);
        }
    }

    // Create transport
    namespace trans_ns = conduit::transceiver::transport;
    std::shared_ptr<trans_ns::ITransport> trans;
    std::string addr(transport->address ? transport->address : "");

    // Parse "host:port" from address string (must not throw — extern "C")
    auto parse_host_port = [](const std::string& a)
        -> std::pair<std::string, uint16_t> {
        auto colon = a.rfind(':');
        if (colon == std::string::npos) return {"", 0};
        const char* start = a.c_str() + colon + 1;
        char* end = nullptr;
        long val = std::strtol(start, &end, 10);
        if (end == start || val < 0 || val > 65535) return {a.substr(0, colon), 0};
        return {a.substr(0, colon), static_cast<uint16_t>(val)};
    };

    switch (transport->type) {
        case CONDUIT_TRANSPORT_UDP: {
            trans_ns::UdpConfig cfg;
            // bind_address / bind_port override for explicit bind+remote split
            if (transport->bind_address && transport->bind_address[0] != '\0') {
                cfg.bind_address = transport->bind_address;
                cfg.bind_port    = transport->bind_port;
                // address is the remote endpoint
                auto [rhost, rport] = parse_host_port(addr);
                cfg.remote_address = rhost;
                cfg.remote_port    = transport->remote_port ? transport->remote_port : rport;
            } else {
                // address encodes remote host:port; bind to 0.0.0.0:0 by default
                auto [rhost, rport] = parse_host_port(addr);
                if (rhost.empty() || rport == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
                cfg.remote_address = rhost;
                cfg.remote_port    = rport;
                cfg.bind_port      = transport->bind_port;
            }
            if (transport->recv_buffer_size)  cfg.recv_buffer_size  = transport->recv_buffer_size;
            if (transport->max_datagram_size) cfg.max_datagram_size = transport->max_datagram_size;
            if (transport->max_peers)         cfg.max_peers         = transport->max_peers;
            if (transport->peer_timeout_s)
                cfg.peer_timeout = std::chrono::seconds(transport->peer_timeout_s);
            trans = std::make_shared<trans_ns::UdpTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_TCP_CLIENT: {
            auto [host, port] = parse_host_port(addr);
            if (port == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
            trans_ns::TcpClientConfig cfg;
            cfg.host = host;
            cfg.port = port;
            if (transport->recv_buffer_size) cfg.recv_buffer_size = transport->recv_buffer_size;
            if (transport->connect_timeout_ms)
                cfg.connect_timeout = std::chrono::milliseconds(transport->connect_timeout_ms);
            // reconnect_enabled: <0 = disabled, 0 = use default (enabled), >0 = enabled
            if (transport->reconnect_enabled < 0) {
                cfg.reconnect.enabled = false;
            } else {
                cfg.reconnect.enabled = true;
                if (transport->reconnect_initial_delay_ms)
                    cfg.reconnect.initial_delay =
                        std::chrono::milliseconds(transport->reconnect_initial_delay_ms);
                if (transport->reconnect_max_delay_ms)
                    cfg.reconnect.max_delay =
                        std::chrono::milliseconds(transport->reconnect_max_delay_ms);
                if (transport->reconnect_backoff_multiplier > 0.0)
                    cfg.reconnect.backoff_multiplier = transport->reconnect_backoff_multiplier;
                cfg.reconnect.max_attempts = transport->reconnect_max_attempts;
            }
            trans = std::make_shared<trans_ns::TcpClientTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_TCP_SERVER: {
            auto [host, port] = parse_host_port(addr);
            trans_ns::TcpServerConfig cfg;
            cfg.bind_address = host.empty() ? "0.0.0.0" : host;
            cfg.port = port;
            if (transport->max_clients)      cfg.max_clients      = transport->max_clients;
            if (transport->recv_buffer_size) cfg.recv_buffer_size = transport->recv_buffer_size;
            trans = std::make_shared<trans_ns::TcpServerTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_SERIAL: {
            if (!transport->address || transport->address[0] == '\0')
                return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
            trans_ns::SerialConfig cfg;
            cfg.port      = transport->address;
            cfg.baud_rate = transport->baud_rate ? transport->baud_rate : 9600;
            if (transport->data_bits)    cfg.data_bits = transport->data_bits;
            if (transport->recv_buffer_size) cfg.recv_buffer_size = transport->recv_buffer_size;
            switch (transport->parity) {
                case CONDUIT_SERIAL_PARITY_ODD:  cfg.parity = trans_ns::Parity::Odd;  break;
                case CONDUIT_SERIAL_PARITY_EVEN: cfg.parity = trans_ns::Parity::Even; break;
                default:                         cfg.parity = trans_ns::Parity::None; break;
            }
            switch (transport->stop_bits) {
                case CONDUIT_SERIAL_STOP_BITS_TWO: cfg.stop_bits = trans_ns::StopBits::Two; break;
                default:                           cfg.stop_bits = trans_ns::StopBits::One; break;
            }
            switch (transport->flow_control) {
                case CONDUIT_SERIAL_FLOW_HARDWARE:
                    cfg.flow_control = trans_ns::FlowControl::Hardware; break;
                case CONDUIT_SERIAL_FLOW_SOFTWARE:
                    cfg.flow_control = trans_ns::FlowControl::Software; break;
                default:
                    cfg.flow_control = trans_ns::FlowControl::None;     break;
            }
            trans = std::make_shared<trans_ns::SerialTransport>(cfg);
            break;
        }
    }

    // Query the transport to decide which add_peer overload to use.
    // Multi-peer transports (TCP server, UDP receiver) need a session factory
    // that creates a new session per connection. Single-peer transports
    // (TCP client, UDP sender, serial) use the already-created session.
    if (trans->is_multi_peer()) {
        auto session_factory = [factory]() -> std::unique_ptr<conduit::traits::ISession> {
            auto* s = factory();
            if (!s) return nullptr;
            return std::unique_ptr<conduit::traits::ISession>(
                static_cast<conduit::traits::ISession*>(s));
        };
        auto result = wrapper->xcvr.add_peer(name, std::move(session_factory), trans);
        if (!result) return map_xcvr_error(result.error());
        *out_peer_id = result->value();
    } else {
        auto result = wrapper->xcvr.add_peer(name, std::move(session), trans);
        if (!result) return map_xcvr_error(result.error());
        *out_peer_id = result->value();
    }
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_peer_by_name(
    const conduit_transceiver_t* xcvr,
    const char* name,
    conduit_peer_id* out_peer_id) {

    if (!xcvr || !name || !out_peer_id)
        return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.peer(name);
    if (!result) return map_xcvr_error(result.error());
    *out_peer_id = result->value();
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_sole_peer(
    const conduit_transceiver_t* xcvr,
    conduit_peer_id* out_peer_id) {

    if (!xcvr || !out_peer_id)
        return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.sole_peer();
    if (!result) return map_xcvr_error(result.error());
    *out_peer_id = result->value();
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Messaging
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_send(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t* data, size_t len) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    if (!data && len > 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.send_raw(
        conduit::transceiver::PeerId{peer}, type_id,
        std::span<const uint8_t>(data, len));
    if (!result) return map_xcvr_error(result.error());
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_send_batch(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* lens, size_t count) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    if (count > 0 && (!payloads || !lens)) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.send_raw_batch(
        conduit::transceiver::PeerId{peer}, type_id,
        payloads, lens, count);
    if (!result) return map_xcvr_error(result.error());
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Message logging (passthrough)
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_log_recv_message(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    const char* type_name,
    size_t byte_count,
    const char* content) {

    if (!xcvr || !type_name) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    wrapper->xcvr.log_recv_message(
        conduit::transceiver::PeerId{peer},
        type_name, byte_count,
        content ? std::string(content) : std::string{});
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_log_send_message(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    const char* type_name,
    size_t byte_count,
    const char* content) {

    if (!xcvr || !type_name) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    wrapper->xcvr.log_send_message(
        conduit::transceiver::PeerId{peer},
        type_name, byte_count,
        content ? std::string(content) : std::string{});
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Handler registration
// ============================================================================

CONDUIT_CABI_API conduit_callback_id conduit_on_message(
    conduit_transceiver_t* xcvr,
    uint64_t type_id,
    conduit_msg_callback_t callback,
    void* user_data) {

    if (!xcvr || !callback) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    std::lock_guard lock(wrapper->msg_cb_mutex);
    auto id = wrapper->next_cb_id++;
    wrapper->msg_callbacks.push_back({id, type_id, callback, user_data});
    wrapper->ensure_raw_catch_all();

    return id;
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API conduit_callback_id conduit_on_any_message(
    conduit_transceiver_t* xcvr,
    conduit_msg_callback_t callback,
    void* user_data) {
    return conduit_on_message(xcvr, 0, callback, user_data);
}

CONDUIT_CABI_API int conduit_remove_handler(
    conduit_transceiver_t* xcvr,
    conduit_peer_id /*peer*/,
    uint64_t type_id) {

    if (!xcvr) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    std::lock_guard lock(wrapper->msg_cb_mutex);
    auto it = std::remove_if(wrapper->msg_callbacks.begin(),
                              wrapper->msg_callbacks.end(),
                              [type_id](const auto& e) { return e.type_id == type_id; });
    bool removed = (it != wrapper->msg_callbacks.end());
    wrapper->msg_callbacks.erase(it, wrapper->msg_callbacks.end());
    return removed ? 1 : 0;
    CABI_CATCH_RET(0)
}

// ============================================================================
// State & error callbacks
// ============================================================================

CONDUIT_CABI_API conduit_callback_id conduit_on_state_change(
    conduit_transceiver_t* xcvr,
    conduit_state_callback_t callback,
    void* user_data) {

    if (!xcvr || !callback) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    auto cb_id = wrapper->xcvr.on_state_change(
        [callback, user_data](
            conduit::transceiver::PeerId peer,
            conduit::net::ConnectionState state) {
            callback(peer.value(), static_cast<int32_t>(state), user_data);
        });

    return static_cast<uint32_t>(cb_id);
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API int conduit_remove_state_change(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id) {

    if (!xcvr) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.remove_state_change(
        static_cast<conduit::transceiver::CallbackId>(id)) ? 1 : 0;
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API conduit_callback_id conduit_on_error(
    conduit_transceiver_t* xcvr,
    conduit_error_callback_t callback,
    void* user_data) {

    if (!xcvr || !callback) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    auto cb_id = wrapper->xcvr.on_error(
        [callback, user_data](const conduit::transceiver::ErrorEvent& event) {
            callback(event.peer.value(),
                     event.peer_name.c_str(),
                     static_cast<int32_t>(event.error.code()),
                     event.error.message().c_str(),
                     user_data);
        });

    return static_cast<uint32_t>(cb_id);
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API int conduit_remove_error_callback(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id) {

    if (!xcvr) return 0;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.remove_error_callback(
        static_cast<conduit::transceiver::CallbackId>(id)) ? 1 : 0;
    CABI_CATCH_RET(0)
}

// ============================================================================
// Query
// ============================================================================

CONDUIT_CABI_API size_t conduit_peer_count(const conduit_transceiver_t* xcvr) {
    if (!xcvr) return 0;
    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.peer_count();
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API int32_t conduit_peer_state(
    const conduit_transceiver_t* xcvr, conduit_peer_id peer) {
    if (!xcvr) return -1;
    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return static_cast<int32_t>(
        wrapper->xcvr.peer_state(conduit::transceiver::PeerId{peer}));
    CABI_CATCH_RET(-1)
}

// ============================================================================
// Statistics
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_stats(
    const conduit_transceiver_t* xcvr,
    conduit_stats_snapshot_t* out) {

    if (!xcvr || !out) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    auto snap = wrapper->xcvr.stats().snapshot();

    out->messages_received  = snap.messages_received;
    out->messages_dispatched = snap.messages_dispatched;
    out->messages_dropped   = snap.messages_dropped;
    out->decode_errors      = snap.decode_errors;
    out->handler_errors     = snap.handler_errors;
    out->handler_timeouts   = snap.handler_timeouts;
    out->bytes_received     = snap.bytes_received;
    out->bytes_sent         = snap.bytes_sent;

    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_stats_reset(
    conduit_transceiver_t* xcvr) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    wrapper->xcvr.stats_reset();

    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Session registry
// ============================================================================

CONDUIT_CABI_API void conduit_xcvr_register_session(
    const char* name, conduit_session_factory_t factory) {
    if (!name || !factory) return;
    CABI_TRY
    auto& reg = XcvrSessionRegistry::instance();
    std::lock_guard lock(reg.mutex);
    reg.factories[name] = factory;
    CABI_CATCH_VOID
}

// ============================================================================
// Pre-start configuration
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_queue_config(
    conduit_transceiver_t* xcvr,
    size_t capacity,
    int drop_policy,
    double back_pressure_threshold) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    conduit::transceiver::QueueConfig cfg;
    cfg.capacity = capacity;
    switch (drop_policy) {
        case CONDUIT_DROP_OLDEST: cfg.drop_policy = conduit::queue::DropPolicy::DropOldest; break;
        case CONDUIT_DROP_NEWEST: cfg.drop_policy = conduit::queue::DropPolicy::DropNewest; break;
        case CONDUIT_DROP_BLOCK:  cfg.drop_policy = conduit::queue::DropPolicy::Block; break;
        default: return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    }
    cfg.back_pressure_threshold = back_pressure_threshold;

    wrapper->xcvr.set_queue_config(std::move(cfg));
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_worker_config(
    conduit_transceiver_t* xcvr,
    size_t thread_count,
    uint64_t handler_timeout_ms) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    conduit::transceiver::WorkerConfig cfg;
    cfg.thread_count = thread_count;
    cfg.handler_timeout = std::chrono::milliseconds(handler_timeout_ms);

    wrapper->xcvr.set_worker_config(std::move(cfg));
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_shutdown_timeout(
    conduit_transceiver_t* xcvr,
    uint64_t timeout_ms) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    wrapper->xcvr.set_shutdown_timeout(std::chrono::milliseconds(timeout_ms));
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_message_log_config(
    conduit_transceiver_t* xcvr,
    const conduit_message_log_config_t* config) {

    if (!xcvr || !config) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    conduit::transceiver::MessageLogConfig cfg;
    cfg.enabled = (config->enabled != 0);
    switch (config->mode) {
        case CONDUIT_LOG_MODE_COMBINED:           cfg.mode = conduit::transceiver::MessageLogMode::Combined; break;
        case CONDUIT_LOG_MODE_SEPARATE_DIRECTION: cfg.mode = conduit::transceiver::MessageLogMode::SeparateDirection; break;
        case CONDUIT_LOG_MODE_PER_PEER:           cfg.mode = conduit::transceiver::MessageLogMode::PerPeer; break;
        case CONDUIT_LOG_MODE_PER_PEER_DIRECTION: cfg.mode = conduit::transceiver::MessageLogMode::PerPeerDirection; break;
        default: return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    }
    switch (config->output) {
        case CONDUIT_LOG_OUTPUT_FILE:   cfg.output = conduit::transceiver::MessageLogOutput::File; break;
        case CONDUIT_LOG_OUTPUT_STDOUT: cfg.output = conduit::transceiver::MessageLogOutput::Stdout; break;
        case CONDUIT_LOG_OUTPUT_BOTH:   cfg.output = conduit::transceiver::MessageLogOutput::Both; break;
        default: return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    }
    if (config->directory)          cfg.directory = config->directory;
    if (config->prefix)             cfg.prefix = config->prefix;
    if (config->filename)           cfg.filename = config->filename;
    if (config->sent_filename)      cfg.sent_filename = config->sent_filename;
    if (config->received_filename)  cfg.received_filename = config->received_filename;
    cfg.include_message_content = (config->include_message_content != 0);

    wrapper->xcvr.set_message_log_config(std::move(cfg));
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Passthrough session
//
// A protocol-agnostic session that handles framing only.  All encode/decode
// of individual messages is done on the caller side (Java/Python).
// This eliminates the need for protocol-specific shared libraries.
// ============================================================================

namespace {

class PassthroughSession : public conduit::traits::ISession {
public:
    struct FrameConfig {
        std::vector<uint8_t> sync;
        size_t min_header_size;
        size_t length_skip_bits;
        size_t length_field_bits;
        bool length_big_endian;
    };

    struct TypeInfo {
        uint64_t type_id;
        std::string type_name;
        bool receive_only;
    };

    PassthroughSession(std::string protocol,
                       FrameConfig frame_config,
                       std::vector<TypeInfo> types)
        : protocol_(std::move(protocol))
        , frame_config_(std::move(frame_config))
        , types_(std::move(types)) {
        for (const auto& t : types_) {
            ids_.push_back(t.type_id);
        }
    }

    // decode_frame: return the raw frame bytes as a single "message" with
    // type_id = 0.  Java/Python will decode the frame itself.
    [[nodiscard]] conduit::Result<std::vector<conduit::traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        std::vector<conduit::traits::DecodedMessage> messages;
        conduit::traits::DecodedMessage dm;
        dm.type_id = 0; // passthrough: Java does the type routing
        dm.type_name = protocol_ + "_frame";
        dm.payload = std::vector<uint8_t>(data.begin(), data.end());
        dm.raw = std::vector<uint8_t>(data.begin(), data.end());
        messages.push_back(std::move(dm));
        return messages;
    }

    // encode_wrap: input bytes are already a complete frame (created by Java).
    // Just pass them through.
    [[nodiscard]] conduit::Result<conduit::traits::EncodeResult>
    encode_wrap(uint64_t /*type_id*/, const std::any& payload) override {
        auto* raw = std::any_cast<std::vector<uint8_t>>(&payload);
        if (!raw) return std::unexpected(conduit::Error(
            conduit::ErrorCode::InvalidArgument,
            "PassthroughSession: payload must be raw bytes"));
        conduit::traits::EncodeResult result;
        result.bytes = *raw;
        return result;
    }

    [[nodiscard]] std::span<const uint8_t> sync_pattern() const override {
        return frame_config_.sync;
    }

    [[nodiscard]] size_t min_frame_header_size() const override {
        return frame_config_.min_header_size;
    }

    [[nodiscard]] size_t extract_frame_length(
        std::span<const uint8_t> header) const override {
        if (header.size() < frame_config_.min_header_size) return 0;
        conduit::io::BitReader r(header);
        if (frame_config_.length_skip_bits > 0) {
            if (!r.skip_bits(frame_config_.length_skip_bits)) return 0;
        }
        auto endian = frame_config_.length_big_endian
                          ? conduit::io::Endian::Big
                          : conduit::io::Endian::Little;
        if (frame_config_.length_field_bits == 8) {
            auto val = r.read_u8();
            return val ? static_cast<size_t>(*val) : 0;
        } else if (frame_config_.length_field_bits == 16) {
            auto val = r.read_u16(endian);
            return val ? static_cast<size_t>(*val) : 0;
        } else if (frame_config_.length_field_bits == 32) {
            auto val = r.read_u32(endian);
            return val ? static_cast<size_t>(*val) : 0;
        }
        return 0;
    }

    [[nodiscard]] std::span<const uint64_t> leaf_type_ids() const override {
        return ids_;
    }

    [[nodiscard]] std::string_view type_name(uint64_t type_id) const override {
        for (const auto& t : types_) {
            if (t.type_id == type_id) return t.type_name;
        }
        return "unknown";
    }

    [[nodiscard]] bool is_receive_only(uint64_t type_id) const override {
        for (const auto& t : types_) {
            if (t.type_id == type_id) return t.receive_only;
        }
        return false;
    }

    [[nodiscard]] std::string_view protocol_name() const override {
        return protocol_;
    }

    [[nodiscard]] bool defers_message_logging() const override { return true; }

    void reset() override {}

private:
    std::string protocol_;
    FrameConfig frame_config_;
    std::vector<TypeInfo> types_;
    std::vector<uint64_t> ids_;
};

// Store passthrough configs so factory lambdas can create sessions
struct PassthroughConfigEntry {
    PassthroughSession::FrameConfig frame_config;
    std::vector<PassthroughSession::TypeInfo> types;
};

// Construct-on-first-use singleton for passthrough config storage.
// Avoids file-scope globals whose construction during DLL initialization
// (DllMain on Windows) can crash when the CRT heap is not yet ready.
struct PassthroughConfigRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<PassthroughConfigEntry>> configs;

    static PassthroughConfigRegistry& instance() {
        static PassthroughConfigRegistry reg;
        return reg;
    }
};

} // namespace

CONDUIT_CABI_API conduit_xcvr_error_t conduit_register_passthrough_session(
    const char* name,
    const conduit_frame_config_t* frame_config,
    const uint64_t* type_ids,
    const char** type_names,
    const int* receive_only,
    size_t type_count) {

    if (!name || !frame_config) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    if (type_count > 0 && (!type_ids || !type_names)) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    CABI_TRY
    // Build the passthrough config
    auto config = std::make_shared<PassthroughConfigEntry>();
    config->frame_config.min_header_size = frame_config->min_header_size;
    config->frame_config.length_skip_bits = frame_config->length_skip_bits;
    config->frame_config.length_field_bits = frame_config->length_field_bits;
    config->frame_config.length_big_endian = (frame_config->length_big_endian != 0);
    if (frame_config->sync_pattern && frame_config->sync_pattern_len > 0) {
        config->frame_config.sync.assign(
            frame_config->sync_pattern,
            frame_config->sync_pattern + frame_config->sync_pattern_len);
    }
    for (size_t i = 0; i < type_count; ++i) {
        PassthroughSession::TypeInfo ti;
        ti.type_id = type_ids ? type_ids[i] : 0;
        ti.type_name = (type_names && type_names[i]) ? type_names[i] : "";
        ti.receive_only = (receive_only && receive_only[i]) ? true : false;
        config->types.push_back(std::move(ti));
    }

    // Store the config
    std::string session_name(name);
    {
        auto& pt = PassthroughConfigRegistry::instance();
        std::lock_guard lock(pt.mutex);
        pt.configs[session_name] = config;
    }

    // Register a factory that creates PassthroughSession instances.
    // We insert directly into the registry (not via conduit_xcvr_register_session)
    // because the factory is a capturing lambda.
    {
        auto& reg = XcvrSessionRegistry::instance();
        std::lock_guard lock(reg.mutex);
        reg.factories[session_name] = [session_name]() -> void* {
            std::shared_ptr<PassthroughConfigEntry> cfg;
            {
                auto& pt = PassthroughConfigRegistry::instance();
                std::lock_guard lock2(pt.mutex);
                auto it = pt.configs.find(session_name);
                if (it == pt.configs.end()) return nullptr;
                cfg = it->second;
            }
            return static_cast<void*>(
                new PassthroughSession(session_name, cfg->frame_config, cfg->types));
        };
    }
    return CONDUIT_XCVR_OK;
    CABI_CATCH_ERR
}

// ============================================================================
// Logger configuration
// ============================================================================

CONDUIT_CABI_API void conduit_set_log_level(int level) {
    if (level < 0 || level > 6) return;
    CABI_TRY
    conduit::logging::Logger::instance().setLevel(
        static_cast<conduit::logging::Level>(level));
    CABI_CATCH_VOID
}

CONDUIT_CABI_API int conduit_get_log_level(void) {
    CABI_TRY
    return static_cast<int>(conduit::logging::Logger::instance().level());
    CABI_CATCH_RET(0)
}

CONDUIT_CABI_API void conduit_log_add_console_sink(int use_stderr, int colorize) {
    CABI_TRY
    conduit::logging::Logger::instance().addSink(
        std::make_shared<conduit::logging::ConsoleSink>(
            use_stderr != 0, colorize != 0));
    CABI_CATCH_VOID
}

CONDUIT_CABI_API void conduit_log_add_file_sink(const char* path, int append) {
    if (!path) return;
    CABI_TRY
    conduit::logging::Logger::instance().addSink(
        std::make_shared<conduit::logging::FileSink>(path, append != 0));
    CABI_CATCH_VOID
}

CONDUIT_CABI_API void conduit_log_clear_sinks(void) {
    CABI_TRY
    conduit::logging::Logger::instance().clearSinks();
    CABI_CATCH_VOID
}

// ============================================================================
// Version
// ============================================================================

CONDUIT_CABI_API const char* conduit_version(void) {
    return "0.1.0";
}

} // extern "C"
