// SPDX-License-Identifier: MIT
// Bgen tests - frame-level auto="length" arithmetic modifier tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "frame_length_arith/messages.hpp"
#include "frame_length_arith/sessions.hpp"

// ============================================================================
// ArithFrame: auto="length * 2"
// Wire value = total_frame_bytes * 2
// ============================================================================

TEST_CASE("frame length * 2 - encode roundtrip", "[frame_length_arith]") {
    frame_len_arith::DataMsg payload;
    payload.set_value(0x1234);

    auto frame = frame_len_arith::ArithFrame::wrap(payload);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = frame_len_arith::ArithFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());

    auto* msg = std::get_if<frame_len_arith::DataMsg>(&dec->payload());
    REQUIRE(msg != nullptr);
    CHECK(msg->value() == 0x1234);
}

TEST_CASE("frame length * 2 - wire bytes verification", "[frame_length_arith][wire]") {
    frame_len_arith::DataMsg payload;
    payload.set_value(0xABCD);

    auto frame = frame_len_arith::ArithFrame::wrap(payload);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;

    // Frame layout: msg-type(1) + length(2) + value(2) = 5 bytes total
    REQUIRE(bytes.size() == 5);
    CHECK(bytes[0] == 1); // msg-type = DataMsg::ID_VALUE

    // length field = total_frame_bytes * 2 = 5 * 2 = 10
    uint16_t wire_length = static_cast<uint16_t>((bytes[1] << 8) | bytes[2]);
    CHECK(wire_length == 10);
}

TEST_CASE("frame length * 2 - extract_frame_length reversal", "[frame_length_arith][session]") {
    frame_len_arith::DataMsg payload;
    payload.set_value(0x5678);

    auto frame = frame_len_arith::ArithFrame::wrap(payload);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());

    auto session = frame_len_arith::create_arith_frame_session();
    // extract_frame_length should reverse: wire_value / 2 = actual_total_bytes
    size_t extracted = session->extract_frame_length(*enc);
    CHECK(extracted == enc->size());
}
