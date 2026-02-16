// SPDX-License-Identifier: MIT
// Bgen tests - auto="length(field)" arithmetic modifier tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "length_arith/messages.hpp"

// ============================================================================
// HalfLenMsg: auto="length(data) / 2"
// Wire value = data_bytes / 2
// ============================================================================

TEST_CASE("length arith / 2 - roundtrip with 4-byte data", "[length_arith]") {
    length_arith::HalfLenMsg msg;
    msg.set_tag(0x42);
    msg.mutable_data() = {0x01, 0x02, 0x03, 0x04};
    msg.set_suffix(0xFF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    // Verify wire: half-len field should contain data_len / 2 = 4 / 2 = 2
    auto& bytes = *enc;
    CHECK(bytes[1] == 2); // half-len field
}

TEST_CASE("length arith / 2 - wire bytes", "[length_arith][wire]") {
    length_arith::HalfLenMsg msg;
    msg.set_tag(0x10);
    msg.mutable_data() = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    msg.set_suffix(0x99);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // tag(1) + half-len(1) + data(6) + suffix(1) = 9 bytes
    REQUIRE(bytes.size() == 9);
    CHECK(bytes[0] == 0x10); // tag
    CHECK(bytes[1] == 3);    // half-len = 6/2 = 3
    CHECK(bytes[2] == 0xAA); // data[0]
    CHECK(bytes[8] == 0x99); // suffix
}

// ============================================================================
// OffsetLenMsg: auto="length(data) + 1"
// Wire value = data_bytes + 1
// ============================================================================

TEST_CASE("length arith + 1 - roundtrip", "[length_arith]") {
    length_arith::OffsetLenMsg msg;
    msg.set_tag(0x01);
    msg.mutable_data() = {0xDE, 0xAD, 0xBE};
    msg.set_suffix(0xEF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // len-plus-one should be data_len + 1 = 3 + 1 = 4
    CHECK(bytes[1] == 4);
}

// ============================================================================
// DoubleLenMsg: auto="length(data) * 2"
// Wire value = data_bytes * 2
// ============================================================================

TEST_CASE("length arith * 2 - roundtrip", "[length_arith]") {
    length_arith::DoubleLenMsg msg;
    msg.set_tag(0x77);
    msg.mutable_data() = {0x01, 0x02, 0x03};
    msg.set_suffix(0x88);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // double-len should be data_len * 2 = 3 * 2 = 6
    CHECK(bytes[1] == 6);
}

// ============================================================================
// FieldOpMsg: auto="length(data) - overhead"
// Wire value = data_bytes - overhead_field_value
// ============================================================================

TEST_CASE("length arith with field operand", "[length_arith]") {
    length_arith::FieldOpMsg msg;
    msg.set_overhead(2);
    msg.mutable_data() = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // overhead(1) + adjusted-len(1) + data(5) = 7 bytes
    REQUIRE(bytes.size() == 7);
    CHECK(bytes[0] == 2);    // overhead
    // adjusted-len = data_len - overhead = 5 - 2 = 3
    CHECK(bytes[1] == 3);
}

TEST_CASE("length arith with field operand = 0", "[length_arith]") {
    length_arith::FieldOpMsg msg;
    msg.set_overhead(0);
    msg.mutable_data() = {0xAA, 0xBB};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // adjusted-len = data_len - 0 = 2
    CHECK(bytes[1] == 2);
}

// ============================================================================
// Decode roundtrip tests — length-from carries the inverse arithmetic
// ============================================================================

TEST_CASE("length arith / 2 - full decode roundtrip", "[length_arith][decode]") {
    length_arith::HalfLenMsg msg;
    msg.set_tag(0x42);
    msg.mutable_data() = {0x01, 0x02, 0x03, 0x04};
    msg.set_suffix(0xFF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::HalfLenMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 0x42);
    CHECK(dec->data() == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});
    CHECK(dec->suffix() == 0xFF);
}

TEST_CASE("length arith / 2 - decode 6-byte data", "[length_arith][decode]") {
    length_arith::HalfLenMsg msg;
    msg.set_tag(0x10);
    msg.mutable_data() = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    msg.set_suffix(0x99);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::HalfLenMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->data().size() == 6);
    CHECK(dec->data()[0] == 0xAA);
    CHECK(dec->data()[5] == 0xFF);
    CHECK(dec->suffix() == 0x99);
}

TEST_CASE("length arith + 1 - full decode roundtrip", "[length_arith][decode]") {
    length_arith::OffsetLenMsg msg;
    msg.set_tag(0x01);
    msg.mutable_data() = {0xDE, 0xAD, 0xBE};
    msg.set_suffix(0xEF);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::OffsetLenMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 0x01);
    CHECK(dec->data() == std::vector<uint8_t>{0xDE, 0xAD, 0xBE});
    CHECK(dec->suffix() == 0xEF);
}

TEST_CASE("length arith * 2 - full decode roundtrip", "[length_arith][decode]") {
    length_arith::DoubleLenMsg msg;
    msg.set_tag(0x77);
    msg.mutable_data() = {0x01, 0x02, 0x03};
    msg.set_suffix(0x88);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::DoubleLenMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 0x77);
    CHECK(dec->data() == std::vector<uint8_t>{0x01, 0x02, 0x03});
    CHECK(dec->suffix() == 0x88);
}

TEST_CASE("length arith with field operand - full decode roundtrip", "[length_arith][decode]") {
    length_arith::FieldOpMsg msg;
    msg.set_overhead(2);
    msg.mutable_data() = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::FieldOpMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->overhead() == 2);
    CHECK(dec->data() == std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05});
}

TEST_CASE("length arith with field operand = 0 - decode roundtrip", "[length_arith][decode]") {
    length_arith::FieldOpMsg msg;
    msg.set_overhead(0);
    msg.mutable_data() = {0xAA, 0xBB};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = length_arith::FieldOpMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->overhead() == 0);
    CHECK(dec->data() == std::vector<uint8_t>{0xAA, 0xBB});
}
