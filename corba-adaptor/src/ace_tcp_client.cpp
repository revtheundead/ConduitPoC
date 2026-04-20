// SPDX-License-Identifier: MIT
// Adaptor - ACE_Reactor based TCP client implementation (C++11)

#include "adaptor/ace_tcp_client.hpp"

#include <algorithm>

#include <ace/Log_Msg.h>

namespace adaptor {

AceTcpClient::AceTcpClient(ACE_Reactor* reactor,
                           const TcpClientConfig& config)
    : reactor_(reactor), config_(config),
      attempts_(0),
      current_delay_ms_(config.initial_delay_ms),
      connected_(false),
      reconnect_timer_(-1) {
    recv_buf_.resize(config_.recv_buffer_size > 0 ? config_.recv_buffer_size : 4096);
}

AceTcpClient::~AceTcpClient() {
    close();
}

int AceTcpClient::connect() {
    return do_connect();
}

int AceTcpClient::do_connect() {
    ACE_INET_Addr addr(config_.port, config_.host.c_str());
    ACE_Time_Value timeout(config_.connect_timeout_ms / 1000,
                           (config_.connect_timeout_ms % 1000) * 1000);
    if (connector_.connect(stream_, addr, &timeout) == -1) {
        ++attempts_;
        if (config_.auto_reconnect &&
            (config_.max_attempts == 0 || attempts_ < config_.max_attempts)) {
            schedule_reconnect();
        }
        return -1;
    }
    attempts_ = 0;
    current_delay_ms_ = config_.initial_delay_ms;
    // Register for READ_MASK events on the connected socket.
    if (reactor_->register_handler(
            stream_.get_handle(), this, ACE_Event_Handler::READ_MASK) == -1) {
        stream_.close();
        return -1;
    }
    do_state_change(true);
    return 0;
}

int AceTcpClient::schedule_reconnect() {
    if (!config_.auto_reconnect) return 0;
    ACE_Time_Value delay(current_delay_ms_ / 1000,
                         (current_delay_ms_ % 1000) * 1000);
    reconnect_timer_ = reactor_->schedule_timer(this, 0, delay);
    uint32_t next = static_cast<uint32_t>(current_delay_ms_ * config_.backoff_multiplier);
    if (next > config_.max_delay_ms) next = config_.max_delay_ms;
    current_delay_ms_ = next;
    return 0;
}

int AceTcpClient::send(const uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mu_);
    send_queue_.push_back(std::vector<uint8_t>(data, data + size));
    if (connected_ && reactor_) {
        reactor_->register_handler(
            stream_.get_handle(), this,
            ACE_Event_Handler::WRITE_MASK);
    }
    return 0;
}

void AceTcpClient::close() {
    if (reactor_) {
        if (reconnect_timer_ != -1) {
            reactor_->cancel_timer(reconnect_timer_);
            reconnect_timer_ = -1;
        }
        if (connected_) {
            reactor_->remove_handler(
                stream_.get_handle(),
                ACE_Event_Handler::ALL_EVENTS_MASK |
                    ACE_Event_Handler::DONT_CALL);
        }
    }
    if (connected_) {
        stream_.close();
        do_state_change(false);
    }
}

bool AceTcpClient::is_connected() const {
    std::lock_guard<std::mutex> lock(mu_);
    return connected_;
}

ACE_HANDLE AceTcpClient::get_handle() const {
    return stream_.get_handle();
}

int AceTcpClient::handle_input(ACE_HANDLE) {
    ssize_t n = stream_.recv(&recv_buf_[0], recv_buf_.size());
    if (n <= 0) {
        return -1;  // triggers handle_close
    }
    if (bytes_cb_) {
        bytes_cb_(&recv_buf_[0], static_cast<std::size_t>(n));
    }
    return 0;
}

int AceTcpClient::handle_output(ACE_HANDLE) {
    std::vector<uint8_t> front;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (send_queue_.empty()) {
            return -1;  // nothing to send — unregister WRITE_MASK
        }
        front.swap(send_queue_.front());
        send_queue_.pop_front();
    }
    ssize_t n = stream_.send_n(&front[0], front.size());
    if (n < 0) return -1;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (send_queue_.empty()) return -1;
    }
    return 0;
}

int AceTcpClient::handle_close(ACE_HANDLE, ACE_Reactor_Mask mask) {
    if (mask == ACE_Event_Handler::WRITE_MASK) {
        // Just unregistering WRITE_MASK (nothing else to write).
        return 0;
    }
    do_state_change(false);
    stream_.close();
    if (config_.auto_reconnect &&
        (config_.max_attempts == 0 || attempts_ < config_.max_attempts)) {
        schedule_reconnect();
    }
    return 0;
}

int AceTcpClient::handle_timeout(const ACE_Time_Value&, const void*) {
    reconnect_timer_ = -1;
    do_connect();
    return 0;
}

void AceTcpClient::do_state_change(bool connected) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        connected_ = connected;
    }
    if (state_cb_) state_cb_(connected);
}

} // namespace adaptor
