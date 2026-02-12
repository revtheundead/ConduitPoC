// SPDX-License-Identifier: MIT
// Bgen tests - Wire format verification
//
// Encodes known messages and compares bytes against hand-computed expected wire format.

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "all_types/messages.hpp"
#include "boundary_types/messages.hpp"
#include "string_features/messages.hpp"

// ============================================================================
// Section: Big-endian byte order verification
// ============================================================================

TEST_CASE("uint16 big-endian wire format", "[wire]") {
    all_types::AllTypesMessage msg;
    msg.set_u8(0);
    msg.set_u16(0xABCD);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // u8=0 at byte 0, u16=0xABCD at bytes 1-2
    REQUIRE(bytes.size() > 2);
    CHECK(bytes[1] == 0xAB);
    CHECK(bytes[2] == 0xCD);
}

TEST_CASE("uint32 big-endian wire format", "[wire]") {
    all_types::AllTypesMessage msg;
    msg.set_u8(0);
    msg.set_u16(0);
    msg.set_u32(0x01020304);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // u32 at bytes 3-6
    REQUIRE(bytes.size() > 6);
    CHECK(bytes[3] == 0x01);
    CHECK(bytes[4] == 0x02);
    CHECK(bytes[5] == 0x03);
    CHECK(bytes[6] == 0x04);
}

// ============================================================================
// Section: Sub-byte bit packing order
// ============================================================================

TEST_CASE("sub-byte field packing order", "[wire]") {
    boundary_types::BoundaryMsg msg;
    msg.set_flag(1);    // 1 bit: value 1
    msg.set_small(5);   // 3 bits: value 101
    msg.set_medium(99); // 7 bits: value 1100011
    // First byte: flag(1) + small(101) + medium_high(110) = 11011100 = 0xDC
    // Second byte: medium_low(0011) + byte_val(0000...) etc.
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() > 1);
    // First 11 bits: 1_101_1100011
    // Byte 0: 1101_1100 = 0xDC
    CHECK(bytes[0] == 0xDC);
    // Byte 1 starts with remaining 3 bits of medium (011) = 0110_0000 + next field bits
    CHECK((bytes[1] & 0xE0) == 0x60);
}

// ============================================================================
// Section: String padding bytes
// ============================================================================

TEST_CASE("null-padded string wire format", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Hi");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // After 2 bytes for id, name is 20 bytes starting at offset 2
    REQUIRE(bytes.size() >= 22);
    CHECK(bytes[2] == 'H');
    CHECK(bytes[3] == 'i');
    // Remaining 18 bytes should be null-padded
    for (size_t i = 4; i < 22; i++) {
        CHECK(bytes[i] == 0x00);
    }
}

TEST_CASE("space-padded string wire format", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_label().set_value("X");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // label starts after id(2) + name(20) = offset 22, length 16
    REQUIRE(bytes.size() >= 38);
    CHECK(bytes[22] == 'X');
    // Remaining 15 bytes should be space-padded
    for (size_t i = 23; i < 38; i++) {
        CHECK(bytes[i] == ' ');
    }
}
