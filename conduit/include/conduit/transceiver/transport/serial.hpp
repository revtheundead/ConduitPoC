// SPDX-License-Identifier: MIT
// Conduit - Serial Transport

#pragma once

#include <conduit/transceiver/transport/itransport.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace conduit::transceiver::transport {

// ============================================================================
// Serial configuration enums
// ============================================================================

enum class Parity { None, Odd, Even };
enum class StopBits { One, Two };
enum class FlowControl { None, Hardware, Software };

[[nodiscard]] constexpr std::string_view to_string(Parity p) noexcept {
    switch (p) {
        case Parity::None: return "None";
        case Parity::Odd:  return "Odd";
        case Parity::Even: return "Even";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(StopBits s) noexcept {
    switch (s) {
        case StopBits::One: return "1";
        case StopBits::Two: return "2";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(FlowControl f) noexcept {
    switch (f) {
        case FlowControl::None:     return "None";
        case FlowControl::Hardware: return "Hardware";
        case FlowControl::Software: return "Software";
    }
    return "Unknown";
}

// ============================================================================
// SerialConfig
// ============================================================================

struct SerialConfig {
    std::string port;                // "COM3" or "/dev/ttyUSB0"
    uint32_t baud_rate = 9600;
    uint8_t data_bits = 8;
    Parity parity = Parity::None;
    StopBits stop_bits = StopBits::One;
    FlowControl flow_control = FlowControl::None;
    size_t recv_buffer_size = 4096;
};

// ============================================================================
// SerialTransport
//
// Single-peer, stream-oriented serial transport.
// Platform-specific: Win32 COM port or POSIX termios.
// ============================================================================

class SerialTransport : public ITransport {
public:
    explicit SerialTransport(SerialConfig config);
    ~SerialTransport() override;

    SerialTransport(const SerialTransport&) = delete;
    SerialTransport& operator=(const SerialTransport&) = delete;

    [[nodiscard]] VoidResult start(TransportCallbacks cb) override;
    void stop() override;
    [[nodiscard]] VoidResult send(PeerId peer, std::span<const uint8_t> data) override;
    [[nodiscard]] bool is_stream_oriented() const noexcept override { return true; }
    [[nodiscard]] bool is_multi_peer() const noexcept override { return false; }
    [[nodiscard]] std::string_view transport_type() const noexcept override { return "serial"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Shared validation (implemented in serial_common.cpp)
// ============================================================================

VoidResult validate_serial_config(const SerialConfig& config);

} // namespace conduit::transceiver::transport
