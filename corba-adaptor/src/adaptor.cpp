// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Main Orchestrator Implementation

#include <adaptor/adaptor.hpp>
#include <adaptor/codec/session_factory.hpp>

#include <ace/Log_Msg.h>

#include <fstream>
#include <sstream>

namespace adaptor {

// ============================================================================
// Helper — write an IOR string to a file
// ============================================================================

namespace {

void write_ior_file(const std::string& path, const std::string& ior) {
    if (path.empty()) return;

    std::ofstream out(path);
    if (!out) {
        ACE_DEBUG((LM_ERROR, "Adaptor: failed to write IOR to %s\n", path.c_str()));
        return;
    }
    out << ior;
    ACE_DEBUG((LM_INFO, "Adaptor: wrote IOR to %s\n", path.c_str()));
}

} // anonymous namespace

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
    // Create sub-components
    tcp_      = std::make_unique<TcpPeer>(config_.tcp);
    corba_    = std::make_unique<CorbaPeer>(orb, poa, config_.corba_peer);
    supplier_ = std::make_unique<DataSupplierServant>();
    command_  = std::make_unique<CommandReceiverServant>();

    // Initialize codec bridge with session factories
    codec_ = std::make_unique<codec::CodecBridge>();
    codec_->set_pipeline(PeerId::tcp_peer,
        std::make_unique<codec::CodecPipeline>(
            PeerId::tcp_peer, codec::create_tcp_peer_session()));
    codec_->set_pipeline(PeerId::corba_peer,
        std::make_unique<codec::CodecPipeline>(
            PeerId::corba_peer, codec::create_corba_peer_session()));
}

Adaptor::~Adaptor() {
    stop();
}

