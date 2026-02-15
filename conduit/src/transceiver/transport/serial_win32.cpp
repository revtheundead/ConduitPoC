// SPDX-License-Identifier: MIT
// Conduit - Serial Transport (Win32 COM port)

#ifdef _WIN32

#include <conduit/transceiver/transport/serial.hpp>
#include <conduit/logging/logger.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace conduit::transceiver::transport {

struct SerialTransport::Impl {
    SerialConfig config;
    TransportCallbacks callbacks;
    PeerId peer_id;

    HANDLE port_handle = INVALID_HANDLE_VALUE;
    HANDLE stop_event = nullptr;
    HANDLE write_event = nullptr;  // Reusable event for overlapped writes
    std::thread read_thread;
    std::atomic<bool> running{false};
    std::mutex write_mutex;

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

    // Prepare port name for CreateFile (prefix with \\.\)
    std::string port_path = "\\\\.\\" + impl_->config.port;

    impl_->port_handle = CreateFileA(
        port_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);

    if (impl_->port_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::ConnectionRefused,
                          std::format("Cannot open serial port '{}': error {}",
                                      impl_->config.port, GetLastError())));
    }

    auto cfg_result = impl_->configure_port();
    if (!cfg_result) {
        CloseHandle(impl_->port_handle);
        impl_->port_handle = INVALID_HANDLE_VALUE;
        return cfg_result;
    }

    impl_->stop_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!impl_->stop_event) {
        CloseHandle(impl_->port_handle);
        impl_->port_handle = INVALID_HANDLE_VALUE;
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InternalError, "Failed to create stop event"));
    }

    impl_->write_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!impl_->write_event) {
        CloseHandle(impl_->stop_event);
        impl_->stop_event = nullptr;
        CloseHandle(impl_->port_handle);
        impl_->port_handle = INVALID_HANDLE_VALUE;
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InternalError, "Failed to create write event"));
    }

    impl_->callbacks = std::move(cb);
    impl_->running = true;

    // Obtain PeerId from transceiver
    if (!impl_->peer_id.valid() && impl_->callbacks.on_peer_connected) {
        impl_->peer_id = impl_->callbacks.on_peer_connected(impl_->config.port);
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

    if (impl_->stop_event) {
        SetEvent(impl_->stop_event);
    }

    // Cancel any pending overlapped I/O BEFORE acquiring write_mutex.
    // If send() is blocked in GetOverlappedResult(), CancelIoEx causes
    // it to return with ERROR_OPERATION_ABORTED, releasing write_mutex
    // so we can acquire it below and close the handle cleanly.
    if (impl_->port_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(impl_->port_handle, nullptr);
    }

    if (impl_->read_thread.joinable()) {
        impl_->read_thread.join();
    }

    {
        // Now safe to acquire — any blocking write has been cancelled.
        std::lock_guard lock(impl_->write_mutex);
        if (impl_->port_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(impl_->port_handle);
            impl_->port_handle = INVALID_HANDLE_VALUE;
        }
    }

    if (impl_->stop_event) {
        CloseHandle(impl_->stop_event);
        impl_->stop_event = nullptr;
    }

    if (impl_->write_event) {
        CloseHandle(impl_->write_event);
        impl_->write_event = nullptr;
    }

    if (impl_->callbacks.on_state_changed && impl_->peer_id.valid()) {
        impl_->callbacks.on_state_changed(
            impl_->peer_id, net::ConnectionState::Disconnected);
    }
}

