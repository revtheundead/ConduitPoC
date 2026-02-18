// SPDX-License-Identifier: MIT
// Bgen tests - Generated code roundtrip verification
//
// These tests compile against bgen-generated headers and verify
// encode/decode roundtrips on real wire formats.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "all_types/messages.hpp"
#include "struct_features/messages.hpp"
#include "bitmap_fx/messages.hpp"
#include "arrays_choices/messages.hpp"
#include "session_protocol/messages.hpp"
#include "inline_struct/messages.hpp"
#include "inline_struct/constants.hpp"
#include "choice_protocol/messages.hpp"
#include "choice_protocol/constants.hpp"

// ============================================================================
// Section: Primitive Types (all_types fixture)
// ============================================================================

TEST_CASE("uint8 roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_u8(0x42);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u8() == 0x42);
}

TEST_CASE("uint16 big-endian roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_u16(0x1234);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // Verify wire bytes for u16 field (offset depends on u8 field = 1 byte)
    REQUIRE(bytes.size() > 2);
    CHECK(bytes[1] == 0x12);
    CHECK(bytes[2] == 0x34);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u16() == 0x1234);
}

TEST_CASE("uint32 roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_u32(0xDEADBEEF);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes[3] == 0xDE); CHECK(bytes[4] == 0xAD);
    CHECK(bytes[5] == 0xBE); CHECK(bytes[6] == 0xEF);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u32() == 0xDEADBEEF);
}

TEST_CASE("uint64 roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_u64(0x0102030405060708ULL);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes[7]==0x01); CHECK(bytes[8]==0x02); CHECK(bytes[9]==0x03); CHECK(bytes[10]==0x04);
    CHECK(bytes[11]==0x05); CHECK(bytes[12]==0x06); CHECK(bytes[13]==0x07); CHECK(bytes[14]==0x08);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u64() == 0x0102030405060708ULL);
}

TEST_CASE("int8 signed roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_i8(static_cast<int8_t>(-42));
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes[15] == 0xD6);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->i8() == static_cast<int8_t>(-42));
}

TEST_CASE("int16 signed roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_i16(static_cast<int16_t>(-1000));
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes[16]==0xFC); CHECK(bytes[17]==0x18);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->i16() == static_cast<int16_t>(-1000));
}

TEST_CASE("int32 signed roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_i32(INT32_MIN);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes[18]==0x80); CHECK(bytes[19]==0x00); CHECK(bytes[20]==0x00); CHECK(bytes[21]==0x00);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->i32() == INT32_MIN);
}

TEST_CASE("float32 roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_f32(3.14f);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->f32(), Catch::Matchers::WithinRel(3.14f, 0.001f));
}

TEST_CASE("float64 roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;
    msg.set_f64(2.718281828459045);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->f64(), Catch::Matchers::WithinRel(2.718281828459045, 1e-12));
}

// ============================================================================
// Section: Float Special Values (T3 test gap)
// ============================================================================

TEST_CASE("float32 NaN roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f32(std::numeric_limits<float>::quiet_NaN());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // NaN != NaN, so use std::isnan
    CHECK(std::isnan(decoded->f32()));
}

TEST_CASE("float32 +Infinity roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f32(std::numeric_limits<float>::infinity());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f32() == std::numeric_limits<float>::infinity());
    CHECK(std::isinf(decoded->f32()));
    CHECK(decoded->f32() > 0.0f);
}

TEST_CASE("float32 -Infinity roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f32(-std::numeric_limits<float>::infinity());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f32() == -std::numeric_limits<float>::infinity());
    CHECK(std::isinf(decoded->f32()));
    CHECK(decoded->f32() < 0.0f);
}

TEST_CASE("float32 negative zero roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f32(-0.0f);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f32() == 0.0f);
    // Verify sign bit is preserved: -0.0f has sign bit set
    CHECK(std::signbit(decoded->f32()));
}

TEST_CASE("float64 NaN roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f64(std::numeric_limits<double>::quiet_NaN());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(std::isnan(decoded->f64()));
}

TEST_CASE("float64 +Infinity roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f64(std::numeric_limits<double>::infinity());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f64() == std::numeric_limits<double>::infinity());
    CHECK(std::isinf(decoded->f64()));
    CHECK(decoded->f64() > 0.0);
}

TEST_CASE("float64 -Infinity roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f64(-std::numeric_limits<double>::infinity());
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f64() == -std::numeric_limits<double>::infinity());
    CHECK(std::isinf(decoded->f64()));
    CHECK(decoded->f64() < 0.0);
}

