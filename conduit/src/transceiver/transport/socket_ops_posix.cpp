// SPDX-License-Identifier: MIT
// Conduit - Socket Operations (POSIX)

#ifndef _WIN32

#include "socket_ops.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace conduit::transceiver::transport::detail {

VoidResult init_networking() {
    // No-op on POSIX
    return {};
}

void cleanup_networking() {
    // No-op on POSIX
}

namespace {
// Set close-on-exec flag portably (SOCK_CLOEXEC is Linux-only)
void set_cloexec([[maybe_unused]] int fd) {
#ifndef SOCK_CLOEXEC
    int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
#endif
}
} // namespace

Result<socket_t> create_tcp_socket() {
#ifdef SOCK_CLOEXEC
    int s = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
#else
    int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (s < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create TCP socket: " +
                          error_to_string(errno)));
    }
    set_cloexec(s);
    return s;
}

Result<socket_t> create_udp_socket() {
#ifdef SOCK_CLOEXEC
    int s = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (s < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create UDP socket: " +
                          error_to_string(errno)));
    }
    set_cloexec(s);
    return s;
}

VoidResult set_nonblocking(socket_t sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to get socket flags: " +
                          error_to_string(errno)));
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set non-blocking: " +
                          error_to_string(errno)));
    }
    return {};
}

VoidResult set_reuse_addr(socket_t sock) {
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set SO_REUSEADDR: " +
                          error_to_string(errno)));
    }
    return {};
}

VoidResult set_tcp_nodelay(socket_t sock) {
    int opt = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set TCP_NODELAY: " +
                          error_to_string(errno)));
    }
    return {};
}

VoidResult set_socket_buffer_sizes(socket_t sock, size_t recv_size, size_t send_size) {
    if (recv_size > 0) {
        int val = static_cast<int>(recv_size);
        if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) != 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Failed to set SO_RCVBUF: " +
                              error_to_string(errno)));
        }
    }
    if (send_size > 0) {
        int val = static_cast<int>(send_size);
        if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) != 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Failed to set SO_SNDBUF: " +
                              error_to_string(errno)));
        }
    }
    return {};
}

void close_socket(socket_t sock) {
    if (sock != invalid_socket) {
        ::close(sock);
    }
}

int get_last_error() {
    return errno;
}

std::string error_to_string(int err) {
    char buf[256];
    // Use the XSI-compliant strerror_r which returns int
    #if (_POSIX_C_SOURCE >= 200112L) && !defined(_GNU_SOURCE)
    if (strerror_r(err, buf, sizeof(buf)) == 0) {
        return std::string(buf);
    }
    return "Unknown error " + std::to_string(err);
    #elif defined(__GLIBC__) && defined(_GNU_SOURCE)
    // GNU version returns char*
    return std::string(strerror_r(err, buf, sizeof(buf)));
    #else
    // Fallback: strerror is good enough on platforms with thread-local buffers
    return std::strerror(err);
    #endif
}

Result<WakePipe> create_wake_pipe() {
    int fds[2];
#ifdef __linux__
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create pipe: " + error_to_string(errno)));
    }
#else
    if (::pipe(fds) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create pipe: " + error_to_string(errno)));
    }
    set_cloexec(fds[0]);
    set_cloexec(fds[1]);
#endif

    // Set both ends non-blocking
    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(fds[i], F_GETFL, 0);
        if (flags < 0 || fcntl(fds[i], F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Failed to set wake pipe non-blocking: " +
                              error_to_string(errno)));
        }
    }

    WakePipe result;
    result.read_fd = fds[0];
    result.write_fd = fds[1];
    return result;
}

void close_wake_pipe(WakePipe& pipe) {
    if (pipe.read_fd != invalid_socket) {
        ::close(pipe.read_fd);
        pipe.read_fd = invalid_socket;
    }
    if (pipe.write_fd != invalid_socket) {
        ::close(pipe.write_fd);
        pipe.write_fd = invalid_socket;
    }
}

void signal_wake_pipe(const WakePipe& pipe) {
    if (pipe.write_fd != invalid_socket) {
        char byte = 1;
        [[maybe_unused]] auto r = ::write(pipe.write_fd, &byte, 1);
    }
}

void drain_wake_pipe(const WakePipe& pipe) {
    if (pipe.read_fd != invalid_socket) {
        char buf[64];
        while (::read(pipe.read_fd, buf, sizeof(buf)) > 0) {}
    }
}

// ============================================================================
// Multicast
// ============================================================================

VoidResult join_multicast_group(socket_t sock, const std::string& group, const std::string& iface) {
    struct ip_mreq mreq{};
    if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) != 1) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "Invalid multicast group address: " + group));
    }
    if (inet_pton(AF_INET, iface.c_str(), &mreq.imr_interface) != 1) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "Invalid multicast interface address: " + iface));
    }
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "IP_ADD_MEMBERSHIP failed: " + error_to_string(errno)));
    }
    return {};
}

VoidResult leave_multicast_group(socket_t sock, const std::string& group, const std::string& iface) {
    struct ip_mreq mreq{};
    inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr);
    inet_pton(AF_INET, iface.c_str(), &mreq.imr_interface);
    // Best-effort: don't fail stop() if leave errors
    setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    return {};
}

VoidResult set_multicast_ttl(socket_t sock, uint8_t ttl) {
    // macOS/BSD requires unsigned char for IP_MULTICAST_TTL (Linux accepts both)
    unsigned char val = ttl;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &val, sizeof(val)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set IP_MULTICAST_TTL: " + error_to_string(errno)));
    }
    return {};
}

VoidResult set_multicast_loop(socket_t sock, bool enable) {
    // macOS/BSD requires unsigned char for IP_MULTICAST_LOOP (Linux accepts both)
    unsigned char val = enable ? 1 : 0;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set IP_MULTICAST_LOOP: " + error_to_string(errno)));
    }
    return {};
}

VoidResult set_multicast_interface(socket_t sock, const std::string& iface) {
    struct in_addr addr{};
    if (inet_pton(AF_INET, iface.c_str(), &addr) != 1) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "Invalid multicast interface address: " + iface));
    }
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set IP_MULTICAST_IF: " + error_to_string(errno)));
    }
    return {};
}

} // namespace conduit::transceiver::transport::detail

#endif // !_WIN32
