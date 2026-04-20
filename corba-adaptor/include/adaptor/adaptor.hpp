// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator (C++11)

#ifndef ADAPTOR_ADAPTOR_HPP
#define ADAPTOR_ADAPTOR_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "compat11/compat11.hpp"
#include "bgen11/bgen11.hpp"

#include "adaptor/command_servant.hpp"
#include "adaptor/corba_peer.hpp"
#include "adaptor/naming.hpp"
#include "adaptor/supplier.hpp"
#include "adaptor/tcp_peer.hpp"
#include "adaptor/types.hpp"

#include "corba-peer/sessions.hpp"

#include <ace/Reactor.h>
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

namespace adaptor {

enum TcpClientMode {
    // Adaptor does not touch any ACE_Reactor; TCP peer runs a dedicated
    // receive thread.  Safe to use when the reactor is owned by another
    // subsystem.  Default.
    TcpClientMode_Standalone = 0,
    // Adaptor spins up a private ACE_Reactor on its own thread and uses
    // an ACE_Event_Handler-based TCP client.  Pick this only when you
    // have a reason to want reactor-driven I/O (e.g. for coalesced
    // wake-ups against many sockets).
    TcpClientMode_Reactor = 1
};

struct AdaptorConfig {
    TcpPeerConfig   tcp;
    CorbaPeerConfig corba_peer;
    uint32_t        health_check_interval_seconds;
    uint32_t        orb_threads;
    TcpClientMode   tcp_client_mode;

    AdaptorConfig()
        : health_check_interval_seconds(30),
          orb_threads(1),
          tcp_client_mode(TcpClientMode_Standalone) {}
};

class Adaptor {
public:
    Adaptor(CORBA::ORB_ptr orb,
            PortableServer::POA_ptr poa,
            const AdaptorConfig& config);
    ~Adaptor();

    Adaptor(const Adaptor&);
    Adaptor& operator=(const Adaptor&);

    void start();
    void stop();
    void request_shutdown();
    void wait_for_shutdown();

    CommandReceiverServant& command_servant();
    AdaptorSupplierServant& supplier();
    TcpPeer&                tcp_peer();
    CorbaPeer&              corba_peer();

private:
    void register_builtin_commands();
    void wire_tcp_handlers();
    void dispatch_corba_decoded(const bgen11::traits::DecodedMessage& msg);
    void on_corba_raw_bytes(cpp11::span<const uint8_t> bytes);
    void orb_thread_func();
    void reactor_thread_func();
    void health_check_loop();

    CORBA::ORB_var          orb_;
    PortableServer::POA_var poa_;
    AdaptorConfig           config_;

    std::unique_ptr<ACE_Reactor>           adaptor_reactor_;

    std::unique_ptr<TcpPeer>               tcp_;
    std::unique_ptr<CorbaPeer>             corba_;
    std::unique_ptr<CommandReceiverServant> command_;
    std::unique_ptr<AdaptorSupplierServant> supplier_;

    std::unique_ptr<corba_peer::CorbaPeerFrameSession> corba_session_;
    std::mutex corba_session_mu_;

    std::unique_ptr<NamingHelper> naming_;

    std::atomic<bool>       running_;
    std::atomic<bool>       shutdown_requested_;
    std::mutex              shutdown_mu_;
    std::condition_variable shutdown_cv_;

    std::thread             health_thread_;
    std::mutex              health_mu_;
    std::condition_variable health_cv_;

    std::thread                 reactor_thread_;
    std::vector<std::thread>    orb_threads_;
};

} // namespace adaptor

#endif
