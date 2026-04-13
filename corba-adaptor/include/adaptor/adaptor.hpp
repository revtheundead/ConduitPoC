// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator
//
// Ties together the (Conduit-backed) TCP peer, the CORBA raw-data peer,
// the AdaptorSupplier, and the CommandReceiver into a single cohesive
// bridge.
//
//    TCP Peer  ──▶ tcp_peer::<Msg>  ──▶ translate ──▶ supplier ──▶ N consumers
//    CORBA Peer (raw bytes)
//         └──▶ Conduit ISession::decode_frame ──▶ corba_peer::<Msg>
//                                             ──▶ translate ──▶ supplier ──▶ N consumers
//
// A single AdaptorSupplier publishes all message types.  The
// CommandReceiver is also registered in the Naming Service.  Clients
// discover services by resolving their IDL-declared NAME constants.

#pragma once

#include <adaptor/command_servant.hpp>
#include <adaptor/corba_peer.hpp>
#include <adaptor/naming.hpp>
#include <adaptor/supplier.hpp>
#include <adaptor/tcp_peer.hpp>
#include <adaptor/types.hpp>

#include <conduit/traits/session_traits.hpp>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
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

    /// Health-check interval for the consumer sweep.
    std::chrono::seconds health_check_interval{30};

    /// Number of ORB thread-pool threads (1 = single-threaded, default).
    std::uint32_t orb_threads{1};
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

    /// Initialize all sub-components, wire typed handlers, activate CORBA
    /// servants, and bind them into the Naming Service.
    void start();

    /// Graceful shutdown of all sub-components.
    void stop();

    /// Request shutdown (signal-safe — only sets an atomic flag).
    void request_shutdown();

    /// Block until shutdown is signalled.
    void wait_for_shutdown();

    // --- Access to sub-components for advanced configuration ----------------

    [[nodiscard]] CommandReceiverServant& command_servant() noexcept;
    [[nodiscard]] AdaptorSupplierServant& supplier()        noexcept;
    [[nodiscard]] TcpPeer&                tcp_peer()         noexcept;
    [[nodiscard]] CorbaPeer&              corba_peer()       noexcept;

private:
    void register_builtin_commands();
    void wire_tcp_handlers();
    void dispatch_corba_decoded(
        const conduit::traits::DecodedMessage& msg);
    void on_corba_raw_bytes(std::span<const std::uint8_t> bytes);
    void orb_thread_func();
    void health_check_loop();

    CORBA::ORB_var              orb_;
    PortableServer::POA_var     poa_;
    AdaptorConfig               config_;

    std::unique_ptr<TcpPeer>                    tcp_;
    std::unique_ptr<CorbaPeer>                  corba_;
    std::unique_ptr<CommandReceiverServant>      command_;
    std::unique_ptr<AdaptorSupplierServant>      supplier_;

    /// CORBA-peer codec session — used to decode the raw byte stream
    /// delivered via `RawDataChannel::on_raw_data`.
    std::unique_ptr<conduit::traits::ISession>  corba_session_;
    std::mutex                                  corba_session_mu_;

    std::unique_ptr<NamingHelper>               naming_;

    std::atomic<bool>           running_{false};
    std::atomic<bool>           shutdown_requested_{false};
    std::mutex                  shutdown_mu_;
    std::condition_variable     shutdown_cv_;

    /// Single periodic sweep thread for reaping dead consumers.
    std::thread                 health_thread_;
    std::mutex                  health_mu_;
    std::condition_variable     health_cv_;

    std::vector<std::thread>    orb_threads_;
};

} // namespace adaptor
