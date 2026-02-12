// SPDX-License-Identifier: MIT
// Bgen tests - Generated session integration tests
//
// Tests ISession implementation from generated session_protocol code:
// decode_frame, encode_wrap, sync_pattern, frame length, type registry.

#include <catch2/catch_test_macros.hpp>
#include <conduit/traits/session_traits.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <memory>
#include <any>

#include "session_protocol/sessions.hpp"
#include "session_protocol/protocol.hpp"
#include "session_protocol/messages.hpp"
#include "inline_struct/sessions.hpp"
#include "inline_struct/protocol.hpp"
#include "inline_struct/messages.hpp"
#include "inline_struct/constants.hpp"
#include "choice_protocol/sessions.hpp"
#include "choice_protocol/protocol.hpp"
#include "choice_protocol/messages.hpp"
#include "choice_protocol/constants.hpp"
#include "direction_qualified/sessions.hpp"
#include "direction_qualified/protocol.hpp"
#include "direction_qualified/messages.hpp"
#include "direction_qualified/constants.hpp"

// Helper: create a session instance
static std::unique_ptr<conduit::traits::ISession> make_session() {
    return session_test::create_packet_session();
}

// ============================================================================
// Section: Session Creation
// ============================================================================

TEST_CASE("Session creates successfully", "[session][creation]") {
    auto session = make_session();
    REQUIRE(session != nullptr);
}

TEST_CASE("ProtocolDescriptor type registry", "[session][creation]") {
    auto session = make_session();
    auto type_ids = session->leaf_type_ids();
    // Should have leaves: PingBody, DataBody, AckBody (receive-only but still a leaf)
    CHECK(type_ids.size() == 3);

    // Verify type_name lookup works for each ID
    for (auto id : type_ids) {
        auto name = session->type_name(id);
        CHECK(name != "unknown");
    }
}

// ============================================================================
// Section: Sync Pattern
// ============================================================================

TEST_CASE("Session sync pattern matches SYNC constant", "[session][sync]") {
    auto session = make_session();
    auto sync = session->sync_pattern();
    // SYNC = 0xDEAD -> 2 bytes: 0xDE, 0xAD
    REQUIRE(sync.size() == 2);
    CHECK(sync[0] == 0xDE);
    CHECK(sync[1] == 0xAD);
}

// ============================================================================
// Section: Frame Length
// ============================================================================

TEST_CASE("Session min_frame_header_size", "[session][framing]") {
    auto session = make_session();
    // sync(2) + seq(2) + msg-id(1) + length(2) = 7 bytes minimum
    CHECK(session->min_frame_header_size() == 7);
}

TEST_CASE("Session extract_frame_length", "[session][framing]") {
    // Encode a valid Packet and check frame length extraction
    session_test::PingBody ping;
    ping.set_timestamp(0);
    auto frame = session_test::Packet::wrap(ping);
    frame.set_seq(0);
    // Set length to body size: PingBody = 4 bytes
    frame.set_length(4);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_session();
    auto len = session->extract_frame_length(bytes);
    CHECK(len == 4);
}

// ============================================================================
// Section: decode_frame
// ============================================================================

TEST_CASE("decode_frame PingBody", "[session][decode]") {
    session_test::PingBody ping;
    ping.set_timestamp(0xAABBCCDD);
    auto frame = session_test::Packet::wrap(ping);
    frame.set_seq(1);
    frame.set_length(4);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    auto& dm = result->front();
    CHECK(dm.type_name == "PingBody");
    CHECK(dm.type_id != 0);
}

TEST_CASE("decode_frame DataBody", "[session][decode]") {
    session_test::DataBody data;
    data.set_channel(3);
    data.set_payload_a(0x1111);
    data.set_payload_b(0x2222);
    auto frame = session_test::Packet::wrap(data);
    frame.set_seq(2);
    frame.set_length(9); // channel(1) + payload_a(4) + payload_b(4)
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    auto& dm = result->front();
    CHECK(dm.type_name == "DataBody");
}

