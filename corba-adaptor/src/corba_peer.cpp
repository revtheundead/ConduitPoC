// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - CORBA Raw-Data Peer Implementation (C++11)

#include "adaptor/corba_peer.hpp"

#include <ace/Log_Msg.h>
#include <ace/Time_Value.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace adaptor {

class RawDataCallbackServant : public POA_CorbaAdaptor::RawDataCallback {
public:
    RawDataCallbackServant(RawBytesCallback& cb, PeerStateCallback& state_cb)
        : data_cb_(cb), state_cb_(state_cb) {}

    virtual void on_raw_data(const CorbaAdaptor::OctetSeq& data) {
        if (data_cb_) {
            data_cb_(cpp11::span<const std::uint8_t>(
                data.get_buffer(), data.length()));
        }
    }

    virtual void on_channel_disconnect(const char* reason) {
        ACE_DEBUG((LM_WARNING,
                   "CorbaPeer: channel disconnect notification: %s\n", reason));
        if (state_cb_) state_cb_(false);
    }

private:
    RawBytesCallback&  data_cb_;
    PeerStateCallback& state_cb_;
};

struct CorbaPeer::Impl {
    CORBA::ORB_var              orb;
    PortableServer::POA_var     poa;
    CorbaPeerConfig             config;

    RawBytesCallback            data_cb;
    PeerStateCallback           state_cb;

    CorbaAdaptor::RawDataChannel_var    channel;
    PortableServer::ServantBase_var     callback_servant;
    CorbaAdaptor::RawDataCallback_var   callback_ref;

    std::atomic<bool>       connected;
    std::atomic<bool>       running;
    std::thread             reconnect_thread;
    std::mutex              mu;
    std::condition_variable cv;

    Impl(CORBA::ORB_ptr o, PortableServer::POA_ptr p, const CorbaPeerConfig& cfg)
        : orb(CORBA::ORB::_duplicate(o)),
          poa(PortableServer::POA::_duplicate(p)),
          config(cfg),
          connected(false),
          running(false) {}

    void activate_callback_servant() {
        if (!CORBA::is_nil(callback_ref.in())) return;
        RawDataCallbackServant* servant =
            new RawDataCallbackServant(data_cb, state_cb);
        callback_servant = servant;
        PortableServer::ObjectId_var oid = poa->activate_object(servant);
        CORBA::Object_var cb_obj = poa->id_to_reference(oid.in());
        callback_ref = CorbaAdaptor::RawDataCallback::_narrow(cb_obj.in());
    }

    bool try_connect() {
        try {
            CORBA::Object_var obj =
                orb->string_to_object(config.channel_ior.c_str());
            if (CORBA::is_nil(obj.in())) return false;

            channel = CorbaAdaptor::RawDataChannel::_narrow(obj.in());
            if (CORBA::is_nil(channel.in())) return false;

            activate_callback_servant();
            channel->register_callback(callback_ref.in());

            connected = true;
            ACE_DEBUG((LM_INFO, "CorbaPeer: connected\n"));
            if (state_cb) state_cb(true);
            return true;
        } catch (const CORBA::Exception&) {
            return false;
        }
    }

    void reconnect_loop() {
        std::chrono::milliseconds delay(config.initial_delay_ms);
        uint32_t attempts = 0;
        while (running && !connected) {
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait_for(lock, delay);
            }
            if (!running) break;
            ++attempts;
            if (try_connect()) return;
            if (config.max_attempts > 0 && attempts >= config.max_attempts)
                return;
            delay = std::chrono::milliseconds(
                static_cast<int64_t>(delay.count() * config.backoff_multiplier));
            if (delay.count() > config.max_delay_ms)
                delay = std::chrono::milliseconds(config.max_delay_ms);
        }
    }

    void start() {
        running = true;
        if (!try_connect() && config.auto_reconnect) {
            reconnect_thread = std::thread(&Impl::reconnect_loop, this);
        }
    }

    void stop() {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false)) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            cv.notify_all();
        }
        if (reconnect_thread.joinable()) reconnect_thread.join();

        if (connected && !CORBA::is_nil(channel.in())) {
            try { channel->unregister_callback(); } catch (const CORBA::Exception&) {}
        }
        if (!CORBA::is_nil(callback_ref.in())) {
            try {
                PortableServer::ObjectId_var oid =
                    poa->reference_to_id(callback_ref.in());
                poa->deactivate_object(oid.in());
            } catch (const CORBA::Exception&) {}
        }
        connected = false;
    }

    bool send(cpp11::span<const uint8_t> data) {
        if (!connected || CORBA::is_nil(channel.in())) return false;
        try {
            CorbaAdaptor::OctetSeq seq;
            seq.length(static_cast<CORBA::ULong>(data.size()));
            for (std::size_t i = 0; i < data.size(); ++i) seq[i] = data[i];
            channel->send_raw(seq);
            return true;
        } catch (const CORBA::Exception&) {
            connected = false;
            if (state_cb) state_cb(false);
            return false;
        }
    }
};

CorbaPeer::CorbaPeer(CORBA::ORB_ptr orb,
                     PortableServer::POA_ptr poa,
                     const CorbaPeerConfig& config)
    : impl_(new Impl(orb, poa, config)) {}

CorbaPeer::~CorbaPeer() { if (impl_) impl_->stop(); }

void CorbaPeer::set_data_callback(RawBytesCallback cb) {
    impl_->data_cb = std::move(cb);
}
void CorbaPeer::set_state_callback(PeerStateCallback cb) {
    impl_->state_cb = std::move(cb);
}
void CorbaPeer::start() { impl_->start(); }
void CorbaPeer::stop()  { impl_->stop(); }
bool CorbaPeer::send(cpp11::span<const uint8_t> data) { return impl_->send(data); }
bool CorbaPeer::is_connected() const { return impl_->connected; }

} // namespace adaptor
