// SPDX-License-Identifier: MIT
// Conduit - Integration Tests: session_protocol (bgen-generated)

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/transceiver/handler.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <session_protocol/sessions.hpp>
#include <any>
#include <cstdint>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("session_protocol: send PingBody full pipeline",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(0xAABBCCDD);

    auto result = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(result.has_value());

    auto& bytes = *result;
    // Decode back to verify
    auto decoded = session_test::Packet::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->sync() == 0xDEAD);
    REQUIRE(std::holds_alternative<session_test::PingBody>(decoded->body()));
    auto& body = std::get<session_test::PingBody>(decoded->body());
    CHECK(body.timestamp() == 0xAABBCCDD);
}

TEST_CASE("session_protocol: receive DataBody with exact values",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    // Build a DataBody and wrap it
    session_test::DataBody data;
    data.set_channel(7);
    data.set_payload_a(0xDEAD);
    data.set_payload_b(0xBEEF);

    auto encoded = session->encode_wrap(session_test::DataBody::TYPE_ID, std::any(data));
    REQUIRE(encoded.has_value());

    // Decode via session
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == session_test::DataBody::TYPE_ID);
    CHECK(dm.type_name == "DataBody");

    auto& body = std::any_cast<const session_test::DataBody&>(dm.payload);
    CHECK(body.channel() == 7);
    CHECK(body.payload_a() == 0xDEAD);
    CHECK(body.payload_b() == 0xBEEF);
}

TEST_CASE("session_protocol: receive AckBody (receive-only) with exact value",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::AckBody ack;
    ack.set_acked_seq(0x1234);

    auto encoded = session->encode_wrap(session_test::AckBody::TYPE_ID, std::any(ack));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == session_test::AckBody::TYPE_ID);
    auto& body = std::any_cast<const session_test::AckBody&>(dm.payload);
    CHECK(body.acked_seq() == 0x1234);
}

TEST_CASE("session_protocol: send+loopback roundtrip",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(0x12345678);

    auto encoded = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(encoded.has_value());

    // Re-inject the bytes as if received
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const session_test::PingBody&>((*decoded)[0].payload);
    CHECK(body.timestamp() == ping.timestamp());
}

TEST_CASE("session_protocol: multiple types dispatched",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    // Encode one of each type
    session_test::PingBody ping;
    ping.set_timestamp(1);
    session_test::DataBody data;
    data.set_channel(2);
    data.set_payload_a(3);
    data.set_payload_b(4);
    session_test::AckBody ack;
    ack.set_acked_seq(5);

    auto e_ping = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    auto e_data = session->encode_wrap(session_test::DataBody::TYPE_ID, std::any(data));
    auto e_ack = session->encode_wrap(session_test::AckBody::TYPE_ID, std::any(ack));
    REQUIRE(e_ping.has_value());
    REQUIRE(e_data.has_value());
    REQUIRE(e_ack.has_value());

    // Reset session so sequence counters align for decode
    session->reset();

    auto d_ping = session->decode_frame(*e_ping);
    auto d_data = session->decode_frame(*e_data);
    auto d_ack = session->decode_frame(*e_ack);

    REQUIRE(d_ping.has_value());
    REQUIRE(d_data.has_value());
    REQUIRE(d_ack.has_value());

    CHECK(d_ping->size() == 1);
    CHECK(d_data->size() == 1);
    CHECK(d_ack->size() == 1);

    CHECK((*d_ping)[0].type_name == "PingBody");
    CHECK((*d_data)[0].type_name == "DataBody");
    CHECK((*d_ack)[0].type_name == "AckBody");
}

TEST_CASE("session_protocol: auto-sequence increments",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(100);

    // Encode 3 times — seq should be 0, 1, 2
    auto e0 = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    auto e1 = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    auto e2 = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(e0.has_value());
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());

    // Decode each and check seq field
    auto p0 = session_test::Packet::decode_bytes(*e0);
    auto p1 = session_test::Packet::decode_bytes(*e1);
    auto p2 = session_test::Packet::decode_bytes(*e2);
    REQUIRE(p0.has_value());
    REQUIRE(p1.has_value());
    REQUIRE(p2.has_value());

    CHECK(p0->seq() == 0);
    CHECK(p1->seq() == 1);
    CHECK(p2->seq() == 2);
}

TEST_CASE("session_protocol: session reset resets sequence",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(100);

    // Encode 2 messages
    session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));

    // Reset
    session->reset();

    // Next encode should have seq=0
    auto e = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(e.has_value());

    auto p = session_test::Packet::decode_bytes(*e);
    REQUIRE(p.has_value());
    CHECK(p->seq() == 0);
}

TEST_CASE("session_protocol: datagram-mode roundtrip PingBody",
          "[integration][session_protocol]") {
    // session_protocol's length field is body-only (not total frame), so
    // it works in datagram mode (one frame per delivery), not with StreamFramer.
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(0xDEADBEEF);

    auto encoded = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(encoded.has_value());

    // Decode the entire frame as a complete datagram
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    auto& body = std::any_cast<const session_test::PingBody&>((*decoded)[0].payload);
    CHECK(body.timestamp() == 0xDEADBEEF);
}