TEST_CASE("float64 negative zero roundtrip", "[roundtrip][primitives][float_special]") {
    all_types::AllTypesMessage msg;
    msg.set_f64(-0.0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->f64() == 0.0);
    CHECK(std::signbit(decoded->f64()));
}

TEST_CASE("bool true/false roundtrip", "[roundtrip][primitives]") {
    all_types::AllTypesMessage msg;

    // true
    msg.set_flag(1);
    {
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->flag() != 0);
    }

    // false
    msg.set_flag(0);
    {
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->flag() == 0);
    }
}

// ============================================================================
// Section: String Types (all_types fixture)
// ============================================================================

TEST_CASE("ascii string roundtrip", "[roundtrip][strings]") {
    all_types::AllTypesMessage msg;
    all_types::ascii_str s;
    s.set_value("Hello");
    msg.set_ascii(s);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // After decode and trim, should have "Hello" (null-padded then trimmed)
    CHECK(decoded->ascii().value() == "Hello");
}

TEST_CASE("utf8 string space padding roundtrip", "[roundtrip][strings]") {
    all_types::AllTypesMessage msg;
    all_types::utf8_str s;
    s.set_value("Test");
    msg.set_utf8(s);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->utf8().value() == "Test");
}

TEST_CASE("bytes field roundtrip", "[roundtrip][strings]") {
    all_types::AllTypesMessage msg;
    std::array<uint8_t, 8> raw_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    msg.set_raw(raw_data);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->raw() == raw_data);
}

// ============================================================================
// Section: Little-Endian (all_types fixture)
// ============================================================================

TEST_CASE("uint16 little-endian wire format", "[roundtrip][endian]") {
    all_types::AllTypesMessage msg;
    msg.set_le16(0x1234);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->le16() == 0x1234);

    // Verify actual wire byte order at le16 offset (69):
    // u8(1)+u16(2)+u32(4)+u64(8)+i8(1)+i16(2)+i32(4)+f32(4)+f64(8)+flag(1)+ascii(10)+utf8(16)+raw(8) = 69
    REQUIRE(bytes.size() > 70);
    CHECK(bytes[69] == 0x34); // low byte first (little-endian)
    CHECK(bytes[70] == 0x12);
}

TEST_CASE("uint32 little-endian wire format", "[roundtrip][endian]") {
    all_types::AllTypesMessage msg;
    msg.set_le32(0xAABBCCDD);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->le32() == 0xAABBCCDD);

    // Verify actual wire byte order at le32 offset (71):
    // le16 is at 69 (2 bytes), so le32 starts at 71
    REQUIRE(bytes.size() > 74);
    CHECK(bytes[71] == 0xDD); // low byte first (little-endian)
    CHECK(bytes[72] == 0xCC);
    CHECK(bytes[73] == 0xBB);
    CHECK(bytes[74] == 0xAA);
}

// ============================================================================
// Section: Enum Types (all_types fixture)
// ============================================================================

TEST_CASE("enum roundtrip", "[roundtrip][enum]") {
    all_types::AllTypesMessage msg;
    msg.set_color(all_types::color_enum::green);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->color() == all_types::color_enum::green);
}

TEST_CASE("flags roundtrip", "[roundtrip][flags]") {
    all_types::AllTypesMessage msg;
    all_types::status_flags flags;
    flags.set_active(true);
    flags.set_ready(true);
    flags.set_error(false);
    msg.set_status(flags);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->status().active());
    CHECK(decoded->status().ready());
    CHECK_FALSE(decoded->status().error());
}

// ============================================================================
// Section: Scaled Type (all_types fixture)
// ============================================================================

TEST_CASE("scaled-temp roundtrip", "[roundtrip][scaled]") {
    all_types::AllTypesMessage msg;
    all_types::scaled_temp temp;
    temp.set_value(25.0); // 25 degC -> raw = (25 - (-40)) / 0.01 = 6500
    msg.set_temp(temp);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_THAT(decoded->temp().value(), Catch::Matchers::WithinAbs(25.0, 0.02));
}

// ============================================================================
// Section: Full Message Roundtrip (all_types fixture)
// ============================================================================

