// SPDX-License-Identifier: MIT
// Conduit - BitWriter Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/io/bit_reader.hpp>
#include <cstring>

using namespace conduit::io;

TEST_CASE("BitWriter write_bits", "[bit_writer]") {
    SECTION("Single bits") {
        BitWriter writer;
        writer.write_bits(1, 1);
        writer.write_bits(0, 1);
        writer.write_bits(1, 1);
        writer.write_bits(1, 1);
        writer.write_bits(0, 1);
        writer.write_bits(1, 1);
        writer.write_bits(0, 1);
        writer.write_bits(0, 1);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0b10110100);
    }

    SECTION("Multi-bit values") {
        BitWriter writer;
        writer.write_bits(0xA, 4);
        writer.write_bits(0xB, 4);
        writer.write_bits(0xCD, 8);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0xAB);
        CHECK(data[1] == 0xCD);
    }

    SECTION("Cross-byte boundary") {
        BitWriter writer;
        writer.write_bits(0xF, 4);
        writer.write_bits(0xFF, 8);
        writer.write_bits(0x0, 4);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xF0);
    }
}

TEST_CASE("BitWriter write_signed_bits", "[bit_writer]") {
    SECTION("negative one in 8 bits") {
        BitWriter writer;
        writer.write_signed_bits(-1, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0xFF);
    }

    SECTION("negative one in 16 bits") {
        BitWriter writer;
        writer.write_signed_bits(-1, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xFF);
    }

    SECTION("min value in 8 bits (-128)") {
        BitWriter writer;
        writer.write_signed_bits(-128, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0x80);
    }

    SECTION("max value in 8 bits (127)") {
        BitWriter writer;
        writer.write_signed_bits(127, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0x7F);
    }

    SECTION("zero in 4 bits") {
        BitWriter writer;
        writer.write_signed_bits(0, 4);
        writer.write_signed_bits(-1, 4);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0x0F);
    }
}

TEST_CASE("BitWriter byte-level writes", "[bit_writer]") {
    SECTION("write_u8") {
        BitWriter writer;
        writer.write_u8(0x42);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0x42);
    }

    SECTION("write_u16 big-endian") {
        BitWriter writer;
        writer.write_u16(0x0102, Endian::Big);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x01);
        CHECK(data[1] == 0x02);
    }

    SECTION("write_u16 little-endian") {
        BitWriter writer;
        writer.write_u16(0x0102, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x02);
        CHECK(data[1] == 0x01);
    }

    SECTION("write_u32 big-endian") {
        BitWriter writer;
        writer.write_u32(0x01020304, Endian::Big);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 4);
        CHECK(data[0] == 0x01);
        CHECK(data[1] == 0x02);
        CHECK(data[2] == 0x03);
        CHECK(data[3] == 0x04);
    }

    SECTION("write_u32 little-endian") {
        BitWriter writer;
        writer.write_u32(0x01020304, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 4);
        CHECK(data[0] == 0x04);
        CHECK(data[1] == 0x03);
        CHECK(data[2] == 0x02);
        CHECK(data[3] == 0x01);
    }

    SECTION("write_u64 big-endian") {
        BitWriter writer;
        writer.write_u64(0x0102030405060708ULL, Endian::Big);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 8);
        CHECK(data[0] == 0x01);
        CHECK(data[7] == 0x08);
    }
}

TEST_CASE("BitWriter write_u64 little-endian", "[bit_writer]") {
    BitWriter writer;
    writer.write_u64(0x0102030405060708ULL, Endian::Little);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 8);
    // Little-endian: least significant byte first
    CHECK(data[0] == 0x08);
    CHECK(data[1] == 0x07);
    CHECK(data[2] == 0x06);
    CHECK(data[3] == 0x05);
    CHECK(data[4] == 0x04);
    CHECK(data[5] == 0x03);
    CHECK(data[6] == 0x02);
    CHECK(data[7] == 0x01);
}

TEST_CASE("BitWriter float writes little-endian", "[bit_writer]") {
    SECTION("write_f32 little-endian round-trip") {
        float expected = 3.14f;

        BitWriter writer;
        writer.write_f32(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 4);

        BitReader reader(data);
        auto val = reader.read_f32(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(3.14f, 0.001));
    }

    SECTION("write_f32 little-endian raw bytes") {
        float expected = 1.0f;
        uint32_t raw;
        std::memcpy(&raw, &expected, 4);

        BitWriter writer;
        writer.write_f32(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 4);

        // Verify little-endian byte order
        CHECK(data[0] == static_cast<uint8_t>(raw));
        CHECK(data[1] == static_cast<uint8_t>(raw >> 8));
        CHECK(data[2] == static_cast<uint8_t>(raw >> 16));
        CHECK(data[3] == static_cast<uint8_t>(raw >> 24));
    }

    SECTION("write_f64 little-endian round-trip") {
        double expected = 2.71828;

        BitWriter writer;
        writer.write_f64(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 8);

        BitReader reader(data);
        auto val = reader.read_f64(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(2.71828, 0.00001));
    }

    SECTION("write_f64 little-endian raw bytes") {
        double expected = 1.0;
        uint64_t raw;
        std::memcpy(&raw, &expected, 8);

        BitWriter writer;
        writer.write_f64(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 8);

        // Verify little-endian byte order
        for (size_t i = 0; i < 8; ++i) {
            CHECK(data[i] == static_cast<uint8_t>(raw >> (i * 8)));
        }
    }
}

