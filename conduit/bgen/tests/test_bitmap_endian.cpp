// SPDX-License-Identifier: MIT
// Bitmap endianness tests
//
// These tests pin down two related aspects of the bitmap encoding:
//
//   1. The FSPEC byte order honors `<bitmap endian="little|big">`. With
//      "big" (default) the byte holding the lowest bit numbers is sent
//      first; with "little" the byte order is reversed.
//
//   2. Multi-byte primitive *field values* inside a bitmap struct honor
//      the field's own `endian` attribute. Before the wire-order
//      renumbering this was broken: all integer bitmap fields were
//      written via `write_bits` (MSB-first regardless of `endian`).
//
// The fixture defines the same set of fields under both a big-endian
// bitmap and a little-endian bitmap, which lets us assert exact wire
// bytes for each.

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <vector>

#include "bitmap_endian/messages.hpp"

using namespace bitmap_endian;

// ============================================================================
// Big-endian bitmap (the default): fixture expectations
// ============================================================================

TEST_CASE("Bitmap big-endian FSPEC and field-value byte order",
          "[roundtrip][bitmap][endian]") {
    BigEndianBitmap bm;
    bm.set_alpha(0x1122);    // bit 0  (byte 0 MSB)
    bm.set_charlie(0x3344);  // bit 8  (byte 1 MSB)

    conduit::io::BitWriter w;
    REQUIRE(bm.encode(w).has_value());
    auto bytes_r = w.finish();
    REQUIRE(bytes_r.has_value());
    auto& bytes = *bytes_r;

    // FSPEC: 2 bytes (no ext), big-endian byte order so byte 0 first.
    // Byte 0: bits 0..7, mask 1<<7 = 0x80 for bit 0 (alpha), bit 1 absent
    // (bravo not set). Byte 0 == 0x80.
    // Byte 1: bits 8..15, mask 1<<7 = 0x80 for bit 8 (charlie). Byte 1 == 0x80.
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0x80);
    CHECK(bytes[1] == 0x80);

    // After FSPEC, alpha (uint16, big) at offset 2..3 = 0x11 0x22.
    REQUIRE(bytes.size() >= 4);
    CHECK(bytes[2] == 0x11);
    CHECK(bytes[3] == 0x22);

    // Then charlie (uint16, big) at offset 4..5 = 0x33 0x44.
    REQUIRE(bytes.size() >= 6);
    CHECK(bytes[4] == 0x33);
    CHECK(bytes[5] == 0x44);

    // Roundtrip
    conduit::io::BitReader r(bytes);
    auto decoded = BigEndianBitmap::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_alpha());
    CHECK(decoded->alpha() == 0x1122);
    CHECK(decoded->has_charlie());
    CHECK(decoded->charlie() == 0x3344);
    CHECK_FALSE(decoded->has_bravo());
    CHECK_FALSE(decoded->has_delta());
}

// ============================================================================
// Little-endian bitmap: same fields, opposite byte order on every multi-byte
// element (FSPEC and field values).
// ============================================================================

TEST_CASE("Bitmap little-endian FSPEC reverses byte order",
          "[roundtrip][bitmap][endian]") {
    LittleEndianBitmap bm;
    bm.set_alpha(0x1122);    // logical bit 0  (logical byte 0 MSB)
    bm.set_charlie(0x3344);  // logical bit 8  (logical byte 1 MSB)

    conduit::io::BitWriter w;
    REQUIRE(bm.encode(w).has_value());
    auto bytes_r = w.finish();
    REQUIRE(bytes_r.has_value());
    auto& bytes = *bytes_r;

    // FSPEC: 2 bytes, little-endian byte order so logical byte 0 (which
    // would be 0x80 — alpha set, byte 0 MSB) comes second on the wire and
    // logical byte 1 (which would be 0x80 — charlie set, byte 1 MSB)
    // comes first.
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0x80);  // logical byte 1 (charlie)
    CHECK(bytes[1] == 0x80);  // logical byte 0 (alpha)

    // After FSPEC, alpha (uint16, little) at offset 2..3 = 0x22 0x11
    // (low byte first).
    REQUIRE(bytes.size() >= 4);
    CHECK(bytes[2] == 0x22);
    CHECK(bytes[3] == 0x11);

    // Then charlie (uint16, little) at offset 4..5 = 0x44 0x33.
    REQUIRE(bytes.size() >= 6);
    CHECK(bytes[4] == 0x44);
    CHECK(bytes[5] == 0x33);

    // Roundtrip
    conduit::io::BitReader r(bytes);
    auto decoded = LittleEndianBitmap::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_alpha());
    CHECK(decoded->alpha() == 0x1122);
    CHECK(decoded->has_charlie());
    CHECK(decoded->charlie() == 0x3344);
}

// ============================================================================
// Mixed-presence patterns to exercise the FSPEC reverse behaviour with only
// some fields set (so logical byte 0 or logical byte 1 may be zero).
// ============================================================================

