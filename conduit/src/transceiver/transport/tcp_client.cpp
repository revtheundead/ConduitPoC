// SPDX-License-Identifier: MIT
// Conduit - TCP Client Transport (implementation)

#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/logging/logger.hpp>
#include "socket_ops.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "poll_set.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace conduit::transceiver::transport {

using namespace detail;

struct TcpClientTransport::Impl {
    TcpClientConfig config;
    TransportCallbacks callbacks;
    PeerId peer_id;

    std::atomic<socket_t> sock{invalid_socket};
    WakePipe wake;
    std::thread io_thread;
    std::atomic<bool> running{false};
    std::mutex send_mutex;

    void io_loop();
    VoidResult try_connect();
    void recv_loop();
    void handle_disconnect();
};

TcpClientTransport::TcpClientTransport(TcpClientConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

TcpClientTransport::~TcpClientTransport() {
    stop();
}

VoidResult TcpClientTransport::start(TransportCallbacks cb) {
    if (impl_->running.load()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::AlreadyRunning, "TCP client transport already running"));
    }

    auto net_result = init_networking();
    if (!net_result) return net_result;
    NetGuard net_guard;

    auto wake_result = create_wake_pipe();
    if (!wake_result) return std::unexpected(wake_result.error());

    impl_->wake = *wake_result;
    impl_->callbacks = std::move(cb);
    impl_->running = true;

    impl_->io_thread = std::thread([this] { impl_->io_loop(); });
    net_guard.release();
    return {};
}

void TcpClientTransport::stop() {
    if (!impl_->running.exchange(false)) return;

    // Shut down the socket WITHOUT holding send_mutex.  sock is atomic, so
    // the load is data-race-free.  shutdown() will unblock any ::send()
    // that is blocked inside send_mutex, allowing it to release the mutex
    // so we can later acquire it to close the socket cleanly.
    {
        socket_t s = impl_->sock.load();
        if (s != invalid_socket) {
            #ifdef _WIN32
            ::shutdown(static_cast<SOCKET>(s), SD_BOTH);
            #else
            ::shutdown(s, SHUT_RDWR);
            #endif
        }
    }

    signal_wake_pipe(impl_->wake);

    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }

    {
        std::lock_guard lock(impl_->send_mutex);
        if (impl_->sock != invalid_socket) {
            close_socket(impl_->sock);
            impl_->sock = invalid_socket;
        }
    }

    close_wake_pipe(impl_->wake);
    cleanup_networking();
}

VoidResult TcpClientTransport::send(PeerId /*peer*/,
                                    std::span<const uint8_t> data) {
    std::lock_guard lock(impl_->send_mutex);

    if (impl_->sock == invalid_socket) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::ConnectionClosed, "Not connected"));
    }

    const char* ptr = reinterpret_cast<const char*>(data.data());
    size_t remaining = data.size();

    while (remaining > 0) {
        #ifdef _WIN32
        int sent = ::send(static_cast<SOCKET>(impl_->sock), ptr,
                          static_cast<int>(remaining), 0);
        #else
        ssize_t sent;
        do {
            sent = ::send(impl_->sock, ptr, remaining, MSG_NOSIGNAL);
        } while (sent < 0 && errno == EINTR);
        #endif

        if (sent <= 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Send failed: " +
                              error_to_string(get_last_error())));
        }

        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }

    return {};
}

// ============================================================================
// I/O thread
// ============================================================================

