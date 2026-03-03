// SPDX-License-Identifier: MIT
// Bgen tests - bytes_attr numeric promotion, raw accessors, large bytes pass-through

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "bytes_numeric/messages.hpp"
#include "field_scale/messages.hpp"

// ============================================================================
// Issue 1: bytes_attr <= 8 resolves to native integer types
// ============================================================================

TEST_CASE("bytes=2 resolves to uint16_t", "[bytes_numeric][types]") {
    bytes_numeric::SmallBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.val16()), const uint16_t&>,
                  "bytes=2 should resolve to uint16_t");
}

TEST_CASE("bytes=3 resolves to uint32_t", "[bytes_numeric][types]") {
    bytes_numeric::SmallBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.val24()), const uint32_t&>,
                  "bytes=3 should resolve to uint32_t");
}

TEST_CASE("bytes=4 resolves to uint32_t", "[bytes_numeric][types]") {
    bytes_numeric::SmallBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.val32()), const uint32_t&>,
                  "bytes=4 should resolve to uint32_t");
}

TEST_CASE("bytes=7 resolves to uint64_t", "[bytes_numeric][types]") {
    bytes_numeric::SmallBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.val56()), const uint64_t&>,
                  "bytes=7 should resolve to uint64_t");
}

TEST_CASE("bytes=8 resolves to uint64_t", "[bytes_numeric][types]") {
    bytes_numeric::SmallBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.val64()), const uint64_t&>,
                  "bytes=8 should resolve to uint64_t");
}

TEST_CASE("bytes=3 signed resolves to int32_t", "[bytes_numeric][types]") {
    bytes_numeric::SignedBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.signed_val()), const int32_t&>,
                  "bytes=3 signed should resolve to int32_t");
}

// ============================================================================
// Issue 1: bytes_attr <= 8 roundtrip
// ============================================================================

TEST_CASE("bytes=2 numeric roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0xABCD);
    msg.set_val24(0);
    msg.set_val32(0);
    msg.set_val56(0);
    msg.set_val64(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->val16() == 0xABCD);
}

TEST_CASE("bytes=3 numeric roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0);
    msg.set_val24(0x123456);
    msg.set_val32(0);
    msg.set_val56(0);
    msg.set_val64(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->val24() == 0x123456);
}

TEST_CASE("bytes=7 numeric roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0);
    msg.set_val24(0);
    msg.set_val32(0);
    msg.set_val56(0xAABBCCDDEEFF00ULL);
    msg.set_val64(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->val56() == 0xAABBCCDDEEFF00ULL);
}

TEST_CASE("bytes=8 numeric roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0);
    msg.set_val24(0);
    msg.set_val32(0);
    msg.set_val56(0);
    msg.set_val64(0xFEDCBA9876543210ULL);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->val64() == 0xFEDCBA9876543210ULL);
}

TEST_CASE("SmallBytesMsg all fields roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0x1234);
    msg.set_val24(0xABCDEF);
    msg.set_val32(0xDEADBEEF);
    msg.set_val56(0x01020304050607ULL);
    msg.set_val64(0x0807060504030201ULL);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->val16() == 0x1234);
    CHECK(dec->val24() == 0xABCDEF);
    CHECK(dec->val32() == 0xDEADBEEF);
    CHECK(dec->val56() == 0x01020304050607ULL);
    CHECK(dec->val64() == 0x0807060504030201ULL);
}

