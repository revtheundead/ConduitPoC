// SPDX-License-Identifier: MIT
// Bgen tests - Frame <payload length-from="expr"/> roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "frame_payload_length_from/messages.hpp"

// ============================================================================
// Roundtrip tests for <payload length-from="body-size"/>
// ============================================================================

TEST_CASE("payload length-from - Ping roundtrip", "[frame_payload_length_from]") {
    frame_payload_length_from::Ping ping;
    ping.set_seq(0x1234);

    auto frame = frame_payload_length_from::ExprFrame::wrap(ping);
    // Set body-size to match the payload size (2 bytes for seq)
    frame.set_body_size(2);

    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_payload_length_from::ExprFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->body_size() == 2);
    auto& payload = std::get<frame_payload_length_from::Ping>(dec->payload());
    CHECK(payload.seq() == 0x1234);
}

TEST_CASE("payload length-from - Data roundtrip", "[frame_payload_length_from]") {
    frame_payload_length_from::Data data;
    data.set_x(0xAA);
    data.set_y(0xBB);
    data.set_z(0xCC);

    auto frame = frame_payload_length_from::ExprFrame::wrap(data);
    // Set body-size to match the payload size (3 bytes for x+y+z)
    frame.set_body_size(3);

    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_payload_length_from::ExprFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->body_size() == 3);
    auto& payload = std::get<frame_payload_length_from::Data>(dec->payload());
    CHECK(payload.x() == 0xAA);
    CHECK(payload.y() == 0xBB);
    CHECK(payload.z() == 0xCC);
}

TEST_CASE("payload length-from - wire bytes verification", "[frame_payload_length_from][wire]") {
    frame_payload_length_from::Ping ping;
    ping.set_seq(0x0102);

    auto frame = frame_payload_length_from::ExprFrame::wrap(ping);
    frame.set_body_size(2);

    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;

    // Header: msg-type(1) + body-size(2) = 3 bytes
    // Payload: seq(2) = 2 bytes
    // Total: 5 bytes
    REQUIRE(bytes.size() == 5);
    CHECK(bytes[0] == 1);    // msg-type = Ping ID
    CHECK(bytes[1] == 0x00); // body-size high
    CHECK(bytes[2] == 0x02); // body-size low
    CHECK(bytes[3] == 0x01); // seq high
    CHECK(bytes[4] == 0x02); // seq low
}

TEST_CASE("payload length-from - decode with trailing data", "[frame_payload_length_from]") {
    // Build wire data with extra trailing bytes beyond body-size
    // The sub_reader should limit the payload read to body-size bytes
    conduit::io::BitWriter w;
    w.write_u8(1);           // msg-type = Ping
    w.write_u16(2, conduit::io::Endian::Big); // body-size = 2
    w.write_u16(0x5678, conduit::io::Endian::Big); // seq
    w.write_u8(0xFF);        // trailing garbage byte
    auto encoded = w.finish();
    REQUIRE(encoded.has_value());

    conduit::io::BitReader r(*encoded);
    auto dec = frame_payload_length_from::ExprFrame::decode(r);
    REQUIRE(dec.has_value());
    auto& payload = std::get<frame_payload_length_from::Ping>(dec->payload());
    CHECK(payload.seq() == 0x5678);
    // The trailing byte should remain unread
    CHECK(r.remaining_bytes() == 1);
}
