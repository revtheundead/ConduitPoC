// SPDX-License-Identifier: MIT
// Conduit - BitReader Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstring>

using namespace conduit;
using namespace conduit::io;

TEST_CASE("BitReader read_bits", "[bit_reader]") {
    SECTION("Read single bits") {
        std::array<uint8_t, 1> data = {0b10110100};
        BitReader reader(data);

        CHECK(*reader.read_bits(1) == 1);
        CHECK(*reader.read_bits(1) == 0);
        CHECK(*reader.read_bits(1) == 1);
        CHECK(*reader.read_bits(1) == 1);
        CHECK(*reader.read_bits(1) == 0);
        CHECK(*reader.read_bits(1) == 1);
        CHECK(*reader.read_bits(1) == 0);
        CHECK(*reader.read_bits(1) == 0);
    }

    SECTION("Read multi-bit values") {
        std::array<uint8_t, 2> data = {0xAB, 0xCD};
        BitReader reader(data);

        CHECK(*reader.read_bits(4) == 0xA);
        CHECK(*reader.read_bits(4) == 0xB);
        CHECK(*reader.read_bits(8) == 0xCD);
    }

    SECTION("Read across byte boundary") {
        std::array<uint8_t, 2> data = {0xFF, 0x00};
        BitReader reader(data);

        CHECK(*reader.read_bits(4) == 0x0F);
        CHECK(*reader.read_bits(8) == 0xF0);
    }

    SECTION("Read 0 bits returns 0") {
        std::array<uint8_t, 1> data = {0xFF};
        BitReader reader(data);
        CHECK(*reader.read_bits(0) == 0);
    }

    SECTION("Error on insufficient data") {
        std::array<uint8_t, 1> data = {0xFF};
        BitReader reader(data);

        auto r = reader.read_bits(16);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::BufferUnderrun);
    }

    SECTION("Error on count > 64") {
        std::array<uint8_t, 16> data = {};
        BitReader reader(data);

        auto r = reader.read_bits(65);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("BitReader read_signed_bits", "[bit_reader]") {
    SECTION("Positive value") {
        std::array<uint8_t, 1> data = {0b01111111};
        BitReader reader(data);

        auto val = reader.read_signed_bits(8);
        REQUIRE(val.has_value());
        CHECK(*val == 127);
    }

    SECTION("Negative value") {
        // -1 in 8 bits = 0xFF
        std::array<uint8_t, 1> data = {0xFF};
        BitReader reader(data);

        auto val = reader.read_signed_bits(8);
        REQUIRE(val.has_value());
        CHECK(*val == -1);
    }

    SECTION("Small negative") {
        // -1 in 4 bits = 0xF
        std::array<uint8_t, 1> data = {0xF0};
        BitReader reader(data);

        auto val = reader.read_signed_bits(4);
        REQUIRE(val.has_value());
        CHECK(*val == -1);
    }

    SECTION("Zero") {
        auto val = BitReader({}).read_signed_bits(0);
        REQUIRE(val.has_value());
        CHECK(*val == 0);
    }
}

TEST_CASE("BitReader byte-level reads big-endian", "[bit_reader]") {
    SECTION("read_u8") {
        std::array<uint8_t, 1> data = {0x42};
        BitReader reader(data);

        auto val = reader.read_u8();
        REQUIRE(val.has_value());
        CHECK(*val == 0x42);
    }

    SECTION("read_u16 big-endian") {
        std::array<uint8_t, 2> data = {0x01, 0x02};
        BitReader reader(data);

        auto val = reader.read_u16(Endian::Big);
        REQUIRE(val.has_value());
        CHECK(*val == 0x0102);
    }

    SECTION("read_u16 little-endian") {
        std::array<uint8_t, 2> data = {0x01, 0x02};
        BitReader reader(data);

        auto val = reader.read_u16(Endian::Little);
        REQUIRE(val.has_value());
        CHECK(*val == 0x0201);
    }

    SECTION("read_u32 big-endian") {
        std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
        BitReader reader(data);

        auto val = reader.read_u32(Endian::Big);
        REQUIRE(val.has_value());
        CHECK(*val == 0x01020304);
    }

    SECTION("read_u32 little-endian") {
        std::array<uint8_t, 4> data = {0x04, 0x03, 0x02, 0x01};
        BitReader reader(data);

        auto val = reader.read_u32(Endian::Little);
        REQUIRE(val.has_value());
        CHECK(*val == 0x01020304);
    }

    SECTION("read_u64 big-endian") {
        std::array<uint8_t, 8> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        BitReader reader(data);

        auto val = reader.read_u64(Endian::Big);
        REQUIRE(val.has_value());
        CHECK(*val == 0x0102030405060708ULL);
    }

    SECTION("read_u64 little-endian") {
        // Little-endian: least significant byte first
        std::array<uint8_t, 8> data = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
        BitReader reader(data);

        auto val = reader.read_u64(Endian::Little);
        REQUIRE(val.has_value());
        CHECK(*val == 0x0102030405060708ULL);
    }

    SECTION("Underrun on read_u16") {
        std::array<uint8_t, 1> data = {0x01};
        BitReader reader(data);

        auto val = reader.read_u16();
        REQUIRE_FALSE(val.has_value());
        CHECK(val.error().code() == ErrorCode::BufferUnderrun);
    }
}

TEST_CASE("BitReader float reads", "[bit_reader]") {
    SECTION("read_f32") {
        float expected = 3.14f;
        std::array<uint8_t, 4> data;
        uint32_t raw;
        std::memcpy(&raw, &expected, 4);
        // Write big-endian
        data[0] = static_cast<uint8_t>(raw >> 24);
        data[1] = static_cast<uint8_t>(raw >> 16);
        data[2] = static_cast<uint8_t>(raw >> 8);
        data[3] = static_cast<uint8_t>(raw);

        BitReader reader(data);
        auto val = reader.read_f32(Endian::Big);
        REQUIRE(val.has_value());
        CHECK_THAT(static_cast<double>(*val), Catch::Matchers::WithinAbs(static_cast<double>(3.14f), 0.001));
    }

    SECTION("read_f64") {
        double expected = 2.71828;
        std::array<uint8_t, 8> data;
        uint64_t raw;
        std::memcpy(&raw, &expected, 8);
        for (size_t i = 8; i > 0; --i) {
            data[8 - i] = static_cast<uint8_t>(raw >> ((i - 1) * 8));
        }

        BitReader reader(data);
        auto val = reader.read_f64(Endian::Big);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(2.71828, 0.00001));
    }
}

