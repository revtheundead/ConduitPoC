// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - CORBA Raw-Data Peer Implementation
//
// Connects to a remote RawDataChannel, registers our RawDataCallback
// servant to receive inbound data, and exposes send() for outbound.
// Reconnects automatically when the channel becomes unreachable.
// Reconnection runs on a dedicated thread with proper lifecycle management.

#include <adaptor/corba_peer.hpp>

#include <ace/Log_Msg.h>
#include <ace/OS_NS_unistd.h>
#include <ace/Time_Value.h>

#include <condition_variable>

namespace adaptor {

// ============================================================================
// RawDataCallbackServant — receives inbound raw data
// ============================================================================

class RawDataCallbackServant : public POA_CorbaAdaptor::RawDataCallback {
public:
    explicit RawDataCallbackServant(DataReceivedCallback& cb,
                                    PeerStateCallback& state_cb)
        : data_cb_(cb), state_cb_(state_cb) {}

    void on_raw_data(const CorbaAdaptor::OctetSeq& data) override {
        if (data_cb_) {
            InternalPacket pkt;
            pkt.source      = PeerId::corba_peer;
            pkt.received_at = std::chrono::steady_clock::now();
            pkt.payload.assign(data.get_buffer(),
                               data.get_buffer() + data.length());
            data_cb_(std::move(pkt));
        }
    }

    void on_channel_disconnect(const char* reason) override {
        ACE_DEBUG((LM_WARNING,
                   "CorbaPeer: channel disconnect notification: %s\n", reason));
        if (state_cb_) {
            state_cb_(PeerId::corba_peer, false);
        }
    }

private:
    DataReceivedCallback& data_cb_;
    PeerStateCallback&    state_cb_;
};

// ============================================================================
// CorbaPeer::Impl
// ============================================================================

struct CorbaPeer::Impl {
    CORBA::ORB_var              orb;
    PortableServer::POA_var     poa;
    CorbaPeerConfig             config;

    DataReceivedCallback        data_cb;
    PeerStateCallback           state_cb;

    CorbaAdaptor::RawDataChannel_var    channel;
    PortableServer::ServantBase_var     callback_servant;
    CorbaAdaptor::RawDataCallback_var   callback_ref;

    std::atomic<bool>       connected{false};
    std::atomic<bool>       running{false};
    std::thread             reconnect_thread;
    std::mutex              mu;
    std::condition_variable cv;

    Impl(CORBA::ORB_ptr o, PortableServer::POA_ptr p, CorbaPeerConfig cfg)
        : orb(CORBA::ORB::_duplicate(o))
        , poa(PortableServer::POA::_duplicate(p))
        , config(std::move(cfg))
    {
    }

    void activate_callback_servant() {
        if (!CORBA::is_nil(callback_ref.in())) return;  // Already activated

        auto* servant = new RawDataCallbackServant(data_cb, state_cb);
        callback_servant = servant;

        PortableServer::ObjectId_var oid = poa->activate_object(servant);
        CORBA::Object_var cb_obj = poa->id_to_reference(oid.in());
        callback_ref = CorbaAdaptor::RawDataCallback::_narrow(cb_obj.in());
    }

    bool try_connect() {
        try {
            CORBA::Object_var obj =
                orb->string_to_object(config.channel_ior.c_str());
            if (CORBA::is_nil(obj.in())) {
                ACE_DEBUG((LM_WARNING, "CorbaPeer: nil object from IOR\n"));
                return false;
            }

            channel = CorbaAdaptor::RawDataChannel::_narrow(obj.in());
            if (CORBA::is_nil(channel.in())) {
                ACE_DEBUG((LM_WARNING, "CorbaPeer: narrow failed\n"));
                return false;
            }

            // Ensure callback servant is activated (only once)
            activate_callback_servant();

            // Register with remote channel
            channel->register_callback(callback_ref.in());

            connected = true;
            ACE_DEBUG((LM_INFO, "CorbaPeer: connected to raw data channel\n"));
            if (state_cb) state_cb(PeerId::corba_peer, true);
            return true;

        } catch (const CORBA::Exception& ex) {
            ACE_DEBUG((LM_WARNING,
                       "CorbaPeer: connection failed: %s\n",
                       ex._info().c_str()));
            return false;
        }
    }

