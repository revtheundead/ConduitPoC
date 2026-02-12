// SPDX-License-Identifier: MIT
// Conduit - Serial Transport (POSIX termios)

#ifndef _WIN32

#include <conduit/transceiver/transport/serial.hpp>
#include <conduit/logging/logger.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "poll_set.hpp"

#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace conduit::transceiver::transport {

namespace {

// Thread-safe errno-to-string conversion
std::string errno_string(int err) {
    char buf[256];
    #if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
    if (strerror_r(err, buf, sizeof(buf)) == 0) {
        return std::string(buf);
    }
    return "Unknown error " + std::to_string(err);
    #elif defined(__GLIBC__) && defined(_GNU_SOURCE)
    return std::string(strerror_r(err, buf, sizeof(buf)));
    #else
    return std::strerror(err);
    #endif
}

} // anonymous namespace

struct SerialTransport::Impl {
    SerialConfig config;
    TransportCallbacks callbacks;
    PeerId peer_id;

    int fd = -1;
    int pipe_fds[2] = {-1, -1};  // Shutdown pipe
    std::thread read_thread;
    std::atomic<bool> running{false};
    std::mutex write_mutex;
    struct termios original_termios{};
    bool termios_saved = false;

    void read_loop();
    VoidResult configure_port();
};

SerialTransport::SerialTransport(SerialConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

SerialTransport::~SerialTransport() {
    stop();
}

VoidResult SerialTransport::start(TransportCallbacks cb) {
    if (impl_->running.load()) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::AlreadyRunning, "Serial transport already running"));
    }

    auto validation = validate_serial_config(impl_->config);
    if (!validation) return validation;

    impl_->fd = ::open(impl_->config.port.c_str(), O_RDWR | O_NOCTTY);
    if (impl_->fd < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::ConnectionRefused,
                          std::format("Cannot open serial port '{}': {}",
                                      impl_->config.port,
                                      errno_string(errno))));
    }

    auto cfg_result = impl_->configure_port();
    if (!cfg_result) {
        ::close(impl_->fd);
        impl_->fd = -1;
        return cfg_result;
    }

    if (::pipe(impl_->pipe_fds) != 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InternalError,
                          "Failed to create shutdown pipe"));
    }

    impl_->callbacks = std::move(cb);
    impl_->running = true;

    // Obtain PeerId from transceiver
    if (!impl_->peer_id.valid() && impl_->callbacks.on_peer_connected) {
        impl_->peer_id = impl_->callbacks.on_peer_connected();
    }

    if (impl_->callbacks.on_state_changed && impl_->peer_id.valid()) {
        impl_->callbacks.on_state_changed(
            impl_->peer_id, net::ConnectionState::Connected);
    }

    impl_->read_thread = std::thread([this] { impl_->read_loop(); });
    return {};
}

void SerialTransport::stop() {
    if (!impl_->running.exchange(false)) return;

    // Signal shutdown via pipe
    if (impl_->pipe_fds[1] >= 0) {
        char byte = 1;
        [[maybe_unused]] auto r = ::write(impl_->pipe_fds[1], &byte, 1);
    }

    // Set the fd to non-blocking mode BEFORE acquiring write_mutex.
    // If send() is blocked in ::write() (e.g., hardware flow control),
    // switching to non-blocking causes it to return with EAGAIN, which
    // releases write_mutex so we can acquire it below and close cleanly.
    if (impl_->fd >= 0) {
        int flags = fcntl(impl_->fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(impl_->fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    if (impl_->read_thread.joinable()) {
        impl_->read_thread.join();
    }

    {
        // Now safe to acquire — any blocking write has been unblocked.
        std::lock_guard lock(impl_->write_mutex);
        if (impl_->fd >= 0) {
            // Restore original terminal settings before closing
            if (impl_->termios_saved) {
                tcsetattr(impl_->fd, TCSANOW, &impl_->original_termios);
                impl_->termios_saved = false;
            }
            ::close(impl_->fd);
            impl_->fd = -1;
        }
    }

    for (int& fd : impl_->pipe_fds) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    if (impl_->callbacks.on_state_changed && impl_->peer_id.valid()) {
        impl_->callbacks.on_state_changed(
            impl_->peer_id, net::ConnectionState::Disconnected);
    }
}

VoidResult SerialTransport::send(PeerId /*peer*/,
                                 std::span<const uint8_t> data) {
    std::lock_guard lock(impl_->write_mutex);

    if (impl_->fd < 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::ConnectionClosed, "Serial port not open"));
    }

    const uint8_t* ptr = data.data();
    size_t remaining = data.size();

    while (remaining > 0) {
        ssize_t written;
        do {
            written = ::write(impl_->fd, ptr, remaining);
        } while (written < 0 && errno == EINTR);
        if (written < 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              std::format("Serial write error: {}",
                                          errno_string(errno))));
        }
        if (written == 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Serial write returned 0: device not ready"));
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }

    return {};
}