TEST_CASE("BitReader float reads little-endian", "[bit_reader]") {
    SECTION("read_f32 little-endian") {
        float expected = 3.14f;
        std::array<uint8_t, 4> data;
        uint32_t raw;
        std::memcpy(&raw, &expected, 4);
        // Write little-endian (least significant byte first)
        data[0] = static_cast<uint8_t>(raw);
        data[1] = static_cast<uint8_t>(raw >> 8);
        data[2] = static_cast<uint8_t>(raw >> 16);
        data[3] = static_cast<uint8_t>(raw >> 24);

        BitReader reader(data);
        auto val = reader.read_f32(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(static_cast<double>(*val), Catch::Matchers::WithinAbs(static_cast<double>(3.14f), 0.001));
    }

    SECTION("read_f64 little-endian") {
        double expected = 2.71828;
        std::array<uint8_t, 8> data;
        uint64_t raw;
        std::memcpy(&raw, &expected, 8);
        // Write little-endian (least significant byte first)
        for (size_t i = 0; i < 8; ++i) {
            data[i] = static_cast<uint8_t>(raw >> (i * 8));
        }

        BitReader reader(data);
        auto val = reader.read_f64(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(2.71828, 0.00001));
    }

    SECTION("read_f32 little-endian roundtrip via writer") {
        float expected = -42.5f;
        BitWriter writer;
        writer.write_f32(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);

        BitReader reader(data);
        auto val = reader.read_f32(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(static_cast<double>(*val), Catch::Matchers::WithinAbs(static_cast<double>(-42.5f), 0.001));
    }

    SECTION("read_f64 little-endian roundtrip via writer") {
        double expected = -99.99;
        BitWriter writer;
        writer.write_f64(expected, Endian::Little);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto data = std::move(*result);

        BitReader reader(data);
        auto val = reader.read_f64(Endian::Little);
        REQUIRE(val.has_value());
        CHECK_THAT(*val, Catch::Matchers::WithinAbs(-99.99, 0.00001));
    }
}

TEST_CASE("BitReader read_bytes", "[bit_reader]") {
    std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
    BitReader reader(data);

    auto bytes = reader.read_bytes(3);
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 3);
    CHECK((*bytes)[0] == 0x01);
    CHECK((*bytes)[1] == 0x02);
    CHECK((*bytes)[2] == 0x03);

    CHECK(reader.remaining_bytes() == 1);
}

TEST_CASE("BitReader read_string", "[bit_reader]") {
    std::array<uint8_t, 5> data = {'H', 'e', 'l', 'l', 'o'};
    BitReader reader(data);

    auto str = reader.read_string(5);
    REQUIRE(str.has_value());
    CHECK(*str == "Hello");
}

TEST_CASE("BitReader sub_reader", "[bit_reader]") {
    std::array<uint8_t, 6> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    BitReader reader(data);

    // Create sub-reader for first 3 bytes
    auto sub = reader.sub_reader(3);
    REQUIRE(sub.has_value());

    // Sub-reader reads from its own scope
    CHECK(*sub->read_u8() == 0x01);
    CHECK(*sub->read_u8() == 0x02);
    CHECK(*sub->read_u8() == 0x03);
    CHECK(sub->at_end());

    // Parent reader advanced past those 3 bytes
    CHECK(*reader.read_u8() == 0x04);
    CHECK(reader.remaining_bytes() == 2);
}

TEST_CASE("BitReader position and state", "[bit_reader]") {
    std::array<uint8_t, 4> data = {0xFF, 0x00, 0xFF, 0x00};
    BitReader reader(data);

    CHECK(reader.remaining_bytes() == 4);
    CHECK(reader.remaining_bits() == 32);
    CHECK(reader.bit_position() == 0);
    CHECK_FALSE(reader.at_end());

    (void)reader.read_bits(4);
    CHECK(reader.bit_position() == 4);
    CHECK_FALSE(reader.is_byte_aligned());

    reader.align_to_byte();
    CHECK(reader.is_byte_aligned());
    CHECK(reader.bit_position() == 8);

    auto skip_result = reader.skip_bits(16);
    REQUIRE(skip_result.has_value());
    CHECK(reader.remaining_bytes() == 1);
}

TEST_CASE("BitReader reset", "[bit_reader]") {
    std::array<uint8_t, 2> data = {0x12, 0x34};
    BitReader reader(data);

    (void)reader.read_u8();
    CHECK(reader.remaining_bytes() == 1);

    reader.reset();
    CHECK(reader.remaining_bytes() == 2);
    CHECK(*reader.read_u8() == 0x12);
}

TEST_CASE("BitReader skip_bits returns error on overrun", "[bit_reader]") {
    std::vector<uint8_t> data = {0xFF, 0xFF};
    conduit::io::BitReader reader(data);

    // Skip within bounds succeeds
    auto ok = reader.skip_bits(8);
    REQUIRE(ok.has_value());

    // Skip beyond end returns error
    auto err = reader.skip_bits(1000);
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().code() == conduit::ErrorCode::BufferUnderrun);
    CHECK(reader.remaining_bits() == 8);  // position unchanged on error
}

