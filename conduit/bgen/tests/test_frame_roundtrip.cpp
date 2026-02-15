// SPDX-License-Identifier: MIT
// Frame roundtrip tests — validates generated frame classes encode/decode correctly

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "frame_basic/messages.hpp"
#include "frame_basic/sessions.hpp"
#include "frame_basic/protocol.hpp"
#include "frame_config/messages.hpp"
#include "frame_config/sessions.hpp"
#include "frame_config/protocol.hpp"
#include "frame_footer/messages.hpp"
#include "frame_footer/sessions.hpp"
#include "frame_direction/messages.hpp"
#include "frame_direction/sessions.hpp"
#include "frame_array/messages.hpp"
#include "frame_array/sessions.hpp"

// ============================================================================
// frame_basic: SimpleFrame with auto="id" and auto="length"
// ============================================================================

TEST_CASE("frame_basic: Heartbeat roundtrip via frame", "[frame][roundtrip]") {
    frame_basic::Heartbeat msg;
    msg.set_timestamp(12345);

    auto frame = frame_basic::SimpleFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Verify wire layout: [msg_type:1][length:2][timestamp:2] = 5 bytes total
    REQUIRE(bytes->size() == 5);
    CHECK((*bytes)[0] == 1);  // msg_type = 1
    // length = 5 (total frame size), big-endian
    CHECK((*bytes)[1] == 0);
    CHECK((*bytes)[2] == 5);
    // timestamp = 12345 (0x3039), big-endian
    CHECK((*bytes)[3] == 0x30);
    CHECK((*bytes)[4] == 0x39);

    auto decoded = frame_basic::SimpleFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);
    CHECK(decoded->length() == 5);

    auto* payload = std::get_if<frame_basic::Heartbeat>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->timestamp() == 12345);
}

TEST_CASE("frame_basic: Status roundtrip via frame", "[frame][roundtrip]") {
    frame_basic::Status msg;
    msg.set_code(42);
    msg.set_detail(9999);

    auto frame = frame_basic::SimpleFrame::wrap(msg);
    CHECK(frame.msg_type() == 2);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // [msg_type:1][length:2][code:1][detail:2] = 6 bytes
    REQUIRE(bytes->size() == 6);
    CHECK((*bytes)[0] == 2);  // msg_type = 2

    auto decoded = frame_basic::SimpleFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 2);
    CHECK(decoded->length() == 6);

    auto* payload = std::get_if<frame_basic::Status>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->code() == 42);
    CHECK(payload->detail() == 9999);
}

TEST_CASE("frame_basic: decode invalid message id returns error", "[frame][error]") {
    // Construct bytes with unknown msg_type=99
    std::vector<uint8_t> data = {99, 0, 5, 0x30, 0x39};
    auto decoded = frame_basic::SimpleFrame::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::UnknownDiscriminator);
}

TEST_CASE("frame_basic: message TYPE_ID and TYPE_NAME", "[frame][constants]") {
    CHECK(frame_basic::Heartbeat::TYPE_NAME == "Heartbeat");
    CHECK(frame_basic::Status::TYPE_NAME == "Status");
    CHECK(frame_basic::Heartbeat::TYPE_ID != 0);
    CHECK(frame_basic::Status::TYPE_ID != 0);
    CHECK(frame_basic::Heartbeat::TYPE_ID != frame_basic::Status::TYPE_ID);
}

TEST_CASE("frame_basic: message ID_VALUE constants", "[frame][constants]") {
    CHECK(frame_basic::Heartbeat::ID_VALUE == 1);
    CHECK(frame_basic::Status::ID_VALUE == 2);
}

// ============================================================================
// frame_basic: Session tests
// ============================================================================

TEST_CASE("frame_basic: session decode_frame roundtrip", "[frame][session]") {
    auto session = frame_basic::create_simple_frame_session();
    REQUIRE(session != nullptr);

    // Encode a Heartbeat via the session
    frame_basic::Heartbeat msg;
    msg.set_timestamp(42);
    auto encoded = session->encode_wrap(frame_basic::Heartbeat::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Decode via session
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == frame_basic::Heartbeat::TYPE_ID);
    CHECK(dm.type_name == "Heartbeat");

    auto* payload = std::any_cast<frame_basic::Heartbeat>(&dm.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->timestamp() == 42);
}

