// SPDX-License-Identifier: MIT
// Conduit - Socket Operations (Win32 / Winsock2)

#ifdef _WIN32

#include "socket_ops.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>

namespace conduit::transceiver::transport::detail {

static std::mutex wsa_mutex;
static int wsa_ref_count = 0;

VoidResult init_networking() {
    std::lock_guard lock(wsa_mutex);
    if (wsa_ref_count == 0) {
        WSADATA wsa_data;
        int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (err != 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "WSAStartup failed: " + error_to_string(err)));
        }
    }
    ++wsa_ref_count;
    return {};
}

void cleanup_networking() {
    std::lock_guard lock(wsa_mutex);
    if (wsa_ref_count > 0 && --wsa_ref_count == 0) {
        WSACleanup();
    }
}

Result<socket_t> create_tcp_socket() {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create TCP socket: " +
                          error_to_string(get_last_error())));
    }
    return static_cast<socket_t>(s);
}

Result<socket_t> create_udp_socket() {
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to create UDP socket: " +
                          error_to_string(get_last_error())));
    }
    return static_cast<socket_t>(s);
}

VoidResult set_nonblocking(socket_t sock) {
    u_long mode = 1;
    if (ioctlsocket(static_cast<SOCKET>(sock), static_cast<long>(FIONBIO), &mode) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set non-blocking: " +
                          error_to_string(get_last_error())));
    }
    return {};
}

VoidResult set_reuse_addr(socket_t sock) {
    int opt = 1;
    if (setsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set SO_REUSEADDR: " +
                          error_to_string(get_last_error())));
    }
    return {};
}

VoidResult set_tcp_nodelay(socket_t sock) {
    int opt = 1;
    if (setsockopt(static_cast<SOCKET>(sock), IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set TCP_NODELAY: " +
                          error_to_string(get_last_error())));
    }
    return {};
}

void close_socket(socket_t sock) {
    if (sock != invalid_socket) {
        ::closesocket(static_cast<SOCKET>(sock));
    }
}

int get_last_error() {
    return WSAGetLastError();
}

std::string error_to_string(int err) {
    char* msg = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(err),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string result;
    if (msg) {
        result = msg;
        LocalFree(msg);
        // Trim trailing newline
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    } else {
        result = "Error code " + std::to_string(err);
    }
    return result;
}

// Wake pipe: On Windows, use a pair of connected UDP sockets (loopback)
Result<WakePipe> create_wake_pipe() {
    // Create a UDP socket bound to loopback
    SOCKET listener = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listener == INVALID_SOCKET) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError, "Failed to create wake pipe socket"));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // Ephemeral

    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(listener);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError, "Failed to bind wake pipe"));
    }

    // Get bound address
    int addrlen = sizeof(addr);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrlen) != 0) {
        ::closesocket(listener);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError, "Failed to get wake pipe address"));
    }

    // Create sender socket
    SOCKET sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) {
        ::closesocket(listener);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError, "Failed to create wake pipe sender"));
    }

    // Connect sender to listener
    if (::connect(sender, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(listener);
        ::closesocket(sender);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError, "Failed to connect wake pipe"));
    }

    // Set read socket non-blocking so drain_wake_pipe never blocks
    u_long nb_mode = 1;
    if (ioctlsocket(listener, static_cast<long>(FIONBIO), &nb_mode) != 0) {
        ::closesocket(listener);
        ::closesocket(sender);
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::SocketError,
                          "Failed to set wake pipe non-blocking: " +
                          error_to_string(get_last_error())));
    }

    WakePipe result;
    result.read_fd = static_cast<socket_t>(listener);
    result.write_fd = static_cast<socket_t>(sender);
    return result;
}

void close_wake_pipe(WakePipe& pipe) {
    if (pipe.read_fd != invalid_socket) {
        ::closesocket(static_cast<SOCKET>(pipe.read_fd));
        pipe.read_fd = invalid_socket;
    }
    if (pipe.write_fd != invalid_socket) {
        ::closesocket(static_cast<SOCKET>(pipe.write_fd));
        pipe.write_fd = invalid_socket;
    }
}

void signal_wake_pipe(const WakePipe& pipe) {
    if (pipe.write_fd != invalid_socket) {
        char byte = 1;
        ::send(static_cast<SOCKET>(pipe.write_fd), &byte, 1, 0);
    }
}

void drain_wake_pipe(const WakePipe& pipe) {
    if (pipe.read_fd != invalid_socket) {
        char buf[64];
        while (::recv(static_cast<SOCKET>(pipe.read_fd), buf, sizeof(buf), 0) > 0) {}
    }
}

} // namespace conduit::transceiver::transport::detail

#endif // _WIN32