TEST_CASE("bytes=3 wire format is big-endian", "[bytes_numeric][wire]") {
    bytes_numeric::SmallBytesMsg msg;
    msg.set_val16(0);
    msg.set_val24(0x010203);
    msg.set_val32(0);
    msg.set_val56(0);
    msg.set_val64(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // val16 occupies bytes 0-1, val24 occupies bytes 2-4
    CHECK(bytes[2] == 0x01);
    CHECK(bytes[3] == 0x02);
    CHECK(bytes[4] == 0x03);
}

TEST_CASE("bytes=3 signed negative roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::SignedBytesMsg msg;
    // 24-bit signed: range is -8388608 to 8388607
    msg.set_signed_val(-1000);
    msg.set_pad(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::SignedBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->signed_val() == -1000);
}

TEST_CASE("SmallBytesMsg truncated buffer fails decode", "[bytes_numeric][error]") {
    // SmallBytesMsg needs 2+3+4+7+8 = 24 bytes; provide only 3
    std::vector<uint8_t> data = {0x00, 0x01, 0x02};
    auto dec = bytes_numeric::SmallBytesMsg::decode_bytes(data);
    CHECK_FALSE(dec.has_value());
}

// ============================================================================
// Issue 1: bytes_attr with scale (now supported for bytes <= 8)
// ============================================================================

TEST_CASE("bytes=3 with scale roundtrip", "[bytes_numeric][scale]") {
    bytes_numeric::ScaledBytesMsg msg;
    // sensor: bytes=3, scale=0.01 → raw = 10000 / 0.01 = 1000000 (fits in 24 bits: max 16777215)
    // Actually: raw = value / scale, decoded = raw * scale
    msg.set_sensor(100.0); // raw = 100.0 / 0.01 = 10000
    msg.set_temperature(0.0);
    msg.set_tag(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ScaledBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK_THAT(dec->sensor(), Catch::Matchers::WithinRel(100.0, 0.001));
}

TEST_CASE("bytes=2 with scale+offset roundtrip", "[bytes_numeric][scale]") {
    bytes_numeric::ScaledBytesMsg msg;
    msg.set_sensor(0.0);
    // temperature: bytes=2, scale=0.1, offset=-50
    // raw = (25.0 - (-50)) / 0.1 = 750
    msg.set_temperature(25.0);
    msg.set_tag(42);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ScaledBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK_THAT(dec->temperature(), Catch::Matchers::WithinRel(25.0, 0.01));
    CHECK(dec->tag() == 42);
}

TEST_CASE("scaled bytes field type is double", "[bytes_numeric][types]") {
    bytes_numeric::ScaledBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.sensor()), const double&>,
                  "bytes=3 with scale should resolve to double");
    static_assert(std::is_same_v<decltype(msg.temperature()), const double&>,
                  "bytes=2 with scale+offset should resolve to double");
}

// ============================================================================
// Issue 1: bytes_attr with constraints (now supported for bytes <= 8)
// ============================================================================

TEST_CASE("bytes=3 range constraint valid roundtrip", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    REQUIRE(msg.set_range_val(1000).has_value());  // within [256, 65535]
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    msg.set_payload(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ConstrainedBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->range_val() == 1000);
    CHECK(dec->magic() == 0xBEEF);
}

TEST_CASE("bytes=3 range constraint min boundary", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    REQUIRE(msg.set_range_val(256).has_value());  // exactly at min
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    msg.set_payload(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ConstrainedBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->range_val() == 256);
}

