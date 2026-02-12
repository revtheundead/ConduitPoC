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

Result<socket_t> create_tcp_socket() {
    int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create TCP socket: " +
                          error_to_string(errno)));
    }
    return s;
}

Result<socket_t> create_udp_socket() {
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create UDP socket: " +
                          error_to_string(errno)));
    }
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
    #if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
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
    if (::pipe(fds) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create pipe: " + error_to_string(errno)));
    }

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

} // namespace conduit::transceiver::transport::detail

#endif // !_WIN32