TEST_CASE("frame_basic: session metadata", "[frame][session]") {
    auto session = frame_basic::create_simple_frame_session();

    CHECK(session->min_frame_header_size() == 3);
    CHECK(session->sync_pattern().empty());

    auto ids = session->leaf_type_ids();
    CHECK(ids.size() == 2);
    CHECK(session->type_name(frame_basic::Heartbeat::TYPE_ID) == "Heartbeat");
    CHECK(session->type_name(frame_basic::Status::TYPE_ID) == "Status");
}

TEST_CASE("frame_basic: session extract_frame_length", "[frame][session]") {
    auto session = frame_basic::create_simple_frame_session();

    // Encode a Status to get real frame bytes
    frame_basic::Status msg;
    msg.set_code(1);
    msg.set_detail(2);
    auto encoded = session->encode_wrap(frame_basic::Status::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Extract length from partial header
    auto len = session->extract_frame_length(*encoded);
    CHECK(len == encoded->size());
}

TEST_CASE("frame_basic: protocol descriptor", "[frame][protocol]") {
    CHECK(frame_basic::ProtocolDescriptor::name == "frame_basic");
    CHECK(frame_basic::ProtocolDescriptor::types.size() == 2);
}

// ============================================================================
// frame_config: ConfigFrame with auto="config(system-id)"
// ============================================================================

TEST_CASE("frame_config: config field set during encode", "[frame][config]") {
    frame_config::ConfigFrameSession::Config config;
    config.system_id = 7;
    auto session = frame_config::create_config_frame_session(config);
    REQUIRE(session != nullptr);

    frame_config::Ping msg;
    msg.set_seq(100);
    auto encoded = session->encode_wrap(frame_config::Ping::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Verify the config field is in the wire data
    // Wire: [system-id:1][msg-type:1][length:2][seq:2] = 6 bytes
    REQUIRE(encoded->size() == 6);
    CHECK((*encoded)[0] == 7);   // system-id from config
    CHECK((*encoded)[1] == 1);   // msg-type = Ping::ID_VALUE

    // Decode and verify
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == frame_config::Ping::TYPE_ID);
    auto* payload = std::any_cast<frame_config::Ping>(&dm.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 100);
}

TEST_CASE("frame_config: Pong roundtrip with config", "[frame][config]") {
    frame_config::ConfigFrameSession::Config config;
    config.system_id = 42;
    auto session = frame_config::create_config_frame_session(config);

    frame_config::Pong msg;
    msg.set_seq(200);
    auto encoded = session->encode_wrap(frame_config::Pong::TYPE_ID, msg);
    REQUIRE(encoded.has_value());
    CHECK((*encoded)[0] == 42);  // system-id from config
    CHECK((*encoded)[1] == 2);   // msg-type = Pong::ID_VALUE

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    CHECK(decoded->at(0).type_id == frame_config::Pong::TYPE_ID);
}

TEST_CASE("frame_config: direct frame encode/decode", "[frame][roundtrip]") {
    frame_config::Ping msg;
    msg.set_seq(300);

    auto frame = frame_config::ConfigFrame::wrap(msg);
    frame.set_system_id(10);
    CHECK(frame.msg_type() == 1);
    CHECK(frame.system_id() == 10);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 6);

    auto decoded = frame_config::ConfigFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->system_id() == 10);
    CHECK(decoded->msg_type() == 1);
    CHECK(decoded->length() == 6);

    auto* payload = std::get_if<frame_config::Ping>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 300);
}

// ============================================================================
// frame_footer: FooterFrame with checksum footer field
// ============================================================================

TEST_CASE("frame_footer: Data roundtrip with footer", "[frame][roundtrip][footer]") {
    frame_footer::Data msg;
    msg.set_value(0xABCD);

    auto frame = frame_footer::FooterFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);
    frame.set_checksum(0x42);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2][value:2][checksum:1] = 6 bytes
    REQUIRE(bytes->size() == 6);
    CHECK((*bytes)[0] == 1);          // msg_type
    CHECK((*bytes)[5] == 0x42);       // checksum (footer)

    auto decoded = frame_footer::FooterFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);
    CHECK(decoded->length() == 6);
    CHECK(decoded->checksum() == 0x42);

    auto* p = std::get_if<frame_footer::Data>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->value() == 0xABCD);
}

