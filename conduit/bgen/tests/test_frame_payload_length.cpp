// SPDX-License-Identifier: MIT
// Bgen tests - Frame auto="length(payload)" roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "frame_payload_length/messages.hpp"

TEST_CASE("frame payload length - SimpleData roundtrip", "[frame_payload_length]") {
    frame_payload_length::SimpleData sd;
    sd.set_a(0x42);
    sd.set_b(0x1234);

    auto frame = frame_payload_length::PayloadFrame::wrap(sd);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_payload_length::PayloadFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    // SimpleData: a(1) + b(2) = 3 bytes payload
    CHECK(dec->payload_len() == 3);
    auto& payload = std::get<frame_payload_length::SimpleData>(dec->payload());
    CHECK(payload.a() == 0x42);
    CHECK(payload.b() == 0x1234);
}

TEST_CASE("frame payload length - LargeData roundtrip", "[frame_payload_length]") {
    frame_payload_length::LargeData ld;
    ld.set_x(0x1111);
    ld.set_y(0x2222);
    ld.set_z(0x3333);

    auto frame = frame_payload_length::PayloadFrame::wrap(ld);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_payload_length::PayloadFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    // LargeData: x(2) + y(2) + z(2) = 6 bytes payload
    CHECK(dec->payload_len() == 6);
    auto& payload = std::get<frame_payload_length::LargeData>(dec->payload());
    CHECK(payload.x() == 0x1111);
    CHECK(payload.y() == 0x2222);
    CHECK(payload.z() == 0x3333);
}

TEST_CASE("frame payload length - wire bytes verification", "[frame_payload_length][wire]") {
    frame_payload_length::SimpleData sd;
    sd.set_a(0xFF);
    sd.set_b(0x0001);

    auto frame = frame_payload_length::PayloadFrame::wrap(sd);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;

    // Header: msg-type(1) + payload-len(2) = 3 bytes
    // Payload: a(1) + b(2) = 3 bytes
    // Total: 6 bytes
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[0] == frame_payload_length::SimpleData::ID_VALUE); // msg-type
    CHECK(bytes[1] == 0x00); // payload-len high = 3
    CHECK(bytes[2] == 0x03); // payload-len low
    CHECK(bytes[3] == 0xFF); // a
    CHECK(bytes[4] == 0x00); // b high
    CHECK(bytes[5] == 0x01); // b low
}

TEST_CASE("frame payload length - different message types preserve length", "[frame_payload_length]") {
    // SimpleData = 3 bytes payload
    frame_payload_length::SimpleData sd;
    sd.set_a(1);
    sd.set_b(2);
    auto f1 = frame_payload_length::PayloadFrame::wrap(sd);
    auto e1 = f1.encode_bytes();
    REQUIRE(e1.has_value());
    auto d1 = frame_payload_length::PayloadFrame::decode_bytes(*e1);
    REQUIRE(d1.has_value());
    CHECK(d1->payload_len() == 3);

    // LargeData = 6 bytes payload
    frame_payload_length::LargeData ld;
    ld.set_x(1);
    ld.set_y(2);
    ld.set_z(3);
    auto f2 = frame_payload_length::PayloadFrame::wrap(ld);
    auto e2 = f2.encode_bytes();
    REQUIRE(e2.has_value());
    auto d2 = frame_payload_length::PayloadFrame::decode_bytes(*e2);
    REQUIRE(d2.has_value());
    CHECK(d2->payload_len() == 6);
}
