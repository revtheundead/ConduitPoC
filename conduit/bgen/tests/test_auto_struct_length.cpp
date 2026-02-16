// SPDX-License-Identifier: MIT
// Bgen tests - auto="length(field)" struct-level tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "auto_struct_length/messages.hpp"

TEST_CASE("auto length(field) - TLV message roundtrip", "[auto_struct_length]") {
    auto_struct_length::TlvMsg msg;
    msg.set_tag(0x42);
    msg.mutable_data() = {0x01, 0x02, 0x03, 0x04, 0x05};
    msg.set_suffix(0xFF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = auto_struct_length::TlvMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 0x42);
    CHECK(dec->len() == 5);
    REQUIRE(dec->data().size() == 5);
    CHECK(dec->data()[0] == 0x01);
    CHECK(dec->data()[4] == 0x05);
    CHECK(dec->suffix() == 0xFF);
}

TEST_CASE("auto length(field) - empty data gives len=0", "[auto_struct_length]") {
    auto_struct_length::TlvMsg msg;
    msg.set_tag(0x01);
    // data is empty
    msg.set_suffix(0xAA);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = auto_struct_length::TlvMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->len() == 0);
    CHECK(dec->data().empty());
    CHECK(dec->suffix() == 0xAA);
}

TEST_CASE("auto length(field) - wire bytes verification", "[auto_struct_length][wire]") {
    auto_struct_length::TlvMsg msg;
    msg.set_tag(0x10);
    msg.mutable_data() = {0xAB, 0xCD};
    msg.set_suffix(0xEF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // tag(1) + len(1) + data(2) + suffix(1) = 5 bytes
    REQUIRE(bytes.size() == 5);
    CHECK(bytes[0] == 0x10); // tag
    CHECK(bytes[1] == 0x02); // len = 2
    CHECK(bytes[2] == 0xAB); // data[0]
    CHECK(bytes[3] == 0xCD); // data[1]
    CHECK(bytes[4] == 0xEF); // suffix
}