TEST_CASE("AllTypesMessage full roundtrip", "[roundtrip][full]") {
    all_types::AllTypesMessage msg;
    msg.set_u8(0xFF);
    msg.set_u16(0xABCD);
    msg.set_u32(0x12345678);
    msg.set_u64(0xFEDCBA9876543210ULL);
    msg.set_i8(static_cast<int8_t>(-1));
    msg.set_i16(static_cast<int16_t>(-32000));
    msg.set_i32(-100000);
    msg.set_f32(1.5f);
    msg.set_f64(-99.99);
    msg.set_flag(1);

    all_types::ascii_str ascii;
    ascii.set_value("ABCDE");
    msg.set_ascii(ascii);

    all_types::utf8_str utf8;
    utf8.set_value("Hello World");
    msg.set_utf8(utf8);

    std::array<uint8_t, 8> raw = {1, 2, 3, 4, 5, 6, 7, 8};
    msg.set_raw(raw);
    msg.set_le16(0x9988);
    msg.set_le32(0x11223344);

    all_types::scaled_temp temp;
    temp.set_raw(5000);
    msg.set_temp(temp);

    msg.set_hex(0xCAFEBABE);
    msg.set_color(all_types::color_enum::blue);

    all_types::status_flags status;
    status.set_raw(0x05);
    msg.set_status(status);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 83);

    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->u8() == 0xFF);
    CHECK(decoded->u16() == 0xABCD);
    CHECK(decoded->u32() == 0x12345678);
    CHECK(decoded->u64() == 0xFEDCBA9876543210ULL);
    CHECK(decoded->i8() == static_cast<int8_t>(-1));
    CHECK(decoded->i16() == static_cast<int16_t>(-32000));
    CHECK(decoded->i32() == -100000);
    CHECK_THAT(decoded->f32(), Catch::Matchers::WithinRel(1.5f, 0.001f));
    CHECK_THAT(decoded->f64(), Catch::Matchers::WithinRel(-99.99, 1e-10));
    CHECK(decoded->flag() != 0);
    CHECK(decoded->raw() == raw);
    CHECK(decoded->le16() == 0x9988);
    CHECK(decoded->le32() == 0x11223344);
    CHECK(decoded->temp().raw() == 5000);
    CHECK(decoded->hex() == 0xCAFEBABE);
    CHECK(decoded->color() == all_types::color_enum::blue);
    CHECK(decoded->status().raw() == 0x05);
}

// ============================================================================
// Section: Constraints (struct_features fixture)
// ============================================================================

TEST_CASE("ConstrainedMessage valid roundtrip", "[roundtrip][constraints]") {
    struct_features::ConstrainedMessage msg;
    REQUIRE(msg.set_magic(0xCAFE).has_value());
    REQUIRE(msg.set_version(3).has_value());
    REQUIRE(msg.set_value(500).has_value());
    struct_features::GpsCoord coord;
    coord.set_latitude(12345678);
    coord.set_longitude(87654321);
    msg.set_position(coord);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 14);
    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->magic() == 0xCAFE);
    CHECK(decoded->version() == 3);
    CHECK(decoded->value() == 500);
    CHECK(decoded->position().latitude() == 12345678);
    CHECK(decoded->position().longitude() == 87654321);
}

TEST_CASE("ConstrainedMessage invalid magic decode fails", "[roundtrip][constraints]") {
    // Build wire bytes with wrong magic
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD); // wrong magic (should be 0xCAFE)
    w.write_u8(3);       // version
    w.write_u16(500);    // value
    w.write_bits(0, 8);  // reserved
    w.write_u32(0);      // latitude
    w.write_u32(0);      // longitude
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("ConstrainedMessage value below min fails", "[roundtrip][constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xCAFE); // magic
    w.write_u8(3);       // version
    w.write_u16(5);      // value = 5 (min is 10)
    w.write_bits(0, 8);  // reserved
    w.write_u32(0);      // latitude
    w.write_u32(0);      // longitude
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("ConstrainedMessage value above max fails", "[roundtrip][constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xCAFE); // magic
    w.write_u8(3);       // version
    w.write_u16(1001);   // value = 1001 (max is 1000)
    w.write_bits(0, 8);  // reserved
    w.write_u32(0);      // latitude
    w.write_u32(0);      // longitude
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: Reserved/Align (struct_features fixture)
// ============================================================================

TEST_CASE("Reserved bits written as zero", "[roundtrip][reserved]") {
    struct_features::ConstrainedMessage msg;
    REQUIRE(msg.set_magic(0xCAFE).has_value());
    REQUIRE(msg.set_version(3).has_value());
    REQUIRE(msg.set_value(100).has_value());
    struct_features::GpsCoord coord;
    msg.set_position(coord);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Reserved byte is at offset: 2 (magic) + 1 (version) + 2 (value) = 5
    REQUIRE(bytes.size() > 5);
    CHECK(bytes[5] == 0x00);
}

TEST_CASE("AlignedMessage encode includes padding", "[roundtrip][align]") {
    struct_features::AlignedMessage msg;
    msg.set_flag(0x42);
    msg.set_data(0xAABBCCDD);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // flag (1 byte) + align_to(2) padding (1 byte) + data (4 bytes) = 6 bytes
    CHECK(bytes.size() == 6);
    // First byte is flag
    CHECK(bytes[0] == 0x42);
    // Padding byte
    CHECK(bytes[1] == 0x00);
    // data in big-endian
    CHECK(bytes[2] == 0xAA);
    CHECK(bytes[3] == 0xBB);
    CHECK(bytes[4] == 0xCC);
    CHECK(bytes[5] == 0xDD);
}

// ============================================================================
// Section: Conditional Fields (struct_features fixture)
// ============================================================================

TEST_CASE("ConditionalMessage with extra present", "[roundtrip][conditional]") {
    struct_features::ConditionalMessage msg;
    msg.set_has_extra(1);
    msg.set_base_value(0xAAAA);
    msg.set_extra_value(0x1234);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 5);

    auto decoded = struct_features::ConditionalMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_extra() == 1);
    CHECK(decoded->base_value() == 0xAAAA);
    CHECK(decoded->has_extra_value());
    CHECK(decoded->extra_value() == 0x1234);
}