VoidResult SerialTransport::send(PeerId /*peer*/,
                                 std::span<const uint8_t> data) {
    std::lock_guard lock(impl_->write_mutex);

    if (impl_->port_handle == INVALID_HANDLE_VALUE || !impl_->running) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::ConnectionClosed, "Serial port not open"));
    }

    OVERLAPPED ov{};
    ov.hEvent = impl_->write_event;
    if (!ov.hEvent) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InternalError, "Write event not initialized"));
    }

    const uint8_t* ptr = data.data();
    size_t remaining = data.size();

    while (remaining > 0) {
        DWORD written = 0;
        ResetEvent(ov.hEvent);

        BOOL ok = WriteFile(impl_->port_handle, ptr,
                            static_cast<DWORD>(remaining),
                            &written, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(impl_->port_handle, &ov, &written, TRUE)) {
                    DWORD gor_err = GetLastError();
                    return std::unexpected(
                        CONDUIT_ERROR(ErrorCode::SocketError,
                                      std::format("Serial overlapped write failed: {}",
                                                  gor_err)));
                }
            } else {
                return std::unexpected(
                    CONDUIT_ERROR(ErrorCode::SocketError,
                                  std::format("Serial WriteFile failed: {}", err)));
            }
        }

        if (written == 0) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::SocketError,
                              "Serial write returned 0 bytes"));
        }

        ptr += written;
        remaining -= written;
    }

    return {};
}

VoidResult SerialTransport::Impl::configure_port() {
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(port_handle, &dcb)) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "GetCommState failed: " +
                          std::to_string(GetLastError())));
    }

    dcb.BaudRate = config.baud_rate;
    dcb.ByteSize = config.data_bits;

    switch (config.parity) {
        case Parity::None: dcb.Parity = NOPARITY; break;
        case Parity::Odd:  dcb.Parity = ODDPARITY; break;
        case Parity::Even: dcb.Parity = EVENPARITY; break;
    }

    switch (config.stop_bits) {
        case StopBits::One: dcb.StopBits = ONESTOPBIT; break;
        case StopBits::Two: dcb.StopBits = TWOSTOPBITS; break;
    }

    dcb.fBinary = TRUE;
    dcb.fParity = (config.parity != Parity::None) ? TRUE : FALSE;

    // Flow control
    switch (config.flow_control) {
        case FlowControl::None:
            dcb.fOutxCtsFlow = FALSE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            dcb.fOutX = FALSE;
            dcb.fInX = FALSE;
            break;
        case FlowControl::Hardware:
            dcb.fOutxCtsFlow = TRUE;
            dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
            dcb.fOutX = FALSE;
            dcb.fInX = FALSE;
            break;
        case FlowControl::Software:
            dcb.fOutxCtsFlow = FALSE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            dcb.fOutX = TRUE;
            dcb.fInX = TRUE;
            break;
    }

    if (!SetCommState(port_handle, &dcb)) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "SetCommState failed: " +
                          std::to_string(GetLastError())));
    }

    // Set timeouts
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;

    if (!SetCommTimeouts(port_handle, &timeouts)) {
        return std::unexpected(
            CONDUIT_ERROR(ErrorCode::InvalidConfig,
                          "SetCommTimeouts failed: " +
                          std::to_string(GetLastError())));
    }

    return {};
}

void SerialTransport::Impl::read_loop() {
    std::vector<uint8_t> buf(config.recv_buffer_size);

    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        LOG_ERROR("Serial read_loop: CreateEvent failed, read thread exiting");
        running = false;
        return;
    }

    HANDLE events[2] = {ov.hEvent, stop_event};

    while (running) {
        ResetEvent(ov.hEvent);

        DWORD bytes_read = 0;
        BOOL ok = ReadFile(port_handle, buf.data(),
                           static_cast<DWORD>(buf.size()),
                           &bytes_read, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) {
                    // Read completed
                    if (!GetOverlappedResult(port_handle, &ov, &bytes_read, FALSE)) {
                        bytes_read = 0;
                        LOG_WARN(std::format("Serial GetOverlappedResult failed: {}",
                                             GetLastError()));
                    }
                } else {
                    // Stop event signaled
                    CancelIo(port_handle);
                    break;
                }
            } else {
                LOG_WARN(std::format("Serial read error: {}", err));
                running = false;
                break;
            }
        }

        if (bytes_read > 0 && callbacks.on_data_received && peer_id.valid()) {
            callbacks.on_data_received(
                peer_id,
                std::span<const uint8_t>(buf.data(), bytes_read));
        }
    }

    CloseHandle(ov.hEvent);
}

} // namespace conduit::transceiver::transport

#endif // _WIN32