TEST_CASE("BitWriter float writes", "[bit_writer]") {
    SECTION("write_f32 round-trip") {
        float expected = 3.14f;

        BitWriter writer;
        writer.write_f32(expected, Endian::Big);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);

        BitReader reader(data);
        auto val = reader.read_f32(Endian::Big);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(3.14f, 0.001));
    }

    SECTION("write_f64 round-trip") {
        double expected = 2.71828;

        BitWriter writer;
        writer.write_f64(expected, Endian::Big);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);

        BitReader reader(data);
        auto val = reader.read_f64(Endian::Big);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(2.71828, 0.00001));
    }
}

TEST_CASE("BitWriter write_bytes", "[bit_writer]") {
    std::array<uint8_t, 3> src = {0x01, 0x02, 0x03};
    BitWriter writer;
    writer.write_bytes(src);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);

    REQUIRE(data.size() == 3);
    CHECK(data[0] == 0x01);
    CHECK(data[1] == 0x02);
    CHECK(data[2] == 0x03);
}

TEST_CASE("BitWriter write_string", "[bit_writer]") {
    SECTION("Exact length") {
        BitWriter writer;
        CHECK(writer.write_string("Hi", 2));
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 'H');
        CHECK(data[1] == 'i');
    }

    SECTION("Padded length") {
        BitWriter writer;
        CHECK(writer.write_string("Hi", 5, ' '));
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 5);
        CHECK(data[0] == 'H');
        CHECK(data[1] == 'i');
        CHECK(data[2] == ' ');
        CHECK(data[3] == ' ');
        CHECK(data[4] == ' ');
    }

    SECTION("Null-padded") {
        BitWriter writer;
        CHECK(writer.write_string("AB", 4));
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 4);
        CHECK(data[2] == 0);
        CHECK(data[3] == 0);
    }

    SECTION("Truncation when string exceeds padded_length") {
        BitWriter writer;
        CHECK_FALSE(writer.write_string("Hello, World!", 5));
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 5);
        CHECK(data[0] == 'H');
        CHECK(data[1] == 'e');
        CHECK(data[2] == 'l');
        CHECK(data[3] == 'l');
        CHECK(data[4] == 'o');
    }
}

TEST_CASE("BitWriter alignment", "[bit_writer]") {
    SECTION("align_to_byte pads with zeros") {
        BitWriter writer;
        writer.write_bits(0x7, 3);
        writer.align_to_byte();
        writer.write_u8(0xFF);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0xE0);  // 0b11100000
        CHECK(data[1] == 0xFF);
    }

    SECTION("align_to boundary") {
        BitWriter writer;
        writer.write_u8(0x01);
        writer.align_to(4);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data.size() == 4);
        CHECK(data[0] == 0x01);
        CHECK(data[1] == 0x00);
    }

    SECTION("align_to when already aligned") {
        BitWriter writer;
        writer.write_u32(0x01020304);
        writer.align_to(4);

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data.size() == 4);  // No padding needed
    }
}

TEST_CASE("BitWriter patch", "[bit_writer]") {
    SECTION("patch_u8") {
        BitWriter writer;
        writer.write_u8(0x00);
        writer.write_u8(0xFF);
        CHECK(writer.patch_u8(0, 0x42));

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data[0] == 0x42);
        CHECK(data[1] == 0xFF);
    }

    SECTION("patch_u16 big-endian") {
        BitWriter writer;
        writer.write_u16(0x0000, Endian::Big);
        writer.write_u8(0xFF);
        CHECK(writer.patch_u16(0, 0xABCD, Endian::Big));

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data[0] == 0xAB);
        CHECK(data[1] == 0xCD);
        CHECK(data[2] == 0xFF);
    }

    SECTION("patch_u16 little-endian") {
        BitWriter writer;
        writer.write_u16(0x0000, Endian::Big);
        CHECK(writer.patch_u16(0, 0xABCD, Endian::Little));

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data[0] == 0xCD);
        CHECK(data[1] == 0xAB);
    }

    SECTION("patch_u32") {
        BitWriter writer;
        writer.write_u32(0x00000000, Endian::Big);
        CHECK(writer.patch_u32(0, 0x12345678, Endian::Big));

        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        CHECK(data[0] == 0x12);
        CHECK(data[1] == 0x34);
        CHECK(data[2] == 0x56);
        CHECK(data[3] == 0x78);
    }

    SECTION("patch_u8 out of range returns false") {
        BitWriter writer;
        writer.write_u8(0x00);
        CHECK_FALSE(writer.patch_u8(5, 0x42));
    }

    SECTION("patch_u16 out of range returns false") {
        BitWriter writer;
        writer.write_u8(0x00);
        CHECK_FALSE(writer.patch_u16(0, 0xABCD, Endian::Big));
    }

    SECTION("patch_u32 out of range returns false") {
        BitWriter writer;
        writer.write_u16(0x0000, Endian::Big);
        CHECK_FALSE(writer.patch_u32(0, 0x12345678, Endian::Big));
    }
}

