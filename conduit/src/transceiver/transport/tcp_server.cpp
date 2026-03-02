// SPDX-License-Identifier: MIT
// Conduit - TCP Server Transport (implementation)

#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/logging/logger.hpp>
#include "socket_ops.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "poll_set.hpp"
// MSG_NOSIGNAL is Linux-specific; macOS/BSD use SO_NOSIGPIPE instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace conduit::transceiver::transport {

using namespace detail;

struct TcpServerTransport::Impl {
    struct ClientInfo {
        socket_t sock = invalid_socket;
        std::mutex send_mutex;  // per-client mutex for send serialization
    };
    TcpServerConfig config;
    TransportCallbacks callbacks;

    socket_t listen_sock = invalid_socket;
    WakePipe wake;
    std::thread io_thread;
    std::atomic<bool> running{false};

    std::mutex clients_mutex;
    std::unordered_map<uint32_t, std::unique_ptr<ClientInfo>> clients;  // PeerId value → client info

    void io_loop();
    void handle_accept();
    void handle_client_data(PeerId peer, socket_t client_sock,
                            std::vector<uint8_t>& buf);
    void remove_client(PeerId peer);
};

TcpServerTransport::TcpServerTransport(TcpServerConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

TcpServerTransport::~TcpServerTransport() {
    stop();
}

VoidResult TcpServerTransport::start(TransportCallbacks cb) {
    if (impl_->running.load()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::AlreadyRunning, "TCP server transport already running"));
    }

    auto net_result = init_networking();
    if (!net_result) return net_result;
    NetGuard net_guard;

    CONDUIT_TRY_ASSIGN(auto, sock, create_tcp_socket());

    auto reuse = set_reuse_addr(sock);
    if (!reuse) {
        close_socket(sock);
        return reuse;
    }

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(impl_->config.port);

    if (inet_pton(AF_INET, impl_->config.bind_address.c_str(),
                  &addr.sin_addr) != 1) {
        close_socket(sock);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          std::format("Invalid bind address: {}",
                                      impl_->config.bind_address)));
    }

    #ifdef _WIN32
    if (::bind(static_cast<SOCKET>(sock),
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    #else
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    #endif
        close_socket(sock);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          std::format("Bind to {}:{} failed: {}",
                                      impl_->config.bind_address,
                                      impl_->config.port,
                                      error_to_string(get_last_error()))));
    }

    // Listen
    #ifdef _WIN32
    if (::listen(static_cast<SOCKET>(sock), SOMAXCONN) != 0) {
    #else
    if (::listen(sock, SOMAXCONN) != 0) {
    #endif
        close_socket(sock);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "listen() failed: " +
                          error_to_string(get_last_error())));
    }

    auto wake_result = create_wake_pipe();
    if (!wake_result) {
        close_socket(sock);
        return std::unexpected(wake_result.error());
    }

    impl_->listen_sock = sock;
    impl_->wake = *wake_result;
    impl_->callbacks = std::move(cb);
    impl_->running = true;

    #ifdef _WIN32
    // Windows select uses fd_set which has FD_SETSIZE limit.
    // Reserve 2 slots for listen socket and wake pipe.
    if (impl_->config.max_clients > FD_SETSIZE - 2) {
        LOG_WARNF("TCP server max_clients reduced from {} to {} due to Windows FD_SETSIZE limit",
                  impl_->config.max_clients, FD_SETSIZE - 2);
        impl_->config.max_clients = FD_SETSIZE - 2;
    }
    #endif

    impl_->io_thread = std::thread([this] { impl_->io_loop(); });
    net_guard.release();
    return {};
}

void TcpServerTransport::stop() {
    if (!impl_->running.exchange(false)) return;

    signal_wake_pipe(impl_->wake);

    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }

    // Phase 1: Shut down all client sockets WITHOUT acquiring per-client
    // send_mutex.  This unblocks any ::send() that is blocked while holding
    // send_mutex, allowing it to return an error and release the mutex.
    {
        std::lock_guard clients_lock(impl_->clients_mutex);
        for (auto& [id, info] : impl_->clients) {
            #ifdef _WIN32
            ::shutdown(static_cast<SOCKET>(info->sock), SD_BOTH);
            #else
            ::shutdown(info->sock, SHUT_RDWR);
            #endif
        }
    }

    // Phase 2: Now that all blocked sends have been unblocked, acquire
    // both locks and close sockets cleanly.
    std::vector<std::pair<PeerId, socket_t>> to_close;
    {
        std::lock_guard clients_lock(impl_->clients_mutex);
        to_close.reserve(impl_->clients.size());
        for (auto& [id, info] : impl_->clients) {
            std::lock_guard send_lock(info->send_mutex);
            to_close.emplace_back(PeerId(id), info->sock);
            close_socket(info->sock);
        }
        impl_->clients.clear();
    }

    // Fire disconnect callbacks outside locks
    for (auto& [peer, sock] : to_close) {
        if (impl_->callbacks.on_state_changed) {
            impl_->callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
        }
        if (impl_->callbacks.on_peer_disconnected) {
            impl_->callbacks.on_peer_disconnected(peer);
        }
    }

    if (impl_->listen_sock != invalid_socket) {
        close_socket(impl_->listen_sock);
        impl_->listen_sock = invalid_socket;
    }

    close_wake_pipe(impl_->wake);
    cleanup_networking();
}

