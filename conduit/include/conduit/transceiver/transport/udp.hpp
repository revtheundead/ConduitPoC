// SPDX-License-Identifier: MIT
// Conduit - UDP Transport

#pragma once

#include <conduit/transceiver/transport/itransport.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace conduit::transceiver::transport {

// ============================================================================
// UdpConfig
// ============================================================================

struct UdpConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t bind_port = 0;          // 0 = ephemeral
    std::string remote_address;       // Single-peer "connected" mode
    uint16_t remote_port = 0;
    size_t recv_buffer_size = 65536;
    size_t max_datagram_size = 65507; // Max UDP payload for IPv4
    size_t max_peers = 1024;          // 0 = unlimited (multi-peer mode only)
    std::chrono::seconds peer_timeout{0};  // 0 = no timeout (multi-peer mode only)
};

// ============================================================================
// UdpTransport
//
// Single socket, one recv thread. Each datagram is one complete frame.
// Single-peer if remote_address is set; multi-peer otherwise.
// ============================================================================

class UdpTransport : public ITransport {
public:
    explicit UdpTransport(UdpConfig config);
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    [[nodiscard]] VoidResult start(TransportCallbacks cb) override;
    void stop() override;
    [[nodiscard]] VoidResult send(PeerId peer, std::span<const uint8_t> data) override;
    [[nodiscard]] bool is_stream_oriented() const noexcept override { return false; }
    [[nodiscard]] bool is_multi_peer() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace conduit::transceiver::transport
