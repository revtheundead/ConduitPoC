// SPDX-License-Identifier: MIT
// Bgen tests - Field-level scale/offset roundtrip verification

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "field_scale/messages.hpp"

// ============================================================================
// Section: Field-level scale/offset (field_scale fixture)
// ============================================================================

TEST_CASE("field-level scale fl roundtrip", "[roundtrip][field_scale]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(100.25);
    msg.set_temp(0.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = field_scale::ScaleMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // fl has scale=0.25 on 12-bit signed: raw = 100.25 / 0.25 = 401
    // decoded = 401 * 0.25 = 100.25
    CHECK_THAT(decoded->fl(), Catch::Matchers::WithinRel(100.25, 0.001));
}

TEST_CASE("field-level scale+offset temp roundtrip", "[roundtrip][field_scale]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(0.0);
    msg.set_temp(25.0);
    msg.set_plain(42);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = field_scale::ScaleMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // temp has scale=0.01, offset=-40: raw = (25.0 - (-40)) / 0.01 = 6500
    // decoded = 6500 * 0.01 + (-40) = 25.0
    CHECK_THAT(decoded->temp(), Catch::Matchers::WithinRel(25.0, 0.001));
    CHECK(decoded->plain() == 42);
}

TEST_CASE("field-level scale fl negative value", "[roundtrip][field_scale]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(-50.0);
    msg.set_temp(0.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = field_scale::ScaleMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // fl: raw = -50.0 / 0.25 = -200 (fits in 12-bit signed: -2048..2047)
    // decoded = -200 * 0.25 = -50.0
    CHECK_THAT(decoded->fl(), Catch::Matchers::WithinRel(-50.0, 0.001));
}

TEST_CASE("field-level scale wire bytes for fl", "[roundtrip][field_scale]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(1.0); // raw = 1.0 / 0.25 = 4
    msg.set_temp(0.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // fl is 12-bit signed big-endian, raw=4 => 0x004
    // First 12 bits: 0000 0000 0100 => bytes[0]=0x00, bytes[1] top 4 bits = 0x4?
    // Actually: 12 bits = 0x004 = 0000 0000 0100
    // byte[0] = 0000 0000 = 0x00, byte[1] top nibble = 0100 = 0x4
    // temp is uint16 big-endian, raw = (0 - (-40)) / 0.01 = 4000 = 0x0FA0
    // Then plain = 0x00
    REQUIRE(bytes.size() >= 1);
    // Verify fl raw=4 in first 12 bits (big-endian)
    uint16_t first_two = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
    int32_t raw_fl = static_cast<int32_t>(first_two >> 4);
    // Sign-extend 12-bit value
    if (raw_fl & 0x800) raw_fl |= static_cast<int32_t>(0xFFFFF000);
    CHECK(raw_fl == 4);
}

// ============================================================================
// Section: Wire-byte verification
// ============================================================================

TEST_CASE("field-level scale temp wire bytes", "[wire][field_scale]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(0.0);
    msg.set_temp(25.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // Layout: fl(12 bits) + temp(16 bits) + plain(8 bits) = 36 bits = 5 bytes
    // Fields are tightly packed (no implicit byte alignment between fields).
    REQUIRE(bytes.size() == 5);
    // fl raw=0 → 12 zero bits
    // temp raw = (25.0 - (-40)) / 0.01 = 6500 = 0x1964
    // Tight packing: fl[11:4] | fl[3:0]+temp[15:12] | temp[11:4] | temp[3:0]+plain[7:4] | plain[3:0]+pad
    CHECK(bytes[0] == 0x00); // fl high 8 bits
    CHECK(bytes[1] == 0x01); // fl low 4 bits (0) + temp high 4 bits (0x1)
    CHECK(bytes[2] == 0x96); // temp bits 11:4
    CHECK(bytes[3] == 0x40); // temp low 4 bits (0x4) + plain high 4 bits (0)
    CHECK(bytes[4] == 0x00); // plain low 4 bits + 4 pad bits
}

// ============================================================================
// Section: Error-path tests
// ============================================================================

TEST_CASE("ScaleMsg truncated fails decode", "[error][field_scale]") {
    // ScaleMsg needs 5 bytes (12+16+8=36 bits); provide only 2
    std::vector<uint8_t> data = {0x00, 0x00};
    auto decoded = field_scale::ScaleMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("ScaleMsg empty fails decode", "[error][field_scale]") {
    std::vector<uint8_t> data;
    auto decoded = field_scale::ScaleMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

// ============================================================================
// Section: Edge-case tests
// ============================================================================

TEST_CASE("field-level scale fl max positive", "[edge][field_scale]") {
    field_scale::ScaleMsg msg;
    // fl is 12-bit signed with scale=0.25. Max raw=2047 → 2047*0.25=511.75
    msg.set_fl(511.75);
    msg.set_temp(0.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = field_scale::ScaleMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->fl(), Catch::Matchers::WithinRel(511.75, 0.001));
}

TEST_CASE("field-level scale fl min negative", "[edge][field_scale]") {
    field_scale::ScaleMsg msg;
    // fl is 12-bit signed with scale=0.25. Min raw=-2048 → -2048*0.25=-512.0
    msg.set_fl(-512.0);
    msg.set_temp(0.0);
    msg.set_plain(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = field_scale::ScaleMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->fl(), Catch::Matchers::WithinRel(-512.0, 0.001));
}