TEST_CASE("ConditionalMessage without extra", "[roundtrip][conditional]") {
    struct_features::ConditionalMessage msg;
    msg.set_has_extra(0);
    msg.set_base_value(0xBBBB);
    // Don't set extra_value
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Without extra: has_extra(1) + base_value(2) = 3 bytes
    CHECK(bytes.size() == 3);

    auto decoded = struct_features::ConditionalMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_extra() == 0);
    CHECK(decoded->base_value() == 0xBBBB);
    CHECK_FALSE(decoded->has_extra_value());
}

// ============================================================================
// Section: Nested Structs (struct_features fixture)
// ============================================================================

TEST_CASE("GpsCoord nested struct roundtrip", "[roundtrip][nested]") {
    struct_features::ConstrainedMessage msg;
    REQUIRE(msg.set_magic(0xCAFE).has_value());
    REQUIRE(msg.set_version(3).has_value());
    REQUIRE(msg.set_value(100).has_value());

    struct_features::GpsCoord coord;
    coord.set_latitude(0x12345678);
    coord.set_longitude(0x9ABCDEF0);
    msg.set_position(coord);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->position().latitude() == 0x12345678);
    CHECK(decoded->position().longitude() == 0x9ABCDEF0);
}

// ============================================================================
// Section: Fixed Arrays (arrays_choices fixture)
// ============================================================================

TEST_CASE("FixedArrayMsg 3 points roundtrip", "[roundtrip][arrays]") {
    arrays_choices::FixedArrayMsg msg;
    auto& pts = msg.mutable_points();
    for (int i = 0; i < 3; i++) {
        arrays_choices::Point p;
        p.set_x(static_cast<uint16_t>(i * 100));
        p.set_y(static_cast<uint16_t>(i * 200));
        pts.push_back(p);
    }
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 12);
    auto decoded = arrays_choices::FixedArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->points().size() == 3);
    for (int i = 0; i < 3; i++) {
        CHECK(decoded->points()[static_cast<size_t>(i)].x() == static_cast<uint16_t>(i * 100));
        CHECK(decoded->points()[static_cast<size_t>(i)].y() == static_cast<uint16_t>(i * 200));
    }
}

TEST_CASE("FixedArrayMsg wire size", "[roundtrip][arrays]") {
    arrays_choices::FixedArrayMsg msg;
    auto& pts = msg.mutable_points();
    for (int i = 0; i < 3; i++) {
        arrays_choices::Point p;
        pts.push_back(p);
    }
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // 3 points * 4 bytes each (2 x uint16) = 12 bytes
    CHECK(bytes.size() == 12);
}

// ============================================================================
// Section: Count-From Arrays (arrays_choices fixture)
// ============================================================================

TEST_CASE("CountFromArrayMsg 0 items", "[roundtrip][arrays]") {
    arrays_choices::CountFromArrayMsg msg;
    msg.set_num_items(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = arrays_choices::CountFromArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->num_items() == 0);
    CHECK(decoded->items().empty());
}