void TcpClientTransport::Impl::io_loop() {
    auto delay = config.reconnect.initial_delay;
    uint32_t attempts = 0;

    // Obtain PeerId upfront so state notifications work from the start
    if (!peer_id.valid() && callbacks.on_peer_connected) {
        peer_id = callbacks.on_peer_connected();
    }

    while (running) {
        // Notify connecting state
        if (callbacks.on_state_changed && peer_id.valid()) {
            callbacks.on_state_changed(peer_id, net::ConnectionState::Connecting);
        }

        auto result = try_connect();
        if (!result) {
            LOG_WARNF("TCP connect to {}:{} failed: {}",
                     config.host, config.port,
                     result.error().format_short());

            if (!config.reconnect.enabled) {
                if (callbacks.on_state_changed && peer_id.valid()) {
                    callbacks.on_state_changed(peer_id, net::ConnectionState::Failed);
                }
                return;
            }

            ++attempts;
            if (config.reconnect.max_attempts > 0 &&
                attempts >= config.reconnect.max_attempts) {
                if (callbacks.on_state_changed && peer_id.valid()) {
                    callbacks.on_state_changed(peer_id, net::ConnectionState::Failed);
                }
                return;
            }

            if (callbacks.on_state_changed && peer_id.valid()) {
                callbacks.on_state_changed(peer_id, net::ConnectionState::Reconnecting);
            }

            // Wait with wake check
            auto deadline = std::chrono::steady_clock::now() + delay;
            while (running && std::chrono::steady_clock::now() < deadline) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (remaining.count() <= 0) break;

                auto ms = std::min<long long>(remaining.count(), 100);

                #ifdef _WIN32
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(static_cast<SOCKET>(wake.read_fd), &rfds);
                timeval tv;
                tv.tv_sec = static_cast<long>(ms / 1000);
                tv.tv_usec = static_cast<long>((ms % 1000) * 1000);
                ::select(0, &rfds, nullptr, nullptr, &tv);
                if (FD_ISSET(static_cast<SOCKET>(wake.read_fd), &rfds)) {
                    drain_wake_pipe(wake);
                }
                #else
                PollSet ps;
                ps.add_read(wake.read_fd);
                ps.wait(static_cast<int>(ms));
                if (ps.is_readable(wake.read_fd)) {
                    drain_wake_pipe(wake);
                }
                #endif
            }

            // Exponential backoff
            delay = std::chrono::milliseconds(
                static_cast<long long>(
                    static_cast<double>(delay.count()) *
                    config.reconnect.backoff_multiplier));
            if (delay > config.reconnect.max_delay) {
                delay = config.reconnect.max_delay;
            }

            continue;
        }

        // Connected successfully
        attempts = 0;
        delay = config.reconnect.initial_delay;

        if (callbacks.on_state_changed && peer_id.valid()) {
            callbacks.on_state_changed(peer_id, net::ConnectionState::Connected);
        }

        recv_loop();

        // Disconnected
        handle_disconnect();

        if (!config.reconnect.enabled || !running) {
            return;
        }

        if (callbacks.on_state_changed && peer_id.valid()) {
            callbacks.on_state_changed(peer_id, net::ConnectionState::Reconnecting);
        }
    }
}

VoidResult TcpClientTransport::Impl::try_connect() {
    CONDUIT_TRY_ASSIGN(auto, s, create_tcp_socket());

    // Resolve host
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto port_str = std::to_string(config.port);
    int gai = getaddrinfo(config.host.c_str(), port_str.c_str(), &hints, &result);
    if (gai != 0 || !result) {
        close_socket(s);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::HostNotFound,
                          std::format("Cannot resolve host '{}' (getaddrinfo error {})",
                                      config.host, gai)));
    }

    // Set non-blocking before connect to avoid hanging on firewalled hosts
    auto nb_result = set_nonblocking(s);
    if (!nb_result) {
        freeaddrinfo(result);
        close_socket(s);
        return nb_result;
    }

    int conn = ::connect(
        #ifdef _WIN32
        static_cast<SOCKET>(s),
        #else
        s,
        #endif
        result->ai_addr,
        static_cast<socklen_t>(result->ai_addrlen));

    freeaddrinfo(result);

    if (conn != 0) {
        int err = get_last_error();
        #ifdef _WIN32
        bool in_progress = (err == WSAEWOULDBLOCK);
        #else
        bool in_progress = (err == EINPROGRESS);
        #endif

        if (!in_progress) {
            close_socket(s);
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::ConnectionRefused,
                              std::format("Connect to {}:{} failed: {}",
                                          config.host, config.port,
                                          error_to_string(err))));
        }

        // Wait for connect with timeout, monitoring wake pipe for shutdown
        auto deadline = std::chrono::steady_clock::now() + config.connect_timeout;
        bool connected = false;

        while (running) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;

            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            auto wait_ms = std::min<long long>(remaining_ms.count(), 100);

            #ifdef _WIN32
            fd_set wfds, rfds;
            FD_ZERO(&wfds);
            FD_ZERO(&rfds);
            FD_SET(static_cast<SOCKET>(s), &wfds);
            FD_SET(static_cast<SOCKET>(wake.read_fd), &rfds);
            int nfds = 0;

            timeval tv;
            tv.tv_sec = static_cast<long>(wait_ms / 1000);
            tv.tv_usec = static_cast<long>((wait_ms % 1000) * 1000);

            int sel = ::select(nfds, &rfds, &wfds, nullptr, &tv);
            if (sel < 0) {
                int sel_err = get_last_error();
                if (sel_err == WSAEINTR) continue;
                break;
            }

            // Check wake pipe (shutdown signal)
            if (FD_ISSET(static_cast<SOCKET>(wake.read_fd), &rfds)) {
                drain_wake_pipe(wake);
                if (!running) break;
            }

            // Check if connect completed
            if (FD_ISSET(static_cast<SOCKET>(s), &wfds)) {
            #else
            PollSet ps;
            ps.add_write(s);
            ps.add_read(wake.read_fd);

            int sel = ps.wait(static_cast<int>(wait_ms));
            if (sel < 0) {
                if (errno == EINTR) continue;
                break;
            }

            // Check wake pipe (shutdown signal)
            if (ps.is_readable(wake.read_fd)) {
                drain_wake_pipe(wake);
                if (!running) break;
            }

            // Check if connect completed
            if (ps.is_writable(s)) {
            #endif
                int so_error = 0;
                #ifdef _WIN32
                int optlen = sizeof(so_error);
                getsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&so_error), &optlen);
                #else
                socklen_t optlen = sizeof(so_error);
                getsockopt(s, SOL_SOCKET, SO_ERROR, &so_error, &optlen);
                #endif

                if (so_error == 0) {
                    connected = true;
                } else {
                    close_socket(s);
                    return std::unexpected(
                        CONDUIT_ERROR(ErrorCode::ConnectionRefused,
                                      std::format("Connect to {}:{} failed: {}",
                                                  config.host, config.port,
                                                  error_to_string(so_error))));
                }
                break;
            }
        }

        if (!connected) {
            close_socket(s);
            if (!running) {
                return std::unexpected(
                    CONDUIT_ERROR(ErrorCode::ConnectionClosed, "Connect aborted by shutdown"));
            }
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::ConnectionTimeout,
                              std::format("Connect to {}:{} timed out",
                                          config.host, config.port)));
        }
    }

    // Restore blocking mode for recv_loop compatibility
    #ifdef _WIN32
    {
        u_long mode = 0;
        if (ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &mode) != 0) {
            close_socket(s);
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Failed to restore blocking mode: " +
                              error_to_string(get_last_error())));
        }
    }
    #else
    {
        int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0 || fcntl(s, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            close_socket(s);
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Failed to restore blocking mode: " +
                              error_to_string(errno)));
        }
    }
    #endif

    // Disable Nagle's algorithm for low-latency protocol messaging
    auto nodelay = set_tcp_nodelay(s);
    if (!nodelay) {
        close_socket(s);
        return nodelay;
    }

    sock = s;
    return {};
}

