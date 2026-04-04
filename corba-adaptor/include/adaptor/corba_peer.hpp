// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - CORBA Raw-Data Peer
//
// Implements the RawDataCallback servant to receive inbound bytes from
// a remote RawDataChannel, and holds a reference to that channel for
// outbound sends.  The codec layer is handled externally by Conduit;
// this class deals only with raw byte transport over CORBA.

#pragma once

#include <CorbaAdaptorS.h>
#include <adaptor/types.hpp>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace adaptor {

// ============================================================================
// CorbaPeerConfig
// ============================================================================

struct CorbaPeerConfig {
    /// IOR or corbaname URI of the remote RawDataChannel.
    std::string channel_ior;

    /// Reconnection parameters (for re-resolving the channel if it drops).
    bool                        auto_reconnect{true};
    std::chrono::milliseconds   initial_delay{1000};
    std::chrono::milliseconds   max_delay{30000};
    double                      backoff_multiplier{2.0};
    uint32_t                    max_attempts{0};
};

// ============================================================================
// CorbaPeer
// ============================================================================

class CorbaPeer {
public:
    CorbaPeer(CORBA::ORB_ptr orb,
              PortableServer::POA_ptr poa,
              CorbaPeerConfig config);
    ~CorbaPeer();

    CorbaPeer(const CorbaPeer&) = delete;
    CorbaPeer& operator=(const CorbaPeer&) = delete;

    /// Set callbacks (call before start()).
    void set_data_callback(DataReceivedCallback cb);
    void set_state_callback(PeerStateCallback cb);

    /// Resolve the remote channel and register our callback.
    void start();

    /// Disconnect and unregister.
    void stop();

    /// Send raw bytes to the remote peer through the CORBA channel.
    [[nodiscard]] bool send(std::span<const uint8_t> data);

    /// Whether the channel is currently connected.
    [[nodiscard]] bool is_connected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace adaptor
