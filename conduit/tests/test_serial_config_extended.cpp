// SPDX-License-Identifier: MIT
// Conduit - Serial Config Extended Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/serial.hpp>

using namespace conduit;
using namespace conduit::transceiver::transport;

TEST_CASE("Serial config: all Parity values accepted", "[serial][config]") {
    for (auto parity : {Parity::None, Parity::Odd, Parity::Even}) {
        SerialConfig cfg;
        cfg.port = "COM1";
        cfg.parity = parity;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("Serial config: all StopBits values accepted", "[serial][config]") {
    for (auto sb : {StopBits::One, StopBits::Two}) {
        SerialConfig cfg;
        cfg.port = "COM1";
        cfg.stop_bits = sb;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("Serial config: high baud rates accepted", "[serial][config]") {
    for (uint32_t baud : {115200u, 921600u, 1000000u}) {
        SerialConfig cfg;
        cfg.port = "COM1";
        cfg.baud_rate = baud;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("Serial config: Linux port names valid", "[serial][config]") {
    for (auto port : {"/dev/ttyUSB0", "/dev/ttyS0"}) {
        SerialConfig cfg;
        cfg.port = port;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("Serial config: Windows port names valid", "[serial][config]") {
    for (auto port : {"COM1", "COM99", "\\\\.\\COM10"}) {
        SerialConfig cfg;
        cfg.port = port;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("SerialTransport: is_stream_oriented and is_multi_peer", "[serial][config]") {
    SerialConfig cfg;
    cfg.port = "NOEXIST";
    SerialTransport transport(cfg);

    CHECK(transport.is_stream_oriented() == true);
    CHECK(transport.is_multi_peer() == false);
}