TEST_CASE("frame_footer: Ack roundtrip with footer", "[frame][roundtrip][footer]") {
    frame_footer::Ack msg;
    msg.set_seq(99);

    auto frame = frame_footer::FooterFrame::wrap(msg);
    frame.set_checksum(0xFF);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2][seq:1][checksum:1] = 5 bytes
    REQUIRE(bytes->size() == 5);

    auto decoded = frame_footer::FooterFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->checksum() == 0xFF);

    auto* p = std::get_if<frame_footer::Ack>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->seq() == 99);
}

TEST_CASE("frame_footer: session roundtrip", "[frame][session][footer]") {
    auto session = frame_footer::create_footer_frame_session();
    REQUIRE(session != nullptr);

    frame_footer::Data msg;
    msg.set_value(12345);
    auto encoded = session->encode_wrap(frame_footer::Data::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    CHECK(decoded->at(0).type_id == frame_footer::Data::TYPE_ID);
    CHECK(decoded->at(0).type_name == "Data");
}

TEST_CASE("frame_footer: truncated data returns error", "[frame][error][footer]") {
    // Build valid bytes, then truncate
    frame_footer::Data msg;
    msg.set_value(1);
    auto frame = frame_footer::FooterFrame::wrap(msg);
    frame.set_checksum(0);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Remove the last byte (footer) — should fail decode
    std::vector<uint8_t> truncated(bytes->begin(), bytes->end() - 1);
    auto decoded = frame_footer::FooterFrame::decode_bytes(truncated);
    // Decode should still succeed if the reader can read the footer from remaining bytes
    // (single payload doesn't use sub_reader, footer reads from main reader after payload)
    // The length field says 6 but we only have 5 bytes — reader should fail
    CHECK(!decoded.has_value());
}

// ============================================================================
// frame_direction: DirFrame with direction-qualified messages
// ============================================================================

TEST_CASE("frame_direction: CommandResponse roundtrip (receive direction)", "[frame][roundtrip][direction]") {
    frame_direction::CommandResponse msg;
    msg.set_status(1);
    msg.set_detail(500);

    auto frame = frame_direction::DirFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);  // ID_VALUE = 1

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_direction::DirFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    // Decode should produce CommandResponse (receive direction), not CommandRequest
    auto* p = std::get_if<frame_direction::CommandResponse>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->status() == 1);
    CHECK(p->detail() == 500);
}

TEST_CASE("frame_direction: CommandRequest encode then decode as receive type fails", "[frame][roundtrip][direction]") {
    frame_direction::CommandRequest msg;
    msg.set_cmd(42);

    auto frame = frame_direction::DirFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);  // Same ID_VALUE = 1 as CommandResponse

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Decoding a send-direction frame: id=1 dispatches to CommandResponse (receive type).
    // CommandRequest has [cmd:1] = 1 byte payload, but CommandResponse expects
    // [status:1][detail:2] = 3 bytes. Decode fails due to insufficient payload data.
    auto decoded = frame_direction::DirFrame::decode_bytes(*bytes);
    CHECK(!decoded.has_value());
}

TEST_CASE("frame_direction: Heartbeat roundtrip (bidirectional)", "[frame][roundtrip][direction]") {
    frame_direction::Heartbeat msg;
    msg.set_seq(9999);

    auto frame = frame_direction::DirFrame::wrap(msg);
    CHECK(frame.msg_type() == 2);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_direction::DirFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* p = std::get_if<frame_direction::Heartbeat>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->seq() == 9999);
}

TEST_CASE("frame_direction: message constants", "[frame][constants][direction]") {
    CHECK(frame_direction::CommandRequest::ID_VALUE == 1);
    CHECK(frame_direction::CommandResponse::ID_VALUE == 1);
    CHECK(frame_direction::Heartbeat::ID_VALUE == 2);
    CHECK(frame_direction::CommandRequest::TYPE_ID != frame_direction::CommandResponse::TYPE_ID);
}

TEST_CASE("frame_direction: session decode prefers receive type", "[frame][session][direction]") {
    auto session = frame_direction::create_dir_frame_session();
    REQUIRE(session != nullptr);

    // Encode a Heartbeat (bidirectional)
    frame_direction::Heartbeat hb;
    hb.set_seq(42);
    auto encoded = session->encode_wrap(frame_direction::Heartbeat::TYPE_ID, hb);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    CHECK(decoded->at(0).type_id == frame_direction::Heartbeat::TYPE_ID);
}

// ============================================================================
// frame_array: ArrayFrame with count="*" array payload
// ============================================================================

