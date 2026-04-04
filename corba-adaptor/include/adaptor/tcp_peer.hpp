// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer Client
//
// Connects to a remote TCP server peer and provides bidirectional
// byte-stream communication.  Runs its own I/O thread using ACE_Reactor
// for non-blocking reads with a configurable reconnection policy.

#pragma once

#include <adaptor/types.hpp>

#include <ace/INET_Addr.h>
#include <ace/Reactor.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace adaptor {

// ============================================================================
// TcpPeerConfig
// ============================================================================

struct TcpPeerConfig {
    std::string host;
    uint16_t    port{0};

    /// Receive buffer size.
    size_t      recv_buffer_size{65536};

    /// Connection timeout.
    std::chrono::milliseconds connect_timeout{10000};

    /// Reconnection parameters.
    bool                        auto_reconnect{true};
    std::chrono::milliseconds   initial_delay{1000};
    std::chrono::milliseconds   max_delay{30000};
    double                      backoff_multiplier{2.0};
    uint32_t                    max_attempts{0};  // 0 = unlimited
};

// ============================================================================
// TcpPeer
// ============================================================================

class TcpPeer {
public:
    explicit TcpPeer(TcpPeerConfig config);
    ~TcpPeer();

    TcpPeer(const TcpPeer&) = delete;
    TcpPeer& operator=(const TcpPeer&) = delete;

    /// Set callbacks (call before start()).
    void set_data_callback(DataReceivedCallback cb);
    void set_state_callback(PeerStateCallback cb);

    /// Start the I/O thread and connect to the peer.
    void start();

    /// Stop the I/O thread, close the connection.
    void stop();

    /// Send data to the TCP peer.  Thread-safe.
    /// @return true if the data was sent successfully.
    [[nodiscard]] bool send(std::span<const uint8_t> data);

    /// Whether the TCP connection is currently established.
    [[nodiscard]] bool is_connected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace adaptor