void Adaptor::start() {
    if (running_.exchange(true)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: starting...\n"));

    // Wire data callbacks — raw bytes feed into codec, decoded messages
    // get published to consumers.
    //
    // Data flow:
    //   Peer → raw bytes → CodecBridge → decoded messages → DataSupplier
    //

    // Codec produces decoded messages → publish to consumers
    codec_->set_message_callback([this](codec::AdaptorMessage msg) {
        InternalPacket pkt;
        pkt.source      = msg.source;
        pkt.received_at = std::chrono::steady_clock::now();
        pkt.label       = std::move(msg.type_name);
        pkt.payload     = std::move(msg.raw_bytes);
        on_data_received(std::move(pkt));
    });

    codec_->set_error_callback([](PeerId peer, const std::string& err) {
        ACE_DEBUG((LM_WARNING, "Adaptor: codec error on '%s': %s\n",
                   to_string(peer), err.c_str()));
    });

    // Peers produce raw bytes → feed into codec bridge
    tcp_->set_data_callback([this](InternalPacket pkt) {
        codec_->on_bytes_received(PeerId::tcp_peer,
            std::span<const uint8_t>(pkt.payload));
    });

    corba_->set_data_callback([this](InternalPacket pkt) {
        codec_->on_bytes_received(PeerId::corba_peer,
            std::span<const uint8_t>(pkt.payload));
    });

    // Peer state logging
    auto state_handler = [](PeerId peer, bool connected) {
        ACE_DEBUG((LM_INFO, "Adaptor: peer '%s' %s\n",
                   to_string(peer),
                   connected ? "connected" : "disconnected"));
    };
    tcp_->set_state_callback(state_handler);
    corba_->set_state_callback(state_handler);

    // Register built-in commands
    register_builtin_commands();

    // Activate CORBA servants and publish IORs
    {
        PortableServer::ObjectId_var oid = poa_->activate_object(supplier_.get());
        CORBA::Object_var ref = poa_->id_to_reference(oid.in());
        CORBA::String_var ior = orb_->object_to_string(ref.in());
        write_ior_file(config_.supplier_ior_file, ior.in());
        ACE_DEBUG((LM_INFO, "Adaptor: DataSupplier IOR: %s\n", ior.in()));
    }
    {
        PortableServer::ObjectId_var oid = poa_->activate_object(command_.get());
        CORBA::Object_var ref = poa_->id_to_reference(oid.in());
        CORBA::String_var ior = orb_->object_to_string(ref.in());
        write_ior_file(config_.command_ior_file, ior.in());
        ACE_DEBUG((LM_INFO, "Adaptor: CommandReceiver IOR: %s\n", ior.in()));
    }

    // Start health checks
    supplier_->start_health_checks(config_.health_check_interval);

    supplier_->set_reap_callback([](CorbaAdaptor::SubscriptionId id) {
        ACE_DEBUG((LM_WARNING, "Adaptor: consumer %u reaped\n", id));
    });

    // Launch ORB thread pool
    auto n_threads = std::max(config_.orb_threads, 1u);
    orb_threads_.reserve(n_threads);
    for (uint32_t i = 0; i < n_threads; ++i) {
        orb_threads_.emplace_back([this] { orb_thread_func(); });
    }

    // Start peers
    tcp_->start();
    corba_->start();

    ACE_DEBUG((LM_INFO, "Adaptor: started successfully (%u ORB thread(s))\n",
               n_threads));
}

void Adaptor::stop() {
    if (!running_.exchange(false)) return;

    ACE_DEBUG((LM_INFO, "Adaptor: stopping...\n"));

    // Stop peers first (stops producing data)
    tcp_->stop();
    corba_->stop();

    // Deactivate CORBA servants before shutting down the supplier's consumer
    // notifications — prevents new CORBA calls from arriving mid-teardown
    try {
        PortableServer::ObjectId_var oid;
        oid = poa_->servant_to_id(supplier_.get());
        poa_->deactivate_object(oid.in());
    } catch (const CORBA::Exception&) {}

    try {
        PortableServer::ObjectId_var oid;
        oid = poa_->servant_to_id(command_.get());
        poa_->deactivate_object(oid.in());
    } catch (const CORBA::Exception&) {}

    // Now notify consumers and clean up
    supplier_->shutdown("adaptor shutting down");

    // Shut down the ORB and join threads
    orb_->shutdown(false);
    for (auto& t : orb_threads_) {
        if (t.joinable()) t.join();
    }
    orb_threads_.clear();

    // Signal shutdown waiters
    {
        std::lock_guard lock(shutdown_mu_);
        shutdown_requested_ = true;
        shutdown_cv_.notify_all();
    }

    ACE_DEBUG((LM_INFO, "Adaptor: stopped\n"));
}

void Adaptor::request_shutdown() {
    // Signal-safe: only sets an atomic flag and notifies the condvar.
    // The actual teardown happens in stop(), called from the main thread.
    shutdown_requested_.store(true, std::memory_order_release);
    std::lock_guard lock(shutdown_mu_);
    shutdown_cv_.notify_all();
}

void Adaptor::wait_for_shutdown() {
    std::unique_lock lock(shutdown_mu_);
    shutdown_cv_.wait(lock, [this] { return shutdown_requested_.load(); });
}

bool Adaptor::send_to_peer(PeerId peer, std::span<const uint8_t> data) {
    switch (peer) {
        case PeerId::tcp_peer:   return tcp_->send(data);
        case PeerId::corba_peer: return corba_->send(data);
    }
    return false;
}

bool Adaptor::send_message(PeerId peer, uint64_t type_id, const std::any& payload) {
    auto encoded = codec_->encode(peer, type_id, payload);
    if (encoded.empty()) return false;
    return send_to_peer(peer, std::span<const uint8_t>(encoded));
}

CommandReceiverServant& Adaptor::command_servant() noexcept { return *command_; }
DataSupplierServant&    Adaptor::supplier()        noexcept { return *supplier_; }
TcpPeer&               Adaptor::tcp_peer()         noexcept { return *tcp_; }
CorbaPeer&             Adaptor::corba_peer()       noexcept { return *corba_; }
codec::CodecBridge&    Adaptor::codec_bridge()     noexcept { return *codec_; }

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

void Adaptor::on_data_received(InternalPacket pkt) {
    auto corba_pkt = pkt.to_corba();
    supplier_->publish(corba_pkt);
}

void Adaptor::register_builtin_commands() {
    // "status" query — returns connection state of both peers
    command_->register_query("status", [this](const std::string&) {
        CorbaAdaptor::CommandResult res;
        res.status = CorbaAdaptor::CMD_OK;

        std::ostringstream ss;
        ss << "tcp_peer=" << (tcp_->is_connected() ? "connected" : "disconnected")
           << " corba_peer=" << (corba_->is_connected() ? "connected" : "disconnected")
           << " subscribers=" << supplier_->subscriber_count();
        res.message = CORBA::string_dup(ss.str().c_str());
        return res;
    });

    // "send_tcp" command — forward params to TCP peer
    command_->register_command("send_tcp",
        [this](const std::string&, std::span<const uint8_t> params) {
            CorbaAdaptor::CommandResult res;
            if (tcp_->send(params)) {
                res.status  = CorbaAdaptor::CMD_OK;
                res.message = CORBA::string_dup("sent");
            } else {
                res.status  = CorbaAdaptor::CMD_ERROR;
                res.message = CORBA::string_dup("tcp peer not connected");
            }
            return res;
        });

    // "send_corba" command — forward params to CORBA peer
    command_->register_command("send_corba",
        [this](const std::string&, std::span<const uint8_t> params) {
            CorbaAdaptor::CommandResult res;
            if (corba_->send(params)) {
                res.status  = CorbaAdaptor::CMD_OK;
                res.message = CORBA::string_dup("sent");
            } else {
                res.status  = CorbaAdaptor::CMD_ERROR;
                res.message = CORBA::string_dup("corba peer not connected");
            }
            return res;
        });

    // "codec_info" query — returns registered codec pipeline info
    command_->register_query("codec_info", [this](const std::string&) {
        CorbaAdaptor::CommandResult res;
        res.status = CorbaAdaptor::CMD_OK;

        std::ostringstream ss;
        ss << "tcp_peer_codec=" << (codec_->pipeline(PeerId::tcp_peer) ? "active" : "none")
           << " corba_peer_codec=" << (codec_->pipeline(PeerId::corba_peer) ? "active" : "none");
        res.message = CORBA::string_dup(ss.str().c_str());
        return res;
    });

    // Shutdown handler — use request_shutdown() for signal-safe path
    command_->set_shutdown_callback([this] {
        ACE_DEBUG((LM_INFO, "Adaptor: shutdown requested via command\n"));
        request_shutdown();
    });
}

} // namespace adaptor