TEST_CASE("BitReader: read_bytes underrun", "[bit_reader]") {
    std::array<uint8_t, 2> data = {0x01, 0x02};
    BitReader reader(data);
    auto result = reader.read_bytes(10);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("BitReader align_to(N)", "[bit_reader]") {
    SECTION("align_to(1) behaves like align_to_byte") {
        std::array<uint8_t, 4> data = {0xFF, 0x42, 0x00, 0x00};
        BitReader reader(data);

        (void)reader.read_bits(3);
        CHECK_FALSE(reader.is_byte_aligned());
        reader.align_to(1);
        CHECK(reader.is_byte_aligned());
        CHECK(reader.bit_position() == 8);
    }

    SECTION("align_to(2) from byte 1 advances to byte 2") {
        std::array<uint8_t, 4> data = {0xFF, 0x42, 0xAB, 0x00};
        BitReader reader(data);

        (void)reader.read_u8();  // now at byte 1
        CHECK(reader.bit_position() == 8);
        reader.align_to(2);
        CHECK(reader.bit_position() == 16);
        auto val = reader.read_u8();
        REQUIRE(val.has_value());
        CHECK(*val == 0xAB);
    }

    SECTION("align_to(4) from byte 1 advances to byte 4") {
        std::array<uint8_t, 8> data = {0x01, 0x02, 0x03, 0x04, 0x42, 0x00, 0x00, 0x00};
        BitReader reader(data);

        (void)reader.read_u8();  // now at byte 1
        reader.align_to(4);
        CHECK(reader.bit_position() == 32);
        auto val = reader.read_u8();
        REQUIRE(val.has_value());
        CHECK(*val == 0x42);
    }

    SECTION("align_to(4) from byte 0 stays at byte 0") {
        std::array<uint8_t, 4> data = {0x42, 0x00, 0x00, 0x00};
        BitReader reader(data);

        reader.align_to(4);
        CHECK(reader.bit_position() == 0);
        auto val = reader.read_u8();
        REQUIRE(val.has_value());
        CHECK(*val == 0x42);
    }

    SECTION("align_to(0) is a no-op") {
        std::array<uint8_t, 4> data = {0xFF, 0x42, 0x00, 0x00};
        BitReader reader(data);

        (void)reader.read_bits(3);
        size_t pos_before = reader.bit_position();
        reader.align_to(0);
        CHECK(reader.bit_position() == pos_before);
    }
}

TEST_CASE("BitReader auto-align on byte reads", "[bit_reader]") {
    // When reading bits then a byte, the byte read should align first
    std::array<uint8_t, 2> data = {0xFF, 0x42};
    BitReader reader(data);

    (void)reader.read_bits(3);  // Read 3 bits from first byte
    // read_u8 should skip remaining 5 bits and read 0x42
    auto val = reader.read_u8();
    REQUIRE(val.has_value());
    CHECK(*val == 0x42);
}

// ============================================================================
// Aviation wire encoding reads: BCD, BCD_S, Sign-Magnitude
// ============================================================================

TEST_CASE("BitReader read_bcd basic", "[bit_reader][bcd]") {
    SECTION("16-bit BCD value 1234") {
        // BCD 1234 = 0x1234 on wire (each nibble is a decimal digit)
        std::array<uint8_t, 2> data = {0x12, 0x34};
        BitReader reader(data);
        auto val = reader.read_bcd(16);
        REQUIRE(val.has_value());
        CHECK(*val == 1234);
    }

    SECTION("8-bit BCD value 99") {
        std::array<uint8_t, 1> data = {0x99};
        BitReader reader(data);
        auto val = reader.read_bcd(8);
        REQUIRE(val.has_value());
        CHECK(*val == 99);
    }

    SECTION("12-bit BCD value 456") {
        // BCD 456 = nibbles 4, 5, 6 → bits: 0100 0101 0110
        // As 12 bits from MSB: 0x456 but we need to write as bit-packed
        BitWriter writer;
        writer.write_bcd(456, 12);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_bcd(12);
        REQUIRE(val.has_value());
        CHECK(*val == 456);
    }

    SECTION("BCD zero") {
        std::array<uint8_t, 2> data = {0x00, 0x00};
        BitReader reader(data);
        auto val = reader.read_bcd(16);
        REQUIRE(val.has_value());
        CHECK(*val == 0);
    }

    SECTION("BCD max 4-digit value 9999") {
        std::array<uint8_t, 2> data = {0x99, 0x99};
        BitReader reader(data);
        auto val = reader.read_bcd(16);
        REQUIRE(val.has_value());
        CHECK(*val == 9999);
    }
}

TEST_CASE("BitReader read_bcd invalid digit", "[bit_reader][bcd]") {
    // Nibble value 0xA is invalid BCD
    std::array<uint8_t, 1> data = {0xAF};
    BitReader reader(data);
    auto val = reader.read_bcd(8);
    REQUIRE_FALSE(val.has_value());
}

TEST_CASE("BitReader read_bcd_signed", "[bit_reader][bcd]") {
    SECTION("Positive value: sign=0, BCD 123") {
        // 13-bit BCD_S: sign(1) + 3 BCD nibbles(12)
        // Positive 123: sign=0, nibbles=1,2,3
        BitWriter writer;
        writer.write_bcd_signed(123, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_bcd_signed(13);
        REQUIRE(val.has_value());
        CHECK(*val == 123);
    }

    SECTION("Negative value: sign=1, BCD 456") {
        BitWriter writer;
        writer.write_bcd_signed(-456, 13);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_bcd_signed(13);
        REQUIRE(val.has_value());
        CHECK(*val == -456);
    }

    SECTION("BCD_S zero") {
        BitWriter writer;
        writer.write_bcd_signed(0, 9);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_bcd_signed(9);
        REQUIRE(val.has_value());
        CHECK(*val == 0);
    }
}

TEST_CASE("BitReader read_sign_magnitude", "[bit_reader][bnr_s]") {
    SECTION("Positive value") {
        // 16-bit sign-magnitude: sign=0, magnitude=1234
        BitWriter writer;
        writer.write_sign_magnitude(1234, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_sign_magnitude(16);
        REQUIRE(val.has_value());
        CHECK(*val == 1234);
    }

    SECTION("Negative value") {
        BitWriter writer;
        writer.write_sign_magnitude(-5678, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_sign_magnitude(16);
        REQUIRE(val.has_value());
        CHECK(*val == -5678);
    }

    SECTION("Zero") {
        BitWriter writer;
        writer.write_sign_magnitude(0, 16);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_sign_magnitude(16);
        REQUIRE(val.has_value());
        CHECK(*val == 0);
    }

    SECTION("Max positive for 8-bit: 127") {
        BitWriter writer;
        writer.write_sign_magnitude(127, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_sign_magnitude(8);
        REQUIRE(val.has_value());
        CHECK(*val == 127);
    }

    SECTION("Max negative for 8-bit: -127") {
        BitWriter writer;
        writer.write_sign_magnitude(-127, 8);
        auto result = writer.finish();
        REQUIRE(result.has_value());
        auto bytes = std::move(*result);
        BitReader reader(bytes);
        auto val = reader.read_sign_magnitude(8);
        REQUIRE(val.has_value());
        CHECK(*val == -127);
    }
}

TEST_CASE("BitReader read_bcd_signed invalid digit", "[bit_reader][bcd]") {
    // 13-bit BCD_S: sign(1) + 3 nibbles(12)
    // Inject invalid nibble 0xA into the BCD portion
    // sign=0, nibbles = 0xA, 0x2, 0x3 → raw bits: 0 0011 0010 1010
    BitWriter writer;
    // Write raw 13 bits: sign=0, then 12 BCD bits with invalid nibble
    // BCD part = 0x32A (nibble[0]=0xA, nibble[1]=2, nibble[2]=3)
    uint64_t raw = (0ULL << 12) | 0x32A;  // sign=0, bcd_part=0x32A
    writer.write_bits(raw, 13);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto bytes = std::move(*result);
    BitReader reader(bytes);
    auto val = reader.read_bcd_signed(13);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("BitReader read_bcd underrun", "[bit_reader][bcd]") {
    // Only 1 byte available, try to read 16-bit BCD
    std::array<uint8_t, 1> data = {0x12};
    BitReader reader(data);
    auto val = reader.read_bcd(16);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::BufferUnderrun);
}

TEST_CASE("BitReader read_bcd_signed underrun", "[bit_reader][bcd]") {
    // Only 1 byte available, try to read 13-bit BCD_S
    std::array<uint8_t, 1> data = {0x00};
    BitReader reader(data);
    auto val = reader.read_bcd_signed(13);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::BufferUnderrun);
}

TEST_CASE("BitReader read_sign_magnitude underrun", "[bit_reader][bnr_s]") {
    // Only 1 byte available, try to read 16-bit sign-magnitude
    std::array<uint8_t, 1> data = {0x00};
    BitReader reader(data);
    auto val = reader.read_sign_magnitude(16);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::BufferUnderrun);
}

TEST_CASE("BitReader read_bcd invalid argument", "[bit_reader][bcd]") {
    std::array<uint8_t, 2> data = {0x00, 0x00};
    BitReader reader(data);
    // bits not a multiple of 4
    auto val = reader.read_bcd(7);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("BitReader read_bcd_signed invalid argument", "[bit_reader][bcd]") {
    std::array<uint8_t, 2> data = {0x00, 0x00};

    SECTION("bits < 5") {
        BitReader reader(data);
        auto val = reader.read_bcd_signed(4);
        REQUIRE_FALSE(val.has_value());
        CHECK(val.error().code() == ErrorCode::InvalidArgument);
    }

    SECTION("(bits-1) not multiple of 4") {
        BitReader reader(data);
        auto val = reader.read_bcd_signed(7);
        REQUIRE_FALSE(val.has_value());
        CHECK(val.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("BitReader read_sign_magnitude invalid argument", "[bit_reader][bnr_s]") {
    std::array<uint8_t, 1> data = {0x00};
    BitReader reader(data);
    // bits < 2
    auto val = reader.read_sign_magnitude(1);
    REQUIRE_FALSE(val.has_value());
    CHECK(val.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("BitWriter/BitReader BCD round-trip", "[bit_writer][bit_reader][bcd]") {
    BitWriter writer;
    writer.write_bcd(42, 8);
    writer.write_bcd(9999, 16);
    writer.write_bcd_signed(-321, 13);
    writer.write_sign_magnitude(-100, 16);
    writer.align_to_byte(); // pad to byte boundary (53 → 56 bits)

    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    BitReader reader(data);

    CHECK(*reader.read_bcd(8) == 42);
    CHECK(*reader.read_bcd(16) == 9999);
    CHECK(*reader.read_bcd_signed(13) == -321);
    CHECK(*reader.read_sign_magnitude(16) == -100);
    reader.align_to(1); // skip padding bits
    CHECK(reader.at_end());
}

// ============================================================================
// Non-byte-aligned position tracking with aviation encodings
// ============================================================================

TEST_CASE("BitReader position after non-byte-aligned BCD_S", "[bit_reader][bcd]") {
    // Write 13-bit BCD_S then 3-bit unsigned → 16 bits total
    BitWriter writer;
    writer.write_bcd_signed(123, 13);
    writer.write_bits(0x5, 3);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 2);

    BitReader reader(data);
    auto val = reader.read_bcd_signed(13);
    REQUIRE(val.has_value());
    CHECK(*val == 123);
    // Position should be exactly at bit 13
    CHECK(reader.bit_position() == 13);
    CHECK_FALSE(reader.is_byte_aligned());

    auto tail = reader.read_bits(3);
    REQUIRE(tail.has_value());
    CHECK(*tail == 0x5);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader position after non-byte-aligned BCD", "[bit_reader][bcd]") {
    // Write 12-bit BCD then 4-bit unsigned → 16 bits total
    BitWriter writer;
    writer.write_bcd(456, 12);
    writer.write_bits(0xA, 4);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 2);

    BitReader reader(data);
    auto val = reader.read_bcd(12);
    REQUIRE(val.has_value());
    CHECK(*val == 456);
    CHECK(reader.bit_position() == 12);
    CHECK_FALSE(reader.is_byte_aligned());

    auto tail = reader.read_bits(4);
    REQUIRE(tail.has_value());
    CHECK(*tail == 0xA);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader position after sign-magnitude", "[bit_reader][bnr_s]") {
    // Write 10-bit sign-magnitude then 6-bit unsigned → 16 bits total
    BitWriter writer;
    writer.write_sign_magnitude(-100, 10);
    writer.write_bits(0x3F, 6);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 2);

    BitReader reader(data);
    auto val = reader.read_sign_magnitude(10);
    REQUIRE(val.has_value());
    CHECK(*val == -100);
    CHECK(reader.bit_position() == 10);
    CHECK_FALSE(reader.is_byte_aligned());

    auto tail = reader.read_bits(6);
    REQUIRE(tail.has_value());
    CHECK(*tail == 0x3F);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader: interleaved 1-bit + BCD_S + 1-bit + normal", "[bit_reader][bcd]") {
    // Simulates real FX block scenario: FX(1) + BCD_S(13) + FX(1) + uint8(8) = 23 bits
    BitWriter writer;
    writer.write_bits(1, 1);            // FX=1
    writer.write_bcd_signed(-999, 13);  // BCD_S value at bit 1
    writer.write_bits(0, 1);            // FX=0
    writer.write_bits(0xAB, 8);         // normal field
    writer.align_to_byte();
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);

    BitReader reader(data);
    CHECK(*reader.read_bits(1) == 1);
    CHECK(reader.bit_position() == 1);

    auto bcd = reader.read_bcd_signed(13);
    REQUIRE(bcd.has_value());
    CHECK(*bcd == -999);
    CHECK(reader.bit_position() == 14);

    CHECK(*reader.read_bits(1) == 0);
    CHECK(reader.bit_position() == 15);

    auto normal = reader.read_bits(8);
    REQUIRE(normal.has_value());
    CHECK(*normal == 0xAB);
    CHECK(reader.bit_position() == 23);
}

TEST_CASE("BitReader: dense mixed encoding sequence", "[bit_reader][bcd][bnr_s]") {
    // Pack: BCD(8) + BCD_S(9) + BNR_S(10) + uint(5) = 32 bits → 4 bytes
    BitWriter writer;
    writer.write_bcd(77, 8);
    writer.write_bcd_signed(-42, 9);
    writer.write_sign_magnitude(-300, 10);
    writer.write_bits(0x15, 5);
    auto result = writer.finish();
    REQUIRE(result.has_value());
    auto data = std::move(*result);
    REQUIRE(data.size() == 4);

    BitReader reader(data);

    auto v1 = reader.read_bcd(8);
    REQUIRE(v1.has_value());
    CHECK(*v1 == 77);
    CHECK(reader.bit_position() == 8);

    auto v2 = reader.read_bcd_signed(9);
    REQUIRE(v2.has_value());
    CHECK(*v2 == -42);
    CHECK(reader.bit_position() == 17);

    auto v3 = reader.read_sign_magnitude(10);
    REQUIRE(v3.has_value());
    CHECK(*v3 == -300);
    CHECK(reader.bit_position() == 27);

    auto v4 = reader.read_bits(5);
    REQUIRE(v4.has_value());
    CHECK(*v4 == 0x15);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader read_bcd rejects 17+ nibbles", "[bit_reader][bcd][error]") {
    std::vector<uint8_t> data(9, 0);
    BitReader reader(data);
    // 68 bits = 17 nibbles, exceeds the 16-nibble limit
    auto result = reader.read_bcd(68);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::InvalidArgument);
}

TEST_CASE("BitReader read_bcd_signed rejects 16+ data nibbles", "[bit_reader][bcd][error]") {
    std::vector<uint8_t> data(9, 0);
    BitReader reader(data);
    // 65 bits = 16 data nibbles + 1 sign bit, exceeds the 15-nibble limit
    auto result = reader.read_bcd_signed(65);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::InvalidArgument);
}

// ============================================================================
// T10: BitReader error recovery and state consistency
// ============================================================================

TEST_CASE("BitReader position unchanged after read error", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xAA, 0xBB};  // 2 bytes = 16 bits
    BitReader reader(data);

    // Read 8 bits successfully
    auto v1 = reader.read_bits(8);
    REQUIRE(v1.has_value());
    CHECK(*v1 == 0xAA);
    CHECK(reader.bit_position() == 8);

    // Attempt to read 16 more bits — only 8 remain, should fail
    size_t pos_before = reader.bit_position();
    auto v2 = reader.read_bits(16);
    REQUIRE_FALSE(v2.has_value());
    CHECK(v2.error().code() == conduit::ErrorCode::BufferUnderrun);

    // Position should be unchanged after failed read
    CHECK(reader.bit_position() == pos_before);

    // Can still read the remaining valid data
    auto v3 = reader.read_bits(8);
    REQUIRE(v3.has_value());
    CHECK(*v3 == 0xBB);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader read_u16 fails gracefully with only 1 byte", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xFF};
    BitReader reader(data);

    auto result = reader.read_u16();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::BufferUnderrun);

    // Position unchanged — can still read the 1 byte
    auto byte_val = reader.read_u8();
    REQUIRE(byte_val.has_value());
    CHECK(*byte_val == 0xFF);
}

TEST_CASE("BitReader read_u32 fails gracefully with insufficient data", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0x01, 0x02};
    BitReader reader(data);

    auto result = reader.read_u32();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::BufferUnderrun);
    CHECK(reader.bit_position() == 0);
}