TEST_CASE("BitWriter size tracking", "[bit_writer]") {
    BitWriter writer;

    CHECK(writer.size_bytes() == 0);
    CHECK(writer.size_bits() == 0);
    CHECK(writer.is_byte_aligned());

    writer.write_bits(0x7, 3);
    CHECK(writer.size_bits() == 3);
    CHECK(writer.size_bytes() == 1);
    CHECK_FALSE(writer.is_byte_aligned());

    writer.align_to_byte();
    CHECK(writer.size_bits() == 8);
    CHECK(writer.is_byte_aligned());

    writer.write_u16(0x1234);
    CHECK(writer.size_bits() == 24);
    CHECK(writer.size_bytes() == 3);
}

TEST_CASE("BitWriter clear", "[bit_writer]") {
    BitWriter writer;
    writer.write_u32(0xDEADBEEF);
    CHECK(writer.size_bytes() == 4);

    writer.clear();
    CHECK(writer.size_bytes() == 0);
    CHECK(writer.size_bits() == 0);
}

TEST_CASE("BitWriter/BitReader round-trip", "[bit_writer][bit_reader]") {
    BitWriter writer;
    writer.write_bits(0x5, 4);
    writer.write_bits(0xA, 4);
    writer.write_u16(0xBEEF, Endian::Big);
    writer.write_u32(0xDEADCAFE, Endian::Little);

    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    BitReader reader(data);

    CHECK(*reader.read_bits(4) == 0x5);
    CHECK(*reader.read_bits(4) == 0xA);
    CHECK(*reader.read_u16(Endian::Big) == 0xBEEF);
    CHECK(*reader.read_u32(Endian::Little) == 0xDEADCAFE);
    CHECK(reader.at_end());
}

// ============================================================================
// Edge cases for write_bits
// ============================================================================

TEST_CASE("BitWriter write_bits count=0 is no-op", "[bit_writer]") {
    BitWriter writer;
    writer.write_bits(0xFF, 0);
    CHECK(writer.size_bits() == 0);
    CHECK(writer.size_bytes() == 0);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    CHECK(data.empty());
}

TEST_CASE("BitWriter write_bits count=64", "[bit_writer]") {
    BitWriter writer;
    writer.write_bits(0x0102030405060708ULL, 64);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 8);

    BitReader reader(data);
    auto val = reader.read_u64(Endian::Big);
    REQUIRE(val.has_value());
    CHECK(*val == 0x0102030405060708ULL);
}

TEST_CASE("BitWriter write_bits count>64 sets error", "[bit_writer]") {
    // count>64 is invalid — sets error state instead of writing
    BitWriter writer;
    writer.write_bits(0xDEADBEEFCAFEBABEULL, 72);
    CHECK(writer.has_error());
    CHECK(writer.size_bits() == 0);
}

// ============================================================================
// Aviation wire encoding writes: BCD, BCD_S, Sign-Magnitude
// ============================================================================

