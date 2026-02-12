// SPDX-License-Identifier: MIT
// Conduit - Serial Config Validation Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/serial.hpp>

using namespace conduit;
using namespace conduit::transceiver::transport;

TEST_CASE("Serial config: valid defaults", "[serial]") {
    SerialConfig cfg;
    cfg.port = "COM1";

    auto result = validate_serial_config(cfg);
    CHECK(result.has_value());
}

TEST_CASE("Serial config: empty port name is invalid", "[serial]") {
    SerialConfig cfg;
    cfg.port = "";

    auto result = validate_serial_config(cfg);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidConfig);
}

TEST_CASE("Serial config: zero baud rate is invalid", "[serial]") {
    SerialConfig cfg;
    cfg.port = "COM1";
    cfg.baud_rate = 0;

    auto result = validate_serial_config(cfg);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidConfig);
}

TEST_CASE("Serial config: data bits out of range", "[serial]") {
    SerialConfig cfg;
    cfg.port = "COM1";

    SECTION("data_bits = 4 is invalid") {
        cfg.data_bits = 4;
        auto result = validate_serial_config(cfg);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("data_bits = 9 is invalid") {
        cfg.data_bits = 9;
        auto result = validate_serial_config(cfg);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("data_bits = 5 is valid") {
        cfg.data_bits = 5;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }

    SECTION("data_bits = 8 is valid") {
        cfg.data_bits = 8;
        auto result = validate_serial_config(cfg);
        CHECK(result.has_value());
    }
}

TEST_CASE("Serial config: zero recv buffer is invalid", "[serial]") {
    SerialConfig cfg;
    cfg.port = "COM1";
    cfg.recv_buffer_size = 0;

    auto result = validate_serial_config(cfg);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidConfig);
}

TEST_CASE("Serial config: default values are sensible", "[serial]") {
    SerialConfig cfg;
    cfg.port = "/dev/ttyUSB0";

    CHECK(cfg.baud_rate == 9600);
    CHECK(cfg.data_bits == 8);
    CHECK(cfg.parity == Parity::None);
    CHECK(cfg.stop_bits == StopBits::One);
    CHECK(cfg.recv_buffer_size == 4096);
}