TEST_CASE("CountFromArrayMsg 0 items explicit empty vector - T2 regression", "[roundtrip][arrays]") {
    // T2 test gap: ensure count-from array with count=0 and an explicitly
    // empty items vector encodes/decodes correctly, and the wire size is minimal.
    arrays_choices::CountFromArrayMsg msg;
    msg.set_num_items(0);
    // Explicitly ensure the items vector is empty
    msg.mutable_items().clear();

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // Wire size: num_items(1 byte) + 0 items = 1 byte
    CHECK(bytes.size() == 1);
    CHECK(bytes[0] == 0x00);

    auto decoded = arrays_choices::CountFromArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->num_items() == 0);
    CHECK(decoded->items().empty());
    CHECK(decoded->items().size() == 0);
}

TEST_CASE("CountFromArrayMsg 100 items stress test", "[roundtrip][arrays]") {
    arrays_choices::CountFromArrayMsg msg;
    msg.set_num_items(100);
    auto& items = msg.mutable_items();
    for (int i = 0; i < 100; i++) {
        arrays_choices::Point p;
        p.set_x(static_cast<uint16_t>(i));
        p.set_y(static_cast<uint16_t>(i * 2));
        items.push_back(p);
    }
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = arrays_choices::CountFromArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->num_items() == 100);
    REQUIRE(decoded->items().size() == 100);
    CHECK(decoded->items()[0].x() == 0);
    CHECK(decoded->items()[0].y() == 0);
    CHECK(decoded->items()[99].x() == 99);
    CHECK(decoded->items()[99].y() == 198);
    // num_items(1 byte) + 100 points * 4 bytes each = 401 bytes
    CHECK(bytes.size() == 401);
}

TEST_CASE("CountFromArrayMsg 5 items roundtrip", "[roundtrip][arrays]") {
    arrays_choices::CountFromArrayMsg msg;
    msg.set_num_items(5);
    auto& items = msg.mutable_items();
    for (int i = 0; i < 5; i++) {
        arrays_choices::Point p;
        p.set_x(static_cast<uint16_t>(i + 1));
        p.set_y(static_cast<uint16_t>((i + 1) * 10));
        items.push_back(p);
    }
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 21);
    auto decoded = arrays_choices::CountFromArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->num_items() == 5);
    REQUIRE(decoded->items().size() == 5);
    for (int i = 0; i < 5; i++) {
        CHECK(decoded->items()[static_cast<size_t>(i)].x() == static_cast<uint16_t>(i + 1));
        CHECK(decoded->items()[static_cast<size_t>(i)].y() == static_cast<uint16_t>((i + 1) * 10));
    }
}

TEST_CASE("CountFromArrayMsg truncated decode fails", "[roundtrip][arrays][errors]") {
    // num_items=5 but only provide 5 bytes total (need 1 + 5*4 = 21)
    std::vector<uint8_t> data = {0x05, 0x00, 0x01, 0x00, 0x02};
    auto decoded = arrays_choices::CountFromArrayMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

// ============================================================================
// Section: Choice (arrays_choices fixture)
// ============================================================================

TEST_CASE("ChoiceMsg TypeA case roundtrip", "[roundtrip][choice]") {
    arrays_choices::TypeABody body;
    body.set_sub_type(10); // SUB_X
    arrays_choices::SubX subx;
    subx.set_val(0xDEAD);
    body.set_sub_body(arrays_choices::TypeABody_sub_bodyVariant{subx});

    arrays_choices::ChoiceMsg msg;
    msg.set_msg_type(1); // TYPE_A
    // length = size of TypeABody: sub-type(1) + SubX(4) = 5
    msg.set_length(5);
    msg.set_body(arrays_choices::ChoiceMsg_bodyVariant{body});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 8);  // msg_type(1)+length(2)+body(5)
    CHECK(bytes[0] == 1);      // TYPE_A discriminator
    auto decoded = arrays_choices::ChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);
    auto& decoded_body = std::get<arrays_choices::TypeABody>(decoded->body());
    CHECK(decoded_body.sub_type() == 10);
    auto& decoded_sub = std::get<arrays_choices::SubX>(decoded_body.sub_body());
    CHECK(decoded_sub.val() == 0xDEAD);
}

TEST_CASE("ChoiceMsg TypeB case roundtrip", "[roundtrip][choice]") {
    arrays_choices::TypeBBody body;
    body.set_tag(0xBEEF);

    arrays_choices::ChoiceMsg msg;
    msg.set_msg_type(2); // TYPE_B
    msg.set_length(4);   // TypeBBody = uint32 = 4 bytes
    msg.set_body(arrays_choices::ChoiceMsg_bodyVariant{body});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 7);  // msg_type(1)+length(2)+TypeBBody(4)
    CHECK(bytes[0] == 2);      // TYPE_B discriminator
    auto decoded = arrays_choices::ChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 2);
    auto& decoded_body = std::get<arrays_choices::TypeBBody>(decoded->body());
    CHECK(decoded_body.tag() == 0xBEEF);
}