TEST_CASE("BitWriter write_bcd wire layout", "[bit_writer][bcd]") {
    SECTION("16-bit BCD 1234 → 0x12 0x34") {
        BitWriter writer;
        writer.write_bcd(1234, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x12);
        CHECK(data[1] == 0x34);
    }

    SECTION("8-bit BCD 42 → 0x42") {
        BitWriter writer;
        writer.write_bcd(42, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        CHECK(data[0] == 0x42);
    }

    SECTION("16-bit BCD 0 → 0x00 0x00") {
        BitWriter writer;
        writer.write_bcd(0, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x00);
        CHECK(data[1] == 0x00);
    }

    SECTION("16-bit BCD 9999 → 0x99 0x99") {
        BitWriter writer;
        writer.write_bcd(9999, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x99);
        CHECK(data[1] == 0x99);
    }
}

TEST_CASE("BitWriter write_bcd_signed wire layout", "[bit_writer][bcd]") {
    SECTION("Positive: sign=0 + BCD nibbles") {
        BitWriter writer;
        // 16-bit BCD_S: sign(1) + 15 bits (but BCD_S requires (bits-1)%4==0)
        // Use 9-bit: sign(1) + 2 BCD nibbles (8 bits) for value 0-99
        writer.write_bcd_signed(42, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        // bit 0: sign=0, bits 1-8: BCD 42 = 0x42
        // byte 0: 0_0100_010 = 0b00100010 = 0x22
        // byte 1: 0_0000000 = 0b00000000 (trailing pad)
        // Actually: 9 bits → 0 0100 0010 0 (padded)
        //   byte 0 = 0010 0001 = 0x21... let me reconsider
        // MSB first: bit0=0 (sign), then BCD 42 = 0100 0010
        // So 9 bits: 0 0100 0010
        // Byte 0: 0010 0001 = 0x21, Byte 1: 0xxx xxxx
        // Wait: MSB first packing:
        // bit0=0, bit1=0, bit2=1, bit3=0, bit4=0, bit5=0, bit6=1, bit7=0 → byte0
        // bit8=0 → byte1 (padded with zeros)
        // 9 bits MSB-first: sign=0, BCD 42 = 0100 0010
        // bits: 0 0100 0010
        // byte 0: bits[0:7] = 0_0100_001 = 0b00100001 = 0x21
        CHECK(data[0] == 0x21);
    }

    SECTION("Negative: sign=1 + BCD nibbles") {
        BitWriter writer;
        writer.write_bcd_signed(-42, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        // 9 bits MSB-first: sign=1, BCD 42 = 0100 0010
        // bits: 1 0100 0010
        // byte 0: bits[0:7] = 1_0100_001 = 0b10100001 = 0xA1
        CHECK(data[0] == 0xA1);
    }
}

TEST_CASE("BitWriter write_sign_magnitude wire layout", "[bit_writer][bnr_s]") {
    SECTION("Positive 1234 in 16-bit") {
        BitWriter writer;
        writer.write_sign_magnitude(1234, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        // sign=0, magnitude=1234 = 0x04D2
        // 16 bits: 0_000 0100 1101 0010
        // byte0 = 0000 0100 = 0x04
        // byte1 = 1101 0010 = 0xD2
        CHECK(data[0] == 0x04);
        CHECK(data[1] == 0xD2);
    }

    SECTION("Negative 1234 in 16-bit") {
        BitWriter writer;
        writer.write_sign_magnitude(-1234, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        // sign=1, magnitude=1234 = 0x04D2
        // 16 bits: 1_000 0100 1101 0010
        // byte0 = 1000 0100 = 0x84
        // byte1 = 1101 0010 = 0xD2
        CHECK(data[0] == 0x84);
        CHECK(data[1] == 0xD2);
    }

    SECTION("Zero in 16-bit") {
        BitWriter writer;
        writer.write_sign_magnitude(0, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 2);
        CHECK(data[0] == 0x00);
        CHECK(data[1] == 0x00);
    }

    SECTION("Max positive for 8-bit: 127") {
        BitWriter writer;
        writer.write_sign_magnitude(127, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        // sign=0, magnitude=127=0x7F → 0111 1111 = 0x7F
        CHECK(data[0] == 0x7F);
    }

    SECTION("Max negative for 8-bit: -127") {
        BitWriter writer;
        writer.write_sign_magnitude(-127, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);
        // sign=1, magnitude=127=0x7F → 1111 1111 = 0xFF
        CHECK(data[0] == 0xFF);
    }
}

TEST_CASE("BitWriter/BitReader aviation encoding multi-field round-trip", "[bit_writer][bit_reader][bcd][bnr_s]") {
    BitWriter writer;

    // Write mixed aviation-encoded fields
    writer.write_bcd(5678, 16);          // BCD unsigned
    writer.write_bcd_signed(-99, 9);     // BCD signed
    writer.write_sign_magnitude(-500, 16); // Sign-magnitude
    writer.write_bits(0xAB, 8);          // Normal field interleaved
    writer.align_to_byte();                // pad to byte boundary (49 → 56 bits)

    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    BitReader reader(data);

    auto bcd_val = reader.read_bcd(16);
    REQUIRE(bcd_val.has_value());
    CHECK(*bcd_val == 5678);

    auto bcds_val = reader.read_bcd_signed(9);
    REQUIRE(bcds_val.has_value());
    CHECK(*bcds_val == -99);

    auto sm_val = reader.read_sign_magnitude(16);
    REQUIRE(sm_val.has_value());
    CHECK(*sm_val == -500);

    auto normal_val = reader.read_bits(8);
    REQUIRE(normal_val.has_value());
    CHECK(*normal_val == 0xAB);

    reader.align_to(1); // skip padding bits
    CHECK(reader.at_end());
}

// ============================================================================
// Aviation wire encoding: edge cases and guard behavior
// ============================================================================

TEST_CASE("BitWriter write_bcd single digit 0-9", "[bit_writer][bcd]") {
    for (uint64_t v = 0; v <= 9; ++v) {
        BitWriter writer;
        writer.write_bcd(v, 4);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        REQUIRE(data.size() == 1);

        BitReader reader(data);
        auto val = reader.read_bcd(4);
        REQUIRE(val.has_value());
        CHECK(*val == v);
    }
}

TEST_CASE("BitWriter write_bcd max values for various widths", "[bit_writer][bcd]") {
    SECTION("4-bit BCD max = 9") {
        BitWriter writer;
        writer.write_bcd(9, 4);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(4) == 9);
    }

    SECTION("8-bit BCD max = 99") {
        BitWriter writer;
        writer.write_bcd(99, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(8) == 99);
    }

    SECTION("12-bit BCD max = 999") {
        BitWriter writer;
        writer.write_bcd(999, 12);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(12) == 999);
    }

    SECTION("20-bit BCD max = 99999") {
        BitWriter writer;
        writer.write_bcd(99999, 20);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(20) == 99999);
    }
}

TEST_CASE("BitWriter write_bcd_signed boundary values", "[bit_writer][bcd]") {
    SECTION("BCD_S 9-bit max positive = 99") {
        BitWriter writer;
        writer.write_bcd_signed(99, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(9) == 99);
    }

    SECTION("BCD_S 9-bit max negative = -99") {
        BitWriter writer;
        writer.write_bcd_signed(-99, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(9) == -99);
    }

    SECTION("BCD_S 13-bit max positive = 999") {
        BitWriter writer;
        writer.write_bcd_signed(999, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(13) == 999);
    }

    SECTION("BCD_S 13-bit max negative = -999") {
        BitWriter writer;
        writer.write_bcd_signed(-999, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(13) == -999);
    }

    SECTION("BCD_S 5-bit (minimum valid) max positive = 9") {
        BitWriter writer;
        writer.write_bcd_signed(9, 5);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(5) == 9);
    }

    SECTION("BCD_S 5-bit max negative = -9") {
        BitWriter writer;
        writer.write_bcd_signed(-9, 5);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(5) == -9);
    }
}

TEST_CASE("BitWriter write_sign_magnitude boundary values", "[bit_writer][bnr_s]") {
    SECTION("2-bit (minimum valid): max positive = 1") {
        BitWriter writer;
        writer.write_sign_magnitude(1, 2);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_sign_magnitude(2) == 1);
    }

    SECTION("2-bit: max negative = -1") {
        BitWriter writer;
        writer.write_sign_magnitude(-1, 2);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_sign_magnitude(2) == -1);
    }

    SECTION("16-bit: max positive = 32767") {
        BitWriter writer;
        writer.write_sign_magnitude(32767, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_sign_magnitude(16) == 32767);
    }

    SECTION("16-bit: max negative = -32767") {
        BitWriter writer;
        writer.write_sign_magnitude(-32767, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_sign_magnitude(16) == -32767);
    }
}