TEST_CASE("Bitmap little-endian FSPEC: only logical byte 0 fields set",
          "[roundtrip][bitmap][endian]") {
    LittleEndianBitmap bm;
    bm.set_alpha(0xAABB);  // bit 0 — logical byte 0
    bm.set_bravo(0xCCDD);  // bit 1 — logical byte 0

    conduit::io::BitWriter w;
    REQUIRE(bm.encode(w).has_value());
    auto bytes_r = w.finish();
    REQUIRE(bytes_r.has_value());
    auto& bytes = *bytes_r;

    // Logical FSPEC: byte 0 = 0xC0 (bits 0 and 1 set: 0x80|0x40), byte 1 = 0x00.
    // Reversed for little-endian: byte 1 first, then byte 0.
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0x00);  // logical byte 1
    CHECK(bytes[1] == 0xC0);  // logical byte 0

    // alpha little-endian: 0xBB 0xAA
    // bravo little-endian: 0xDD 0xCC
    // Order: alpha first (lowest BMDL bit number in byte 0), then bravo.
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[2] == 0xBB);
    CHECK(bytes[3] == 0xAA);
    CHECK(bytes[4] == 0xDD);
    CHECK(bytes[5] == 0xCC);

    conduit::io::BitReader r(bytes);
    auto decoded = LittleEndianBitmap::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->alpha() == 0xAABB);
    CHECK(decoded->bravo() == 0xCCDD);
    CHECK_FALSE(decoded->has_charlie());
    CHECK_FALSE(decoded->has_delta());
}

TEST_CASE("Bitmap little-endian FSPEC: 32-bit field value byte-swapped",
          "[roundtrip][bitmap][endian]") {
    LittleEndianBitmap bm;
    bm.set_charlie(0x1234);
    bm.set_delta(0xDEADBEEFu);

    conduit::io::BitWriter w;
    REQUIRE(bm.encode(w).has_value());
    auto bytes_r = w.finish();
    REQUIRE(bytes_r.has_value());
    auto& bytes = *bytes_r;

    // FSPEC reversed: logical byte 1 first.
    // Logical byte 1 sets bit 8 (charlie, mask 0x80) and bit 9 (delta, mask 0x40).
    // Logical byte 0 = 0x00.
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0xC0);  // logical byte 1 (charlie + delta)
    CHECK(bytes[1] == 0x00);  // logical byte 0

    // charlie (uint16, little) at offset 2..3
    CHECK(bytes[2] == 0x34);
    CHECK(bytes[3] == 0x12);

    // delta (uint32, little) at offset 4..7
    REQUIRE(bytes.size() >= 8);
    CHECK(bytes[4] == 0xEF);
    CHECK(bytes[5] == 0xBE);
    CHECK(bytes[6] == 0xAD);
    CHECK(bytes[7] == 0xDE);

    conduit::io::BitReader r(bytes);
    auto decoded = LittleEndianBitmap::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->charlie() == 0x1234);
    CHECK(decoded->delta() == 0xDEADBEEFu);
}

// ============================================================================
// Symmetry: encoding a value, decoding it, re-encoding must produce the
// same bytes.  Belt-and-braces against partial fixes that flip one direction.
// ============================================================================

TEST_CASE("Bitmap big/little roundtrip stability", "[roundtrip][bitmap][endian]") {
    {
        BigEndianBitmap bm;
        bm.set_alpha(0x0001);
        bm.set_bravo(0x0203);
        bm.set_charlie(0x0405);
        bm.set_delta(0x06070809u);

        conduit::io::BitWriter w1;
        REQUIRE(bm.encode(w1).has_value());
        auto b1 = w1.finish();
        REQUIRE(b1.has_value());

        conduit::io::BitReader r(*b1);
        auto decoded = BigEndianBitmap::decode(r);
        REQUIRE(decoded.has_value());

        conduit::io::BitWriter w2;
        REQUIRE(decoded->encode(w2).has_value());
        auto b2 = w2.finish();
        REQUIRE(b2.has_value());
        CHECK(*b1 == *b2);
    }

    {
        LittleEndianBitmap bm;
        bm.set_alpha(0x0001);
        bm.set_bravo(0x0203);
        bm.set_charlie(0x0405);
        bm.set_delta(0x06070809u);

        conduit::io::BitWriter w1;
        REQUIRE(bm.encode(w1).has_value());
        auto b1 = w1.finish();
        REQUIRE(b1.has_value());

        conduit::io::BitReader r(*b1);
        auto decoded = LittleEndianBitmap::decode(r);
        REQUIRE(decoded.has_value());

        conduit::io::BitWriter w2;
        REQUIRE(decoded->encode(w2).has_value());
        auto b2 = w2.finish();
        REQUIRE(b2.has_value());
        CHECK(*b1 == *b2);
    }
}
