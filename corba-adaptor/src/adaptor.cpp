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

    // 1. Resolve the naming service.
    naming_ = std::make_unique<NamingHelper>(orb_.in());
    if (!naming_->is_valid()) {
        throw std::runtime_error(
            "Adaptor: NameService unavailable; pass "
            "-ORBInitRef NameService=corbaloc:...");
    }

    // 2. Create, activate and bind the supplier and command receiver.
    supplier_ = std::make_unique<AdaptorSupplierServant>();

    auto activate_and_bind =
        [this](PortableServer::Servant servant, const char* name) {
            PortableServer::ObjectId_var oid =
                poa_->activate_object(servant);
            CORBA::Object_var ref = poa_->id_to_reference(oid.in());
            naming_->bind(name, ref.in());
        };

    activate_and_bind(supplier_.get(),
                      CorbaAdaptor::AdaptorSupplier::NAME);
    activate_and_bind(command_.get(),
                      CorbaAdaptor::CommandReceiver::NAME);

    // 3. Register built-in commands.
    register_builtin_commands();

    // 4. Wire typed TCP handlers (before tcp_->start()).
    wire_tcp_handlers();

    // 5. Wire CORBA raw-byte callback — bytes run through the codec
    //    session and each decoded message is dispatched by type_id.
    corba_->set_data_callback([this](std::span<const std::uint8_t> bytes) {
        on_corba_raw_bytes(bytes);
    });
    corba_->set_state_callback([](bool connected) {
        ACE_DEBUG((LM_INFO, "Adaptor: corba-peer %s\n",
                   connected ? "connected" : "disconnected"));
    });

    // 6. Launch ORB thread pool.
    auto n_threads = std::max(config_.orb_threads, 1u);
    orb_threads_.reserve(n_threads);
    for (std::uint32_t i = 0; i < n_threads; ++i) {
        orb_threads_.emplace_back([this] { orb_thread_func(); });
    }

    // 7. Start peers.
    tcp_->start();
    corba_->start();

    // 8. Start the periodic health-check sweep.
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

    // Wake and join the health sweep thread.
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

    // Remove naming-service bindings (best-effort, noexcept).
    if (naming_) {
        naming_->unbind(CorbaAdaptor::AdaptorSupplier::NAME);
        naming_->unbind(CorbaAdaptor::CommandReceiver::NAME);
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

    deactivate(supplier_.get());
    deactivate(command_.get());

    // Notify consumers of shutdown.
    if (supplier_) {
        supplier_->shutdown("adaptor shutting down");
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
AdaptorSupplierServant& Adaptor::supplier()        noexcept { return *supplier_; }
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

// --- Health-check sweep thread ---------------------------------------------

void Adaptor::health_check_loop() {
    const auto interval = config_.health_check_interval;
    while (running_) {
        {
            std::unique_lock lock(health_mu_);
            health_cv_.wait_for(lock, interval,
                [this] { return !running_.load(); });
        }
        if (!running_) break;

        supplier_->sweep_dead_consumers();
    }
}

// --- TCP handler wiring -----------------------------------------------------

void Adaptor::wire_tcp_handlers() {
    tcp_->on<tcp_peer::Heartbeat>(
        [this](const tcp_peer::Heartbeat& m) {
            supplier_->publish_heartbeat(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::StatusReport>(
        [this](const tcp_peer::StatusReport& m) {
            supplier_->publish_status_report(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::DataPayload>(
        [this](const tcp_peer::DataPayload& m) {
            supplier_->publish_data_payload(translate::to_corba(m));
        });
    tcp_->on<tcp_peer::CommandResponse>(
        [this](const tcp_peer::CommandResponse& m) {
            supplier_->publish_command_response(translate::to_corba(m));
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
            supplier_->publish_telemetry(translate::to_corba(m));
        } else if (msg.type_id == corba_peer::EventRecord::TYPE_ID) {
            const auto& m = std::any_cast<const corba_peer::EventRecord&>(msg.payload);
            supplier_->publish_event(translate::to_corba(m));
        } else if (msg.type_id == corba_peer::AlarmRecord::TYPE_ID) {
            const auto& m = std::any_cast<const corba_peer::AlarmRecord&>(msg.payload);
            supplier_->publish_alarm(translate::to_corba(m));
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
           << " subscribers=" << supplier_->subscriber_count();
        res.message = CORBA::string_dup(ss.str().c_str());
        return res;
    });

    command_->set_shutdown_callback([this] {
        ACE_DEBUG((LM_INFO, "Adaptor: shutdown requested via command\n"));
        request_shutdown();
    });
}

} // namespace adaptor
