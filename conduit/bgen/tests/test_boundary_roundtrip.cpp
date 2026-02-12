// SPDX-License-Identifier: MIT
// Bgen tests - Boundary types roundtrip verification

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "boundary_types/messages.hpp"

// ============================================================================
// Section: Boundary types (boundary_types fixture)
// ============================================================================

TEST_CASE("single-bit field roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_flag(1);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flag() == 1);
}

TEST_CASE("3-bit field roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_small(7); // max value for 3-bit unsigned
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->small() == 7);
}

TEST_CASE("7-bit field roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_medium(127); // max value for 7-bit unsigned
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->medium() == 127);
}

TEST_CASE("uint64 max roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_qword(UINT64_MAX);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->qword() == UINT64_MAX);
}

TEST_CASE("int64 extremes roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_signed_qword(INT64_MIN);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->signed_qword() == INT64_MIN);
}

TEST_CASE("int64 max roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_signed_qword(INT64_MAX);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->signed_qword() == INT64_MAX);
}

TEST_CASE("int8 negative roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_signed_byte(static_cast<int8_t>(-128));
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->signed_byte() == static_cast<int8_t>(-128));
}

TEST_CASE("scaled type roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    // Set raw value for 25.0 degC: (25.0 + 40) / 0.1 = 650
    msg.mutable_temp().set_value(25.0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->temp().value(), Catch::Matchers::WithinAbs(25.0, 0.1));
}

TEST_CASE("4-bit enum roundtrip", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_level(boundary_types::nybble_enum::high);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->level() == boundary_types::nybble_enum::high);
}

// ============================================================================
// Section: Odd sub-byte widths (12-bit, 20-bit)
// ============================================================================

TEST_CASE("12-bit unsigned max roundtrip", "[roundtrip][boundary][oddwidth]") {
    boundary_types::OddWidthMsg msg;
    msg.set_u12(4095); // max value for 12-bit unsigned
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::OddWidthMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u12() == 4095);
}

TEST_CASE("12-bit unsigned mid roundtrip", "[roundtrip][boundary][oddwidth]") {
    boundary_types::OddWidthMsg msg;
    msg.set_u12(2048);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::OddWidthMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u12() == 2048);
}

TEST_CASE("20-bit unsigned max roundtrip", "[roundtrip][boundary][oddwidth]") {
    boundary_types::OddWidthMsg msg;
    msg.set_u20(1048575); // max value for 20-bit unsigned
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = boundary_types::OddWidthMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u20() == 1048575);
}

TEST_CASE("12-bit signed extremes roundtrip", "[roundtrip][boundary][oddwidth]") {
    // Min value: -2048
    {
        boundary_types::OddWidthMsg msg;
        msg.set_s12(-2048);
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = boundary_types::OddWidthMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->s12() == -2048);
    }
    // Max value: +2047
    {
        boundary_types::OddWidthMsg msg;
        msg.set_s12(2047);
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = boundary_types::OddWidthMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->s12() == 2047);
    }
}

TEST_CASE("12-bit wire byte boundary check", "[roundtrip][boundary][oddwidth][wire]") {
    boundary_types::OddWidthMsg msg;
    msg.set_u12(0xABC); // 12-bit value
    msg.set_u20(0);
    msg.set_s12(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // u12 is first 12 bits: 1010 1011 1100 => byte[0]=0xAB, byte[1] top nibble=0xC
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0xAB);
    CHECK((bytes[1] >> 4) == 0x0C);
}

TEST_CASE("sub-byte bit packing (1+3+7=11 bits -> 2 bytes)", "[roundtrip][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.set_flag(1);
    msg.set_small(5);
    msg.set_medium(99);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // First 11 bits: 1 (1 bit) + 101 (3 bits) + 1100011 (7 bits) = 11011000 11...
    // Verify we can round-trip correctly
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flag() == 1);
    CHECK(decoded->small() == 5);
    CHECK(decoded->medium() == 99);
}

// ============================================================================
// Section: Error-path tests
// ============================================================================

TEST_CASE("OddWidthMsg truncated fails decode", "[error][boundary]") {
    // OddWidthMsg has uint12+uint20+int12 = 44 bits → 6 bytes. Provide 3 bytes.
    std::vector<uint8_t> data = {0x00, 0x00, 0x00};
    auto decoded = boundary_types::OddWidthMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("BoundaryMsg empty fails decode", "[error][boundary]") {
    std::vector<uint8_t> data;
    auto decoded = boundary_types::BoundaryMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}
