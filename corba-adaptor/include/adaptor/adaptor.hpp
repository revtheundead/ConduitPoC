// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator
//
// Ties together the TCP peer, CORBA peer, supplier, and command servant
// into a single cohesive bridge.  Data flows:
//
//   TCP Peer  ──┐
//               ├──▶ Adaptor ──▶ DataSupplier ──▶ N consumers
//   CORBA Peer ─┘         ▲
//                         │
//         CommandReceiver ─┘  (external systems issue commands)
//
// The adaptor also routes outbound data: commands or supplier-driven
// writes can send data back to either peer.

#pragma once

#include <adaptor/codec/codec_bridge.hpp>
#include <adaptor/command_servant.hpp>
#include <adaptor/corba_peer.hpp>
#include <adaptor/supplier.hpp>
#include <adaptor/tcp_peer.hpp>
#include <adaptor/types.hpp>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace adaptor {

// ============================================================================
// AdaptorConfig
// ============================================================================

struct AdaptorConfig {
    TcpPeerConfig       tcp;
    CorbaPeerConfig     corba_peer;

    /// Health-check interval for the supplier's consumer sweep.
    std::chrono::seconds health_check_interval{30};

    /// Write the supplier IOR to this file (empty = don't write).
    std::string supplier_ior_file;

    /// Write the command receiver IOR to this file (empty = don't write).
    std::string command_ior_file;

    /// Number of ORB thread-pool threads (1 = single-threaded, default).
    /// Higher values allow concurrent CORBA request processing.
    uint32_t orb_threads{1};
};

// ============================================================================
// Adaptor
// ============================================================================

class Adaptor {
public:
    Adaptor(CORBA::ORB_ptr orb,
            PortableServer::POA_ptr poa,
            AdaptorConfig config);
    ~Adaptor();

    Adaptor(const Adaptor&) = delete;
    Adaptor& operator=(const Adaptor&) = delete;

    /// Initialize all sub-components and activate CORBA servants.
    void start();

    /// Graceful shutdown of all sub-components.
    void stop();

    /// Request shutdown (signal-safe — only sets an atomic flag).
    void request_shutdown();

    /// Block until shutdown is signalled (e.g. via signal or CommandReceiver).
    void wait_for_shutdown();

    /// Send raw data to a specific peer (bypasses codec).
    [[nodiscard]] bool send_to_peer(PeerId peer, std::span<const uint8_t> data);

    /// Encode a typed message via the codec and send to a specific peer.
    [[nodiscard]] bool send_message(PeerId peer, uint64_t type_id, const std::any& payload);

    // --- Access to sub-components for advanced configuration ----------------

    [[nodiscard]] CommandReceiverServant& command_servant() noexcept;
    [[nodiscard]] DataSupplierServant&    supplier() noexcept;
    [[nodiscard]] TcpPeer&               tcp_peer() noexcept;
    [[nodiscard]] CorbaPeer&             corba_peer() noexcept;
    [[nodiscard]] codec::CodecBridge&    codec_bridge() noexcept;

private:
    void on_data_received(InternalPacket pkt);
    void register_builtin_commands();
    void orb_thread_func();

    CORBA::ORB_var              orb_;
    PortableServer::POA_var     poa_;
    AdaptorConfig               config_;

    std::unique_ptr<TcpPeer>                tcp_;
    std::unique_ptr<CorbaPeer>              corba_;
    std::unique_ptr<DataSupplierServant>    supplier_;
    std::unique_ptr<CommandReceiverServant> command_;
    std::unique_ptr<codec::CodecBridge>     codec_;

    std::atomic<bool>           running_{false};
    std::atomic<bool>           shutdown_requested_{false};
    std::mutex                  shutdown_mu_;
    std::condition_variable     shutdown_cv_;

    std::vector<std::thread>    orb_threads_;
};

} // namespace adaptor
