// SPDX-License-Identifier: MIT
// Adaptor - ACE_Reactor based TCP client (C++11)

#ifndef ADAPTOR_ACE_TCP_CLIENT_HPP
#define ADAPTOR_ACE_TCP_CLIENT_HPP

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <ace/Event_Handler.h>
#include <ace/INET_Addr.h>
#include <ace/Reactor.h>
#include <ace/SOCK_Connector.h>
#include <ace/SOCK_Stream.h>
#include <ace/Time_Value.h>

#include "adaptor/tcp_client.hpp"

namespace adaptor {

// Retained as an alias for source-backward-compatibility; the concrete
// config struct is now `TcpClientConfig` in tcp_client.hpp.
typedef TcpClientConfig AceTcpClientConfig;

class AceTcpClient : public ITcpClient, public ACE_Event_Handler {
public:
    AceTcpClient(ACE_Reactor* reactor, const TcpClientConfig& config);
    virtual ~AceTcpClient();

    // ITcpClient overrides.
    virtual void set_bytes_callback(const BytesCallback& cb) { bytes_cb_ = cb; }
    virtual void set_state_callback(const StateCallback& cb) { state_cb_ = cb; }
    virtual int connect();
    virtual int send(const uint8_t* data, std::size_t size);
    virtual void close();
    virtual bool is_connected() const;

    // ACE_Event_Handler overrides.
    virtual ACE_HANDLE get_handle() const;
    virtual int handle_input(ACE_HANDLE);
    virtual int handle_output(ACE_HANDLE);
    virtual int handle_close(ACE_HANDLE, ACE_Reactor_Mask);
    virtual int handle_timeout(const ACE_Time_Value&, const void*);

private:
    int schedule_reconnect();
    int do_connect();
    void do_state_change(bool connected);

    ACE_Reactor* reactor_;
    TcpClientConfig config_;
    ACE_SOCK_Stream stream_;
    ACE_SOCK_Connector connector_;
    mutable std::mutex mu_;
    std::deque<std::vector<uint8_t> > send_queue_;
    std::vector<uint8_t> recv_buf_;
    BytesCallback bytes_cb_;
    StateCallback state_cb_;
    uint32_t attempts_;
    uint32_t current_delay_ms_;
    bool connected_;
    long reconnect_timer_;
};

} // namespace adaptor

#endif