void TcpClientTransport::Impl::recv_loop() {
    std::vector<uint8_t> buf(config.recv_buffer_size);

    while (running) {
        #ifdef _WIN32
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(static_cast<SOCKET>(sock), &rfds);
        FD_SET(static_cast<SOCKET>(wake.read_fd), &rfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (get_last_error() == WSAEINTR) continue;
            break;
        }

        if (sel == 0) continue;

        if (FD_ISSET(static_cast<SOCKET>(wake.read_fd), &rfds)) {
            drain_wake_pipe(wake);
            if (!running) break;
        }

        if (FD_ISSET(static_cast<SOCKET>(sock), &rfds)) {
            int n = ::recv(static_cast<SOCKET>(sock),
                           reinterpret_cast<char*>(buf.data()),
                           static_cast<int>(buf.size()), 0);
        #else
        PollSet ps;
        ps.add_read(sock);
        ps.add_read(wake.read_fd);

        int sel = ps.wait(100);  // 100ms
        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (sel == 0) continue;

        if (ps.has_error(sock)) {
            break;  // Socket error, will trigger disconnect handling
        }

        if (ps.is_readable(wake.read_fd)) {
            drain_wake_pipe(wake);
            if (!running) break;
        }

        if (ps.is_readable(sock)) {
            auto n = ::recv(sock, buf.data(), buf.size(), 0);
        #endif
            if (n <= 0) {
                break;  // Disconnected or error
            }

            if (callbacks.on_data_received && peer_id.valid()) {
                callbacks.on_data_received(
                    peer_id,
                    std::span<const uint8_t>(buf.data(),
                                             static_cast<size_t>(n)));
            }
        }
    }
}

void TcpClientTransport::Impl::handle_disconnect() {
    {
        std::lock_guard lock(send_mutex);
        if (sock != invalid_socket) {
            #ifdef _WIN32
            ::shutdown(static_cast<SOCKET>(sock), SD_SEND);
            #else
            ::shutdown(sock, SHUT_WR);
            #endif
            close_socket(sock);
            sock = invalid_socket;
        }
    }

    if (callbacks.on_state_changed && peer_id.valid()) {
        callbacks.on_state_changed(peer_id, net::ConnectionState::Disconnected);
    }

    if (callbacks.on_peer_disconnected && peer_id.valid()) {
        callbacks.on_peer_disconnected(peer_id);
    }
}

} // namespace conduit::transceiver::transport