TEST_CASE("ChoiceMsg otherwise fallback roundtrip", "[roundtrip][choice]") {
    arrays_choices::FallbackBody body;
    body.set_raw(0xF00D);

    arrays_choices::ChoiceMsg msg;
    msg.set_msg_type(99); // Unknown type -> otherwise
    msg.set_length(4);
    msg.set_body(arrays_choices::ChoiceMsg_bodyVariant{body});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = arrays_choices::ChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 99);
    auto& decoded_body = std::get<arrays_choices::FallbackBody>(decoded->body());
    CHECK(decoded_body.raw() == 0xF00D);
}

TEST_CASE("ChoiceMsg nested choice TypeA->SubY roundtrip", "[roundtrip][choice]") {
    arrays_choices::TypeABody body;
    body.set_sub_type(20); // SUB_Y
    arrays_choices::SubY suby;
    suby.set_a(0x1111);
    suby.set_b(0x2222);
    body.set_sub_body(arrays_choices::TypeABody_sub_bodyVariant{suby});

    arrays_choices::ChoiceMsg msg;
    msg.set_msg_type(1); // TYPE_A
    msg.set_length(5);   // sub-type(1) + SubY(4) = 5
    msg.set_body(arrays_choices::ChoiceMsg_bodyVariant{body});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = arrays_choices::ChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& decoded_body = std::get<arrays_choices::TypeABody>(decoded->body());
    auto& decoded_sub = std::get<arrays_choices::SubY>(decoded_body.sub_body());
    CHECK(decoded_sub.a() == 0x1111);
    CHECK(decoded_sub.b() == 0x2222);
}

// ============================================================================
// Section: Known Wire Vectors
// ============================================================================

TEST_CASE("ConstrainedMessage known wire bytes", "[roundtrip][wirevec]") {
    struct_features::ConstrainedMessage msg;
    REQUIRE(msg.set_magic(0xCAFE).has_value());
    REQUIRE(msg.set_version(3).has_value());
    REQUIRE(msg.set_value(100).has_value());
    struct_features::GpsCoord coord;
    coord.set_latitude(0);
    coord.set_longitude(0);
    msg.set_position(coord);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // magic(2) + version(1) + value(2) + reserved(1) + lat(4) + lon(4) = 14
    REQUIRE(bytes.size() == 14);
    CHECK(bytes[0] == 0xCA);
    CHECK(bytes[1] == 0xFE);
    CHECK(bytes[2] == 0x03);     // version=3
    CHECK(bytes[3] == 0x00);     // value=100 big-endian high
    CHECK(bytes[4] == 0x64);     // value=100 big-endian low
    CHECK(bytes[5] == 0x00);     // reserved
}

TEST_CASE("FixedArrayMsg known wire bytes", "[roundtrip][wirevec]") {
    arrays_choices::FixedArrayMsg msg;
    auto& pts = msg.mutable_points();
    for (int i = 0; i < 3; i++) {
        arrays_choices::Point p; // zero-initialized
        pts.push_back(p);
    }
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 12);
    for (size_t i = 0; i < 12; i++) {
        CHECK(bytes[i] == 0x00);
    }
}

// ============================================================================
// Section: Decode Error Paths
// ============================================================================

TEST_CASE("Decode from empty buffer fails", "[roundtrip][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = all_types::AllTypesMessage::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Decode from truncated buffer fails", "[roundtrip][errors]") {
    std::vector<uint8_t> partial = {0x42, 0x00}; // Just 2 bytes
    auto decoded = all_types::AllTypesMessage::decode_bytes(partial);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Decode with invalid constraint fails", "[roundtrip][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBAAD); // Wrong magic (should be 0xCAFE)
    w.write_u8(3);
    w.write_u16(100);
    w.write_bits(0, 8);
    w.write_u32(0);
    w.write_u32(0);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);
    auto decoded = struct_features::ConstrainedMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: Bitmap/FSPEC (bitmap_fx fixture)
// ============================================================================

TEST_CASE("Bitmap only item010 present", "[roundtrip][bitmap]") {
    bitmap_fx::BitmapItems msg_items;
    bitmap_fx::DataItem010 item;
    item.set_sac(1);
    item.set_sic(2);
    msg_items.set_item010(item);

    bitmap_fx::Category msg;
    msg.set_items(msg_items);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    CHECK((bytes[0] & 0x80) != 0);
    auto decoded = bitmap_fx::Category::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_item010());
    CHECK(decoded->items().item010().sac() == 1);
    CHECK(decoded->items().item010().sic() == 2);
    CHECK_FALSE(decoded->items().has_item020());
    CHECK_FALSE(decoded->items().has_item030());
}

