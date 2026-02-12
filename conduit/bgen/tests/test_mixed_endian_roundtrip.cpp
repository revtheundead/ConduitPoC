// SPDX-License-Identifier: MIT
// Bgen tests - Mixed-endian roundtrip verification

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "mixed_endian/messages.hpp"

// ============================================================================
// Section: Mixed endian (mixed_endian fixture)
// ============================================================================

TEST_CASE("mixed-endian roundtrip", "[roundtrip][mixed_endian]") {
    mixed_endian::MixedMsg msg;
    msg.set_be16(0x1234);
    msg.set_le16(0x5678);
    msg.set_be32(0x01020304);
    msg.set_le32(0x05060708);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = mixed_endian::MixedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->be16() == 0x1234);
    CHECK(decoded->le16() == 0x5678);
    CHECK(decoded->be32() == 0x01020304);
    CHECK(decoded->le32() == 0x05060708);
}

TEST_CASE("mixed-endian wire byte order", "[roundtrip][mixed_endian][wire]") {
    mixed_endian::MixedMsg msg;
    msg.set_be16(0x1234);
    msg.set_le16(0x1234);
    msg.set_be32(0x01020304);
    msg.set_le32(0x01020304);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // be16=0x1234 at offset 0: [0x12, 0x34]
    REQUIRE(bytes.size() == 12); // 2+2+4+4
    CHECK(bytes[0] == 0x12);
    CHECK(bytes[1] == 0x34);

    // le16=0x1234 at offset 2: [0x34, 0x12]
    CHECK(bytes[2] == 0x34);
    CHECK(bytes[3] == 0x12);

    // be32=0x01020304 at offset 4: [01, 02, 03, 04]
    CHECK(bytes[4] == 0x01);
    CHECK(bytes[5] == 0x02);
    CHECK(bytes[6] == 0x03);
    CHECK(bytes[7] == 0x04);

    // le32=0x01020304 at offset 8: [04, 03, 02, 01]
    CHECK(bytes[8] == 0x04);
    CHECK(bytes[9] == 0x03);
    CHECK(bytes[10] == 0x02);
    CHECK(bytes[11] == 0x01);
}

// ============================================================================
// Section: Error-path tests
// ============================================================================

TEST_CASE("MixedMsg truncated fails decode", "[error][mixed_endian]") {
    // MixedMsg has be16+le16+be32+le32 = 12 bytes. Provide 6.
    std::vector<uint8_t> data = {0x12, 0x34, 0x56, 0x78, 0x01, 0x02};
    auto decoded = mixed_endian::MixedMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("MixedMsg empty fails decode", "[error][mixed_endian]") {
    std::vector<uint8_t> data;
    auto decoded = mixed_endian::MixedMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

// ============================================================================
// Section: Encode size verification
// ============================================================================

TEST_CASE("MixedMsg wire size is 12", "[wire][mixed_endian]") {
    mixed_endian::MixedMsg msg;
    msg.set_be16(0);
    msg.set_le16(0);
    msg.set_be32(0);
    msg.set_le32(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 12);
}