uint16_t TcpServerTransport::local_port() const {
    if (impl_->listen_sock == invalid_socket) return 0;
    sockaddr_in addr{};
    #ifdef _WIN32
    int addrlen = sizeof(addr);
    if (::getsockname(static_cast<SOCKET>(impl_->listen_sock),
                      reinterpret_cast<sockaddr*>(&addr), &addrlen) != 0) {
        return 0;
    }
    #else
    socklen_t addrlen = sizeof(addr);
    if (::getsockname(impl_->listen_sock,
                      reinterpret_cast<sockaddr*>(&addr), &addrlen) != 0) {
        return 0;
    }
    #endif
    return ntohs(addr.sin_port);
}

VoidResult TcpServerTransport::send(PeerId peer,
                                    std::span<const uint8_t> data) {
    socket_t sock;
    std::unique_lock<std::mutex> send_lock;
    {
        std::lock_guard clients_lock(impl_->clients_mutex);
        auto it = impl_->clients.find(peer.value());
        if (it == impl_->clients.end()) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::PeerNotFound,
                              std::format("Peer {} not connected", peer.value())));
        }
        // Acquire per-client send_mutex while still holding clients_mutex
        // to prevent use-after-free if client is removed concurrently
        send_lock = std::unique_lock<std::mutex>(it->second->send_mutex);
        sock = it->second->sock;
    }
    // Now clients_mutex is released but we hold send_lock

    const char* ptr = reinterpret_cast<const char*>(data.data());
    size_t remaining = data.size();

    while (remaining > 0) {
        #ifdef _WIN32
        int sent = ::send(static_cast<SOCKET>(sock), ptr,
                          static_cast<int>(remaining), 0);
        #else
        ssize_t sent;
        do {
            sent = ::send(sock, ptr, remaining, MSG_NOSIGNAL);
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

void TcpServerTransport::Impl::io_loop() {
    std::vector<uint8_t> buf(config.recv_buffer_size);
    std::vector<std::pair<PeerId, socket_t>> client_snapshot;

    while (running) {
        #ifdef _WIN32
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(static_cast<SOCKET>(listen_sock), &rfds);
        FD_SET(static_cast<SOCKET>(wake.read_fd), &rfds);
        #else
        PollSet ps;
        ps.add_read(listen_sock);
        ps.add_read(wake.read_fd);
        #endif

        // Add client sockets
        client_snapshot.clear();
        #ifdef _WIN32
        std::vector<PeerId> overflow_peers;
        #endif
        {
            std::lock_guard lock(clients_mutex);
            client_snapshot.reserve(clients.size());
            for (auto& [id, info] : clients) {
                #ifdef _WIN32
                if (rfds.fd_count >= FD_SETSIZE) {
                    overflow_peers.emplace_back(id);
                    continue;
                }
                #endif
                client_snapshot.emplace_back(PeerId(id), info->sock);
                #ifdef _WIN32
                FD_SET(static_cast<SOCKET>(info->sock), &rfds);
                #else
                ps.add_read(info->sock);
                #endif
            }
        }

        #ifdef _WIN32
        // Disconnect overflow clients to prevent zombie connections that
        // can never be polled and would linger indefinitely.
        for (auto& peer : overflow_peers) {
            LOG_WARNF("Disconnecting overflow client peer {}: FD_SETSIZE limit reached", peer.value());
            remove_client(peer);
        }
        #endif

        #ifdef _WIN32
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (get_last_error() == WSAEINTR) continue;
            break;
        }
        #else
        int sel = ps.wait(100);  // 100ms
        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }
        #endif

        if (sel == 0) continue;

        // Check wake pipe
        #ifdef _WIN32
        if (FD_ISSET(static_cast<SOCKET>(wake.read_fd), &rfds)) {
        #else
        if (ps.is_readable(wake.read_fd)) {
        #endif
            drain_wake_pipe(wake);
            if (!running) break;
        }

        // Check listen socket for new connections
        #ifdef _WIN32
        if (FD_ISSET(static_cast<SOCKET>(listen_sock), &rfds)) {
        #else
        if (ps.is_readable(listen_sock)) {
        #endif
            handle_accept();
        }

        // Check client sockets for data
        for (auto& [peer, sock] : client_snapshot) {
            #ifdef _WIN32
            if (FD_ISSET(static_cast<SOCKET>(sock), &rfds)) {
            #else
            if (ps.is_readable(sock) || ps.has_error(sock)) {
            #endif
                handle_client_data(peer, sock, buf);
            }
        }
    }
}

void TcpServerTransport::Impl::handle_accept() {
    sockaddr_in client_addr{};
    #ifdef _WIN32
    int addrlen = sizeof(client_addr);
    SOCKET client = ::accept(static_cast<SOCKET>(listen_sock),
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &addrlen);
    if (client == INVALID_SOCKET) {
        LOG_WARNF("TCP server accept failed: {}", error_to_string(get_last_error()));
        return;
    }
    socket_t client_sock = static_cast<socket_t>(client);
    #else
    socklen_t addrlen = sizeof(client_addr);
    int client = ::accept(listen_sock,
                          reinterpret_cast<sockaddr*>(&client_addr),
                          &addrlen);
    if (client < 0) {
        LOG_WARNF("TCP server accept failed: {}", error_to_string(get_last_error()));
        return;
    }
    socket_t client_sock = client;
    #endif

    // Disable Nagle's algorithm for low-latency protocol messaging
    auto nodelay = set_tcp_nodelay(client_sock);
    if (!nodelay) {
        close_socket(client_sock);
        LOG_WARN("Failed to set TCP_NODELAY on accepted client");
        return;
    }

    // Check max clients
    {
        std::lock_guard lock(clients_mutex);
        if (clients.size() >= config.max_clients) {
            close_socket(client_sock);
            LOG_WARN("Max clients reached, rejecting connection");
            return;
        }
    }

    // Format remote endpoint for peer identification
    char addr_buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_buf, sizeof(addr_buf));
    std::string endpoint = std::format("{}:{}", addr_buf, ntohs(client_addr.sin_port));

    // Ask transceiver for a PeerId
    PeerId peer_id;
    if (callbacks.on_peer_connected) {
        peer_id = callbacks.on_peer_connected(std::move(endpoint));
    }

    if (!peer_id.valid()) {
        close_socket(client_sock);
        return;
    }

    {
        std::lock_guard lock(clients_mutex);
        auto info = std::make_unique<ClientInfo>();
        info->sock = client_sock;
        clients[peer_id.value()] = std::move(info);
    }

    LOG_INFOF("TCP client connected, peer={}", peer_id.value());
}

void TcpServerTransport::Impl::handle_client_data(
    PeerId peer, socket_t client_sock, std::vector<uint8_t>& buf) {

    #ifdef _WIN32
    int n = ::recv(static_cast<SOCKET>(client_sock),
                   reinterpret_cast<char*>(buf.data()),
                   static_cast<int>(buf.size()), 0);
    #else
    auto n = ::recv(client_sock, buf.data(), buf.size(), 0);
    #endif

    if (n <= 0) {
        // Disconnected
        remove_client(peer);
        return;
    }

    if (callbacks.on_data_received) {
        callbacks.on_data_received(
            peer, std::span<const uint8_t>(buf.data(), static_cast<size_t>(n)));
    }
}

void TcpServerTransport::Impl::remove_client(PeerId peer) {
    socket_t sock = invalid_socket;
    std::unique_ptr<ClientInfo> removed_client;
    {
        std::lock_guard clients_lock(clients_mutex);
        auto it = clients.find(peer.value());
        if (it != clients.end()) {
            // Move the client out of the map so we can erase the entry
            // while still holding send_mutex.  The lock_guard on send_mutex
            // must be released BEFORE ~ClientInfo destroys the mutex.
            removed_client = std::move(it->second);
            clients.erase(it);

            // Acquire per-client send_mutex while holding clients_mutex
            // so that any in-progress send() completes and no new send()
            // can start using this client's socket.
            std::lock_guard send_lock(removed_client->send_mutex);
            sock = removed_client->sock;
            #ifdef _WIN32
            ::shutdown(static_cast<SOCKET>(sock), SD_SEND);
            #else
            ::shutdown(sock, SHUT_WR);
            #endif
            close_socket(sock);
        }
        // send_lock released here, then removed_client destroyed safely
    }

    if (sock != invalid_socket) {
        if (callbacks.on_state_changed) {
            callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
        }
        if (callbacks.on_peer_disconnected) {
            callbacks.on_peer_disconnected(peer);
        }
        LOG_INFOF("TCP client disconnected, peer={}", peer.value());
    }
}

} // namespace conduit::transceiver::transport
