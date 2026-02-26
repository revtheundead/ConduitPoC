// SPDX-License-Identifier: MIT
// Conduit Full Transceiver C ABI Implementation

#define CONDUIT_CABI_EXPORTS

#include <conduit/cabi/conduit_cabi.h>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/udp.hpp>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// ============================================================================
// Session registry
// ============================================================================

namespace {

struct XcvrSessionRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, conduit_session_factory_t> factories;

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
        default:
            if (err.is_encode_error()) return CONDUIT_XCVR_ERR_ENCODE_FAILED;
            return CONDUIT_XCVR_ERR_UNKNOWN;
    }
}

// Wrapper around Transceiver with C ABI state
struct TransceiverWrapper {
    conduit::transceiver::Transceiver xcvr;
    uint32_t next_cb_id = 1;

    // Track the catch-all handler that dispatches to C callbacks
    bool catch_all_installed = false;

    // Message callback entries
    struct MsgCbEntry {
        conduit_callback_id id;
        uint64_t type_id;  // 0 means any-message
        conduit_msg_callback_t callback;
        void* user_data;
    };
    std::mutex msg_cb_mutex;
    std::vector<MsgCbEntry> msg_callbacks;

    // Install a global catch-all that dispatches to registered C callbacks
    void ensure_catch_all() {
        if (catch_all_installed) return;
        catch_all_installed = true;

        // Install a catch-all that fires all registered C callbacks
        conduit::transceiver::MessageHandler handler;
        handler.on_any([this](uint64_t type_id, const std::any& /*payload*/) {
            // Try to extract DecodedMessage info
            // The catch-all receives (type_id, payload-as-any)
            // We need to find the raw bytes. The payload is the typed message,
            // not the raw bytes. For C ABI we need raw bytes.
            // Unfortunately, the catch-all gets the typed std::any, not raw bytes.

            // For the C ABI, we invoke callbacks that registered for this type_id
            std::lock_guard lock(msg_cb_mutex);
            for (auto& entry : msg_callbacks) {
                if (entry.type_id == 0 || entry.type_id == type_id) {
                    // We don't have raw bytes in the catch-all path.
                    // Pass nullptr/0 for data since we can't extract raw bytes here.
                    entry.callback(0, type_id, "", nullptr, 0, entry.user_data);
                }
            }
        });
        xcvr.set_handler(handler);
    }
};

} // namespace

// ============================================================================
// Lifecycle
// ============================================================================

