// SPDX-License-Identifier: MIT
// Adaptor - Standalone TCP client: no ACE_Reactor dependency (C++11)
//
// Uses ACE_SOCK_Connector + ACE_SOCK_Stream for portable socket
// primitives but owns a dedicated receive thread and does NOT
// register with (or require the existence of) an ACE_Reactor.
//
// Intended for host applications that either do not run an ACE event
// loop at all, or delegate reactor ownership to another subsystem the
// adaptor must not interfere with.
//
// Threading model:
//   - recv thread: drives connect (with reconnect backoff), then loops
//                  on a blocking ACE_SOCK_Stream::recv() and dispatches
//                  bytes via BytesCallback.
//   - caller thread(s): send() serializes on a mutex and performs a
//                  blocking ACE_SOCK_Stream::send_n().
//   - shutdown:    close() sets running_=false and closes the socket
//                  from the caller thread; the blocking recv() unblocks
//                  immediately, the recv thread exits, and close()
//                  joins it.

#ifndef ADAPTOR_ACE_TCP_CLIENT_STANDALONE_HPP
#define ADAPTOR_ACE_TCP_CLIENT_STANDALONE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ace/INET_Addr.h>
#include <ace/SOCK_Connector.h>
#include <ace/SOCK_Stream.h>

#include "adaptor/tcp_client.hpp"

namespace adaptor {

class AceTcpClientStandalone : public ITcpClient {
public:
    explicit AceTcpClientStandalone(const TcpClientConfig& config);
    virtual ~AceTcpClientStandalone();

    AceTcpClientStandalone(const AceTcpClientStandalone&);
    AceTcpClientStandalone& operator=(const AceTcpClientStandalone&);

    virtual void set_bytes_callback(const BytesCallback& cb) { bytes_cb_ = cb; }
    virtual void set_state_callback(const StateCallback& cb) { state_cb_ = cb; }

    virtual int connect();
    virtual int send(const uint8_t* data, std::size_t size);
    virtual void close();
    virtual bool is_connected() const;

private:
    void run();                  // recv thread body
    bool try_connect_once();     // returns true on success
    void wait_backoff();         // sleep for current_delay_ms_, interruptible
    void set_connected(bool c);

    TcpClientConfig          config_;
    ACE_SOCK_Connector       connector_;
    ACE_SOCK_Stream          stream_;
    mutable std::mutex       stream_mu_;   // guards stream_ open/close + send
    std::thread              recv_thread_;
    std::atomic<bool>        running_;
    std::atomic<bool>        connected_;
    std::mutex               wake_mu_;     // paired with wake_cv_
    std::condition_variable  wake_cv_;     // wakes the backoff sleep on close()
    std::vector<uint8_t>     recv_buf_;
    uint32_t                 current_delay_ms_;
    uint32_t                 attempts_;
    BytesCallback            bytes_cb_;
    StateCallback            state_cb_;
};

} // namespace adaptor

#endif