TEST_CASE("decode_frame with bad sync", "[session][decode]") {
    // Build raw bytes with wrong sync
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF); // wrong sync (should be 0xDEAD)
    w.write_u16(0);       // seq
    w.write_u8(1);        // msg-id = ping
    w.write_u16(4);       // length
    w.write_u32(0);       // timestamp
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = make_session();
    auto result = session->decode_frame(bytes);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode_frame with truncated data", "[session][decode]") {
    // Only 3 bytes — not enough for a full header
    std::vector<uint8_t> short_data = {0xDE, 0xAD, 0x00};

    auto session = make_session();
    auto result = session->decode_frame(short_data);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode_frame type_name matches", "[session][decode]") {
    session_test::PingBody ping;
    ping.set_timestamp(42);
    auto frame = session_test::Packet::wrap(ping);
    frame.set_seq(0);
    frame.set_length(4);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());

    // type_name from decode should match type_name from registry
    auto& dm = result->front();
    auto name_from_registry = session->type_name(dm.type_id);
    CHECK(dm.type_name == name_from_registry);
}

// ============================================================================
// Section: encode_wrap
// ============================================================================

TEST_CASE("encode_wrap PingBody roundtrip", "[session][wrap]") {
    session_test::PingBody ping;
    ping.set_timestamp(0x12345678);

    auto session = make_session();
    // Find PingBody type_id
    uint64_t ping_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "PingBody") {
            ping_type_id = id;
            break;
        }
    }
    REQUIRE(ping_type_id != 0);

    auto wrapped = session->encode_wrap(ping_type_id, std::any{ping});
    REQUIRE(wrapped.has_value());

    // Decode back
    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(!decoded->empty());
    CHECK(decoded->front().type_name == "PingBody");

    auto* body = std::any_cast<session_test::PingBody>(&decoded->front().payload);
    REQUIRE(body != nullptr);
    CHECK(body->timestamp() == 0x12345678);
}

TEST_CASE("encode_wrap DataBody roundtrip", "[session][wrap]") {
    session_test::DataBody data;
    data.set_channel(7);
    data.set_payload_a(0xAAAA);
    data.set_payload_b(0xBBBB);

    auto session = make_session();
    uint64_t data_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "DataBody") {
            data_type_id = id;
            break;
        }
    }
    REQUIRE(data_type_id != 0);

    auto wrapped = session->encode_wrap(data_type_id, std::any{data});
    REQUIRE(wrapped.has_value());

    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(!decoded->empty());
    CHECK(decoded->front().type_name == "DataBody");
}

TEST_CASE("encode_wrap unknown type_id fails", "[session][wrap]") {
    auto session = make_session();
    session_test::PingBody ping;
    auto result = session->encode_wrap(0xDEADDEAD, std::any{ping});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("encode_wrap auto-sequence increments", "[session][wrap]") {
    session_test::PingBody ping;
    ping.set_timestamp(100);

    auto session = make_session();
    uint64_t ping_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "PingBody") {
            ping_type_id = id;
            break;
        }
    }
    REQUIRE(ping_type_id != 0);

    auto wrap1 = session->encode_wrap(ping_type_id, std::any{ping});
    REQUIRE(wrap1.has_value());
    auto wrap2 = session->encode_wrap(ping_type_id, std::any{ping});
    REQUIRE(wrap2.has_value());

    // Decode both and check seq differs by 1
    auto dec1 = session_test::Packet::decode_bytes(*wrap1);
    auto dec2 = session_test::Packet::decode_bytes(*wrap2);
    REQUIRE(dec1.has_value());
    REQUIRE(dec2.has_value());
    CHECK(dec2->seq() == dec1->seq() + 1);
}

// ============================================================================
// Section: Session Reset
// ============================================================================

TEST_CASE("reset resets sequence counter", "[session][reset]") {
    session_test::PingBody ping;
    ping.set_timestamp(0);

    auto session = make_session();
    uint64_t ping_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "PingBody") {
            ping_type_id = id;
            break;
        }
    }
    REQUIRE(ping_type_id != 0);

    // Encode a few times to advance sequence
    session->encode_wrap(ping_type_id, std::any{ping});
    session->encode_wrap(ping_type_id, std::any{ping});

    // Reset and encode again
    session->reset();
    auto wrapped = session->encode_wrap(ping_type_id, std::any{ping});
    REQUIRE(wrapped.has_value());

    auto decoded = session_test::Packet::decode_bytes(*wrapped);
    REQUIRE(decoded.has_value());
    // After reset, sequence should restart at 0
    CHECK(decoded->seq() == 0);
}