TEST_CASE("BitReader read_u64 fails gracefully with insufficient data", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    BitReader reader(data);

    auto result = reader.read_u64();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::BufferUnderrun);
    CHECK(reader.bit_position() == 0);
}

TEST_CASE("BitReader read_bytes fails then succeeds with smaller count", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE};
    BitReader reader(data);

    // Try to read 4 bytes — only 3 available
    auto r1 = reader.read_bytes(4);
    REQUIRE_FALSE(r1.has_value());
    CHECK(r1.error().code() == conduit::ErrorCode::BufferUnderrun);

    // Position unchanged — can read 3 bytes
    auto r2 = reader.read_bytes(3);
    REQUIRE(r2.has_value());
    CHECK(r2->size() == 3);
    CHECK((*r2)[0] == 0xDE);
    CHECK((*r2)[1] == 0xAD);
    CHECK((*r2)[2] == 0xBE);
}

TEST_CASE("BitReader read_string fails then succeeds", "[bit_reader][error]") {
    std::vector<uint8_t> data = {'H', 'i', '!'};
    BitReader reader(data);

    auto r1 = reader.read_string(5);  // Too many
    REQUIRE_FALSE(r1.has_value());

    auto r2 = reader.read_string(3);
    REQUIRE(r2.has_value());
    CHECK(*r2 == "Hi!");
}

