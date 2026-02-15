// SPDX-License-Identifier: MIT
// Tests for EBCDIC string encoding roundtrip via generated code

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "ebcdic_strings/messages.hpp"

TEST_CASE("EBCDIC roundtrip: encode then decode preserves string", "[ebcdic][roundtrip]") {
    ebcdic_test::EbcdicMsg msg;
    msg.set_id(1);
    msg.set_label("HELLO");
    msg.set_trailer(0xBEEF);

    auto bytes = msg.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [id:1][label:8][trailer:2] = 11 bytes
    REQUIRE(bytes->size() == 11);

    auto decoded = ebcdic_test::EbcdicMsg::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->id() == 1);
    CHECK(decoded->label() == "HELLO");
    CHECK(decoded->trailer() == 0xBEEF);
}

TEST_CASE("EBCDIC wire format: known byte values", "[ebcdic][wire]") {
    ebcdic_test::EbcdicMsg msg;
    msg.set_id(0);
    msg.set_label("AB");
    msg.set_trailer(0);

    auto bytes = msg.encode_bytes();
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 11);

    // 'A' in EBCDIC = 0xC1, 'B' = 0xC2, space padding = 0x40
    CHECK((*bytes)[1] == 0xC1);  // 'A'
    CHECK((*bytes)[2] == 0xC2);  // 'B'
    CHECK((*bytes)[3] == 0x40);  // space padding
    CHECK((*bytes)[4] == 0x40);  // space padding
    CHECK((*bytes)[5] == 0x40);  // space padding
    CHECK((*bytes)[6] == 0x40);  // space padding
    CHECK((*bytes)[7] == 0x40);  // space padding
    CHECK((*bytes)[8] == 0x40);  // space padding
}

TEST_CASE("EBCDIC roundtrip: empty string padded with spaces", "[ebcdic][roundtrip]") {
    ebcdic_test::EbcdicMsg msg;
    msg.set_id(2);
    msg.set_label("");
    msg.set_trailer(100);

    auto bytes = msg.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = ebcdic_test::EbcdicMsg::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->id() == 2);
    CHECK(decoded->label().empty());  // trimmed spaces
    CHECK(decoded->trailer() == 100);
}

TEST_CASE("EBCDIC roundtrip: full 8-char string", "[ebcdic][roundtrip]") {
    ebcdic_test::EbcdicMsg msg;
    msg.set_id(3);
    msg.set_label("ABCDEFGH");
    msg.set_trailer(0x1234);

    auto bytes = msg.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = ebcdic_test::EbcdicMsg::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label() == "ABCDEFGH");
    CHECK(decoded->trailer() == 0x1234);
}
