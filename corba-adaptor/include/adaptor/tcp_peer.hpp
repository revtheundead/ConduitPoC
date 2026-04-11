// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer (Conduit-backed)
//
// Thin wrapper around a `conduit::transceiver::Transceiver` parameterised
// with the bgen-generated `tcp_peer::TcpPeerFrameSession`.  The adaptor
// only ever deals in typed messages (`tcp_peer::Heartbeat`,
// `tcp_peer::StatusReport`, ...) — raw byte handling is owned entirely by
// Conduit.
//
// Usage:
//   TcpPeer peer{TcpPeerConfig{...}};
//   peer.on<tcp_peer::Heartbeat>([](const auto& m) { ... });
//   peer.start();
//   peer.send(tcp_peer::Heartbeat{...});
//   peer.stop();

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/net/connection_state.hpp>
#include <conduit/traits/session_traits.hpp>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/reconnect_policy.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>

#include <tcp-peer/tcp_peer.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace adaptor {

// ============================================================================
// TcpPeerConfig
// ============================================================================

struct TcpPeerConfig {
    std::string host{"127.0.0.1"};
    uint16_t    port{0};

    /// Receive buffer size for the underlying TCP transport.
    std::size_t recv_buffer_size{65536};

    /// TCP connect timeout.
    std::chrono::milliseconds connect_timeout{10000};

    // --- Reconnection --------------------------------------------------------

    bool                        auto_reconnect{true};
    std::chrono::milliseconds   initial_delay{1000};
    std::chrono::milliseconds   max_delay{30000};
    double                      backoff_multiplier{2.0};
    std::uint32_t               max_attempts{0};   // 0 = unlimited

    /// Peer name as reported by the transceiver (used for logs).
    std::string name{"tcp-peer"};
};

// ============================================================================
// TcpPeer
// ============================================================================

class TcpPeer {
public:
    using StateCallback =
        std::function<void(conduit::net::ConnectionState)>;

    explicit TcpPeer(TcpPeerConfig config);
    ~TcpPeer();

    TcpPeer(const TcpPeer&) = delete;
    TcpPeer& operator=(const TcpPeer&) = delete;

    // ------------------------------------------------------------------------
    // Typed handler registration (forwarded to the Transceiver)
    // ------------------------------------------------------------------------

    /// Register a typed handler for message `T`.  Must be called before
    /// `start()`.  `T` must be one of the bgen-generated message types in
    /// the `tcp_peer` namespace (e.g. `tcp_peer::Heartbeat`).
    template <conduit::traits::Message T>
    void on(std::function<void(const T&)> cb) {
        tx_.on<T>(std::move(cb));
    }

    /// Register a callback for connection state changes.  May be called
    /// multiple times; each call adds a new subscriber.
    void on_state_change(StateCallback cb);

    // ------------------------------------------------------------------------
    // Send
    // ------------------------------------------------------------------------

    /// Send a typed message `T` to the remote peer.  Returns true on
    /// success.  Thread-safe.
    template <conduit::traits::Message T>
    [[nodiscard]] bool send(const T& msg) {
        if (!peer_id_.valid()) return false;
        auto result = tx_.send<T>(peer_id_, msg);
        return result.has_value();
    }

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------

    /// Construct the session + transport, add the peer to the transceiver
    /// and start it.  Throws `std::runtime_error` on configuration or
    /// start-up errors.
    void start();

    /// Stop the transceiver (idempotent).
    void stop();

    /// Whether the TCP connection is currently up.
    [[nodiscard]] bool is_connected() const noexcept;

    /// Current connection state (Disconnected / Connecting / Connected / ...).
    [[nodiscard]] conduit::net::ConnectionState state() const noexcept;

private:
    TcpPeerConfig                       config_;
    conduit::transceiver::Transceiver   tx_;
    conduit::transceiver::PeerId        peer_id_{};
    bool                                started_{false};
};

} // namespace adaptor