// ============================================================================
// Section: Inline Struct Session
// ============================================================================

static std::unique_ptr<conduit::traits::ISession> make_inline_session() {
    return inline_struct::create_frame_session();
}

TEST_CASE("Inline struct session creation", "[session][inline]") {
    auto session = make_inline_session();
    REQUIRE(session != nullptr);
}

TEST_CASE("Inline struct session sync pattern", "[session][inline]") {
    auto session = make_inline_session();
    auto sync = session->sync_pattern();
    // SYNC = 0xCAFE -> 2 bytes: 0xCA, 0xFE
    REQUIRE(sync.size() == 2);
    CHECK(sync[0] == 0xCA);
    CHECK(sync[1] == 0xFE);
}

TEST_CASE("Inline struct session min header size", "[session][inline]") {
    auto session = make_inline_session();
    // sync(2) + seq(2) + length(2) + msg-type(1) = 7 bytes
    CHECK(session->min_frame_header_size() == 7);
}

TEST_CASE("Inline struct session frame length extraction", "[session][inline]") {
    inline_struct::BodyX body;
    body.set_x_data(0);
    auto frame = inline_struct::Frame::wrap(body);
    REQUIRE(frame.set_sync(0xCAFE).has_value());
    frame.set_seq(0);
    frame.set_length(4);
    frame.set_msg_type(inline_struct::MSG_X);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_inline_session();
    auto len = session->extract_frame_length(bytes);
    CHECK(len == 4);
}

TEST_CASE("Inline struct encode_wrap sets sync and auto-seq", "[session][inline]") {
    auto session = make_inline_session();

    inline_struct::BodyX body;
    body.set_x_data(0x42424242);

    uint64_t bodyx_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "BodyX") {
            bodyx_id = id;
            break;
        }
    }
    REQUIRE(bodyx_id != 0);

    auto wrapped = session->encode_wrap(bodyx_id, std::any{body});
    REQUIRE(wrapped.has_value());

    auto decoded = inline_struct::Frame::decode_bytes(*wrapped);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == 0xCAFE);
    // First encode should have seq = 0
    CHECK(decoded->seq() == 0);

    // Second encode should increment
    auto wrapped2 = session->encode_wrap(bodyx_id, std::any{body});
    REQUIRE(wrapped2.has_value());
    auto decoded2 = inline_struct::Frame::decode_bytes(*wrapped2);
    REQUIRE(decoded2.has_value());
    CHECK(decoded2->seq() == 1);
}

TEST_CASE("Inline struct decode_frame BodyX", "[session][inline]") {
    inline_struct::BodyX body;
    body.set_x_data(0xAAAAAAAA);
    auto frame = inline_struct::Frame::wrap(body);
    REQUIRE(frame.set_sync(0xCAFE).has_value());
    frame.set_seq(0);
    frame.set_length(4);
    frame.set_msg_type(inline_struct::MSG_X);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_inline_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    CHECK(result->front().type_name == "BodyX");

    auto* payload = std::any_cast<inline_struct::BodyX>(&result->front().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->x_data() == 0xAAAAAAAA);
}

TEST_CASE("Inline struct decode_frame BodyY", "[session][inline]") {
    inline_struct::BodyY body;
    body.set_y_data(0x5555);
    auto frame = inline_struct::Frame::wrap(body);
    REQUIRE(frame.set_sync(0xCAFE).has_value());
    frame.set_seq(0);
    frame.set_length(2);
    frame.set_msg_type(inline_struct::MSG_Y);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto session = make_inline_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    CHECK(result->front().type_name == "BodyY");

    auto* payload = std::any_cast<inline_struct::BodyY>(&result->front().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->y_data() == 0x5555);
}

// ============================================================================
// Section: Choice Protocol Session
// ============================================================================