TEST_CASE("Bitmap item010+item020", "[roundtrip][bitmap]") {
    bitmap_fx::BitmapItems msg_items;
    bitmap_fx::DataItem010 i010;
    i010.set_sac(10);
    i010.set_sic(20);
    msg_items.set_item010(i010);

    bitmap_fx::DataItem020 i020;
    i020.set_code(0x1234);
    msg_items.set_item020(i020);

    bitmap_fx::Category msg;
    msg.set_items(msg_items);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    CHECK((bytes[0] & 0xC0) == 0xC0);
    auto decoded = bitmap_fx::Category::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_item010());
    CHECK(decoded->items().has_item020());
    CHECK(decoded->items().item010().sac() == 10);
    CHECK(decoded->items().item020().code() == 0x1234);
}

TEST_CASE("Bitmap all first-octet items", "[roundtrip][bitmap]") {
    bitmap_fx::BitmapItems msg_items;
    bitmap_fx::DataItem010 i010;
    i010.set_sac(0xAA);
    i010.set_sic(0xBB);
    msg_items.set_item010(i010);

    bitmap_fx::DataItem020 i020;
    i020.set_code(0x5678);
    msg_items.set_item020(i020);

    bitmap_fx::DataItem030 i030;
    i030.set_value(0xDEADBEEF);
    msg_items.set_item030(i030);

    bitmap_fx::Category msg;
    msg.set_items(msg_items);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = bitmap_fx::Category::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().has_item010());
    CHECK(decoded->items().has_item020());
    CHECK(decoded->items().has_item030());
    CHECK(decoded->items().item010().sac() == 0xAA);
    CHECK(decoded->items().item030().value() == 0xDEADBEEF);
}

TEST_CASE("Bitmap no items present", "[roundtrip][bitmap]") {
    bitmap_fx::BitmapItems msg_items;
    // Don't set any items

    bitmap_fx::Category msg;
    msg.set_items(msg_items);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    CHECK(bytes.size() == 1); CHECK(bytes[0] == 0x00);
    auto decoded = bitmap_fx::Category::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded->items().has_item010());
    CHECK_FALSE(decoded->items().has_item020());
    CHECK_FALSE(decoded->items().has_item030());
}

TEST_CASE("Bitmap wire size varies", "[roundtrip][bitmap]") {
    // No items: just FSPEC byte
    {
        bitmap_fx::BitmapItems msg_items;
        bitmap_fx::Category msg;
        msg.set_items(msg_items);
        auto enc1 = msg.encode_bytes();
        REQUIRE(enc1.has_value());
        auto bytes_empty = std::move(*enc1);

        bitmap_fx::BitmapItems msg_items2;
        bitmap_fx::DataItem010 i010;
        msg_items2.set_item010(i010);
        bitmap_fx::Category msg2;
        msg2.set_items(msg_items2);
        auto enc2 = msg2.encode_bytes();
        REQUIRE(enc2.has_value());
        auto bytes_one = std::move(*enc2);

        // With item010 should be larger than without
        CHECK(bytes_one.size() > bytes_empty.size());
    }
}

// ============================================================================
// Section: Session Protocol (session_protocol fixture)
// ============================================================================

TEST_CASE("Session protocol Packet ping roundtrip", "[roundtrip][session]") {
    session_test::PingBody ping;
    ping.set_timestamp(0x12345678);

    auto frame = session_test::Packet::wrap(ping);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = session_test::Packet::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<session_test::PingBody>(decoded->payload());
    CHECK(body.timestamp() == 0x12345678);
}

TEST_CASE("Session protocol Packet data roundtrip", "[roundtrip][session]") {
    session_test::DataBody data;
    data.set_channel(5);
    data.set_payload_a(0xAAAA);
    data.set_payload_b(0xBBBB);

    auto frame = session_test::Packet::wrap(data);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = session_test::Packet::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<session_test::DataBody>(decoded->payload());
    CHECK(body.channel() == 5);
    CHECK(body.payload_a() == 0xAAAA);
    CHECK(body.payload_b() == 0xBBBB);
}

// ============================================================================
// Section: Inline Struct (inline_struct fixture)
// ============================================================================

TEST_CASE("Inline struct field flattening", "[roundtrip][inline]") {
    inline_struct::BodyX body;
    body.set_x_data(0xDEADBEEF);

    auto frame = inline_struct::Frame::wrap(body);
    frame.set_sync(inline_struct::SYNC);
    frame.set_seq(1);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = inline_struct::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == inline_struct::SYNC);
    CHECK(decoded->seq() == 1);
}

