// SPDX-License-Identifier: MIT
// Conduit - Transport Interface

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/net/connection_state.hpp>
#include <conduit/transceiver/peer.hpp>
#include <cstdint>
#include <functional>
#include <span>

namespace conduit::transceiver::transport {

// ============================================================================
// TransportCallbacks: I/O thread → Transceiver notifications
//
// All callbacks fire on the transport's I/O thread. Implementations must be
// lightweight (push to queue, no blocking).
// ============================================================================

struct TransportCallbacks {
    std::function<void(PeerId, std::span<const uint8_t>)> on_data_received;
    std::function<PeerId()> on_peer_connected;
    std::function<void(PeerId)> on_peer_disconnected;
    std::function<void(PeerId, net::ConnectionState)> on_state_changed;
};

// ============================================================================
// ITransport: Abstract transport interface
//
// Each transport owns its I/O thread(s). The transceiver calls start()/stop()
// and sends data via send(). Inbound data arrives through callbacks.
// ============================================================================

class ITransport {
public:
    virtual ~ITransport() = default;

    // Start the transport, spawning I/O thread(s). Callbacks fire on I/O thread.
    [[nodiscard]] virtual VoidResult start(TransportCallbacks cb) = 0;

    // Stop the transport, joining I/O thread(s).
    virtual void stop() = 0;

    // Send data to a specific peer.
    [[nodiscard]] virtual VoidResult send(PeerId peer,
                                          std::span<const uint8_t> data) = 0;

    // Stream-oriented transports (TCP, Serial) require framing.
    // Datagram transports (UDP) deliver complete frames.
    [[nodiscard]] virtual bool is_stream_oriented() const noexcept = 0;

    // Multi-peer transports (TCP server, UDP multi) can have multiple peers.
    [[nodiscard]] virtual bool is_multi_peer() const noexcept = 0;

    // Back-pressure: pause/resume reading from this transport.
    // TCP transports can override to stop/start reading from the socket.
    // Default is no-op (appropriate for UDP and serial).
    virtual void pause() {}
    virtual void resume() {}
};

} // namespace conduit::transceiver::transport
