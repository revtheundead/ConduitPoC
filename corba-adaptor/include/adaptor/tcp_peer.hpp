// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer (C++11)
//
// Holds a pluggable ITcpClient backend (either AceTcpClient which binds
// to a caller-owned ACE_Reactor, or AceTcpClientStandalone which runs a
// self-contained receive thread without touching any reactor).  Incoming
// bytes are decoded via the bgen-generated `tcp_peer::TcpPeerFrameSession`
// codec.  Typed handlers are dispatched on decode.

#ifndef ADAPTOR_TCP_PEER_HPP
#define ADAPTOR_TCP_PEER_HPP

#include <cstdint>
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

#include "adaptor/tcp_client.hpp"

class ACE_Reactor;

namespace adaptor {

enum ConnectionState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2
};

struct TcpPeerConfig : public TcpClientConfig {
    std::string name;

    TcpPeerConfig() : TcpClientConfig(), name("tcp-peer") {}
};

class TcpPeer {
public:
    typedef std::function<void(ConnectionState)> StateCallback;
    typedef std::function<void(const cpp11::any&)> TypedHandler;

    /// Reactor-backed constructor: the adaptor (or caller) supplies an
    /// ACE_Reactor pointer whose event loop drives the TCP client.
    TcpPeer(const TcpPeerConfig& config, ACE_Reactor* reactor);

    /// Standalone (reactor-free) constructor: the TCP client runs on its
    /// own dedicated receive thread and never touches an ACE_Reactor.
    /// Use this when the host environment owns its reactor and does not
    /// permit the adaptor to register event handlers against it.
    explicit TcpPeer(const TcpPeerConfig& config);

    /// Dependency-injection constructor: caller constructs the ITcpClient
    /// and hands it over.  Useful for tests or exotic transports.
    TcpPeer(const TcpPeerConfig& config,
            std::unique_ptr<ITcpClient> client);

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
    std::unique_ptr<ITcpClient> client_;
    tcp_peer::TcpPeerFrameSession session_;
    std::map<uint64_t, TypedHandler> handlers_;
    std::vector<StateCallback> state_callbacks_;
    bool started_;
};

} // namespace adaptor

#endif
