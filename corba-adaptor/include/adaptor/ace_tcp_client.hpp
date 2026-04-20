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

namespace adaptor {

struct AceTcpClientConfig {
    std::string host;
    uint16_t    port;
    std::size_t recv_buffer_size;
    uint32_t    connect_timeout_ms;
    bool        auto_reconnect;
    uint32_t    initial_delay_ms;
    uint32_t    max_delay_ms;
    double      backoff_multiplier;
    uint32_t    max_attempts;

    AceTcpClientConfig()
        : host("127.0.0.1"), port(0), recv_buffer_size(65536),
          connect_timeout_ms(10000), auto_reconnect(true),
          initial_delay_ms(1000), max_delay_ms(30000),
          backoff_multiplier(2.0), max_attempts(0) {}
};

class AceTcpClient : public ACE_Event_Handler {
public:
    typedef std::function<void(const uint8_t*, std::size_t)> BytesCallback;
    typedef std::function<void(bool /*connected*/)> StateCallback;

    AceTcpClient(ACE_Reactor* reactor, const AceTcpClientConfig& config);
    virtual ~AceTcpClient();

    void set_bytes_callback(const BytesCallback& cb) { bytes_cb_ = cb; }
    void set_state_callback(const StateCallback& cb) { state_cb_ = cb; }

    int connect();
    int send(const uint8_t* data, std::size_t size);
    void close();
    bool is_connected() const;

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
    AceTcpClientConfig config_;
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