TEST_CASE("frame_array: single record roundtrip", "[frame][roundtrip][array]") {
    frame_array::ArrayFrame frame;
    frame_array::Record rec;
    rec.set_key(1);
    rec.set_value(1000);
    frame.payload().push_back(rec);
    frame.set_msg_type(1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2][key:1][value:2] = 6 bytes
    REQUIRE(bytes->size() == 6);

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload().size() == 1);

    auto* p = std::get_if<frame_array::Record>(&decoded->payload()[0]);
    REQUIRE(p != nullptr);
    CHECK(p->key() == 1);
    CHECK(p->value() == 1000);
}

TEST_CASE("frame_array: multiple records roundtrip", "[frame][roundtrip][array]") {
    frame_array::ArrayFrame frame;
    for (uint8_t i = 0; i < 5; i++) {
        frame_array::Record rec;
        rec.set_key(i);
        rec.set_value(i * 100);
        frame.payload().push_back(rec);
    }
    frame.set_msg_type(1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2] + 5 * [key:1][value:2] = 3 + 15 = 18 bytes
    REQUIRE(bytes->size() == 18);

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload().size() == 5);

    for (uint8_t i = 0; i < 5; i++) {
        auto* p = std::get_if<frame_array::Record>(&decoded->payload()[i]);
        REQUIRE(p != nullptr);
        CHECK(p->key() == i);
        CHECK(p->value() == i * 100);
    }
}

TEST_CASE("frame_array: wrap helper", "[frame][roundtrip][array]") {
    frame_array::Record rec;
    rec.set_key(7);
    rec.set_value(777);

    auto frame = frame_array::ArrayFrame::wrap(rec);
    CHECK(frame.msg_type() == 1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    // wrap() creates a single-element payload (not array)
    // The frame payload is a variant, not a vector, when using wrap()
}

TEST_CASE("frame_array: session decode_frame", "[frame][session][array]") {
    auto session = frame_array::create_array_frame_session();
    REQUIRE(session != nullptr);

    frame_array::Record rec;
    rec.set_key(3);
    rec.set_value(300);
    auto encoded = session->encode_wrap(frame_array::Record::TYPE_ID, rec);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    // Session returns one DecodedMessage per record in the array
    REQUIRE(decoded->size() >= 1);
    CHECK(decoded->at(0).type_id == frame_array::Record::TYPE_ID);
}

// ============================================================================
// frame_array: Batch API tests
// ============================================================================

TEST_CASE("frame_array: batch wrap roundtrip", "[frame][roundtrip][array][batch]") {
    std::vector<frame_array::Record> records;
    for (uint8_t i = 0; i < 5; i++) {
        frame_array::Record rec;
        rec.set_key(i);
        rec.set_value(i * 100);
        records.push_back(rec);
    }
    auto frame = frame_array::ArrayFrame::wrap(std::span{records});
    CHECK(frame.msg_type() == 1);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 18);  // 3 header + 5*3 payload

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload().size() == 5);
    for (uint8_t i = 0; i < 5; i++) {
        auto* p = std::get_if<frame_array::Record>(&decoded->payload()[i]);
        REQUIRE(p != nullptr);
        CHECK(p->key() == i);
        CHECK(p->value() == i * 100);
    }
}

TEST_CASE("frame_array: batch wrap empty", "[frame][roundtrip][array][batch]") {
    std::vector<frame_array::Record> empty;
    auto frame = frame_array::ArrayFrame::wrap(std::span{empty});
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 3);  // header only
}

TEST_CASE("frame_array: session encode_batch", "[frame][session][array][batch]") {
    auto session = frame_array::create_array_frame_session();
    REQUIRE(session != nullptr);

    std::vector<std::any> payloads;
    for (uint8_t i = 0; i < 3; i++) {
        frame_array::Record rec;
        rec.set_key(i);
        rec.set_value(i * 50);
        payloads.emplace_back(rec);
    }

    auto encoded = session->encode_batch(frame_array::Record::TYPE_ID, payloads);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 3);
    for (uint8_t i = 0; i < 3; i++) {
        CHECK(decoded->at(i).type_id == frame_array::Record::TYPE_ID);
        auto* p = std::any_cast<frame_array::Record>(&decoded->at(i).payload);
        REQUIRE(p != nullptr);
        CHECK(p->key() == i);
        CHECK(p->value() == i * 50);
    }
}

TEST_CASE("frame_array: session encode_batch type mismatch", "[frame][session][array][batch]") {
    auto session = frame_array::create_array_frame_session();
    std::vector<std::any> payloads = { std::any(42) };
    auto result = session->encode_batch(frame_array::Record::TYPE_ID, payloads);
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::InvalidArgument);
}

