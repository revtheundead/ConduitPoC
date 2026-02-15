// SPDX-License-Identifier: MIT
// Bgen tests - Bitmap struct roundtrip verification
//
// Tests generated code from bitmap_advanced fixture: bitmap-controlled struct
// with enum, string, bytes, BCD, and primitive fields.

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "bitmap_advanced/messages.hpp"
#include "bitmap_wide_fixed/messages.hpp"

// ============================================================================
// Bitmap: all items present
// ============================================================================

TEST_CASE("Bitmap all items present roundtrip", "[roundtrip][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_status(bitmap_advanced::status_code::warning);
    bm.set_label("Hello");
    std::array<uint8_t, 4> raw_data = {0xDE, 0xAD, 0xBE, 0xEF};
    bm.set_data(raw_data);
    bm.set_bcd_field(42);
    bm.set_counter(0x1234);

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0xAA);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->header() == 0xAA);
    auto& d = decoded->bitmap_data();
    REQUIRE(d.has_status());
    CHECK(d.status() == bitmap_advanced::status_code::warning);
    REQUIRE(d.has_label());
    CHECK(d.label() == "Hello");
    REQUIRE(d.has_data());
    CHECK(d.data() == raw_data);
    REQUIRE(d.has_bcd_field());
    CHECK(d.bcd_field() == 42);
    REQUIRE(d.has_counter());
    CHECK(d.counter() == 0x1234);
}

// ============================================================================
// Bitmap: no items present
// ============================================================================

TEST_CASE("Bitmap no items present roundtrip", "[roundtrip][bitmap]") {
    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x55);
    // Leave bitmap_data default (all items absent)

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->header() == 0x55);
    auto& d = decoded->bitmap_data();
    CHECK_FALSE(d.has_status());
    CHECK_FALSE(d.has_label());
    CHECK_FALSE(d.has_data());
    CHECK_FALSE(d.has_bcd_field());
    CHECK_FALSE(d.has_counter());
}

// ============================================================================
// Bitmap: partial items (only enum + counter, bits 0 and 4)
// ============================================================================

TEST_CASE("Bitmap partial items roundtrip", "[roundtrip][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_status(bitmap_advanced::status_code::critical);
    bm.set_counter(9999);

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x01);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto& d = decoded->bitmap_data();
    REQUIRE(d.has_status());
    CHECK(d.status() == bitmap_advanced::status_code::critical);
    CHECK_FALSE(d.has_label());
    CHECK_FALSE(d.has_data());
    CHECK_FALSE(d.has_bcd_field());
    REQUIRE(d.has_counter());
    CHECK(d.counter() == 9999);
}

// ============================================================================
// Bitmap: BCD field isolation test
// ============================================================================

TEST_CASE("Bitmap BCD field roundtrip", "[roundtrip][bitmap][bcd]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_bcd_field(99); // max for 8-bit BCD (2 nibbles)

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto& d = decoded->bitmap_data();
    CHECK_FALSE(d.has_status());
    CHECK_FALSE(d.has_label());
    CHECK_FALSE(d.has_data());
    REQUIRE(d.has_bcd_field());
    CHECK(d.bcd_field() == 99);
    CHECK_FALSE(d.has_counter());
}

TEST_CASE("Bitmap BCD zero roundtrip", "[roundtrip][bitmap][bcd]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_bcd_field(0);

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bitmap_data().has_bcd_field());
    CHECK(decoded->bitmap_data().bcd_field() == 0);
}

// ============================================================================
// Bitmap: string field with padding
// ============================================================================

TEST_CASE("Bitmap string field roundtrip", "[roundtrip][bitmap][string]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_label("Test");

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bitmap_data().has_label());
    CHECK(decoded->bitmap_data().label() == "Test");
}

TEST_CASE("Bitmap string field exact length roundtrip", "[roundtrip][bitmap][string]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_label("12345678"); // exact 8-byte field

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bitmap_data().has_label());
    CHECK(decoded->bitmap_data().label() == "12345678");
}

// ============================================================================
// Bitmap: bytes field
// ============================================================================

TEST_CASE("Bitmap bytes field roundtrip", "[roundtrip][bitmap][bytes]") {
    std::array<uint8_t, 4> payload = {0xFF, 0x00, 0xAA, 0x55};
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_data(payload);

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bitmap_data().has_data());
    CHECK(decoded->bitmap_data().data() == payload);
}

TEST_CASE("Bitmap bytes field all zeros roundtrip", "[roundtrip][bitmap][bytes]") {
    std::array<uint8_t, 4> payload = {0x00, 0x00, 0x00, 0x00};
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_data(payload);

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bitmap_data().has_data());
    CHECK(decoded->bitmap_data().data() == payload);
}

// ============================================================================
// Bitmap: enum field all values
// ============================================================================

TEST_CASE("Bitmap enum all values roundtrip", "[roundtrip][bitmap][enum]") {
    for (auto val : {bitmap_advanced::status_code::ok,
                     bitmap_advanced::status_code::warning,
                     bitmap_advanced::status_code::error,
                     bitmap_advanced::status_code::critical}) {
        bitmap_advanced::AdvancedBitmap bm;
        bm.set_status(val);

        bitmap_advanced::BitmapAdvancedMsg msg;
        msg.set_header(0x00);
        msg.mutable_bitmap_data() = bm;

        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->bitmap_data().has_status());
        CHECK(decoded->bitmap_data().status() == val);
    }
}

