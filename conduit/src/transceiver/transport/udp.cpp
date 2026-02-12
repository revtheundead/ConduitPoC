// SPDX-License-Identifier: MIT
// Conduit - UDP Transport (implementation)

#include <conduit/transceiver/transport/udp.hpp>
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
#include <unordered_map>
#include <vector>

namespace conduit::transceiver::transport {

using namespace detail;

// Hash for sockaddr_in (address + port)
struct AddrHash {
    size_t operator()(const sockaddr_in& a) const noexcept {
        auto h1 = std::hash<uint32_t>{}(a.sin_addr.s_addr);
        auto h2 = std::hash<uint16_t>{}(a.sin_port);
        return h1 ^ (h2 << 16);
    }
};

struct AddrEqual {
    bool operator()(const sockaddr_in& a, const sockaddr_in& b) const noexcept {
        return a.sin_addr.s_addr == b.sin_addr.s_addr &&
               a.sin_port == b.sin_port;
    }
};

struct UdpTransport::Impl {
    UdpConfig config;
    TransportCallbacks callbacks;
    PeerId peer_id;  // For single-peer mode

    socket_t sock = invalid_socket;
    WakePipe wake;
    std::thread io_thread;
    std::atomic<bool> running{false};
    std::mutex send_mutex;

    sockaddr_in remote_addr{};
    bool single_peer = false;

    // Multi-peer mode: map address → PeerId
    std::mutex peers_mutex;
    std::unordered_map<sockaddr_in, PeerId, AddrHash, AddrEqual> addr_to_peer;
    std::unordered_map<uint32_t, sockaddr_in> peer_to_addr;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> peer_last_activity;

    void io_loop();
    PeerId resolve_peer(const sockaddr_in& addr);
    void evict_stale_peers();
};

UdpTransport::UdpTransport(UdpConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->single_peer = !impl_->config.remote_address.empty();
}

UdpTransport::~UdpTransport() {
    stop();
}

bool UdpTransport::is_multi_peer() const noexcept {
    return !impl_->single_peer;
}

VoidResult UdpTransport::start(TransportCallbacks cb) {
    if (impl_->running.load()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::AlreadyRunning, "UDP transport already running"));
    }

    auto net_result = init_networking();
    if (!net_result) return net_result;
    NetGuard net_guard;

    CONDUIT_TRY_ASSIGN(auto, sock, create_udp_socket());

    auto reuse = set_reuse_addr(sock);
    if (!reuse) {
        close_socket(sock);
        return reuse;
    }

    // Bind
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(impl_->config.bind_port);

    if (inet_pton(AF_INET, impl_->config.bind_address.c_str(),
                  &bind_addr.sin_addr) != 1) {
        close_socket(sock);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          std::format("Invalid bind address: {}",
                                      impl_->config.bind_address)));
    }

    #ifdef _WIN32
    if (::bind(static_cast<SOCKET>(sock),
               reinterpret_cast<sockaddr*>(&bind_addr),
               sizeof(bind_addr)) != 0) {
    #else
    if (::bind(sock, reinterpret_cast<sockaddr*>(&bind_addr),
               sizeof(bind_addr)) != 0) {
    #endif
        close_socket(sock);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          std::format("Bind to {}:{} failed: {}",
                                      impl_->config.bind_address,
                                      impl_->config.bind_port,
                                      error_to_string(get_last_error()))));
    }

    // Set up remote address for single-peer mode
    if (impl_->single_peer) {
        impl_->remote_addr.sin_family = AF_INET;
        impl_->remote_addr.sin_port = htons(impl_->config.remote_port);
        if (inet_pton(AF_INET, impl_->config.remote_address.c_str(),
                      &impl_->remote_addr.sin_addr) != 1) {
            close_socket(sock);
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::InvalidConfig,
                              std::format("Invalid remote address: {}",
                                          impl_->config.remote_address)));
        }
    }

    auto wake_result = create_wake_pipe();
    if (!wake_result) {
        close_socket(sock);
        return std::unexpected(wake_result.error());
    }

    impl_->sock = sock;
    impl_->wake = *wake_result;
    impl_->callbacks = std::move(cb);
    impl_->running = true;

    impl_->io_thread = std::thread([this] { impl_->io_loop(); });
    net_guard.release();
    return {};
}