TEST_CASE("BitReader sub_reader fails then succeeds", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    BitReader reader(data);

    // Try to create sub-reader for 5 bytes — only 4 available
    auto r1 = reader.sub_reader(5);
    REQUIRE_FALSE(r1.has_value());
    CHECK(reader.bit_position() == 0);

    // Create sub-reader for 2 bytes — should succeed
    auto r2 = reader.sub_reader(2);
    REQUIRE(r2.has_value());

    // Original reader advanced past those 2 bytes
    CHECK(reader.bit_position() == 16);

    // Sub-reader has its own 2-byte scope
    auto v = r2->read_u8();
    REQUIRE(v.has_value());
    CHECK(*v == 0x01);
}

TEST_CASE("BitReader skip_bits fails without advancing", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xFF, 0x00};
    BitReader reader(data);

    (void)reader.read_bits(8);  // Read first byte
    CHECK(reader.bit_position() == 8);

    // Try to skip 16 bits — only 8 remain
    auto r = reader.skip_bits(16);
    REQUIRE_FALSE(r.has_value());
    CHECK(reader.bit_position() == 8);

    // Can still skip 8
    auto r2 = reader.skip_bits(8);
    CHECK(r2.has_value());
    CHECK(reader.at_end());
}

TEST_CASE("BitReader reset restores to beginning", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC};
    BitReader reader(data);

    (void)reader.read_u8();
    (void)reader.read_u8();
    CHECK(reader.bit_position() == 16);

    reader.reset();
    CHECK(reader.bit_position() == 0);
    CHECK(reader.remaining_bytes() == 3);

    auto v = reader.read_u8();
    REQUIRE(v.has_value());
    CHECK(*v == 0xAA);
}

