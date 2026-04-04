// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer Client Implementation
//
// Uses ACE_SOCK_Connector / ACE_SOCK_Stream for the TCP connection,
// with ACE_Reactor for non-blocking event-driven I/O.  A dedicated
// thread runs the reactor loop.  Reconnection runs on a separate thread
// to avoid blocking the reactor during backoff waits.

#include <adaptor/tcp_peer.hpp>

#include <ace/INET_Addr.h>
#include <ace/Log_Msg.h>
#include <ace/OS_NS_unistd.h>
#include <ace/Reactor.h>
#include <ace/SOCK_Connector.h>
#include <ace/SOCK_Stream.h>
#include <ace/Event_Handler.h>
#include <ace/Time_Value.h>

#include <condition_variable>
#include <cstring>
#include <vector>

namespace adaptor {

// ============================================================================
// TcpPeer::Impl — reactor-based I/O handler
// ============================================================================

struct TcpPeer::Impl : public ACE_Event_Handler {
    TcpPeerConfig           config;
    ACE_SOCK_Stream         stream;
    ACE_Reactor             reactor;
    std::vector<uint8_t>    recv_buf;

    DataReceivedCallback    data_cb;
    PeerStateCallback       state_cb;

    std::atomic<bool>       connected{false};
    std::atomic<bool>       running{false};
    std::thread             io_thread;
    std::thread             reconnect_thread;
    std::mutex              reconnect_mu;
    std::condition_variable reconnect_cv;
    std::mutex              send_mu;

    explicit Impl(TcpPeerConfig cfg)
        : config(std::move(cfg))
        , recv_buf(config.recv_buffer_size)
    {
    }

    ~Impl() override {
        stop();
    }

    // --- ACE_Event_Handler overrides ----------------------------------------

    ACE_HANDLE get_handle() const override {
        return stream.get_handle();
    }

    int handle_input(ACE_HANDLE) override {
        auto n = stream.recv(recv_buf.data(), recv_buf.size());
        if (n <= 0) {
            ACE_DEBUG((LM_WARNING, "TcpPeer: connection lost (recv returned %d)\n", n));
            on_disconnect();
            return -1;  // Remove from reactor
        }

        if (data_cb) {
            InternalPacket pkt;
            pkt.source      = PeerId::tcp_peer;
            pkt.received_at = std::chrono::steady_clock::now();
            pkt.payload.assign(recv_buf.data(), recv_buf.data() + n);
            data_cb(std::move(pkt));
        }

        return 0;
    }

    int handle_close(ACE_HANDLE, ACE_Reactor_Mask) override {
        // Don't delete — we manage lifetime ourselves
        return 0;
    }

    // --- Connection management ----------------------------------------------

    bool try_connect() {
        ACE_INET_Addr addr(config.port, config.host.c_str());
        ACE_SOCK_Connector connector;

        ACE_Time_Value timeout(
            config.connect_timeout.count() / 1000,
            (config.connect_timeout.count() % 1000) * 1000);

        if (connector.connect(stream, addr, &timeout) == -1) {
            ACE_DEBUG((LM_WARNING, "TcpPeer: connect to %s:%u failed: %m\n",
                       config.host.c_str(), config.port));
            return false;
        }

        ACE_DEBUG((LM_INFO, "TcpPeer: connected to %s:%u\n",
                   config.host.c_str(), config.port));

        connected = true;
        if (state_cb) state_cb(PeerId::tcp_peer, true);

        // Register with reactor for read events
        reactor.register_handler(this, ACE_Event_Handler::READ_MASK);
        return true;
    }

    void on_disconnect() {
        connected = false;
        stream.close();
        if (state_cb) state_cb(PeerId::tcp_peer, false);

        // Trigger reconnection on a separate thread (don't block the reactor)
        if (running && config.auto_reconnect) {
            start_reconnect();
        }
    }

    void start_reconnect() {
        // Join any prior reconnect thread
        if (reconnect_thread.joinable()) {
            {
                std::lock_guard lock(reconnect_mu);
                reconnect_cv.notify_all();
            }
            reconnect_thread.join();
        }
        reconnect_thread = std::thread([this] { reconnect_loop(); });
    }

    void reconnect_loop() {
        auto delay = config.initial_delay;
        uint32_t attempts = 0;

        while (running && !connected) {
            {
                std::unique_lock lock(reconnect_mu);
                reconnect_cv.wait_for(lock, delay,
                    [this] { return !running.load(); });
            }

            if (!running) break;

            ++attempts;
            ACE_DEBUG((LM_INFO, "TcpPeer: reconnect attempt %u\n", attempts));

            if (try_connect()) return;

            if (config.max_attempts > 0 && attempts >= config.max_attempts) {
                ACE_DEBUG((LM_ERROR,
                           "TcpPeer: max reconnect attempts (%u) reached\n",
                           config.max_attempts));
                return;
            }

            delay = std::chrono::milliseconds(
                static_cast<int64_t>(delay.count() * config.backoff_multiplier));
            if (delay > config.max_delay) delay = config.max_delay;
        }
    }

    // --- I/O thread ---------------------------------------------------------

    void io_loop() {
        // Initial connection (with reconnect if needed)
        if (!try_connect() && config.auto_reconnect) {
            start_reconnect();
        }

        // Run reactor event loop
        while (running) {
            ACE_Time_Value timeout(1, 0);  // 1-second poll interval
            reactor.handle_events(&timeout);
        }
    }

    void start() {
        running = true;
        io_thread = std::thread([this] { io_loop(); });
    }

    void stop() {
        if (!running.exchange(false)) return;

        // Wake reconnect thread
        {
            std::lock_guard lock(reconnect_mu);
            reconnect_cv.notify_all();
        }
        if (reconnect_thread.joinable()) {
            reconnect_thread.join();
        }

        // Wake the reactor
        reactor.end_reactor_event_loop();

        if (io_thread.joinable()) {
            io_thread.join();
        }

        if (connected) {
            reactor.remove_handler(this, ACE_Event_Handler::READ_MASK |
                                         ACE_Event_Handler::DONT_CALL);
            stream.close();
            connected = false;
        }
    }

    bool send(std::span<const uint8_t> data) {
        if (!connected) return false;

        std::lock_guard lock(send_mu);
        auto n = stream.send_n(data.data(), data.size());
        if (n <= 0) {
            ACE_DEBUG((LM_WARNING, "TcpPeer: send failed: %m\n"));
            return false;
        }
        return static_cast<size_t>(n) == data.size();
    }
};

// ============================================================================
// TcpPeer public API
// ============================================================================

TcpPeer::TcpPeer(TcpPeerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

TcpPeer::~TcpPeer() = default;

void TcpPeer::set_data_callback(DataReceivedCallback cb) {
    impl_->data_cb = std::move(cb);
}

void TcpPeer::set_state_callback(PeerStateCallback cb) {
    impl_->state_cb = std::move(cb);
}

void TcpPeer::start() { impl_->start(); }
void TcpPeer::stop()  { impl_->stop(); }

bool TcpPeer::send(std::span<const uint8_t> data) {
    return impl_->send(data);
}

bool TcpPeer::is_connected() const noexcept {
    return impl_->connected;
}

} // namespace adaptor