void UdpTransport::stop() {
    if (!impl_->running.exchange(false)) return;

    signal_wake_pipe(impl_->wake);

    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }

    {
        std::lock_guard send_lock(impl_->send_mutex);
        if (impl_->sock != invalid_socket) {
            close_socket(impl_->sock);
            impl_->sock = invalid_socket;
        }
    }

    // Collect tracked peers, then clear maps
    std::vector<PeerId> tracked_peers;
    {
        std::lock_guard lock(impl_->peers_mutex);
        tracked_peers.reserve(impl_->addr_to_peer.size());
        for (auto& [addr, peer] : impl_->addr_to_peer) {
            tracked_peers.push_back(peer);
        }
        impl_->addr_to_peer.clear();
        impl_->peer_to_addr.clear();
        impl_->peer_last_activity.clear();
    }

    // Fire disconnect callbacks outside lock
    for (auto& peer : tracked_peers) {
        if (impl_->callbacks.on_state_changed) {
            impl_->callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
        }
        if (impl_->callbacks.on_peer_disconnected) {
            impl_->callbacks.on_peer_disconnected(peer);
        }
    }

    // Also notify single-peer mode disconnect
    if (impl_->single_peer && impl_->callbacks.on_state_changed && impl_->peer_id.valid()) {
        impl_->callbacks.on_state_changed(impl_->peer_id, net::ConnectionState::Disconnected);
    }

    close_wake_pipe(impl_->wake);
    cleanup_networking();
}

VoidResult UdpTransport::send(PeerId peer, std::span<const uint8_t> data) {
    std::lock_guard send_lock(impl_->send_mutex);

    if (impl_->sock == invalid_socket) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::NotRunning, "Transport not started"));
    }

    if (data.size() > impl_->config.max_datagram_size) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::BufferOverrun,
                          std::format("UDP datagram size {} exceeds max {}",
                                      data.size(),
                                      impl_->config.max_datagram_size)));
    }

    sockaddr_in dest{};

    if (impl_->single_peer) {
        dest = impl_->remote_addr;
    } else {
        std::lock_guard lock(impl_->peers_mutex);
        auto it = impl_->peer_to_addr.find(peer.value());
        if (it == impl_->peer_to_addr.end()) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::PeerNotFound,
                              std::format("Peer {} has no known address",
                                          peer.value())));
        }
        dest = it->second;  // Copy by value to avoid dangling after unlock
    }

    #ifdef _WIN32
    int sent = ::sendto(static_cast<SOCKET>(impl_->sock),
                        reinterpret_cast<const char*>(data.data()),
                        static_cast<int>(data.size()), 0,
                        reinterpret_cast<const sockaddr*>(&dest),
                        sizeof(dest));
    #else
    auto sent = ::sendto(impl_->sock,
                         data.data(), data.size(), 0,
                         reinterpret_cast<const sockaddr*>(&dest),
                         sizeof(dest));
    #endif

    if (sent < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "sendto failed: " +
                          error_to_string(get_last_error())));
    }

    if (static_cast<size_t>(sent) != data.size()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          std::format("sendto: partial send ({} of {} bytes)",
                                      sent, data.size())));
    }

    return {};
}

// ============================================================================
// I/O thread
// ============================================================================

void UdpTransport::Impl::io_loop() {
    std::vector<uint8_t> buf(config.recv_buffer_size);
    auto last_eviction = std::chrono::steady_clock::now();

    // Obtain PeerId for single-peer mode
    if (single_peer && !peer_id.valid() && callbacks.on_peer_connected) {
        peer_id = callbacks.on_peer_connected();
    }

    // Notify connected state for single-peer
    if (single_peer && callbacks.on_state_changed && peer_id.valid()) {
        callbacks.on_state_changed(peer_id, net::ConnectionState::Connected);
    }

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
        #else
        PollSet ps;
        ps.add_read(sock);
        ps.add_read(wake.read_fd);

        int sel = ps.wait(100);  // 100ms
        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }
        #endif

        if (sel == 0) {
            if (!single_peer) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_eviction >= std::chrono::seconds(1)) {
                    evict_stale_peers();
                    last_eviction = now;
                }
            }
            continue;
        }

        #ifdef _WIN32
        if (FD_ISSET(static_cast<SOCKET>(wake.read_fd), &rfds)) {
        #else
        if (ps.has_error(sock)) {
            break;  // Socket error
        }

        if (ps.is_readable(wake.read_fd)) {
        #endif
            drain_wake_pipe(wake);
            if (!running) break;
        }

        #ifdef _WIN32
        if (FD_ISSET(static_cast<SOCKET>(sock), &rfds)) {
        #else
        if (ps.is_readable(sock)) {
        #endif
            sockaddr_in sender_addr{};
            #ifdef _WIN32
            int addrlen = sizeof(sender_addr);
            int n = ::recvfrom(static_cast<SOCKET>(sock),
                               reinterpret_cast<char*>(buf.data()),
                               static_cast<int>(buf.size()), 0,
                               reinterpret_cast<sockaddr*>(&sender_addr),
                               &addrlen);
            #else
            socklen_t addrlen = sizeof(sender_addr);
            auto n = ::recvfrom(sock, buf.data(), buf.size(), 0,
                                reinterpret_cast<sockaddr*>(&sender_addr),
                                &addrlen);
            #endif

            if (n <= 0) continue;

            PeerId sender;
            if (single_peer) {
                // Validate sender matches configured remote address
                if (sender_addr.sin_addr.s_addr != remote_addr.sin_addr.s_addr ||
                    sender_addr.sin_port != remote_addr.sin_port) {
                    LOG_WARN("UDP single-peer: dropping packet from unexpected sender");
                    continue;
                }
                sender = peer_id;
            } else {
                sender = resolve_peer(sender_addr);
            }

            if (callbacks.on_data_received && sender.valid()) {
                callbacks.on_data_received(
                    sender,
                    std::span<const uint8_t>(buf.data(),
                                             static_cast<size_t>(n)));
            }
        }
    }
}