static std::unique_ptr<conduit::traits::ISession> make_choice_session() {
    return choice_test::create_frame_session();
}

TEST_CASE("Choice protocol session sync", "[session][choice_protocol]") {
    auto session = make_choice_session();
    auto sync = session->sync_pattern();
    // SYNC = 0xBEEF -> 2 bytes: 0xBE, 0xEF
    REQUIRE(sync.size() == 2);
    CHECK(sync[0] == 0xBE);
    CHECK(sync[1] == 0xEF);
}

TEST_CASE("Choice protocol encode_wrap AlphaBody full roundtrip", "[session][choice_protocol]") {
    auto session = make_choice_session();

    choice_test::AlphaBody alpha;
    alpha.set_x(100);
    alpha.set_y(200);

    uint64_t alpha_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "AlphaBody") {
            alpha_id = id;
            break;
        }
    }
    REQUIRE(alpha_id != 0);

    auto wrapped = session->encode_wrap(alpha_id, std::any{alpha});
    REQUIRE(wrapped.has_value());

    auto result = session->decode_frame(*wrapped);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    CHECK(result->front().type_name == "AlphaBody");

    auto* payload = std::any_cast<choice_test::AlphaBody>(&result->front().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->x() == 100);
    CHECK(payload->y() == 200);
}

TEST_CASE("Choice protocol receive-only BetaBody encode succeeds (direction is documentary)", "[session][choice_protocol]") {
    auto session = make_choice_session();

    // BetaBody is direction="receive" — encoding should still succeed
    // (direction constraints are documentary, with a runtime warning)
    uint64_t beta_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "BetaBody") {
            beta_id = id;
            break;
        }
    }
    REQUIRE(beta_id != 0);

    choice_test::BetaBody beta;
    beta.set_payload_size(1);
    beta.set_tag(2);
    auto result = session->encode_wrap(beta_id, std::any{beta});
    CHECK(result.has_value());
}

// ============================================================================
// Section: Receive-only type decode (session_protocol AckBody)
// ============================================================================

TEST_CASE("decode_frame AckBody (receive-only)", "[session][decode]") {
    // Manually construct a Packet with msg-id=3 (MSG_ACK) and AckBody payload
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD);  // sync = SYNC
    w.write_u16(0);       // seq
    w.write_u8(3);        // msg-id = ack (MSG_ACK)
    w.write_u16(2);       // length = AckBody wire size (acked_seq: uint16 = 2 bytes)
    w.write_u16(0x00FF);  // acked_seq
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = make_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    auto& dm = result->front();
    CHECK(dm.type_name == "AckBody");

    auto* body = std::any_cast<session_test::AckBody>(&dm.payload);
    REQUIRE(body != nullptr);
    CHECK(body->acked_seq() == 0x00FF);
}

TEST_CASE("encode_wrap AckBody succeeds (direction is documentary)", "[session][wrap]") {
    auto session = make_session();

    // Find AckBody type_id
    uint64_t ack_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "AckBody") {
            ack_type_id = id;
            break;
        }
    }
    REQUIRE(ack_type_id != 0);

    // AckBody is direction="receive" — encoding should still succeed
    // (direction constraints are documentary, with a runtime warning)
    session_test::AckBody ack;
    ack.set_acked_seq(42);
    auto result = session->encode_wrap(ack_type_id, std::any{ack});
    CHECK(result.has_value());
}

// ============================================================================
// Section: Full session encode→decode roundtrip with payload verification
// ============================================================================

TEST_CASE("decode_frame via encode_wrap roundtrip preserves payload data", "[session][roundtrip]") {
    auto session = make_session();

    // Encode a DataBody through the full session path
    session_test::DataBody data;
    data.set_channel(42);
    data.set_payload_a(0xDEADBEEF);
    data.set_payload_b(0xCAFEBABE);

    uint64_t data_type_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "DataBody") {
            data_type_id = id;
            break;
        }
    }
    REQUIRE(data_type_id != 0);

    auto wrapped = session->encode_wrap(data_type_id, std::any{data});
    REQUIRE(wrapped.has_value());

    // Decode through decode_frame and verify ALL payload fields
    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(!decoded->empty());
    CHECK(decoded->front().type_name == "DataBody");

    auto* body = std::any_cast<session_test::DataBody>(&decoded->front().payload);
    REQUIRE(body != nullptr);
    CHECK(body->channel() == 42);
    CHECK(body->payload_a() == 0xDEADBEEF);
    CHECK(body->payload_b() == 0xCAFEBABE);
}