    void start_reconnect() {
        // Join any prior reconnect thread safely
        {
            std::lock_guard lock(mu);
            cv.notify_all();
        }
        if (reconnect_thread.joinable()) {
            reconnect_thread.join();
        }
        reconnect_thread = std::thread([this] { reconnect_loop(); });
    }

    void reconnect_loop() {
        auto delay = config.initial_delay;
        uint32_t attempts = 0;

        while (running && !connected) {
            {
                std::unique_lock lock(mu);
                cv.wait_for(lock, delay, [this] { return !running.load(); });
            }

            if (!running) break;

            ++attempts;
            ACE_DEBUG((LM_INFO, "CorbaPeer: reconnect attempt %u\n", attempts));

            if (try_connect()) return;

            if (config.max_attempts > 0 && attempts >= config.max_attempts) {
                ACE_DEBUG((LM_ERROR,
                           "CorbaPeer: max reconnect attempts (%u) reached\n",
                           config.max_attempts));
                return;
            }

            delay = std::chrono::milliseconds(
                static_cast<int64_t>(delay.count() * config.backoff_multiplier));
            if (delay > config.max_delay) delay = config.max_delay;
        }
    }

    void start() {
        running = true;

        if (!try_connect() && config.auto_reconnect) {
            reconnect_thread = std::thread([this] { reconnect_loop(); });
        }
    }

    void stop() {
        if (!running.exchange(false)) return;

        // Wake and join reconnect thread
        {
            std::lock_guard lock(mu);
            cv.notify_all();
        }
        if (reconnect_thread.joinable()) {
            reconnect_thread.join();
        }

        // Unregister callback from channel
        if (connected && !CORBA::is_nil(channel.in())) {
            try {
                channel->unregister_callback();
            } catch (const CORBA::Exception&) {
                // Channel already gone
            }
        }

        // Deactivate servant
        if (!CORBA::is_nil(callback_ref.in())) {
            try {
                PortableServer::ObjectId_var oid =
                    poa->reference_to_id(callback_ref.in());
                poa->deactivate_object(oid.in());
            } catch (const CORBA::Exception&) {}
        }

        connected = false;
    }

    bool send(std::span<const uint8_t> data) {
        if (!connected || CORBA::is_nil(channel.in())) return false;

        try {
            CorbaAdaptor::OctetSeq seq;
            seq.length(static_cast<CORBA::ULong>(data.size()));
            std::copy(data.begin(), data.end(), seq.get_buffer());
            channel->send_raw(seq);
            return true;
        } catch (const CORBA::Exception& ex) {
            ACE_DEBUG((LM_WARNING,
                       "CorbaPeer: send failed: %s\n", ex._info().c_str()));
            connected = false;
            if (state_cb) state_cb(PeerId::corba_peer, false);

            // Trigger reconnection safely (no thread leak)
            if (running && config.auto_reconnect) {
                start_reconnect();
            }
            return false;
        }
    }
};

// ============================================================================
// CorbaPeer public API
// ============================================================================

CorbaPeer::CorbaPeer(CORBA::ORB_ptr orb,
                      PortableServer::POA_ptr poa,
                      CorbaPeerConfig config)
    : impl_(std::make_unique<Impl>(orb, poa, std::move(config)))
{
}

CorbaPeer::~CorbaPeer() {
    if (impl_) impl_->stop();
}

void CorbaPeer::set_data_callback(DataReceivedCallback cb) {
    impl_->data_cb = std::move(cb);
}

void CorbaPeer::set_state_callback(PeerStateCallback cb) {
    impl_->state_cb = std::move(cb);
}

void CorbaPeer::start() { impl_->start(); }
void CorbaPeer::stop()  { impl_->stop(); }

bool CorbaPeer::send(std::span<const uint8_t> data) {
    return impl_->send(data);
}

bool CorbaPeer::is_connected() const noexcept {
    return impl_->connected;
}

} // namespace adaptor
