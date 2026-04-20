// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Client Abstract Interface (C++11)
//
// Two implementations live alongside this header:
//
//   - AceTcpClient (ace_tcp_client.hpp): integrates with a caller-owned
//       ACE_Reactor.  Use this when a host application already drives an
//       ACE event loop that the adaptor can hook into.
//
//   - AceTcpClientStandalone (ace_tcp_client_standalone.hpp): fully
//       self-contained, spawns its own receive thread, does NOT touch
//       the ACE_Reactor.  Use this when the reactor (if any) is owned
//       by another subsystem that the adaptor cannot cooperate with.
//
// Both speak the same BytesCallback + StateCallback contract so TcpPeer
// (tcp_peer.hpp) can hold either backend behind this interface.

#ifndef ADAPTOR_TCP_CLIENT_HPP
#define ADAPTOR_TCP_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace adaptor {

struct TcpClientConfig {
    std::string host;
    uint16_t    port;
    std::size_t recv_buffer_size;
    uint32_t    connect_timeout_ms;
    bool        auto_reconnect;
    uint32_t    initial_delay_ms;
    uint32_t    max_delay_ms;
    double      backoff_multiplier;
    uint32_t    max_attempts;  // 0 = unlimited

    TcpClientConfig()
        : host("127.0.0.1"),
          port(0),
          recv_buffer_size(65536),
          connect_timeout_ms(10000),
          auto_reconnect(true),
          initial_delay_ms(1000),
          max_delay_ms(30000),
          backoff_multiplier(2.0),
          max_attempts(0) {}
};

class ITcpClient {
public:
    typedef std::function<void(const uint8_t* data, std::size_t size)> BytesCallback;
    typedef std::function<void(bool connected)>                          StateCallback;

    virtual ~ITcpClient() {}

    virtual void set_bytes_callback(const BytesCallback& cb) = 0;
    virtual void set_state_callback(const StateCallback& cb) = 0;

    /// Begin connecting. Non-blocking for the reactor variant
    /// (registration is queued); non-blocking for the standalone
    /// variant (the recv thread drives the connect).  Returns 0 on
    /// immediate success/queue acceptance, non-zero on hard error.
    virtual int connect() = 0;

    /// Enqueue bytes to send.  Thread-safe.  Returns 0 on success.
    virtual int send(const uint8_t* data, std::size_t size) = 0;

    /// Close the connection and stop any reconnect loop.  Idempotent.
    virtual void close() = 0;

    /// Current link state.
    virtual bool is_connected() const = 0;
};

} // namespace adaptor

#endif
