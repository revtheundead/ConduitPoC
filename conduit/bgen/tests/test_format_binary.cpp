// SPDX-License-Identifier: MIT
// Bgen tests - format="binary" display in to_string()

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <string>

#include "format_binary/messages.hpp"

// ============================================================================
// Roundtrip tests
// ============================================================================

TEST_CASE("format binary - basic roundtrip", "[format_binary][roundtrip]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0b10101010);
    msg.set_mask(0b1100);
    msg.set_tag(5);
    msg.set_value(0x1234);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = format_binary::BinaryMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->flags() == 0b10101010);
    CHECK(dec->mask() == 0b1100);
    CHECK(dec->tag() == 5);
    CHECK(dec->value() == 0x1234);
}

// ============================================================================
// to_string() binary format tests
// ============================================================================

TEST_CASE("format binary - inline field to_string shows 0b prefix", "[format_binary][to_string]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0);
    msg.set_mask(0b1010);
    msg.set_tag(3);
    msg.set_value(100);

    auto str = msg.to_string();
    // Inline field mask (4-bit, format="binary") should display as 0b1010
    CHECK(str.find("mask=0b1010") != std::string::npos);
}

TEST_CASE("format binary - zero value to_string", "[format_binary][to_string]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0);
    msg.set_mask(0);
    msg.set_tag(0);
    msg.set_value(0);

    auto str = msg.to_string();
    // mask=0 should display as 0b0000 (4-bit binary)
    CHECK(str.find("mask=0b0000") != std::string::npos);
}

TEST_CASE("format binary - max value to_string", "[format_binary][to_string]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0);
    msg.set_mask(0b1111);
    msg.set_tag(0);
    msg.set_value(0);

    auto str = msg.to_string();
    // mask=15 should display as 0b1111 (4-bit binary)
    CHECK(str.find("mask=0b1111") != std::string::npos);
}

TEST_CASE("format binary - type-level format in to_string", "[format_binary][to_string]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0b10101010);
    msg.set_mask(0);
    msg.set_tag(0);
    msg.set_value(0);

    auto str = msg.to_string();
    // flags field uses type bin8 (format="binary", 8-bit) -> should display as 0b10101010
    CHECK(str.find("flags=0b10101010") != std::string::npos);
}

TEST_CASE("format binary - non-binary field not affected", "[format_binary][to_string]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0);
    msg.set_mask(0);
    msg.set_tag(7);
    msg.set_value(42);

    auto str = msg.to_string();
    // tag field has no format, should display as decimal
    CHECK(str.find("tag=7") != std::string::npos);
    // value field has no format, should display as decimal
    CHECK(str.find("value=42") != std::string::npos);
    // Neither should have 0b prefix
    CHECK(str.find("tag=0b") == std::string::npos);
    CHECK(str.find("value=0b") == std::string::npos);
}

TEST_CASE("format binary - roundtrip then to_string", "[format_binary][to_string][roundtrip]") {
    format_binary::BinaryMsg msg;
    msg.set_flags(0);
    msg.set_mask(0b0110);
    msg.set_tag(1);
    msg.set_value(256);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = format_binary::BinaryMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());

    auto str = dec->to_string();
    CHECK(str.find("mask=0b0110") != std::string::npos);
    CHECK(str.find("tag=1") != std::string::npos);
    CHECK(str.find("value=256") != std::string::npos);
}