VoidResult SerialTransport::Impl::configure_port() {
    // Save original terminal settings for restoration on stop()
    if (tcgetattr(fd, &original_termios) == 0) {
        termios_saved = true;
    }

    struct termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          std::format("tcgetattr failed: {}",
                                      errno_string(errno))));
    }

    // Baud rate
    speed_t speed = B9600;
    switch (config.baud_rate) {
        case 1200:   speed = B1200; break;
        case 2400:   speed = B2400; break;
        case 4800:   speed = B4800; break;
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        #ifdef B460800
        case 460800: speed = B460800; break;
        #endif
        #ifdef B921600
        case 921600: speed = B921600; break;
        #endif
        default:
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::InvalidConfig,
                              std::format("Unsupported baud rate: {}",
                                          config.baud_rate)));
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Data bits
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    switch (config.data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
    }

    // Parity
    switch (config.parity) {
        case Parity::None:
            tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
            break;
        case Parity::Odd:
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
        case Parity::Even:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= static_cast<tcflag_t>(~PARODD);
            break;
    }

    // Stop bits
    switch (config.stop_bits) {
        case StopBits::One: tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB); break;
        case StopBits::Two: tty.c_cflag |= CSTOPB; break;
    }

    // Flow control
    switch (config.flow_control) {
        case FlowControl::None:
            tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
            tty.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
            break;
        case FlowControl::Hardware:
            tty.c_cflag |= CRTSCTS;
            tty.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
            break;
        case FlowControl::Software:
            tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
            tty.c_iflag |= (IXON | IXOFF);
            tty.c_iflag &= static_cast<tcflag_t>(~IXANY);
            break;
    }
    tty.c_cflag |= CLOCAL | CREAD;

    // Raw mode
    tty.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ECHOE | ISIG));
    tty.c_iflag &= static_cast<tcflag_t>(~(IGNBRK | BRKINT | PARMRK |
                      ISTRIP | INLCR | IGNCR | ICRNL));
    tty.c_oflag &= static_cast<tcflag_t>(~OPOST);

    // Read with timeout
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;  // 100ms timeout

    if (tcsetattr(fd, TCSAFLUSH, &tty) != 0) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          std::format("tcsetattr failed: {}",
                                      errno_string(errno))));
    }

    return {};
}

void SerialTransport::Impl::read_loop() {
    std::vector<uint8_t> buf(config.recv_buffer_size);

    while (running) {
        PollSet ps;
        ps.add_read(fd);
        ps.add_read(pipe_fds[0]);

        int sel = ps.wait(100);  // 100ms
        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (sel == 0) continue;

        if (ps.has_error(fd)) {
            break;  // Serial port error
        }

        if (ps.is_readable(pipe_fds[0])) {
            break;  // Shutdown
        }

        if (ps.is_readable(fd)) {
            auto n = ::read(fd, buf.data(), buf.size());
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                LOG_WARN(std::format("Serial read error: {}",
                                     errno_string(errno)));
                break;
            }
            if (n == 0) continue;

            if (callbacks.on_data_received && peer_id.valid()) {
                callbacks.on_data_received(
                    peer_id,
                    std::span<const uint8_t>(buf.data(),
                                             static_cast<size_t>(n)));
            }
        }
    }
}

} // namespace conduit::transceiver::transport

#endif // !_WIN32