extern "C" {

CONDUIT_CABI_API conduit_transceiver_t* conduit_create(void) {
    auto* wrapper = new (std::nothrow) TransceiverWrapper{};
    if (!wrapper) return nullptr;
    return reinterpret_cast<conduit_transceiver_t*>(wrapper);
}

CONDUIT_CABI_API void conduit_destroy(conduit_transceiver_t* xcvr) {
    if (!xcvr) return;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    if (wrapper->xcvr.is_running()) {
        wrapper->xcvr.stop();
    }
    delete wrapper;
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_start(conduit_transceiver_t* xcvr) {
    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.start();
    if (!result) return map_xcvr_error(result.error());
    return CONDUIT_XCVR_OK;
}

CONDUIT_CABI_API void conduit_stop(conduit_transceiver_t* xcvr) {
    if (!xcvr) return;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    wrapper->xcvr.stop();
}

CONDUIT_CABI_API int conduit_is_running(const conduit_transceiver_t* xcvr) {
    if (!xcvr) return 0;
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.is_running() ? 1 : 0;
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

    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    // Look up session factory
    conduit_session_factory_t factory = nullptr;
    {
        auto& reg = XcvrSessionRegistry::instance();
        std::lock_guard lock(reg.mutex);
        auto it = reg.factories.find(session_name);
        if (it == reg.factories.end())
            return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
        factory = it->second;
    }

    // Create session
    auto* raw_session = factory();
    if (!raw_session) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    std::unique_ptr<conduit::traits::ISession> session(
        static_cast<conduit::traits::ISession*>(raw_session));

    // Create transport
    namespace trans_ns = conduit::transceiver::transport;
    std::shared_ptr<trans_ns::ITransport> trans;
    std::string addr(transport->address ? transport->address : "");

    // Parse "host:port" from address string
    auto parse_host_port = [](const std::string& a)
        -> std::pair<std::string, uint16_t> {
        auto colon = a.rfind(':');
        if (colon == std::string::npos) return {"", 0};
        return {a.substr(0, colon),
                static_cast<uint16_t>(std::stoi(a.substr(colon + 1)))};
    };

    switch (transport->type) {
        case CONDUIT_TRANSPORT_UDP: {
            auto [host, port] = parse_host_port(addr);
            if (port == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
            trans_ns::UdpConfig cfg;
            cfg.remote_address = host;
            cfg.remote_port = port;
            trans = std::make_shared<trans_ns::UdpTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_TCP_CLIENT: {
            auto [host, port] = parse_host_port(addr);
            if (port == 0) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
            trans_ns::TcpClientConfig cfg;
            cfg.host = host;
            cfg.port = port;
            trans = std::make_shared<trans_ns::TcpClientTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_TCP_SERVER: {
            auto [host, port] = parse_host_port(addr);
            trans_ns::TcpServerConfig cfg;
            cfg.bind_address = host.empty() ? "0.0.0.0" : host;
            cfg.port = port;
            trans = std::make_shared<trans_ns::TcpServerTransport>(cfg);
            break;
        }
        case CONDUIT_TRANSPORT_SERIAL:
            return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    }

    auto result = wrapper->xcvr.add_peer(name, std::move(session), trans);
    if (!result) return map_xcvr_error(result.error());

    *out_peer_id = result->value();
    return CONDUIT_XCVR_OK;
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_peer_by_name(
    const conduit_transceiver_t* xcvr,
    const char* name,
    conduit_peer_id* out_peer_id) {

    if (!xcvr || !name || !out_peer_id)
        return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.peer(name);
    if (!result) return map_xcvr_error(result.error());
    *out_peer_id = result->value();
    return CONDUIT_XCVR_OK;
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_sole_peer(
    const conduit_transceiver_t* xcvr,
    conduit_peer_id* out_peer_id) {

    if (!xcvr || !out_peer_id)
        return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;

    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    auto result = wrapper->xcvr.sole_peer();
    if (!result) return map_xcvr_error(result.error());
    *out_peer_id = result->value();
    return CONDUIT_XCVR_OK;
}

// ============================================================================
// Messaging (send is not available without typed messages in the public API)
// These are placeholders that return errors for raw-bytes send.
// Full implementation would require extending Transceiver with raw-bytes API.
// ============================================================================

CONDUIT_CABI_API conduit_xcvr_error_t conduit_send(
    conduit_transceiver_t* xcvr,
    conduit_peer_id /*peer*/,
    uint64_t /*type_id*/,
    const uint8_t* /*data*/, size_t /*len*/) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    // Raw-bytes send requires extending Transceiver's public API.
    // For now, return not supported.
    return CONDUIT_XCVR_ERR_UNKNOWN;
}

CONDUIT_CABI_API conduit_xcvr_error_t conduit_send_batch(
    conduit_transceiver_t* xcvr,
    conduit_peer_id /*peer*/,
    uint64_t /*type_id*/,
    const uint8_t** /*payloads*/, const size_t* /*lens*/, size_t /*count*/) {

    if (!xcvr) return CONDUIT_XCVR_ERR_INVALID_ARGUMENT;
    return CONDUIT_XCVR_ERR_BATCH_NOT_SUPPORTED;
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

    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    std::lock_guard lock(wrapper->msg_cb_mutex);
    auto id = wrapper->next_cb_id++;
    wrapper->msg_callbacks.push_back({id, type_id, callback, user_data});
    wrapper->ensure_catch_all();

    return id;
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
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    std::lock_guard lock(wrapper->msg_cb_mutex);
    auto it = std::remove_if(wrapper->msg_callbacks.begin(),
                              wrapper->msg_callbacks.end(),
                              [type_id](const auto& e) { return e.type_id == type_id; });
    bool removed = (it != wrapper->msg_callbacks.end());
    wrapper->msg_callbacks.erase(it, wrapper->msg_callbacks.end());
    return removed ? 1 : 0;
}

// ============================================================================
// State & error callbacks
// ============================================================================

CONDUIT_CABI_API conduit_callback_id conduit_on_state_change(
    conduit_transceiver_t* xcvr,
    conduit_state_callback_t callback,
    void* user_data) {

    if (!xcvr || !callback) return 0;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);

    auto cb_id = wrapper->xcvr.on_state_change(
        [callback, user_data](
            conduit::transceiver::PeerId peer,
            conduit::net::ConnectionState state) {
            callback(peer.value(), static_cast<int32_t>(state), user_data);
        });

    return static_cast<uint32_t>(cb_id);
}

CONDUIT_CABI_API int conduit_remove_state_change(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id) {

    if (!xcvr) return 0;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.remove_state_change(
        static_cast<conduit::transceiver::CallbackId>(id)) ? 1 : 0;
}

CONDUIT_CABI_API conduit_callback_id conduit_on_error(
    conduit_transceiver_t* xcvr,
    conduit_error_callback_t callback,
    void* user_data) {

    if (!xcvr || !callback) return 0;
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
}

CONDUIT_CABI_API int conduit_remove_error_callback(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id) {

    if (!xcvr) return 0;
    auto* wrapper = reinterpret_cast<TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.remove_error_callback(
        static_cast<conduit::transceiver::CallbackId>(id)) ? 1 : 0;
}

// ============================================================================
// Query
// ============================================================================

CONDUIT_CABI_API size_t conduit_peer_count(const conduit_transceiver_t* xcvr) {
    if (!xcvr) return 0;
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return wrapper->xcvr.peer_count();
}

CONDUIT_CABI_API int32_t conduit_peer_state(
    const conduit_transceiver_t* xcvr, conduit_peer_id peer) {
    if (!xcvr) return -1;
    auto* wrapper = reinterpret_cast<const TransceiverWrapper*>(xcvr);
    return static_cast<int32_t>(
        wrapper->xcvr.peer_state(conduit::transceiver::PeerId{peer}));
}

// ============================================================================
// Session registry
// ============================================================================

CONDUIT_CABI_API void conduit_xcvr_register_session(
    const char* name, conduit_session_factory_t factory) {
    if (!name || !factory) return;
    auto& reg = XcvrSessionRegistry::instance();
    std::lock_guard lock(reg.mutex);
    reg.factories[name] = factory;
}

// ============================================================================
// Version
// ============================================================================

CONDUIT_CABI_API const char* conduit_version(void) {
    return "0.1.0";
}

} // extern "C"
