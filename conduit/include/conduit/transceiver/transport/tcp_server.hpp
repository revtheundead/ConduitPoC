// SPDX-License-Identifier: MIT
// Conduit - TCP Server Transport

#pragma once

#include <conduit/transceiver/transport/itransport.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace conduit::transceiver::transport {

// ============================================================================
// TcpServerConfig
// ============================================================================

struct TcpServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 0;
    size_t max_clients = 64;
    size_t recv_buffer_size = 65536;
};

// ============================================================================
// TcpServerTransport
//
// Multi-peer TCP server. Accepts connections, notifies transceiver via
// on_peer_connected, routes data to on_data_received per peer.
// ============================================================================

class TcpServerTransport : public ITransport {
public:
    explicit TcpServerTransport(TcpServerConfig config);
    ~TcpServerTransport() override;

    TcpServerTransport(const TcpServerTransport&) = delete;
    TcpServerTransport& operator=(const TcpServerTransport&) = delete;

    [[nodiscard]] VoidResult start(TransportCallbacks cb) override;
    void stop() override;
    [[nodiscard]] VoidResult send(PeerId peer, std::span<const uint8_t> data) override;
    [[nodiscard]] bool is_stream_oriented() const noexcept override { return true; }
    [[nodiscard]] bool is_multi_peer() const noexcept override { return true; }
    [[nodiscard]] std::string_view transport_type() const noexcept override { return "tcp-server"; }

    /// Returns the actual bound port (useful when configured with port 0).
    [[nodiscard]] uint16_t local_port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace conduit::transceiver::transport
