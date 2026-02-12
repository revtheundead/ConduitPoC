// SPDX-License-Identifier: MIT
// Conduit - TCP Client Transport

#pragma once

#include <conduit/transceiver/transport/itransport.hpp>
#include <conduit/transceiver/transport/reconnect_policy.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace conduit::transceiver::transport {

// ============================================================================
// TcpClientConfig
// ============================================================================

struct TcpClientConfig {
    std::string host;
    uint16_t port = 0;
    ReconnectPolicy reconnect;
    size_t recv_buffer_size = 65536;
    std::chrono::milliseconds connect_timeout{10000};
};

// ============================================================================
// TcpClientTransport
//
// Single-peer TCP client with automatic reconnection.
// Spawns one I/O thread: connect → recv loop with select.
// Self-pipe trick for shutdown signaling.
// ============================================================================

class TcpClientTransport : public ITransport {
public:
    explicit TcpClientTransport(TcpClientConfig config);
    ~TcpClientTransport() override;

    TcpClientTransport(const TcpClientTransport&) = delete;
    TcpClientTransport& operator=(const TcpClientTransport&) = delete;

    [[nodiscard]] VoidResult start(TransportCallbacks cb) override;
    void stop() override;
    [[nodiscard]] VoidResult send(PeerId peer, std::span<const uint8_t> data) override;
    [[nodiscard]] bool is_stream_oriented() const noexcept override { return true; }
    [[nodiscard]] bool is_multi_peer() const noexcept override { return false; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace conduit::transceiver::transport