TEST_CASE("frame_basic: session encode_batch rejected (non-array)", "[frame][session][batch]") {
    auto session = frame_basic::create_simple_frame_session();
    std::vector<std::any> payloads = { std::any(frame_basic::Heartbeat{}) };
    auto result = session->encode_batch(frame_basic::Heartbeat::TYPE_ID, payloads);
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::BatchNotSupported);
}

TEST_CASE("frame_array: empty payload roundtrip", "[frame][roundtrip][array]") {
    frame_array::ArrayFrame frame;
    frame.set_msg_type(1);
    // No records added

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][length:2] = 3 bytes (header only)
    REQUIRE(bytes->size() == 3);

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->payload().empty());
}

// ============================================================================
// Frame fields pushed into message classes
// ============================================================================

TEST_CASE("frame_basic: message carries frame header fields", "[frame][frame-fields]") {
    frame_basic::Heartbeat msg;
    msg.set_timestamp(12345);

    auto frame = frame_basic::SimpleFrame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_basic::SimpleFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<frame_basic::Heartbeat>(&decoded->payload());
    REQUIRE(payload != nullptr);

    // Frame header fields are populated on the message
    CHECK(payload->msg_type() == 1);
    CHECK(payload->length() == 5);
    // Regular message field still works
    CHECK(payload->timestamp() == 12345);
}

TEST_CASE("frame_basic: default message has zero frame fields", "[frame][frame-fields]") {
    frame_basic::Heartbeat msg;
    CHECK(msg.msg_type() == 0);
    CHECK(msg.length() == 0);
}

TEST_CASE("frame_footer: message carries header and footer fields", "[frame][frame-fields][footer]") {
    frame_footer::Data msg;
    msg.set_value(0xABCD);

    auto frame = frame_footer::FooterFrame::wrap(msg);
    frame.set_checksum(0x42);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_footer::FooterFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* p = std::get_if<frame_footer::Data>(&decoded->payload());
    REQUIRE(p != nullptr);

    // Header frame fields
    CHECK(p->msg_type() == 1);
    CHECK(p->length() == 6);
    // Footer frame field
    CHECK(p->checksum() == 0x42);
    // Message field
    CHECK(p->value() == 0xABCD);
}

TEST_CASE("frame_array: records carry frame header fields", "[frame][frame-fields][array]") {
    std::vector<frame_array::Record> records;
    for (uint8_t i = 0; i < 3; i++) {
        frame_array::Record rec;
        rec.set_key(i);
        rec.set_value(i * 100);
        records.push_back(rec);
    }
    auto frame = frame_array::ArrayFrame::wrap(std::span{records});
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_array::ArrayFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload().size() == 3);

    for (size_t i = 0; i < 3; i++) {
        auto* p = std::get_if<frame_array::Record>(&decoded->payload()[i]);
        REQUIRE(p != nullptr);
        // Each record carries the same frame header values
        CHECK(p->msg_type() == 1);
        CHECK(p->length() == 12);  // 3 header + 3*3 payload
        // Own payload fields
        CHECK(p->key() == static_cast<uint8_t>(i));
        CHECK(p->value() == static_cast<uint16_t>(i * 100));
    }
}

TEST_CASE("frame_basic: to_string includes frame fields", "[frame][frame-fields][to_string]") {
    frame_basic::Heartbeat msg;
    msg.set_timestamp(42);

    auto frame = frame_basic::SimpleFrame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = frame_basic::SimpleFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    auto* p = std::get_if<frame_basic::Heartbeat>(&decoded->payload());
    REQUIRE(p != nullptr);

    auto str = p->to_string();
    CHECK(str.find("msg-type=1") != std::string::npos);
    CHECK(str.find("length=5") != std::string::npos);
    CHECK(str.find("timestamp=42") != std::string::npos);
}

TEST_CASE("frame_basic: session decode populates frame fields", "[frame][frame-fields][session]") {
    auto session = frame_basic::create_simple_frame_session();

    frame_basic::Status msg;
    msg.set_code(42);
    msg.set_detail(9999);
    auto encoded = session->encode_wrap(frame_basic::Status::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<frame_basic::Status>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->msg_type() == 2);
    CHECK(payload->length() == 6);
    CHECK(payload->code() == 42);
    CHECK(payload->detail() == 9999);
}