// ============================================================================
// Non-byte-aligned encoding sequences
// ============================================================================

TEST_CASE("Non-byte-aligned: 3-bit field then BCD then normal field", "[bit_writer][bcd]") {
    // Write 3 bits + 8-bit BCD + 5-bit unsigned → 16 bits total → 2 bytes
    BitWriter writer;
    writer.write_bits(0x5, 3);   // 3-bit value
    writer.write_bcd(42, 8);     // BCD 42 at non-byte-aligned position
    writer.write_bits(0x1F, 5);  // 5-bit value

    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 2);

    BitReader reader(data);
    auto v1 = reader.read_bits(3);
    REQUIRE(v1.has_value());
    CHECK(*v1 == 0x5);

    auto v2 = reader.read_bcd(8);
    REQUIRE(v2.has_value());
    CHECK(*v2 == 42);

    auto v3 = reader.read_bits(5);
    REQUIRE(v3.has_value());
    CHECK(*v3 == 0x1F);

    CHECK(reader.at_end());
}

TEST_CASE("Non-byte-aligned: 1-bit field then BCD_S then normal field", "[bit_writer][bcd]") {
    // Simulates FX bit(1) + BCD_S(13) + another field(2) = 16 bits → 2 bytes
    BitWriter writer;
    writer.write_bits(1, 1);          // FX bit
    writer.write_bcd_signed(-456, 13); // BCD_S at 1-bit offset
    writer.write_bits(0x3, 2);        // 2-bit value

    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 2);

    BitReader reader(data);
    CHECK(*reader.read_bits(1) == 1);
    CHECK(*reader.read_bcd_signed(13) == -456);
    CHECK(*reader.read_bits(2) == 0x3);
    CHECK(reader.at_end());
}

// ============================================================================
// BCD encode overflow boundary tests (T6)
// Verify that max-capacity values encode/decode correctly,
// confirming the BCD digit extraction loop handles boundaries properly.
// ============================================================================

TEST_CASE("BCD encode at capacity boundary", "[bit_writer][bcd][overflow]") {
    SECTION("8-bit BCD: max 99 encodes correctly") {
        BitWriter writer;
        writer.write_bcd(99, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(8) == 99);
    }

    SECTION("8-bit BCD: 0 encodes correctly") {
        BitWriter writer;
        writer.write_bcd(0, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(8) == 0);
    }

    SECTION("16-bit BCD: max 9999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd(9999, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(16) == 9999);
    }

    SECTION("4-bit BCD: max 9 encodes correctly") {
        BitWriter writer;
        writer.write_bcd(9, 4);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(4) == 9);
    }

    SECTION("24-bit BCD: max 999999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd(999999, 24);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd(24) == 999999);
    }
}