// ============================================================================
// Bitmap: wire format verification
// ============================================================================

TEST_CASE("Bitmap wire format: empty bitmap is just the fspec byte", "[roundtrip][bitmap][wire]") {
    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0xBB);
    // No bitmap items set

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // header (1 byte) + fspec (1 byte, all zeros; sized by max bit index) = 2 bytes
    REQUIRE(bytes.size() == 2);
    CHECK(bytes[0] == 0xBB); // header
    CHECK(bytes[1] == 0x00); // fspec byte (no bits set)
}

TEST_CASE("Bitmap wire format: single item sets correct bitmap bit", "[roundtrip][bitmap][wire]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_status(bitmap_advanced::status_code::ok); // bit 0

    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // header(1) + fspec(1) + status_code(1) = 3 bytes
    REQUIRE(bytes.size() == 3);
    // Bit 0 set: fspec[0] |= (1 << 0) → LSB of fspec byte
    CHECK((bytes[1] & 0x01) != 0); // bit 0 is set (LSB)
}

// ============================================================================
// Bitmap: decode from truncated data fails
// ============================================================================

TEST_CASE("Bitmap decode from empty buffer fails", "[bitmap][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Bitmap decode truncated bitmap bytes fails", "[bitmap][errors]") {
    // Only header, no bitmap bytes
    std::vector<uint8_t> data = {0x42};
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(data);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Bitmap decode truncated field data fails", "[bitmap][errors]") {
    // header(1) + 1-byte fspec with bit 4 set (counter, uint16) but no field data
    conduit::io::BitWriter w;
    w.write_u8(0x00);   // header
    w.write_u8(0x10);   // fspec: bit 4 set (1 << 4 = 0x10), counter needs 16 bits
    // No counter data follows — decode should fail
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Wide fixed bitmap (non-FX, 3-byte FSPEC with bits at 0, 8, 16)
// ============================================================================

TEST_CASE("Wide fixed bitmap: all fields present roundtrip", "[roundtrip][bitmap][wide]") {
    bitmap_wide_fixed::WideBitmap bm;
    bm.set_alpha(0xAA);
    bm.set_beta(0x1234);
    bm.set_gamma(0xDEADBEEF);

    bitmap_wide_fixed::WideBitmapMsg msg;
    msg.set_header(0x42);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_wide_fixed::WideBitmapMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->header() == 0x42);
    auto& d = decoded->bitmap_data();
    REQUIRE(d.has_alpha());
    CHECK(d.alpha() == 0xAA);
    REQUIRE(d.has_beta());
    CHECK(d.beta() == 0x1234);
    REQUIRE(d.has_gamma());
    CHECK(d.gamma() == 0xDEADBEEF);
}

TEST_CASE("Wide fixed bitmap: partial fields (bits 0 and 16 only)", "[roundtrip][bitmap][wide]") {
    bitmap_wide_fixed::WideBitmap bm;
    bm.set_alpha(0x55);
    bm.set_gamma(0x12345678);

    bitmap_wide_fixed::WideBitmapMsg msg;
    msg.set_header(0x01);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = bitmap_wide_fixed::WideBitmapMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto& d = decoded->bitmap_data();
    REQUIRE(d.has_alpha());
    CHECK(d.alpha() == 0x55);
    CHECK_FALSE(d.has_beta());
    REQUIRE(d.has_gamma());
    CHECK(d.gamma() == 0x12345678);
}

TEST_CASE("Wide fixed bitmap: no fields present", "[roundtrip][bitmap][wide]") {
    bitmap_wide_fixed::WideBitmapMsg msg;
    msg.set_header(0xFF);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // header(1) + fspec(3 bytes, all zeros) = 4 bytes
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[1] == 0x00);
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == 0x00);

    auto decoded = bitmap_wide_fixed::WideBitmapMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xFF);
    auto& d = decoded->bitmap_data();
    CHECK_FALSE(d.has_alpha());
    CHECK_FALSE(d.has_beta());
    CHECK_FALSE(d.has_gamma());
}

TEST_CASE("Wide fixed bitmap: wire format verification", "[roundtrip][bitmap][wide][wire]") {
    bitmap_wide_fixed::WideBitmap bm;
    bm.set_alpha(0x01);    // bit 0 in byte 0
    bm.set_gamma(0x00000001); // bit 16 in byte 2

    bitmap_wide_fixed::WideBitmapMsg msg;
    msg.set_header(0x00);
    msg.mutable_bitmap_data() = bm;

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // header(1) + fspec(3) + alpha(1) + gamma(4) = 9 bytes
    REQUIRE(bytes.size() == 9);
    CHECK((bytes[1] & 0x01) != 0); // bit 0 set in fspec byte 0
    CHECK(bytes[2] == 0x00);       // fspec byte 1: no bits set
    CHECK((bytes[3] & 0x01) != 0); // bit 0 set in fspec byte 2 (bit 16)
}