TEST_CASE("Inline struct nested body roundtrip", "[roundtrip][inline]") {
    inline_struct::BodyX body;
    body.set_x_data(0x12345678);

    auto frame = inline_struct::Frame::wrap(body);
    frame.set_sync(inline_struct::SYNC);
    frame.set_seq(42);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = inline_struct::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == inline_struct::SYNC);
    CHECK(decoded->seq() == 42);
    auto& decoded_body = std::get<inline_struct::BodyX>(decoded->payload());
    CHECK(decoded_body.x_data() == 0x12345678);
}

// ============================================================================
// Section: Choice Protocol (choice_protocol fixture)
// ============================================================================

TEST_CASE("Choice protocol Frame with AlphaBody roundtrip", "[roundtrip][choice_protocol]") {
    choice_test::AlphaBody alpha;
    alpha.set_x(0x1111);
    alpha.set_y(0x2222);

    auto frame = choice_test::Frame::wrap(alpha);
    frame.set_sync(choice_test::SYNC);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = choice_test::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == choice_test::SYNC);
    auto& decoded_body = std::get<choice_test::AlphaBody>(decoded->payload());
    CHECK(decoded_body.x() == 0x1111);
    CHECK(decoded_body.y() == 0x2222);
}

TEST_CASE("Choice protocol Frame with BetaBody roundtrip", "[roundtrip][choice_protocol]") {
    choice_test::BetaBody beta;
    beta.set_payload_size(42);
    beta.set_tag(0xABCD1234);

    auto frame = choice_test::Frame::wrap(beta);
    frame.set_sync(choice_test::SYNC);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = choice_test::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& decoded_body = std::get<choice_test::BetaBody>(decoded->payload());
    CHECK(decoded_body.payload_size() == 42);
    CHECK(decoded_body.tag() == 0xABCD1234);
}

TEST_CASE("Session constraint sync word on wire", "[roundtrip][wire]") {
    inline_struct::BodyX body;
    body.set_x_data(0);

    auto frame = inline_struct::Frame::wrap(body);
    frame.set_sync(inline_struct::SYNC);
    frame.set_seq(0);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() >= 2);
    // Sync word should be at offset 0-1 in big-endian
    CHECK(bytes[0] == 0xCA);
    CHECK(bytes[1] == 0xFE);
}

TEST_CASE("Session wrap sets discriminator on wire", "[roundtrip][wire]") {
    inline_struct::BodyX body;
    body.set_x_data(0);

    auto frame = inline_struct::Frame::wrap(body);
    frame.set_sync(inline_struct::SYNC);
    frame.set_seq(0);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // msg-type is at offset: sync(2) + seq(2) + length(2) = 6
    REQUIRE(bytes.size() > 6);
    CHECK(bytes[6] == inline_struct::BodyX::ID_VALUE);
}

// ============================================================================
// Section: Enum Decode Rejection
// ============================================================================

TEST_CASE("enum decode rejects invalid value", "[roundtrip][enum]") {
    // Encode a valid message first to get correctly-sized bytes
    all_types::AllTypesMessage msg;
    msg.set_u8(0);
    msg.set_color(all_types::color_enum::red);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() > 81);

    // Overwrite the color_enum byte (offset 81) with an invalid value (0)
    // Valid enum values are 1 (red), 2 (green), 3 (blue)
    bytes[81] = 0;
    auto decoded = all_types::AllTypesMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: wrap() auto-computes length-from
// ============================================================================

TEST_CASE("wrap auto-computes length for PingBody", "[roundtrip][wrap]") {
    session_test::PingBody ping;
    ping.set_timestamp(0x12345678);

    // wrap() should auto-set length -- do NOT set it manually
    auto frame = session_test::Packet::wrap(ping);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = session_test::Packet::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<session_test::PingBody>(decoded->payload());
    CHECK(body.timestamp() == 0x12345678);
}

TEST_CASE("wrap auto-computes length for DataBody", "[roundtrip][wrap]") {
    session_test::DataBody data;
    data.set_channel(5);
    data.set_payload_a(0xAAAA);
    data.set_payload_b(0xBBBB);

    // wrap() should auto-set length -- do NOT set it manually
    auto frame = session_test::Packet::wrap(data);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = session_test::Packet::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<session_test::DataBody>(decoded->payload());
    CHECK(body.channel() == 5);
    CHECK(body.payload_a() == 0xAAAA);
    CHECK(body.payload_b() == 0xBBBB);
}
