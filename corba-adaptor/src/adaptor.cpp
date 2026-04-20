// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator Implementation (C++11)

#include "adaptor/adaptor.hpp"
#include "adaptor/typed/translate.hpp"

#include "corba-peer/corba_peer.hpp"
#include "tcp-peer/tcp_peer.hpp"

#include <ace/Log_Msg.h>
#include <ace/Reactor.h>
#include <ace/Select_Reactor.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace adaptor {

Adaptor::Adaptor(CORBA::ORB_ptr orb,
                 PortableServer::POA_ptr poa,
                 const AdaptorConfig& config)
    : orb_(CORBA::ORB::_duplicate(orb)),
      poa_(PortableServer::POA::_duplicate(poa)),
      config_(config),
      running_(false),
      shutdown_requested_(false) {
    if (config_.tcp_client_mode == TcpClientMode_Reactor) {
        adaptor_reactor_.reset(new ACE_Reactor(new ACE_Select_Reactor, 1));
        tcp_.reset(new TcpPeer(config_.tcp, adaptor_reactor_.get()));
    } else {
        // Default: standalone client, no reactor.
        tcp_.reset(new TcpPeer(config_.tcp));
    }
    corba_.reset(new CorbaPeer(orb, poa, config_.corba_peer));
    command_.reset(new CommandReceiverServant);
    corba_session_.reset(new corba_peer::CorbaPeerFrameSession);
}

Adaptor::~Adaptor() { stop(); }

void Adaptor::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: starting...\n"));

    naming_.reset(new NamingHelper(orb_.in()));
    if (!naming_->is_valid()) {
        throw std::runtime_error(
            "Adaptor: NameService unavailable; pass "
            "-ORBInitRef NameService=corbaloc:...");
    }

    supplier_.reset(new AdaptorSupplierServant);

    PortableServer::ObjectId_var sup_oid = poa_->activate_object(supplier_.get());
    CORBA::Object_var sup_ref = poa_->id_to_reference(sup_oid.in());
    naming_->bind(CorbaAdaptor::AdaptorSupplier::NAME, sup_ref.in());

    PortableServer::ObjectId_var cmd_oid = poa_->activate_object(command_.get());
    CORBA::Object_var cmd_ref = poa_->id_to_reference(cmd_oid.in());
    naming_->bind(CorbaAdaptor::CommandReceiver::NAME, cmd_ref.in());

    register_builtin_commands();
    wire_tcp_handlers();

    corba_->set_data_callback([this](cpp11::span<const uint8_t> bytes) {
        on_corba_raw_bytes(bytes);
    });
    corba_->set_state_callback([](bool connected) {
        ACE_DEBUG((LM_INFO, "Adaptor: corba-peer %s\n",
                   connected ? "connected" : "disconnected"));
    });

    uint32_t n = config_.orb_threads == 0 ? 1u : config_.orb_threads;
    orb_threads_.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        orb_threads_.push_back(std::thread(&Adaptor::orb_thread_func, this));
    }
    if (adaptor_reactor_) {
        reactor_thread_ = std::thread(&Adaptor::reactor_thread_func, this);
    }

    tcp_->start();
    corba_->start();

    health_thread_ = std::thread(&Adaptor::health_check_loop, this);

    ACE_DEBUG((LM_INFO, "Adaptor: started (%u ORB thread(s))\n",
               static_cast<unsigned>(n)));
}

void Adaptor::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: stopping...\n"));

    {
        std::lock_guard<std::mutex> lock(health_mu_);
        health_cv_.notify_all();
    }
    if (health_thread_.joinable()) health_thread_.join();

    if (tcp_)   tcp_->stop();
    if (corba_) corba_->stop();

    if (adaptor_reactor_) {
        adaptor_reactor_->end_reactor_event_loop();
    }
    if (reactor_thread_.joinable()) reactor_thread_.join();

    if (naming_) {
        naming_->unbind(CorbaAdaptor::AdaptorSupplier::NAME);
        naming_->unbind(CorbaAdaptor::CommandReceiver::NAME);
    }

    if (supplier_) {
        try {
            PortableServer::ObjectId_var oid = poa_->servant_to_id(supplier_.get());
            poa_->deactivate_object(oid.in());
        } catch (const CORBA::Exception&) {}
    }
    if (command_) {
        try {
            PortableServer::ObjectId_var oid = poa_->servant_to_id(command_.get());
            poa_->deactivate_object(oid.in());
        } catch (const CORBA::Exception&) {}
    }

    if (supplier_) {
        supplier_->shutdown("adaptor shutting down");
    }

    try { orb_->shutdown(0); } catch (const CORBA::Exception&) {}
    for (std::size_t i = 0; i < orb_threads_.size(); ++i) {
        if (orb_threads_[i].joinable()) orb_threads_[i].join();
    }
    orb_threads_.clear();

    {
        std::lock_guard<std::mutex> lock(shutdown_mu_);
        shutdown_requested_ = true;
        shutdown_cv_.notify_all();
    }

    ACE_DEBUG((LM_INFO, "Adaptor: stopped\n"));
}

