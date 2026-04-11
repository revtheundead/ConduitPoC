// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator
//
// Ties together the (Conduit-backed) TCP peer, the CORBA raw-data peer,
// the full set of typed named suppliers, and the command receiver into a
// single cohesive bridge.
//
//    TCP Peer  ──▶ tcp_peer::<Msg>  ──▶ translate ──▶ typed supplier ──▶ N consumers
//    CORBA Peer (raw bytes)
//         └──▶ Conduit ISession::decode_frame ──▶ corba_peer::<Msg>
//                                             ──▶ translate ──▶ typed supplier ──▶ N consumers
//
// All suppliers + the command receiver are registered in the CORBA Naming
// Service under their IDL-declared `NAME` constants (e.g.
// "CorbaAdaptor/Heartbeat"). Clients discover suppliers by name — there is
// no dynamic filtering and no IOR file exchange.

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
#include <functional>
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

    /// Health-check interval for each supplier's consumer sweep.
    std::chrono::seconds health_check_interval{30};

    /// Number of ORB thread-pool threads (1 = single-threaded, default).
    std::uint32_t orb_threads{1};
};

// ============================================================================
// TypedSuppliers
// ============================================================================
//
// Owns one servant per leaf message type. The Adaptor constructs these at
// start(), activates them on the POA, and binds each one into the Naming
// Service under its IDL-declared NAME constant.

struct TypedSuppliers {
    // TCP peer side
    std::unique_ptr<HeartbeatSupplierServant>       heartbeat;
    std::unique_ptr<StatusReportSupplierServant>    status_report;
    std::unique_ptr<DataPayloadSupplierServant>     data_payload;
    std::unique_ptr<CommandResponseSupplierServant> command_response;

    // CORBA peer side
    std::unique_ptr<TelemetrySupplierServant>       telemetry;
    std::unique_ptr<EventSupplierServant>           event;
    std::unique_ptr<AlarmSupplierServant>           alarm;
};

// ============================================================================
// SupplierEntry
// ============================================================================
//
// Type-erased handle to one typed supplier servant. The Adaptor builds a
// vector of these at `start()` and drives every bulk operation (bind in the
// naming service, health-check sweep, shutdown notification, status query
// formatting, POA deactivation) off it — so adding a new typed message
// requires touching exactly one location in `Adaptor::build_supplier_entries`
// rather than a dozen scattered sites.

struct SupplierEntry {
    /// POA servant pointer, needed for activation/deactivation. The actual
    /// owning `unique_ptr` lives in `TypedSuppliers`.
    PortableServer::Servant                 servant{nullptr};

    /// Fully-qualified naming-service path (e.g. "CorbaAdaptor/Heartbeat").
    /// Comes from the IDL `NAME` constant on the supplier interface.
    const char*                             idl_name{nullptr};

    /// Short label used in the `status` query output.
    const char*                             status_label{nullptr};

    /// Invoke `sweep_dead_consumers()` on the underlying servant.
    std::function<void()>                   sweep;

    /// Notify all consumers of shutdown and drain the subscription map.
    std::function<void(const std::string&)> shutdown;

    /// Current subscriber count, used by the `status` builtin command.
    std::function<CORBA::ULong()>           subscriber_count;
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
    [[nodiscard]] TypedSuppliers&         suppliers()        noexcept;
    [[nodiscard]] TcpPeer&                tcp_peer()         noexcept;
    [[nodiscard]] CorbaPeer&              corba_peer()       noexcept;

private:
    void register_builtin_commands();
    void wire_tcp_handlers();
    void dispatch_corba_decoded(
        const conduit::traits::DecodedMessage& msg);
    void on_corba_raw_bytes(std::span<const std::uint8_t> bytes);
    void build_supplier_entries();
    void activate_and_bind_suppliers();
    void unbind_all();
    void orb_thread_func();
    void health_check_loop();

    CORBA::ORB_var              orb_;
    PortableServer::POA_var     poa_;
    AdaptorConfig               config_;

    std::unique_ptr<TcpPeer>                tcp_;
    std::unique_ptr<CorbaPeer>              corba_;
    std::unique_ptr<CommandReceiverServant> command_;

    TypedSuppliers                          suppliers_;

    /// Type-erased handles over every typed supplier. Built once at
    /// `start()`. Every bulk operation (bind, unbind, shutdown, sweep,
    /// status) iterates this vector instead of enumerating each servant
    /// by name — so adding a new message type is a one-location edit.
    std::vector<SupplierEntry>              supplier_entries_;

    /// CORBA-peer codec session — used to decode the raw byte stream
    /// delivered via `RawDataChannel::on_raw_data`.
    std::unique_ptr<conduit::traits::ISession> corba_session_;
    std::mutex                              corba_session_mu_;

    std::unique_ptr<NamingHelper>           naming_;

    std::atomic<bool>           running_{false};
    std::atomic<bool>           shutdown_requested_{false};
    std::mutex                  shutdown_mu_;
    std::condition_variable     shutdown_cv_;

    /// Single periodic sweep thread that walks `supplier_entries_` and
    /// calls `sweep()` on each. Replaces the per-supplier health thread
    /// that used to live inside `NamedSupplierServant`.
    std::thread                 health_thread_;
    std::mutex                  health_mu_;
    std::condition_variable     health_cv_;

    std::vector<std::thread>    orb_threads_;
};

} // namespace adaptor