// ============================================================================
// Section: Direction-Qualified Dispatch
// ============================================================================

static std::unique_ptr<conduit::traits::ISession> make_direction_session() {
    return direction_qualified::create_frame_session();
}

TEST_CASE("direction: decode shared discriminator produces receive variant", "[session][direction]") {
    // Build wire bytes: tag=1 (TAG_SHARED) + uint32 rx-data payload
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::TAG_SHARED);  // tag = 1
    w.write_u32(0xAABBCCDD);                      // rx-data (DownlinkPayload is uint32)
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = direction_qualified::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(std::holds_alternative<direction_qualified::DownlinkPayload>(decoded->body()));
}

TEST_CASE("direction: decode shared discriminator never produces send variant", "[session][direction]") {
    // Build wire bytes: tag=1 (TAG_SHARED) + uint32 payload for DownlinkPayload
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::TAG_SHARED);
    w.write_u32(0x12345678);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = direction_qualified::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(std::holds_alternative<direction_qualified::UplinkPayload>(decoded->body()));
}

TEST_CASE("direction: encode_wrap UplinkPayload (send-only) succeeds", "[session][direction]") {
    auto session = make_direction_session();

    direction_qualified::UplinkPayload uplink;
    uplink.set_tx_data(0x1234);

    auto result = session->encode_wrap(direction_qualified::UplinkPayload::TYPE_ID, std::any(uplink));
    CHECK(result.has_value());
}

TEST_CASE("direction: encode_wrap DownlinkPayload (receive-only) succeeds", "[session][direction]") {
    auto session = make_direction_session();

    direction_qualified::DownlinkPayload downlink;
    downlink.set_rx_data(0xDEADBEEF);

    auto result = session->encode_wrap(direction_qualified::DownlinkPayload::TYPE_ID, std::any(downlink));
    CHECK(result.has_value());
}

TEST_CASE("direction: common case decodes normally", "[session][direction]") {
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::TAG_COMMON);  // tag = 2
    w.write_u8(0x42);                             // common-data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = direction_qualified::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(std::holds_alternative<direction_qualified::CommonPayload>(decoded->body()));
    auto& payload = std::get<direction_qualified::CommonPayload>(decoded->body());
    CHECK(payload.common_data() == 0x42);
}

TEST_CASE("direction: wrap UplinkPayload sets correct discriminator", "[session][direction]") {
    direction_qualified::UplinkPayload uplink;
    uplink.set_tx_data(0x5678);
    auto frame = direction_qualified::Frame::wrap(uplink);
    CHECK(frame.tag() == direction_qualified::TAG_SHARED);
    CHECK(std::holds_alternative<direction_qualified::UplinkPayload>(frame.body()));
}

TEST_CASE("direction: wrap DownlinkPayload sets correct discriminator", "[session][direction]") {
    direction_qualified::DownlinkPayload downlink;
    downlink.set_rx_data(0xBEEF);
    auto frame = direction_qualified::Frame::wrap(downlink);
    CHECK(frame.tag() == direction_qualified::TAG_SHARED);
    CHECK(std::holds_alternative<direction_qualified::DownlinkPayload>(frame.body()));
}

TEST_CASE("direction: decode_frame extracts receive variant from shared discriminator", "[session][direction]") {
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::TAG_SHARED);
    w.write_u32(0xCAFEBABE);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = make_direction_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    CHECK(result->front().type_name == "DownlinkPayload");
}

TEST_CASE("direction: decode_frame does NOT extract send variant", "[session][direction]") {
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::TAG_SHARED);
    w.write_u32(0xCAFEBABE);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = make_direction_session();
    auto result = session->decode_frame(bytes);
    REQUIRE(result.has_value());
    REQUIRE(!result->empty());
    CHECK(result->front().type_name != "UplinkPayload");
}