TEST_CASE("bytes=3 range constraint max boundary", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    REQUIRE(msg.set_range_val(65535).has_value());  // exactly at max
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    msg.set_payload(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
}

TEST_CASE("bytes=3 range constraint below min rejected", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    auto result = msg.set_range_val(100);  // below min=256
    CHECK_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("bytes=3 range constraint above max rejected", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    auto result = msg.set_range_val(70000);  // above max=65535
    CHECK_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("bytes=2 equals constraint valid", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    REQUIRE(msg.set_range_val(256).has_value());
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    msg.set_payload(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ConstrainedBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->magic() == 0xBEEF);
}

TEST_CASE("bytes=2 equals constraint wrong value rejected", "[bytes_numeric][constraints]") {
    bytes_numeric::ConstrainedBytesMsg msg;
    auto result = msg.set_magic(0xDEAD);  // not 0xBEEF
    CHECK_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("bytes=2 equals constraint decode wrong magic fails", "[bytes_numeric][constraints]") {
    conduit::io::BitWriter w;
    // range_val: 3 bytes = 0x000100 (256 - valid)
    w.write_bits(256, 24);
    // magic: 2 bytes = 0xDEAD (wrong - should be 0xBEEF)
    w.write_u16(0xDEAD, conduit::io::Endian::Big);
    // payload: 2 bytes
    w.write_u16(0, conduit::io::Endian::Big);
    auto finish = w.finish();
    REQUIRE(finish.has_value());
    auto dec = bytes_numeric::ConstrainedBytesMsg::decode_bytes(*finish);
    CHECK_FALSE(dec.has_value());
}

// ============================================================================
// Issue 2: bytes > 8 stays as byte array, pass-through
// ============================================================================

TEST_CASE("bytes=10 resolves to std::array", "[bytes_numeric][types]") {
    bytes_numeric::LargeBytesMsg msg;
    static_assert(std::is_same_v<decltype(msg.blob()), const std::array<uint8_t, 10>&>,
                  "bytes=10 should stay as std::array<uint8_t, 10>");
}

TEST_CASE("bytes=10 pass-through roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::LargeBytesMsg msg;
    msg.set_header(0x1234);
    std::array<uint8_t, 10> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    msg.set_blob(data);
    msg.set_footer(0xFF);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::LargeBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->header() == 0x1234);
    CHECK(dec->blob() == data);
    CHECK(dec->footer() == 0xFF);
}

TEST_CASE("bytes=10 zero data roundtrip", "[bytes_numeric][roundtrip]") {
    bytes_numeric::LargeBytesMsg msg;
    msg.set_header(0);
    std::array<uint8_t, 10> data = {};
    msg.set_blob(data);
    msg.set_footer(0);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::LargeBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->blob() == data);
}

TEST_CASE("LargeBytesMsg truncated buffer fails", "[bytes_numeric][error]") {
    // LargeBytesMsg needs 2+10+1 = 13 bytes; provide only 5
    std::vector<uint8_t> data = {0x12, 0x34, 0x01, 0x02, 0x03};
    auto dec = bytes_numeric::LargeBytesMsg::decode_bytes(data);
    CHECK_FALSE(dec.has_value());
}

// ============================================================================
// Issue 3: Raw accessors for scaled fields
// ============================================================================

TEST_CASE("raw accessor type for bits-based scale", "[raw_accessor][types]") {
    field_scale::ScaleMsg msg;
    // fl: 12-bit signed → raw type is int16_t
    static_assert(std::is_same_v<decltype(msg.fl_raw()), int16_t>,
                  "fl_raw() should return int16_t (12-bit signed raw)");
    // temp: uint16 with scale → raw type is uint16_t
    static_assert(std::is_same_v<decltype(msg.temp_raw()), uint16_t>,
                  "temp_raw() should return uint16_t");
}

TEST_CASE("raw accessor type for bytes-based scale", "[raw_accessor][types]") {
    bytes_numeric::ScaledBytesMsg msg;
    // sensor: bytes=3 with scale → raw type is uint32_t (24-bit)
    static_assert(std::is_same_v<decltype(msg.sensor_raw()), uint32_t>,
                  "sensor_raw() should return uint32_t (24-bit raw)");
    // temperature: bytes=2 with scale+offset → raw type is uint16_t
    static_assert(std::is_same_v<decltype(msg.temperature_raw()), uint16_t>,
                  "temperature_raw() should return uint16_t");
}

TEST_CASE("raw getter returns correct integer for scale-only", "[raw_accessor][roundtrip]") {
    field_scale::ScaleMsg msg;
    // fl has scale=0.25: set double value 100.0, raw should be 100.0/0.25 = 400
    msg.set_fl(100.0);
    msg.set_temp(0.0);
    msg.set_plain(0);
    CHECK(msg.fl_raw() == 400);
}

TEST_CASE("raw getter returns correct integer for scale+offset", "[raw_accessor][roundtrip]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(0.0);
    // temp has scale=0.01, offset=-40: raw = (25.0 - (-40)) / 0.01 = 6500
    msg.set_temp(25.0);
    msg.set_plain(0);
    CHECK(msg.temp_raw() == 6500);
}

TEST_CASE("raw setter sets correct double value", "[raw_accessor][roundtrip]") {
    field_scale::ScaleMsg msg;
    // fl has scale=0.25: raw=400 → double = 400 * 0.25 = 100.0
    msg.set_fl_raw(400);
    msg.set_temp(0.0);
    msg.set_plain(0);
    CHECK_THAT(msg.fl(), Catch::Matchers::WithinRel(100.0, 0.001));
}

TEST_CASE("raw setter with scale+offset sets correct double", "[raw_accessor][roundtrip]") {
    field_scale::ScaleMsg msg;
    msg.set_fl(0.0);
    // temp has scale=0.01, offset=-40: raw=6500 → double = 6500 * 0.01 + (-40) = 25.0
    msg.set_temp_raw(6500);
    msg.set_plain(0);
    CHECK_THAT(msg.temp(), Catch::Matchers::WithinRel(25.0, 0.001));
}

TEST_CASE("raw accessor roundtrip for bytes-based scale", "[raw_accessor][roundtrip]") {
    bytes_numeric::ScaledBytesMsg msg;
    // sensor: bytes=3, scale=0.01: set raw=5000 → double = 50.0
    msg.set_sensor_raw(5000);
    msg.set_temperature(0.0);
    msg.set_tag(0);
    CHECK_THAT(msg.sensor(), Catch::Matchers::WithinRel(50.0, 0.001));
    CHECK(msg.sensor_raw() == 5000);
}

TEST_CASE("raw accessor roundtrip for bytes-based scale+offset", "[raw_accessor][roundtrip]") {
    bytes_numeric::ScaledBytesMsg msg;
    msg.set_sensor(0.0);
    // temperature: bytes=2, scale=0.1, offset=-50: raw=750 → double = 750*0.1 + (-50) = 25.0
    msg.set_temperature_raw(750);
    msg.set_tag(0);
    CHECK_THAT(msg.temperature(), Catch::Matchers::WithinRel(25.0, 0.01));
    CHECK(msg.temperature_raw() == 750);
}

TEST_CASE("raw accessor encode-decode preserves raw value", "[raw_accessor][roundtrip]") {
    field_scale::ScaleMsg msg;
    msg.set_fl_raw(401);
    msg.set_temp_raw(6500);
    msg.set_plain(99);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = field_scale::ScaleMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    // The raw value should survive encode-decode
    CHECK(dec->fl_raw() == 401);
    CHECK(dec->temp_raw() == 6500);
    CHECK(dec->plain() == 99);
}

TEST_CASE("raw accessor zero value", "[raw_accessor][edge]") {
    field_scale::ScaleMsg msg;
    msg.set_fl_raw(0);
    msg.set_temp_raw(0);
    msg.set_plain(0);
    CHECK_THAT(msg.fl(), Catch::Matchers::WithinAbs(0.0, 1e-10));
    // temp offset=-40: raw=0 → 0 * 0.01 + (-40) = -40
    CHECK_THAT(msg.temp(), Catch::Matchers::WithinRel(-40.0, 0.001));
    CHECK(msg.fl_raw() == 0);
    CHECK(msg.temp_raw() == 0);
}

TEST_CASE("raw accessor negative signed value", "[raw_accessor][edge]") {
    field_scale::ScaleMsg msg;
    // fl: 12-bit signed, scale=0.25: set raw=-200 → double = -200 * 0.25 = -50
    msg.set_fl_raw(-200);
    msg.set_temp(0.0);
    msg.set_plain(0);
    CHECK_THAT(msg.fl(), Catch::Matchers::WithinRel(-50.0, 0.001));
    CHECK(msg.fl_raw() == -200);
}

TEST_CASE("scaled bytes raw accessor encode-decode preserves raw", "[raw_accessor][roundtrip]") {
    bytes_numeric::ScaledBytesMsg msg;
    msg.set_sensor_raw(12345);
    msg.set_temperature_raw(600);
    msg.set_tag(7);
    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = bytes_numeric::ScaledBytesMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->sensor_raw() == 12345);
    CHECK(dec->temperature_raw() == 600);
    CHECK(dec->tag() == 7);
}
