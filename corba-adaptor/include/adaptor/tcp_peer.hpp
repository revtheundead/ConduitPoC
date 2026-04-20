// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer (ACE-backed, C++11)
//
// Connects to a TCP server using an ACE_Reactor + ACE_Event_Handler based
// client. Incoming bytes are decoded via the bgen-generated
// `tcp_peer::TcpPeerFrameSession` codec (translated to C++11 via
// tools/bgen-cpp11). Typed handlers are dispatched on decode.

#ifndef ADAPTOR_TCP_PEER_HPP
#define ADAPTOR_TCP_PEER_HPP

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "compat11/compat11.hpp"
#include "bgen11/bgen11.hpp"

#include "tcp-peer/tcp_peer.hpp"
#include "tcp-peer/sessions.hpp"

#include <ace/Event_Handler.h>
#include <ace/INET_Addr.h>
#include <ace/Reactor.h>
#include <ace/SOCK_Connector.h>
#include <ace/SOCK_Stream.h>

namespace adaptor {

enum ConnectionState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2
};

struct TcpPeerConfig {
    std::string host;
    uint16_t    port;
    std::size_t recv_buffer_size;
    uint32_t    connect_timeout_ms;
    bool        auto_reconnect;
    uint32_t    initial_delay_ms;
    uint32_t    max_delay_ms;
    double      backoff_multiplier;
    uint32_t    max_attempts;  // 0 = unlimited
    std::string name;

    TcpPeerConfig()
        : host("127.0.0.1"), port(0), recv_buffer_size(65536),
          connect_timeout_ms(10000), auto_reconnect(true),
          initial_delay_ms(1000), max_delay_ms(30000),
          backoff_multiplier(2.0), max_attempts(0), name("tcp-peer") {}
};

class AceTcpClient;

class TcpPeer {
public:
    typedef std::function<void(ConnectionState)> StateCallback;
    typedef std::function<void(const cpp11::any&)> TypedHandler;

    explicit TcpPeer(const TcpPeerConfig& config, ACE_Reactor* reactor);
    ~TcpPeer();

    TcpPeer(const TcpPeer&);              // = delete, but C++11 friendly.
    TcpPeer& operator=(const TcpPeer&);   // = delete, but C++11 friendly.

    template <typename T>
    void on(std::function<void(const T&)> cb) {
        TypedHandler wrapper = [cb](const cpp11::any& payload) {
            const T* p = cpp11::any_cast<T>(&payload);
            if (p) cb(*p);
        };
        // Copy TYPE_ID into a local to avoid ODR-use: std::map::operator[]
        // takes its key by const reference, which under strict C++11 would
        // bind to the static constexpr class member and require an
        // out-of-class definition that bgen never emits.
        const uint64_t id = T::TYPE_ID;
        handlers_[id] = wrapper;
    }

    void on_state_change(StateCallback cb);

    template <typename T>
    bool send(const T& msg) {
        bgen11::Result<bgen11::traits::EncodeResult> enc =
            session_.encode_wrap(T::TYPE_ID, cpp11::any(msg));
        if (!enc) return false;
        return send_bytes(enc->bytes);
    }

    void start();
    void stop();

    bool is_connected() const;
    ConnectionState state() const;

private:
    bool send_bytes(const std::vector<uint8_t>& bytes);
    void on_bytes(const uint8_t* data, std::size_t size);
    void on_state_change_internal(ConnectionState s);

    TcpPeerConfig config_;
    ACE_Reactor* reactor_;
    std::unique_ptr<AceTcpClient> client_;
    tcp_peer::TcpPeerFrameSession session_;
    std::map<uint64_t, TypedHandler> handlers_;
    std::vector<StateCallback> state_callbacks_;
    bool started_;
};

} // namespace adaptor

#endif
