// SPDX-License-Identifier: MIT
// Frame length offset roundtrip tests — validates auto="length - 3" offset expression

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "frame_length_offset/messages.hpp"
#include "frame_length_offset/sessions.hpp"
#include "frame_length_offset/protocol.hpp"

// ============================================================================
// OffsetFrame: auto="length - 3" means length field = total_frame_size - 3
// Wire layout: [msg_type:1][length:2][payload...]
// So length = payload_size (since header is 3 bytes and offset subtracts 3)
// ============================================================================

TEST_CASE("frame_length_offset: Ping roundtrip via frame", "[frame][roundtrip][offset]") {
    frame_len_offset::Ping msg;
    msg.set_seq(12345);

    auto frame = frame_len_offset::OffsetFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2][seq:2] = 5 bytes total
    REQUIRE(bytes->size() == 5);
    CHECK((*bytes)[0] == 1);  // msg_type = Ping::ID_VALUE

    // length = total_frame_size - 3 = 5 - 3 = 2 (payload size only)
    CHECK((*bytes)[1] == 0);  // length high byte
    CHECK((*bytes)[2] == 2);  // length low byte

    // seq = 12345 (0x3039), big-endian
    CHECK((*bytes)[3] == 0x30);
    CHECK((*bytes)[4] == 0x39);

    auto decoded = frame_len_offset::OffsetFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);
    CHECK(decoded->length() == 2);  // offset-adjusted length

    auto* payload = std::get_if<frame_len_offset::Ping>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 12345);
}

TEST_CASE("frame_length_offset: decode invalid message id returns error", "[frame][error][offset]") {
    // Construct bytes with unknown msg_type=99
    std::vector<uint8_t> data = {99, 0, 2, 0x30, 0x39};
    auto decoded = frame_len_offset::OffsetFrame::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::UnknownDiscriminator);
}

TEST_CASE("frame_length_offset: Ping message constants", "[frame][constants][offset]") {
    CHECK(frame_len_offset::Ping::TYPE_NAME == "Ping");
    CHECK(frame_len_offset::Ping::ID_VALUE == 1);
    CHECK(frame_len_offset::Ping::TYPE_ID != 0);
}

// ============================================================================
// Session integration
// ============================================================================

TEST_CASE("frame_length_offset: session decode_frame roundtrip", "[frame][session][offset]") {
    auto session = frame_len_offset::create_offset_frame_session();
    REQUIRE(session != nullptr);

    frame_len_offset::Ping msg;
    msg.set_seq(42);
    auto encoded = session->encode_wrap(frame_len_offset::Ping::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == frame_len_offset::Ping::TYPE_ID);
    CHECK(dm.type_name == "Ping");

    auto* payload = std::any_cast<frame_len_offset::Ping>(&dm.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 42);
}

TEST_CASE("frame_length_offset: session extract_frame_length reverses offset", "[frame][session][offset]") {
    auto session = frame_len_offset::create_offset_frame_session();

    frame_len_offset::Ping msg;
    msg.set_seq(100);
    auto encoded = session->encode_wrap(frame_len_offset::Ping::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // extract_frame_length should return the total frame size
    // by reversing the offset: wire_length + 3 = 2 + 3 = 5
    auto len = session->extract_frame_length(encoded->bytes);
    CHECK(len == encoded->bytes.size());
}

TEST_CASE("frame_length_offset: session metadata", "[frame][session][offset]") {
    auto session = frame_len_offset::create_offset_frame_session();

    CHECK(session->min_frame_header_size() == 3);  // msg_type:1 + length:2
    CHECK(session->sync_pattern().empty());

    auto ids = session->leaf_type_ids();
    CHECK(ids.size() == 1);
    CHECK(session->type_name(frame_len_offset::Ping::TYPE_ID) == "Ping");
}

TEST_CASE("frame_length_offset: protocol descriptor", "[frame][protocol][offset]") {
    CHECK(frame_len_offset::ProtocolDescriptor::name == "frame_len_offset");
    CHECK(frame_len_offset::ProtocolDescriptor::types.size() == 1);
}

// ============================================================================
// Direct Ping encode/decode (without frame wrapper)
// ============================================================================

TEST_CASE("frame_length_offset: Ping standalone roundtrip", "[frame][roundtrip][offset]") {
    frame_len_offset::Ping msg;
    msg.set_seq(0xFFFF);

    auto bytes = msg.encode_bytes();
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 2);

    auto decoded = frame_len_offset::Ping::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->seq() == 0xFFFF);
}