PeerId UdpTransport::Impl::resolve_peer(const sockaddr_in& addr) {
    {
        std::lock_guard lock(peers_mutex);

        auto it = addr_to_peer.find(addr);
        if (it != addr_to_peer.end()) {
            // Update activity timestamp
            peer_last_activity[it->second.value()] = std::chrono::steady_clock::now();
            return it->second;
        }

        // Enforce max_peers limit to prevent unbounded map growth
        if (config.max_peers > 0 && addr_to_peer.size() >= config.max_peers) {
            LOG_WARNF("UDP max_peers limit reached ({}), rejecting new peer",
                      config.max_peers);
            return PeerId{};
        }
    }

    // New peer — ask transceiver OUTSIDE the lock to prevent deadlock
    // (callback may trigger send() which acquires peers_mutex)
    PeerId new_id;
    if (callbacks.on_peer_connected) {
        new_id = callbacks.on_peer_connected();
    }

    if (new_id.valid()) {
        bool orphaned = false;
        {
            std::lock_guard lock(peers_mutex);
            // Re-check max_peers inside lock to prevent TOCTOU race
            // (another thread could have added a peer while we were outside the lock)
            if (config.max_peers > 0 && addr_to_peer.size() >= config.max_peers) {
                LOG_WARN("UDP max_peers limit reached during peer registration, rejecting");
                orphaned = true;
            } else {
                addr_to_peer[addr] = new_id;
                peer_to_addr[new_id.value()] = addr;
                peer_last_activity[new_id.value()] = std::chrono::steady_clock::now();
            }
        }
        if (orphaned) {
            // Notify transceiver so it can clean up the allocated PeerId
            if (callbacks.on_peer_disconnected) {
                callbacks.on_peer_disconnected(new_id);
            }
            return PeerId{};
        }
    }

    return new_id;
}

void UdpTransport::Impl::evict_stale_peers() {
    if (config.peer_timeout.count() == 0) return;

    std::vector<PeerId> evicted;
    {
        std::lock_guard lock(peers_mutex);
        auto now = std::chrono::steady_clock::now();

        // Collect peers to evict
        std::vector<std::pair<PeerId, sockaddr_in>> to_evict;

        for (auto& [id, last] : peer_last_activity) {
            if (now - last > config.peer_timeout) {
                auto addr_it = peer_to_addr.find(id);
                if (addr_it != peer_to_addr.end()) {
                    to_evict.emplace_back(PeerId{id}, addr_it->second);
                }
            }
        }

        for (auto& [peer, addr] : to_evict) {
            addr_to_peer.erase(addr);
            peer_to_addr.erase(peer.value());
            peer_last_activity.erase(peer.value());
            evicted.push_back(peer);
            LOG_INFOF("UDP peer {} timed out, evicting", peer.value());
        }
    }

    // Fire callbacks outside lock
    for (auto& peer : evicted) {
        if (callbacks.on_state_changed) {
            callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
        }
        if (callbacks.on_peer_disconnected) {
            callbacks.on_peer_disconnected(peer);
        }
    }
}

} // namespace conduit::transceiver::transport
