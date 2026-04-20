// SPDX-License-Identifier: MIT
// Adaptor - Standalone TCP client implementation (C++11, no reactor)

#include "adaptor/ace_tcp_client_standalone.hpp"

#include <chrono>

#include <ace/Log_Msg.h>
#include <ace/OS_NS_errno.h>
#include <ace/Time_Value.h>

namespace adaptor {

AceTcpClientStandalone::AceTcpClientStandalone(const TcpClientConfig& config)
    : config_(config),
      running_(false),
      connected_(false),
      current_delay_ms_(config.initial_delay_ms),
      attempts_(0) {
    recv_buf_.resize(config_.recv_buffer_size > 0
                         ? config_.recv_buffer_size
                         : 4096);
}

AceTcpClientStandalone::~AceTcpClientStandalone() {
    close();
}

int AceTcpClientStandalone::connect() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return 0;  // already running
    }
    recv_thread_ = std::thread(&AceTcpClientStandalone::run, this);
    return 0;
}

int AceTcpClientStandalone::send(const uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(stream_mu_);
    if (!connected_.load()) return -1;
    ssize_t n = stream_.send_n(data, size);
    if (n < 0 || static_cast<std::size_t>(n) != size) {
        // Kick the recv thread to reconnect by closing the socket.
        stream_.close();
        connected_.store(false);
        return -1;
    }
    return 0;
}

void AceTcpClientStandalone::close() {
    bool was_running = running_.exchange(false);
    if (!was_running) return;

    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        if (connected_.load()) {
            stream_.close();          // unblocks recv()
            connected_.store(false);
        }
    }
    {
        std::lock_guard<std::mutex> lock(wake_mu_);
        wake_cv_.notify_all();        // unblocks backoff sleep
    }
    if (recv_thread_.joinable()) recv_thread_.join();

    if (state_cb_) state_cb_(false);
}

bool AceTcpClientStandalone::is_connected() const {
    return connected_.load();
}

void AceTcpClientStandalone::set_connected(bool c) {
    bool prev = connected_.exchange(c);
    if (prev != c && state_cb_) state_cb_(c);
}

bool AceTcpClientStandalone::try_connect_once() {
    ACE_INET_Addr addr(config_.port, config_.host.c_str());
    ACE_Time_Value timeout(
        static_cast<time_t>(config_.connect_timeout_ms / 1000),
        static_cast<suseconds_t>((config_.connect_timeout_ms % 1000) * 1000));

    ACE_SOCK_Stream fresh;
    if (connector_.connect(fresh, addr, &timeout) == -1) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        stream_ = fresh;
    }
    return true;
}

void AceTcpClientStandalone::wait_backoff() {
    std::unique_lock<std::mutex> lock(wake_mu_);
    wake_cv_.wait_for(
        lock,
        std::chrono::milliseconds(current_delay_ms_),
        [this] { return !running_.load(); });

    uint32_t next = static_cast<uint32_t>(
        current_delay_ms_ * config_.backoff_multiplier);
    if (next > config_.max_delay_ms) next = config_.max_delay_ms;
    current_delay_ms_ = next;
}

void AceTcpClientStandalone::run() {
    while (running_.load()) {
        // Connect phase --------------------------------------------------
        attempts_ = 0;
        current_delay_ms_ = config_.initial_delay_ms;

        while (running_.load() && !try_connect_once()) {
            ++attempts_;
            if (config_.max_attempts > 0 &&
                attempts_ >= config_.max_attempts) {
                running_.store(false);
                return;
            }
            wait_backoff();
            if (!running_.load()) return;
        }
        if (!running_.load()) return;

        set_connected(true);

        // Receive phase --------------------------------------------------
        while (running_.load() && connected_.load()) {
            ssize_t n = stream_.recv(
                recv_buf_.data(), recv_buf_.size());
            if (n > 0) {
                if (bytes_cb_) {
                    bytes_cb_(recv_buf_.data(),
                              static_cast<std::size_t>(n));
                }
            } else {
                // n == 0 (peer closed) or n < 0 (error).  Drop link.
                std::lock_guard<std::mutex> lock(stream_mu_);
                stream_.close();
                break;
            }
        }
        set_connected(false);

        if (!running_.load()) return;
        if (!config_.auto_reconnect) {
            running_.store(false);
            return;
        }
        // Loop back to the connect phase.
    }
}

} // namespace adaptor