TEST_CASE("BCD_S encode at capacity boundary", "[bit_writer][bcd][overflow]") {
    SECTION("9-bit BCD_S: max +99 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(99, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(9) == 99);
    }

    SECTION("9-bit BCD_S: max -99 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(-99, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(9) == -99);
    }

    SECTION("13-bit BCD_S: max +999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(999, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(13) == 999);
    }

    SECTION("13-bit BCD_S: max -999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(-999, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(13) == -999);
    }

    SECTION("17-bit BCD_S: max +9999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(9999, 17);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(17) == 9999);
    }

    SECTION("17-bit BCD_S: max -9999 encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(-9999, 17);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(17) == -9999);
    }

    SECTION("BCD_S zero encodes correctly") {
        BitWriter writer;
        writer.write_bcd_signed(0, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);
        BitReader reader(data);
        CHECK(*reader.read_bcd_signed(9) == 0);
    }
}

// ============================================================================
// Error state tests
// ============================================================================

TEST_CASE("BitWriter error state: initially no error", "[bit_writer][error]") {
    BitWriter writer;
    CHECK_FALSE(writer.has_error());
}

TEST_CASE("BitWriter error state: BCD overflow sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bcd(100, 8);  // 100 > max for 2-nibble BCD (99)
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
    CHECK(writer.error().message().find("write_bcd") != std::string::npos);
}

