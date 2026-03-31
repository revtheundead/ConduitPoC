// SPDX-License-Identifier: MIT
// Conduit - Platform Socket Abstraction (internal)

#pragma once

#include <conduit/core/error.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace conduit::transceiver::transport::detail {

// ============================================================================
// Platform type aliases
// ============================================================================

#ifdef _WIN32
    using socket_t = uintptr_t;  // SOCKET is UINT_PTR on Windows
    inline constexpr socket_t invalid_socket = static_cast<socket_t>(~0);
#else
    using socket_t = int;
    inline constexpr socket_t invalid_socket = -1;
#endif

// ============================================================================
// Networking init / cleanup
// ============================================================================

VoidResult init_networking();
void cleanup_networking();

// ============================================================================
// Socket creation
// ============================================================================

Result<socket_t> create_tcp_socket();
Result<socket_t> create_udp_socket();

// ============================================================================
// Socket options
// ============================================================================

VoidResult set_nonblocking(socket_t sock);
VoidResult set_reuse_addr(socket_t sock);
VoidResult set_tcp_nodelay(socket_t sock);
VoidResult set_socket_buffer_sizes(socket_t sock, size_t recv_size, size_t send_size);

// ============================================================================
// Multicast
// ============================================================================

VoidResult join_multicast_group(socket_t sock, const std::string& group, const std::string& iface);
VoidResult leave_multicast_group(socket_t sock, const std::string& group, const std::string& iface);
VoidResult set_multicast_ttl(socket_t sock, uint8_t ttl);
VoidResult set_multicast_loop(socket_t sock, bool enable);
VoidResult set_multicast_interface(socket_t sock, const std::string& iface);

// ============================================================================
// Socket lifecycle
// ============================================================================

void close_socket(socket_t sock);

// ============================================================================
// Error reporting
// ============================================================================

int get_last_error();
std::string error_to_string(int err);

// ============================================================================
// Self-pipe for waking select() (shutdown signaling)
// ============================================================================

struct WakePipe {
    socket_t read_fd = invalid_socket;
    socket_t write_fd = invalid_socket;
};

Result<WakePipe> create_wake_pipe();
void close_wake_pipe(WakePipe& pipe);
void signal_wake_pipe(const WakePipe& pipe);
void drain_wake_pipe(const WakePipe& pipe);

// RAII guard: calls cleanup_networking() on scope exit unless released.
struct NetGuard {
    bool released = false;
    ~NetGuard() { if (!released) cleanup_networking(); }
    void release() noexcept { released = true; }
};

} // namespace conduit::transceiver::transport::detail