void Adaptor::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(shutdown_mu_);
    shutdown_cv_.notify_all();
}

void Adaptor::wait_for_shutdown() {
    std::unique_lock<std::mutex> lock(shutdown_mu_);
    while (!shutdown_requested_.load()) shutdown_cv_.wait(lock);
}

CommandReceiverServant& Adaptor::command_servant() { return *command_; }
AdaptorSupplierServant& Adaptor::supplier()        { return *supplier_; }
TcpPeer&                Adaptor::tcp_peer()         { return *tcp_; }
CorbaPeer&              Adaptor::corba_peer()       { return *corba_; }

void Adaptor::orb_thread_func() {
    try {
        orb_->run();
    } catch (const CORBA::Exception&) {
        ACE_DEBUG((LM_ERROR, "Adaptor: ORB thread exception\n"));
    }
}

void Adaptor::reactor_thread_func() {
    if (!adaptor_reactor_) return;
    adaptor_reactor_->owner(ACE_OS::thr_self());
    adaptor_reactor_->run_reactor_event_loop();
}

void Adaptor::health_check_loop() {
    std::chrono::seconds interval(config_.health_check_interval_seconds);
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(health_mu_);
            health_cv_.wait_for(lock, interval);
        }
        if (!running_) break;
        if (supplier_) supplier_->sweep_dead_consumers();
    }
}

void Adaptor::wire_tcp_handlers() {
    tcp_->on<tcp_peer::Heartbeat>(
        std::function<void(const tcp_peer::Heartbeat&)>(
            [this](const tcp_peer::Heartbeat& m) {
                supplier_->publish_heartbeat(translate::to_corba(m));
            }));
    tcp_->on<tcp_peer::StatusReport>(
        std::function<void(const tcp_peer::StatusReport&)>(
            [this](const tcp_peer::StatusReport& m) {
                supplier_->publish_status_report(translate::to_corba(m));
            }));
    tcp_->on<tcp_peer::DataPayload>(
        std::function<void(const tcp_peer::DataPayload&)>(
            [this](const tcp_peer::DataPayload& m) {
                supplier_->publish_data_payload(translate::to_corba(m));
            }));
    tcp_->on<tcp_peer::CommandResponse>(
        std::function<void(const tcp_peer::CommandResponse&)>(
            [this](const tcp_peer::CommandResponse& m) {
                supplier_->publish_command_response(translate::to_corba(m));
            }));
}

void Adaptor::on_corba_raw_bytes(cpp11::span<const uint8_t> bytes) {
    std::vector<bgen11::traits::DecodedMessage> decoded;
    {
        std::lock_guard<std::mutex> lock(corba_session_mu_);
        if (!corba_session_) return;
        bgen11::Result<std::vector<bgen11::traits::DecodedMessage> > result =
            corba_session_->decode_frame(bytes);
        if (!result) return;
        decoded = std::move(*result);
    }
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        dispatch_corba_decoded(decoded[i]);
    }
}

void Adaptor::dispatch_corba_decoded(const bgen11::traits::DecodedMessage& msg) {
    if (msg.type_id == corba_peer::TelemetryRecord::TYPE_ID) {
        const corba_peer::TelemetryRecord* m =
            cpp11::any_cast<corba_peer::TelemetryRecord>(&msg.payload);
        if (m) supplier_->publish_telemetry(translate::to_corba(*m));
    } else if (msg.type_id == corba_peer::EventRecord::TYPE_ID) {
        const corba_peer::EventRecord* m =
            cpp11::any_cast<corba_peer::EventRecord>(&msg.payload);
        if (m) supplier_->publish_event(translate::to_corba(*m));
    } else if (msg.type_id == corba_peer::AlarmRecord::TYPE_ID) {
        const corba_peer::AlarmRecord* m =
            cpp11::any_cast<corba_peer::AlarmRecord>(&msg.payload);
        if (m) supplier_->publish_alarm(translate::to_corba(*m));
    }
}

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