TEST_CASE("BitWriter error state: BCD_S overflow sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bcd_signed(100, 9);  // 100 > max for 2-nibble BCD_S (99)
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("BitWriter error state: sign_magnitude overflow sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_sign_magnitude(128, 8);  // 128 > max for 7-bit magnitude (127)
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("BitWriter error state: subsequent writes are no-ops after error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bcd(100, 8);  // Sets error
    REQUIRE(writer.has_error());

    size_t bits_before = writer.size_bits();
    writer.write_u8(0xFF);       // Should be no-op
    writer.write_u16(0x1234);    // Should be no-op
    writer.write_bits(0xAB, 8);  // Should be no-op
    CHECK(writer.size_bits() == bits_before);
}

TEST_CASE("BitWriter error state: clear resets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bcd(100, 8);  // Sets error
    REQUIRE(writer.has_error());

    writer.clear();
    CHECK_FALSE(writer.has_error());
    CHECK(writer.size_bits() == 0);
}

TEST_CASE("BitWriter error state: clear_error resets only error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u8(0x42);       // Valid write
    writer.write_bcd(100, 8);   // Sets error
    REQUIRE(writer.has_error());

    writer.clear_error();
    CHECK_FALSE(writer.has_error());
    CHECK(writer.size_bits() > 0);  // Data still present
}

TEST_CASE("BitWriter error state: invalid BCD bits sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bcd(5, 6);  // 6 is not multiple of 4
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter error state: invalid BCD_S bits sets error", "[bit_writer][error]") {
    SECTION("bits < 5") {
        BitWriter writer;
        writer.write_bcd_signed(1, 3);
        CHECK(writer.has_error());
        CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
    }

    SECTION("(bits-1) not multiple of 4") {
        BitWriter writer;
        writer.write_bcd_signed(1, 7);
        CHECK(writer.has_error());
        CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
    }
}

TEST_CASE("BitWriter error state: invalid sign_magnitude bits sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_sign_magnitude(0, 1);  // bits < 2
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter error state: write_bits count > 64 sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bits(0, 65);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter finish returns error on error state", "[bit_writer]") {
    conduit::io::BitWriter writer;
    writer.write_bcd(100, 8);  // Overflow: sets error
    REQUIRE(writer.has_error());

    auto result = writer.finish();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
    // Writer is reset after finish
    CHECK(writer.size_bits() == 0);
    CHECK_FALSE(writer.has_error());
}

TEST_CASE("Non-byte-aligned: BNR_S after odd-width prefix", "[bit_writer][bnr_s]") {
    // 5-bit prefix + 16-bit sign-magnitude + 3-bit suffix = 24 bits → 3 bytes
    BitWriter writer;
    writer.write_bits(0x15, 5);
    writer.write_sign_magnitude(-789, 16);
    writer.write_bits(0x7, 3);

    auto finish_result = writer.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);
    REQUIRE(data.size() == 3);

    BitReader reader(data);
    CHECK(*reader.read_bits(5) == 0x15);
    CHECK(*reader.read_sign_magnitude(16) == -789);
    CHECK(*reader.read_bits(3) == 0x7);
    CHECK(reader.at_end());
}

TEST_CASE("BitWriter write_bits rejects count exceeding 64", "[bit_writer][error]") {
    BitWriter writer;
    // write_bits rejects count > 64 before reaching ensure_capacity
    writer.write_bits(0, 65);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

// Note: ensure_capacity overflow guard (bit_writer.cpp:402) now sets error instead
// of silently returning, but is unreachable through the public API because write_bits
// limits count to 64 and bit_pos_ cannot practically reach near SIZE_MAX.

TEST_CASE("BitWriter write_bcd rejects 17+ nibbles", "[bit_writer][bcd][error]") {
    BitWriter writer;
    // 68 bits = 17 nibbles, exceeds the 16-nibble limit
    writer.write_bcd(0, 68);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter write_bcd accepts 16 nibbles", "[bit_writer][bcd]") {
    BitWriter writer;
    // 64 bits = 16 nibbles, should be accepted
    writer.write_bcd(1234567890123456ULL, 64);
    CHECK_FALSE(writer.has_error());
}

TEST_CASE("BitWriter write_bcd_signed rejects 16+ data nibbles", "[bit_writer][bcd][error]") {
    BitWriter writer;
    // 65 bits = 16 data nibbles + 1 sign bit, exceeds the 15-nibble limit
    writer.write_bcd_signed(0, 65);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter write_bcd_signed accepts 15 data nibbles", "[bit_writer][bcd]") {
    BitWriter writer;
    // 61 bits = 15 data nibbles + 1 sign bit, should be accepted
    writer.write_bcd_signed(123456789012345LL, 61);
    CHECK_FALSE(writer.has_error());
}

// ============================================================================
// T7: write_string truncation behavior
// ============================================================================

TEST_CASE("BitWriter write_string exact fit", "[bit_writer][string]") {
    BitWriter writer;
    bool ok = writer.write_string("ABCD", 4);
    CHECK(ok);
    CHECK(writer.size_bytes() == 4);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    CHECK((*result)[0] == 'A');
    CHECK((*result)[1] == 'B');
    CHECK((*result)[2] == 'C');
    CHECK((*result)[3] == 'D');
}

TEST_CASE("BitWriter write_string truncation returns false", "[bit_writer][string]") {
    BitWriter writer;
    bool ok = writer.write_string("Hello, World!", 5);
    CHECK_FALSE(ok);  // Truncated
    CHECK(writer.size_bytes() == 5);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    CHECK((*result)[0] == 'H');
    CHECK((*result)[1] == 'e');
    CHECK((*result)[2] == 'l');
    CHECK((*result)[3] == 'l');
    CHECK((*result)[4] == 'o');
}

TEST_CASE("BitWriter write_string padding with null", "[bit_writer][string]") {
    BitWriter writer;
    bool ok = writer.write_string("AB", 6, '\0');
    CHECK(ok);
    CHECK(writer.size_bytes() == 6);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    CHECK((*result)[0] == 'A');
    CHECK((*result)[1] == 'B');
    CHECK((*result)[2] == 0);
    CHECK((*result)[3] == 0);
    CHECK((*result)[4] == 0);
    CHECK((*result)[5] == 0);
}

TEST_CASE("BitWriter write_string padding with space", "[bit_writer][string]") {
    BitWriter writer;
    bool ok = writer.write_string("Hi", 5, ' ');
    CHECK(ok);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    CHECK((*result)[0] == 'H');
    CHECK((*result)[1] == 'i');
    CHECK((*result)[2] == ' ');
    CHECK((*result)[3] == ' ');
    CHECK((*result)[4] == ' ');
}

TEST_CASE("BitWriter write_string empty string fills with padding", "[bit_writer][string]") {
    BitWriter writer;
    bool ok = writer.write_string("", 4, 'X');
    CHECK(ok);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 4);
    for (auto b : *result) {
        CHECK(b == 'X');
    }
}

TEST_CASE("BitWriter write_string zero padded_length", "[bit_writer][string]") {
    BitWriter writer;
    // Empty padded_length — no bytes written, string truncated
    bool ok = writer.write_string("ABC", 0);
    CHECK_FALSE(ok);  // Truncated (3 > 0)
    CHECK(writer.size_bytes() == 0);
}

TEST_CASE("BitWriter write_string does not set error on truncation", "[bit_writer][string]") {
    BitWriter writer;
    writer.write_string("Long string data", 4);
    // Truncation is signaled by return value, not by setting error state
    CHECK_FALSE(writer.has_error());
    auto result = writer.finish();
    REQUIRE(result.has_value());
}

TEST_CASE("BitWriter write_string after bit-level writes aligns", "[bit_writer][string]") {
    BitWriter writer;
    writer.write_bits(0x7, 3);  // 3 bits, not byte-aligned
    CHECK_FALSE(writer.is_byte_aligned());

    writer.write_string("AB", 4, '\0');
    // Should have aligned to byte before writing
    CHECK(writer.is_byte_aligned());
    CHECK(writer.size_bytes() == 5);  // 1 byte (aligned bits) + 4 string bytes
}

// ============================================================================
// T9: BitWriter error handling edge cases
// ============================================================================

TEST_CASE("BitWriter error state prevents further writes", "[bit_writer][error]") {
    BitWriter writer;
    // Trigger an error via write_bits with count > 64
    writer.write_bits(0, 65);
    REQUIRE(writer.has_error());

    // All subsequent writes should be no-ops
    size_t size_before = writer.size_bytes();
    writer.write_u8(0xFF);
    writer.write_u16(0xBEEF);
    writer.write_u32(0xDEADBEEF);
    writer.write_u64(0);
    writer.write_bits(1, 1);
    writer.write_bytes(std::array<uint8_t, 4>{1, 2, 3, 4});
    writer.write_string("hello", 5);
    writer.write_bcd(42, 8);
    writer.write_bcd_signed(-42, 9);
    writer.write_sign_magnitude(-1, 4);
    CHECK(writer.size_bytes() == size_before);
}

TEST_CASE("BitWriter finish returns error and resets", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_bits(0, 65);  // Trigger error
    REQUIRE(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);

    auto result = writer.finish();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodingFailed);

    // After finish(), writer is reset
    CHECK_FALSE(writer.has_error());
    CHECK(writer.size_bytes() == 0);

    // Can write again
    writer.write_u8(42);
    CHECK(writer.size_bytes() == 1);
}