TEST_CASE("session_protocol: encode_wrap produces valid Packet bytes",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(42);

    auto encoded = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    REQUIRE(encoded.has_value());

    // Parse as a raw Packet to verify structure
    auto pkt = session_test::Packet::decode_bytes(*encoded);
    REQUIRE(pkt.has_value());

    CHECK(pkt->sync() == 0xDEAD);
    CHECK(pkt->msg_id() == session_test::msg_id::ping);
    CHECK(pkt->length() == 4);  // PingBody is 4 bytes
    REQUIRE(std::holds_alternative<session_test::PingBody>(pkt->body()));
    CHECK(std::get<session_test::PingBody>(pkt->body()).timestamp() == 42);
}

TEST_CASE("session_protocol: decode all three body types",
          "[integration][session_protocol]") {
    auto session = session_test::create_packet_session();

    session_test::PingBody ping;
    ping.set_timestamp(111);
    session_test::DataBody data;
    data.set_channel(5);
    data.set_payload_a(222);
    data.set_payload_b(333);
    session_test::AckBody ack;
    ack.set_acked_seq(444);

    auto e_ping = session->encode_wrap(session_test::PingBody::TYPE_ID, std::any(ping));
    auto e_data = session->encode_wrap(session_test::DataBody::TYPE_ID, std::any(data));
    auto e_ack = session->encode_wrap(session_test::AckBody::TYPE_ID, std::any(ack));
    REQUIRE(e_ping.has_value());
    REQUIRE(e_data.has_value());
    REQUIRE(e_ack.has_value());

    session->reset();

    // Decode each independently (datagram mode)
    auto d_ping = session->decode_frame(*e_ping);
    auto d_data = session->decode_frame(*e_data);
    auto d_ack = session->decode_frame(*e_ack);

    REQUIRE(d_ping.has_value());
    REQUIRE(d_data.has_value());
    REQUIRE(d_ack.has_value());

    CHECK((*d_ping)[0].type_name == "PingBody");
    auto& pb = std::any_cast<const session_test::PingBody&>((*d_ping)[0].payload);
    CHECK(pb.timestamp() == 111);

    CHECK((*d_data)[0].type_name == "DataBody");
    auto& db = std::any_cast<const session_test::DataBody&>((*d_data)[0].payload);
    CHECK(db.channel() == 5);
    CHECK(db.payload_a() == 222);
    CHECK(db.payload_b() == 333);

    CHECK((*d_ack)[0].type_name == "AckBody");
    auto& ab = std::any_cast<const session_test::AckBody&>((*d_ack)[0].payload);
    CHECK(ab.acked_seq() == 444);
}

TEST_CASE("session_protocol: set_handler with MessageHandler",
          "[integration][session_protocol]") {
    HandlerRegistry registry;
    PeerId peer(1);

    uint64_t typed_ts = 0;
    uint64_t catch_all_tid = 0;

    // Generated leaf structs (PingBody etc.) don't satisfy traits::Message
    // (no TYPE_ID/TYPE_NAME), so we use on_group with raw type_id + any_cast.
    MessageHandler handler;
    handler
        .on_group({session_test::PingBody::TYPE_ID},  // PingBody type_id
            [&](uint64_t, const std::any& payload) {
                auto* p = std::any_cast<session_test::PingBody>(&payload);
                if (p) typed_ts = p->timestamp();
            })
        .on_any([&](uint64_t tid, const std::any&) {
            catch_all_tid = tid;
        });

    registry.install_handler(peer, handler);

    // Dispatch PingBody — group handler fires
    session_test::PingBody ping;
    ping.set_timestamp(0xABCD);
    registry.dispatch(peer, session_test::PingBody::TYPE_ID, std::any(ping));
    CHECK(typed_ts == 0xABCD);
    CHECK(catch_all_tid == 0);

    // Dispatch DataBody — catch-all fires
    session_test::DataBody data;
    registry.dispatch(peer, session_test::DataBody::TYPE_ID, std::any(data));
    CHECK(catch_all_tid == session_test::DataBody::TYPE_ID);
}

TEST_CASE("session_protocol: MessageHandler group dispatch",
          "[integration][session_protocol]") {
    HandlerRegistry registry;
    PeerId peer(1);

    std::vector<uint64_t> group_tids;

    MessageHandler handler;
    handler.on_group({session_test::PingBody::TYPE_ID, session_test::DataBody::TYPE_ID},  // PingBody, DataBody
        [&](uint64_t tid, const std::any&) {
            group_tids.push_back(tid);
        });

    registry.install_handler(peer, handler);

    session_test::PingBody ping;
    registry.dispatch(peer, session_test::PingBody::TYPE_ID, std::any(ping));

    session_test::DataBody data;
    registry.dispatch(peer, session_test::DataBody::TYPE_ID, std::any(data));

    REQUIRE(group_tids.size() == 2);
    CHECK(group_tids[0] == session_test::PingBody::TYPE_ID);
    CHECK(group_tids[1] == session_test::DataBody::TYPE_ID);
}
