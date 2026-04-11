// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator Implementation

#include <adaptor/adaptor.hpp>
#include <adaptor/typed/translate.hpp>

#include <corba-peer/corba_peer.hpp>
#include <tcp-peer/tcp_peer.hpp>

#include <ace/Log_Msg.h>

#include <algorithm>
#include <any>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace adaptor {

// ============================================================================
// Adaptor
// ============================================================================

Adaptor::Adaptor(CORBA::ORB_ptr orb,
                 PortableServer::POA_ptr poa,
                 AdaptorConfig config)
    : orb_(CORBA::ORB::_duplicate(orb))
    , poa_(PortableServer::POA::_duplicate(poa))
    , config_(std::move(config))
{
    tcp_     = std::make_unique<TcpPeer>(config_.tcp);
    corba_   = std::make_unique<CorbaPeer>(orb, poa, config_.corba_peer);
    command_ = std::make_unique<CommandReceiverServant>();

    // CORBA peer is raw-bytes-only; the adaptor owns the codec session.
    corba_session_ = std::make_unique<corba_peer::CorbaPeerFrameSession>();
}

Adaptor::~Adaptor() {
    stop();
}

// ============================================================================
// start
// ============================================================================

void Adaptor::start() {
    if (running_.exchange(true)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: starting...\n"));

    // 1. Resolve the naming service before touching any servant.
    naming_ = std::make_unique<NamingHelper>(orb_.in());
    if (!naming_->is_valid()) {
        throw std::runtime_error(
            "Adaptor: NameService unavailable; pass "
            "-ORBInitRef NameService=corbaloc:...");
    }

    // 2. Construct typed suppliers, activate them on the POA, and bind
    //    each one into the Naming Service under its IDL NAME constant
    //    (also binds the CommandReceiver).
    activate_and_bind_suppliers();

    // 3. Build the type-erased supplier registry. Every bulk operation
    //    below (sweep, shutdown, unbind, status query) iterates this
    //    single vector instead of enumerating each servant by name.
    build_supplier_entries();

    // 4. Register built-in commands.
    register_builtin_commands();

    // 5. Wire typed TCP handlers (before tcp_->start()).
    wire_tcp_handlers();

    // 6. Wire CORBA raw-byte callback — bytes run through the codec
    //    session and each decoded message is dispatched by type_id.
    corba_->set_data_callback([this](std::span<const std::uint8_t> bytes) {
        on_corba_raw_bytes(bytes);
    });
    corba_->set_state_callback([](bool connected) {
        ACE_DEBUG((LM_INFO, "Adaptor: corba-peer %s\n",
                   connected ? "connected" : "disconnected"));
    });

    // 7. Launch ORB thread pool.
    auto n_threads = std::max(config_.orb_threads, 1u);
    orb_threads_.reserve(n_threads);
    for (std::uint32_t i = 0; i < n_threads; ++i) {
        orb_threads_.emplace_back([this] { orb_thread_func(); });
    }

    // 8. Start peers.
    tcp_->start();
    corba_->start();

    // 9. Start the single periodic health-check sweep. It walks the
    //    supplier registry on every tick and reaps dead consumers.
    health_thread_ = std::thread([this] { health_check_loop(); });

    ACE_DEBUG((LM_INFO, "Adaptor: started successfully (%u ORB thread(s))\n",
               n_threads));
}

// ============================================================================
// stop
// ============================================================================

void Adaptor::stop() {
    if (!running_.exchange(false)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: stopping...\n"));

    // Wake and join the health sweep thread before touching suppliers.
    {
        std::lock_guard lock(health_mu_);
        health_cv_.notify_all();
    }
    if (health_thread_.joinable()) {
        health_thread_.join();
    }

    // Stop peers first so no more data flows in.
    tcp_->stop();
    corba_->stop();

    // Remove all naming-service bindings (best-effort, noexcept).
    if (naming_) {
        unbind_all();
    }

    // Deactivate CORBA servants before tearing down subscriptions.
    auto deactivate = [this](PortableServer::Servant servant) {
        if (!servant) return;
        try {
            PortableServer::ObjectId_var oid = poa_->servant_to_id(servant);
            poa_->deactivate_object(oid.in());
        } catch (const CORBA::Exception&) {
            // Already gone
        }
    };

    for (const auto& entry : supplier_entries_) {
        deactivate(entry.servant);
    }
    deactivate(command_.get());

    // Notify consumers (shutdown()) for each supplier via the type-erased
    // hook stored in the registry.
    const std::string reason = "adaptor shutting down";
    for (const auto& entry : supplier_entries_) {
        if (entry.shutdown) entry.shutdown(reason);
    }

    // Shut down the ORB and join threads.
    try {
        orb_->shutdown(false);
    } catch (const CORBA::Exception&) {}
    for (auto& t : orb_threads_) {
        if (t.joinable()) t.join();
    }
    orb_threads_.clear();

    // Signal shutdown waiters.
    {
        std::lock_guard lock(shutdown_mu_);
        shutdown_requested_ = true;
        shutdown_cv_.notify_all();
    }

    ACE_DEBUG((LM_INFO, "Adaptor: stopped\n"));
}

void Adaptor::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    std::lock_guard lock(shutdown_mu_);
    shutdown_cv_.notify_all();
}

void Adaptor::wait_for_shutdown() {
    std::unique_lock lock(shutdown_mu_);
    shutdown_cv_.wait(lock, [this] { return shutdown_requested_.load(); });
}

CommandReceiverServant& Adaptor::command_servant() noexcept { return *command_; }
TypedSuppliers&         Adaptor::suppliers()        noexcept { return suppliers_; }
TcpPeer&                Adaptor::tcp_peer()         noexcept { return *tcp_; }
CorbaPeer&              Adaptor::corba_peer()       noexcept { return *corba_; }

// ============================================================================
// Private
// ============================================================================

void Adaptor::orb_thread_func() {
    try {
        orb_->run();
    } catch (const CORBA::Exception& ex) {
        ACE_DEBUG((LM_ERROR, "Adaptor: ORB thread exception: %s\n",
                   ex._info().c_str()));
    }
}

// --- Single health-check sweep thread --------------------------------------

void Adaptor::health_check_loop() {
    const auto interval = config_.health_check_interval;
    while (running_) {
        {
            std::unique_lock lock(health_mu_);
            health_cv_.wait_for(lock, interval,
                [this] { return !running_.load(); });
        }
        if (!running_) break;

        for (const auto& entry : supplier_entries_) {
            if (entry.sweep) entry.sweep();
        }
    }
}

// --- Typed supplier activation + naming-service binding ---------------------

void Adaptor::activate_and_bind_suppliers() {
    suppliers_.heartbeat        = std::make_unique<HeartbeatSupplierServant>();
    suppliers_.status_report    = std::make_unique<StatusReportSupplierServant>();
    suppliers_.data_payload     = std::make_unique<DataPayloadSupplierServant>();
    suppliers_.command_response = std::make_unique<CommandResponseSupplierServant>();
    suppliers_.telemetry        = std::make_unique<TelemetrySupplierServant>();
    suppliers_.event            = std::make_unique<EventSupplierServant>();
    suppliers_.alarm            = std::make_unique<AlarmSupplierServant>();

    auto activate_and_bind =
        [this](PortableServer::Servant servant, const char* name) {
            PortableServer::ObjectId_var oid =
                poa_->activate_object(servant);
            CORBA::Object_var ref = poa_->id_to_reference(oid.in());
            naming_->bind(name, ref.in());
        };

    activate_and_bind(suppliers_.heartbeat.get(),
                      CorbaAdaptor::HeartbeatSupplier::NAME);
    activate_and_bind(suppliers_.status_report.get(),
                      CorbaAdaptor::StatusReportSupplier::NAME);
    activate_and_bind(suppliers_.data_payload.get(),
                      CorbaAdaptor::DataPayloadSupplier::NAME);
    activate_and_bind(suppliers_.command_response.get(),
                      CorbaAdaptor::CommandResponseSupplier::NAME);
    activate_and_bind(suppliers_.telemetry.get(),
                      CorbaAdaptor::TelemetrySupplier::NAME);
    activate_and_bind(suppliers_.event.get(),
                      CorbaAdaptor::EventSupplier::NAME);
    activate_and_bind(suppliers_.alarm.get(),
                      CorbaAdaptor::AlarmSupplier::NAME);
    activate_and_bind(command_.get(),
                      CorbaAdaptor::CommandReceiver::NAME);
}

void Adaptor::build_supplier_entries() {
    // Single source of truth for the typed-supplier list. Every bulk
    // operation (sweep, shutdown, unbind, status query, POA deactivation)
    // iterates this vector — adding a new typed message means adding a
    // field to `TypedSuppliers`, one `add(...)` call here, and one new
    // `tcp_->on<T>` or `dispatch_corba_decoded` arm.
    supplier_entries_.clear();
    supplier_entries_.reserve(7);

    auto add = [this](auto* servant,
                      const char* idl_name,
                      const char* status_label) {
        SupplierEntry e;
        e.servant          = servant;
        e.idl_name         = idl_name;
        e.status_label     = status_label;
        e.sweep            = [servant] { servant->sweep_dead_consumers(); };
        e.shutdown         = [servant](const std::string& r) { servant->shutdown(r); };
        e.subscriber_count = [servant] { return servant->subscriber_count(); };
        supplier_entries_.push_back(std::move(e));
    };

    add(suppliers_.heartbeat.get(),
        CorbaAdaptor::HeartbeatSupplier::NAME,        "heartbeat");
    add(suppliers_.status_report.get(),
        CorbaAdaptor::StatusReportSupplier::NAME,     "status_report");
    add(suppliers_.data_payload.get(),
        CorbaAdaptor::DataPayloadSupplier::NAME,      "data_payload");
    add(suppliers_.command_response.get(),
        CorbaAdaptor::CommandResponseSupplier::NAME,  "command_response");
    add(suppliers_.telemetry.get(),
        CorbaAdaptor::TelemetrySupplier::NAME,        "telemetry");
    add(suppliers_.event.get(),
        CorbaAdaptor::EventSupplier::NAME,            "event");
    add(suppliers_.alarm.get(),
        CorbaAdaptor::AlarmSupplier::NAME,            "alarm");
}

void Adaptor::unbind_all() {
    for (const auto& entry : supplier_entries_) {
        naming_->unbind(entry.idl_name);
    }
    naming_->unbind(CorbaAdaptor::CommandReceiver::NAME);
}

// --- TCP handler wiring -----------------------------------------------------

void Adaptor::wire_tcp_handlers() {
    tcp_->on<tcp_peer::Heartbeat>(
        [this](const tcp_peer::Heartbeat& m) {
            suppliers_.heartbeat->publish(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::StatusReport>(
        [this](const tcp_peer::StatusReport& m) {
            suppliers_.status_report->publish(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::DataPayload>(
        [this](const tcp_peer::DataPayload& m) {
            suppliers_.data_payload->publish(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::CommandResponse>(
        [this](const tcp_peer::CommandResponse& m) {
            suppliers_.command_response->publish(translate::to_corba(m));
        });
}

// --- CORBA raw-byte callback + typed dispatch -------------------------------

void Adaptor::on_corba_raw_bytes(std::span<const std::uint8_t> bytes) {
    std::vector<conduit::traits::DecodedMessage> decoded;
    {
        std::lock_guard lock(corba_session_mu_);
        if (!corba_session_) return;
        auto result = corba_session_->decode_frame(bytes);
        if (!result) {
            ACE_DEBUG((LM_WARNING,
                "Adaptor: corba-peer decode error: %s\n",
                result.error().format_short().c_str()));
            return;
        }
        decoded = std::move(*result);
    }

    for (const auto& msg : decoded) {
        dispatch_corba_decoded(msg);
    }
}

void Adaptor::dispatch_corba_decoded(const conduit::traits::DecodedMessage& msg) {
    try {
        if (msg.type_id == corba_peer::TelemetryRecord::TYPE_ID) {
            const auto& m = std::any_cast<const corba_peer::TelemetryRecord&>(msg.payload);
            suppliers_.telemetry->publish(translate::to_corba(m));
        } else if (msg.type_id == corba_peer::EventRecord::TYPE_ID) {
            const auto& m = std::any_cast<const corba_peer::EventRecord&>(msg.payload);
            suppliers_.event->publish(translate::to_corba(m));
        } else if (msg.type_id == corba_peer::AlarmRecord::TYPE_ID) {
            const auto& m = std::any_cast<const corba_peer::AlarmRecord&>(msg.payload);
            suppliers_.alarm->publish(translate::to_corba(m));
        } else {
            ACE_DEBUG((LM_DEBUG,
                "Adaptor: corba-peer dropped message with unhandled type_id=%llu\n",
                static_cast<unsigned long long>(msg.type_id)));
        }
    } catch (const std::bad_any_cast& ex) {
        ACE_DEBUG((LM_WARNING,
            "Adaptor: corba-peer dispatch bad_any_cast (type_id=%llu): %s\n",
            static_cast<unsigned long long>(msg.type_id), ex.what()));
    }
}

// --- Built-in commands ------------------------------------------------------

void Adaptor::register_builtin_commands() {
    command_->register_query("status", [this](const std::string&) {
        CorbaAdaptor::CommandResult res;
        res.status = CorbaAdaptor::CMD_OK;

        std::ostringstream ss;
        ss << "tcp_peer="    << (tcp_->is_connected()   ? "connected" : "disconnected")
           << " corba_peer=" << (corba_->is_connected() ? "connected" : "disconnected")
           << " subscribers";
        for (const auto& entry : supplier_entries_) {
            ss << ' ' << entry.status_label << '='
               << (entry.subscriber_count ? entry.subscriber_count() : 0u);
        }
        res.message = CORBA::string_dup(ss.str().c_str());
        return res;
    });

    command_->set_shutdown_callback([this] {
        ACE_DEBUG((LM_INFO, "Adaptor: shutdown requested via command\n"));
        request_shutdown();
    });
}

} // namespace adaptor