TEST_CASE("BitWriter clear_error allows continued writing", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u8(0xAA);
    writer.write_bits(0, 65);  // Trigger error
    REQUIRE(writer.has_error());

    writer.clear_error();
    CHECK_FALSE(writer.has_error());

    // Can write again; previous data still present
    writer.write_u8(0xBB);
    CHECK_FALSE(writer.has_error());
}

TEST_CASE("BitWriter clear resets error and data", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u8(0xAA);
    writer.write_bits(0, 65);  // Trigger error
    REQUIRE(writer.has_error());

    writer.clear();
    CHECK_FALSE(writer.has_error());
    CHECK(writer.size_bytes() == 0);
    CHECK(writer.size_bits() == 0);
}

TEST_CASE("BitWriter patch out of range sets error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u8(0x00);
    writer.write_u8(0x00);
    CHECK(writer.size_bytes() == 2);

    // patch_u8 at valid offset
    CHECK(writer.patch_u8(0, 0xAA));
    CHECK_FALSE(writer.has_error());

    // patch_u8 at out-of-range offset
    CHECK_FALSE(writer.patch_u8(5, 0xFF));
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::BufferOverrun);
}

TEST_CASE("BitWriter patch_u16 out of range", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u8(0x00);
    writer.write_u8(0x00);
    // 2 bytes total — patch_u16 at offset 1 needs bytes [1,2], but only [0,1] exist
    CHECK_FALSE(writer.patch_u16(1, 0xBEEF));
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::BufferOverrun);
}

TEST_CASE("BitWriter patch_u32 out of range", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u16(0x0000);
    // 2 bytes total — patch_u32 needs 4 bytes
    CHECK_FALSE(writer.patch_u32(0, 0xDEADBEEF));
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::BufferOverrun);
}

TEST_CASE("BitWriter patch with existing error is no-op", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_u32(0);
    writer.write_bits(0, 65);  // Trigger error
    REQUIRE(writer.has_error());

    // Patches should return false without changing anything
    CHECK_FALSE(writer.patch_u8(0, 0xFF));
    CHECK_FALSE(writer.patch_u16(0, 0xBEEF));
    CHECK_FALSE(writer.patch_u32(0, 0xDEAD));
}

TEST_CASE("BitWriter write_bcd non-multiple-of-4 bits sets error", "[bit_writer][bcd][error]") {
    BitWriter writer;
    writer.write_bcd(42, 6);  // 6 is not a multiple of 4
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter write_bcd value too large for bits", "[bit_writer][bcd][error]") {
    BitWriter writer;
    // 8 bits = 2 BCD digits → max value 99; 100 should fail
    writer.write_bcd(100, 8);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("BitWriter write_sign_magnitude bits < 2 error", "[bit_writer][error]") {
    BitWriter writer;
    writer.write_sign_magnitude(0, 1);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}

TEST_CASE("BitWriter write_sign_magnitude magnitude overflow", "[bit_writer][error]") {
    BitWriter writer;
    // 4 bits = 1 sign + 3 magnitude → max magnitude 7; -8 should fail
    writer.write_sign_magnitude(-8, 4);
    CHECK(writer.has_error());
    CHECK(writer.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("BitWriter first error is sticky", "[bit_writer][error]") {
    BitWriter writer;
    // Trigger first error
    writer.write_bits(0, 65);
    REQUIRE(writer.has_error());
    auto first_msg = writer.error().message();

    // Trigger what would be a second error — should be ignored
    writer.clear_error();
    writer.write_bcd(42, 6);  // Not multiple of 4
    CHECK(writer.has_error());
    // Now has the BCD error (different from first)
    CHECK(writer.error().message() != first_msg);

    // But if we don't clear, second error doesn't overwrite
    writer.write_bcd(0, 5);  // Another bad call — should not overwrite
    CHECK(writer.error().code() == conduit::ErrorCode::EncodingFailed);
}