TEST_CASE("BitReader empty data returns errors for all reads", "[bit_reader][error]") {
    std::vector<uint8_t> empty;
    BitReader reader(empty);

    CHECK(reader.at_end());
    CHECK(reader.remaining_bytes() == 0);
    CHECK(reader.remaining_bits() == 0);

    CHECK_FALSE(reader.read_bits(1).has_value());
    CHECK_FALSE(reader.read_u8().has_value());
    CHECK_FALSE(reader.read_u16().has_value());
    CHECK_FALSE(reader.read_u32().has_value());
    CHECK_FALSE(reader.read_u64().has_value());
    CHECK_FALSE(reader.read_f32().has_value());
    CHECK_FALSE(reader.read_f64().has_value());
    CHECK_FALSE(reader.read_bytes(1).has_value());
    CHECK_FALSE(reader.read_string(1).has_value());
    CHECK_FALSE(reader.sub_reader(1).has_value());
}

TEST_CASE("BitReader interleaved bit and byte reads with errors", "[bit_reader][error]") {
    std::vector<uint8_t> data = {0xFF, 0xAA, 0x55};
    BitReader reader(data);

    // Read 4 bits
    auto v1 = reader.read_bits(4);
    REQUIRE(v1.has_value());
    CHECK(*v1 == 0xF);

    // Now not byte-aligned. read_u8 aligns first (skips 4 bits), then reads byte
    auto v2 = reader.read_u8();
    REQUIRE(v2.has_value());
    CHECK(*v2 == 0xAA);  // Byte at position 1

    // Only 1 byte (8 bits) remaining
    CHECK(reader.remaining_bytes() == 1);

    // read_u16 should fail (need 2 bytes)
    auto v3 = reader.read_u16();
    REQUIRE_FALSE(v3.has_value());

    // Can still read the last byte
    auto v4 = reader.read_u8();
    REQUIRE(v4.has_value());
    CHECK(*v4 == 0x55);
    CHECK(reader.at_end());
}

TEST_CASE("BitReader read_bcd with invalid digit returns error", "[bit_reader][bcd][error]") {
    // Encode a nibble > 9 (e.g., 0xFA has nibbles F and A, both invalid)
    std::vector<uint8_t> data = {0xFA};
    BitReader reader(data);

    auto result = reader.read_bcd(8);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::InvalidArgument);
}

TEST_CASE("BitReader read_bcd_signed with invalid digit returns error", "[bit_reader][bcd][error]") {
    // 9 bits: 1 sign + 2 BCD nibbles. Put invalid digit in BCD part
    // Sign=0, nibble1=0xF, nibble2=0x0 → raw = 0b0_1111_0000 = 0xF0
    // Need 9 bits from byte boundary: need 2 bytes
    BitWriter writer;
    writer.write_bits(0b011110000, 9);  // sign=0, nibble1=0xF(invalid), nibble0=0x0
    auto data_result = writer.finish();
    REQUIRE(data_result.has_value());
    auto& data = *data_result;

    BitReader reader(data);
    auto result = reader.read_bcd_signed(9);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::InvalidArgument);
}
