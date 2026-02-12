// SPDX-License-Identifier: MIT
// Bgen tests - Batch dispatch (dispatch attribute on <array>)
//
// Tests the dispatch="batch" (default) and dispatch="per-record" behavior
// on arrays inside inline choice cases.

#include <catch2/catch_test_macros.hpp>
#include <conduit/traits/session_traits.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/io/bit_reader.hpp>
#include <cstdint>
#include <memory>
#include <any>

#include "batch_dispatch/sessions.hpp"
#include "batch_dispatch/messages.hpp"

// Helper: create batch_test session
static std::unique_ptr<conduit::traits::ISession> make_batch_session() {
    return batch_test::create_frame_session();
}

// Helper: find type_id by name
static uint64_t find_type_id(conduit::traits::ISession& session, std::string_view name) {
    for (auto id : session.leaf_type_ids()) {
        if (session.type_name(id) == name) return id;
    }
    return 0;
}

// Helper: encode a Container with records (batch case)
static std::vector<uint8_t> encode_records_container(
        const std::vector<std::pair<uint16_t, uint32_t>>& records) {
    // Build body bytes first to compute length
    conduit::io::BitWriter body_w;
    for (auto& [id, val] : records) {
        body_w.write_u16(id);
        body_w.write_u32(val);
    }
    auto body_result = body_w.finish();
    auto& body = *body_result;

    conduit::io::BitWriter w;
    w.write_u8(1);  // msg-type = records
    auto len = static_cast<uint16_t>(body.size() + 3);  // +3 for msg-type(1) + len(2)
    w.write_u16(len);
    for (auto b : body) w.write_u8(b);
    auto result = w.finish();
    return std::move(*result);
}

// Helper: encode a Container with events (per-record case)
static std::vector<uint8_t> encode_events_container(
        const std::vector<uint8_t>& codes) {
    conduit::io::BitWriter w;
    w.write_u8(2);  // msg-type = events
    auto len = static_cast<uint16_t>(codes.size() + 3);
    w.write_u16(len);
    for (auto c : codes) w.write_u8(c);
    auto result = w.finish();
    return std::move(*result);
}

// Helper: encode a Frame with given container bytes
static std::vector<uint8_t> encode_frame(
        const std::vector<std::vector<uint8_t>>& containers) {
    conduit::io::BitWriter w;
    for (auto& c : containers) {
        for (auto b : c) w.write_u8(b);
    }
    auto result = w.finish();
    return std::move(*result);
}

// ============================================================================
// Happy-path: Batch dispatch (default)
// ============================================================================

TEST_CASE("Batch: decode 3 records returns 1 DecodedMessage", "[batch_dispatch]") {
    auto container = encode_records_container({{10, 100}, {20, 200}, {30, 300}});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto& dm = result->front();
    CHECK(dm.type_name == "records");
    CHECK(dm.type_id == batch_test::records::TYPE_ID);

    auto* wrapper = std::any_cast<batch_test::records>(&dm.payload);
    REQUIRE(wrapper != nullptr);
    REQUIRE(wrapper->items().size() == 3);
    CHECK(wrapper->items()[0].id() == 10);
    CHECK(wrapper->items()[0].value() == 100);
    CHECK(wrapper->items()[1].id() == 20);
    CHECK(wrapper->items()[1].value() == 200);
    CHECK(wrapper->items()[2].id() == 30);
    CHECK(wrapper->items()[2].value() == 300);
}

TEST_CASE("Batch: single record returns 1 DecodedMessage with 1 item", "[batch_dispatch]") {
    auto container = encode_records_container({{0xFFFF, 0xDEADBEEF}});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto* wrapper = std::any_cast<batch_test::records>(&result->front().payload);
    REQUIRE(wrapper != nullptr);
    REQUIRE(wrapper->items().size() == 1);
    CHECK(wrapper->items()[0].id() == 0xFFFF);
    CHECK(wrapper->items()[0].value() == 0xDEADBEEF);
}

TEST_CASE("Batch: empty array returns 1 DecodedMessage with 0 items", "[batch_dispatch]") {
    auto container = encode_records_container({});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto* wrapper = std::any_cast<batch_test::records>(&result->front().payload);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->items().empty());
}

TEST_CASE("Batch: multiple containers each produce 1 DecodedMessage", "[batch_dispatch]") {
    auto c1 = encode_records_container({{1, 10}});
    auto c2 = encode_records_container({{2, 20}, {3, 30}});
    auto c3 = encode_records_container({{4, 40}});
    auto frame_bytes = encode_frame({c1, c2, c3});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    for (size_t i = 0; i < 3; i++) {
        CHECK(result->at(i).type_name == "records");
    }

    auto* w0 = std::any_cast<batch_test::records>(&result->at(0).payload);
    auto* w1 = std::any_cast<batch_test::records>(&result->at(1).payload);
    auto* w2 = std::any_cast<batch_test::records>(&result->at(2).payload);
    REQUIRE(w0 != nullptr);
    REQUIRE(w1 != nullptr);
    REQUIRE(w2 != nullptr);
    CHECK(w0->items().size() == 1);
    CHECK(w1->items().size() == 2);
    CHECK(w2->items().size() == 1);
}

TEST_CASE("Batch: encode_wrap roundtrip preserves all records", "[batch_dispatch]") {
    auto session = make_batch_session();
    auto records_id = find_type_id(*session, "records");
    REQUIRE(records_id != 0);

    batch_test::records recs;
    batch_test::Record r1; r1.set_id(42); r1.set_value(999);
    batch_test::Record r2; r2.set_id(100); r2.set_value(0);
    recs.mutable_items().push_back(r1);
    recs.mutable_items().push_back(r2);

    auto wrapped = session->encode_wrap(records_id, std::any{recs});
    REQUIRE(wrapped.has_value());

    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    CHECK(decoded->front().type_name == "records");

    auto* wrapper = std::any_cast<batch_test::records>(&decoded->front().payload);
    REQUIRE(wrapper != nullptr);
    REQUIRE(wrapper->items().size() == 2);
    CHECK(wrapper->items()[0].id() == 42);
    CHECK(wrapper->items()[0].value() == 999);
    CHECK(wrapper->items()[1].id() == 100);
    CHECK(wrapper->items()[1].value() == 0);
}

TEST_CASE("Batch: encode_wrap empty records roundtrip", "[batch_dispatch]") {
    auto session = make_batch_session();

    batch_test::records recs;  // no items

    auto wrapped = session->encode_wrap(batch_test::records::TYPE_ID, std::any{recs});
    REQUIRE(wrapped.has_value());

    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* wrapper = std::any_cast<batch_test::records>(&decoded->front().payload);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->items().empty());
}

// ============================================================================
// Happy-path: Per-record dispatch
// ============================================================================

TEST_CASE("Per-record: decode 3 events returns 3 DecodedMessages", "[batch_dispatch]") {
    auto container = encode_events_container({0x11, 0x22, 0x33});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    for (size_t i = 0; i < 3; i++) {
        CHECK(result->at(i).type_name == "Event");
        CHECK(result->at(i).type_id == batch_test::Event::TYPE_ID);
        auto* ev = std::any_cast<batch_test::Event>(&result->at(i).payload);
        REQUIRE(ev != nullptr);
    }
    CHECK(std::any_cast<batch_test::Event>(&result->at(0).payload)->code() == 0x11);
    CHECK(std::any_cast<batch_test::Event>(&result->at(1).payload)->code() == 0x22);
    CHECK(std::any_cast<batch_test::Event>(&result->at(2).payload)->code() == 0x33);
}

TEST_CASE("Per-record: single event returns 1 DecodedMessage", "[batch_dispatch]") {
    auto container = encode_events_container({0xFF});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->front().type_name == "Event");

    auto* ev = std::any_cast<batch_test::Event>(&result->front().payload);
    REQUIRE(ev != nullptr);
    CHECK(ev->code() == 0xFF);
}

TEST_CASE("Per-record: empty events container returns 0 DecodedMessages", "[batch_dispatch]") {
    auto container = encode_events_container({});
    auto frame_bytes = encode_frame({container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("Per-record: encode_wrap roundtrip", "[batch_dispatch]") {
    auto session = make_batch_session();
    auto event_id = find_type_id(*session, "Event");
    REQUIRE(event_id != 0);

    batch_test::Event ev;
    ev.set_code(0xAB);

    auto wrapped = session->encode_wrap(event_id, std::any{ev});
    REQUIRE(wrapped.has_value());

    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    CHECK(decoded->front().type_name == "Event");

    auto* payload = std::any_cast<batch_test::Event>(&decoded->front().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->code() == 0xAB);
}

TEST_CASE("Per-record: multiple events containers", "[batch_dispatch]") {
    auto c1 = encode_events_container({0x01, 0x02});
    auto c2 = encode_events_container({0x03});
    auto frame_bytes = encode_frame({c1, c2});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    CHECK(std::any_cast<batch_test::Event>(&result->at(0).payload)->code() == 0x01);
    CHECK(std::any_cast<batch_test::Event>(&result->at(1).payload)->code() == 0x02);
    CHECK(std::any_cast<batch_test::Event>(&result->at(2).payload)->code() == 0x03);
}

// ============================================================================
// Happy-path: Mixed batch + per-record
// ============================================================================

TEST_CASE("Mixed: batch records + per-record events in one frame", "[batch_dispatch]") {
    auto records_container = encode_records_container({{1, 10}, {2, 20}});
    auto events_container = encode_events_container({0xAA, 0xBB, 0xCC});
    auto frame_bytes = encode_frame({records_container, events_container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    // 1 batch message (records wrapper) + 3 per-record messages (events)
    REQUIRE(result->size() == 4);

    // First: batch records
    CHECK(result->at(0).type_name == "records");
    auto* wrapper = std::any_cast<batch_test::records>(&result->at(0).payload);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->items().size() == 2);
    CHECK(wrapper->items()[0].id() == 1);
    CHECK(wrapper->items()[1].id() == 2);

    // Next 3: per-record events
    for (size_t i = 1; i < 4; i++) {
        CHECK(result->at(i).type_name == "Event");
    }
    CHECK(std::any_cast<batch_test::Event>(&result->at(1).payload)->code() == 0xAA);
    CHECK(std::any_cast<batch_test::Event>(&result->at(2).payload)->code() == 0xBB);
    CHECK(std::any_cast<batch_test::Event>(&result->at(3).payload)->code() == 0xCC);
}

TEST_CASE("Mixed: per-record events before batch records", "[batch_dispatch]") {
    // Reverse order: events first, records second
    auto events_container = encode_events_container({0x10, 0x20});
    auto records_container = encode_records_container({{5, 50}});
    auto frame_bytes = encode_frame({events_container, records_container});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    // 2 per-record events + 1 batch records
    REQUIRE(result->size() == 3);

    CHECK(result->at(0).type_name == "Event");
    CHECK(result->at(1).type_name == "Event");
    CHECK(result->at(2).type_name == "records");

    auto* wrapper = std::any_cast<batch_test::records>(&result->at(2).payload);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->items().size() == 1);
    CHECK(wrapper->items()[0].id() == 5);
}

TEST_CASE("Mixed: interleaved batch and per-record containers", "[batch_dispatch]") {
    auto r1 = encode_records_container({{1, 100}});
    auto e1 = encode_events_container({0xAA});
    auto r2 = encode_records_container({{2, 200}});
    auto e2 = encode_events_container({0xBB, 0xCC});
    auto frame_bytes = encode_frame({r1, e1, r2, e2});

    auto session = make_batch_session();
    auto result = session->decode_frame(frame_bytes);
    REQUIRE(result.has_value());
    // 1 batch + 1 per-record + 1 batch + 2 per-record = 5
    REQUIRE(result->size() == 5);

    CHECK(result->at(0).type_name == "records");
    CHECK(result->at(1).type_name == "Event");
    CHECK(result->at(2).type_name == "records");
    CHECK(result->at(3).type_name == "Event");
    CHECK(result->at(4).type_name == "Event");
}

// ============================================================================
// Session API tests
// ============================================================================

TEST_CASE("leaf_type_ids includes both types", "[batch_dispatch]") {
    auto session = make_batch_session();
    auto ids = session->leaf_type_ids();
    REQUIRE(ids.size() == 2);

    auto records_id = find_type_id(*session, "records");
    auto event_id = find_type_id(*session, "Event");
    CHECK(records_id != 0);
    CHECK(event_id != 0);
    CHECK(records_id != event_id);
}

TEST_CASE("type_name resolves correctly for both leaf types", "[batch_dispatch]") {
    auto session = make_batch_session();
    auto records_id = find_type_id(*session, "records");
    auto event_id = find_type_id(*session, "Event");
    CHECK(session->type_name(records_id) == "records");
    CHECK(session->type_name(event_id) == "Event");
    CHECK(session->type_name(0xDEAD) == "unknown");
}

TEST_CASE("TYPE_ID consistency between class and session", "[batch_dispatch]") {
    auto session = make_batch_session();
    auto records_id = find_type_id(*session, "records");
    auto event_id = find_type_id(*session, "Event");
    CHECK(records_id == batch_test::records::TYPE_ID);
    CHECK(event_id == batch_test::Event::TYPE_ID);
}

TEST_CASE("Batch wrapper has TYPE_ID and TYPE_NAME", "[batch_dispatch]") {
    CHECK(batch_test::records::TYPE_ID != 0);
    CHECK(batch_test::records::TYPE_NAME == std::string_view("records"));
}

TEST_CASE("Per-record element has TYPE_ID and TYPE_NAME", "[batch_dispatch]") {
    CHECK(batch_test::Event::TYPE_ID != 0);
    CHECK(batch_test::Event::TYPE_NAME == std::string_view("Event"));
}

// ============================================================================
// Generated struct API tests
// ============================================================================

TEST_CASE("Batch wrapper: to_string", "[batch_dispatch]") {
    batch_test::records recs;
    batch_test::Record r; r.set_id(1); r.set_value(2);
    recs.mutable_items().push_back(r);
    auto s = recs.to_string();
    CHECK(s.find("records{") != std::string::npos);
    // Arrays of structs now recurse into each element's to_string()
    CHECK(s.find("Record{") != std::string::npos);
    CHECK(s.find("id=1") != std::string::npos);
    CHECK(s.find("value=2") != std::string::npos);
}

TEST_CASE("Batch wrapper: equality", "[batch_dispatch]") {
    batch_test::records a, b;
    CHECK(a == b);  // both empty

    batch_test::Record r; r.set_id(1); r.set_value(2);
    a.mutable_items().push_back(r);
    CHECK_FALSE(a == b);

    b.mutable_items().push_back(r);
    CHECK(a == b);
}

TEST_CASE("Batch wrapper: encode/decode standalone", "[batch_dispatch]") {
    batch_test::records original;
    batch_test::Record r1; r1.set_id(100); r1.set_value(200);
    batch_test::Record r2; r2.set_id(300); r2.set_value(400);
    original.mutable_items().push_back(r1);
    original.mutable_items().push_back(r2);

    conduit::io::BitWriter w;
    auto enc = original.encode(w);
    REQUIRE(enc.has_value());
    auto bytes = w.finish();
    REQUIRE(bytes.has_value());

    conduit::io::BitReader r(*bytes);
    auto decoded = batch_test::records::decode(r);
    REQUIRE(decoded.has_value());
    CHECK(decoded->items().size() == 2);
    CHECK(*decoded == original);
}

TEST_CASE("Container: encode_bytes/decode_bytes roundtrip with batch body", "[batch_dispatch]") {
    batch_test::Container c;
    c.set_msg_type(batch_test::msg_type::records);
    batch_test::records body;
    batch_test::Record r; r.set_id(7); r.set_value(77);
    body.mutable_items().push_back(r);
    c.set_body(batch_test::bodyVariant{body});
    c.set_len(6 + 3);  // 1 record = 6 bytes body + 3 bytes header

    auto enc = c.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = batch_test::Container::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->msg_type() == batch_test::msg_type::records);
    REQUIRE(std::holds_alternative<batch_test::records>(dec->body()));
    auto& recs = std::get<batch_test::records>(dec->body());
    REQUIRE(recs.items().size() == 1);
    CHECK(recs.items()[0].id() == 7);
    CHECK(recs.items()[0].value() == 77);
}

TEST_CASE("Frame: wrap(records) sets correct discriminator and length", "[batch_dispatch]") {
    batch_test::records recs;
    batch_test::Record r; r.set_id(1); r.set_value(2);
    recs.mutable_items().push_back(r);

    auto frame = batch_test::Frame::wrap(recs);
    REQUIRE(frame.containers().size() == 1);
    auto& c = frame.containers()[0];
    CHECK(c.msg_type() == batch_test::msg_type::records);
    CHECK(c.len() == 6 + 3);  // 1 record = 6 bytes + 3 header
    REQUIRE(std::holds_alternative<batch_test::records>(c.body()));
}

TEST_CASE("Frame: wrap(Event) sets correct discriminator and length", "[batch_dispatch]") {
    batch_test::Event ev;
    ev.set_code(0x42);

    auto frame = batch_test::Frame::wrap(ev);
    REQUIRE(frame.containers().size() == 1);
    auto& c = frame.containers()[0];
    CHECK(c.msg_type() == batch_test::msg_type::events);
    CHECK(c.len() == 1 + 3);  // 1 event = 1 byte + 3 header
    REQUIRE(std::holds_alternative<batch_test::events>(c.body()));
    auto& evts = std::get<batch_test::events>(c.body());
    REQUIRE(evts.items().size() == 1);
    CHECK(evts.items()[0].code() == 0x42);
}

// ============================================================================
// Wire format verification
// ============================================================================

TEST_CASE("Wire format: batch records container bytes", "[batch_dispatch][wire]") {
    // 1 record: id=0x0001, value=0x00000002
    auto bytes = encode_records_container({{1, 2}});
    // Expected: msg-type(1)=0x01, len(2)=0x00 0x09, body: id=0x00 0x01, value=0x00 0x00 0x00 0x02
    REQUIRE(bytes.size() == 9);
    CHECK(bytes[0] == 0x01);  // msg-type = records
    CHECK(bytes[1] == 0x00);  // len high byte
    CHECK(bytes[2] == 0x09);  // len low byte = 9
    CHECK(bytes[3] == 0x00);  // id high
    CHECK(bytes[4] == 0x01);  // id low
    CHECK(bytes[5] == 0x00);  // value byte 0
    CHECK(bytes[6] == 0x00);  // value byte 1
    CHECK(bytes[7] == 0x00);  // value byte 2
    CHECK(bytes[8] == 0x02);  // value byte 3
}

TEST_CASE("Wire format: per-record events container bytes", "[batch_dispatch][wire]") {
    auto bytes = encode_events_container({0xAA, 0xBB});
    // Expected: msg-type(1)=0x02, len(2)=0x00 0x05, body: 0xAA, 0xBB
    REQUIRE(bytes.size() == 5);
    CHECK(bytes[0] == 0x02);  // msg-type = events
    CHECK(bytes[1] == 0x00);  // len high byte
    CHECK(bytes[2] == 0x05);  // len low byte = 5
    CHECK(bytes[3] == 0xAA);
    CHECK(bytes[4] == 0xBB);
}

// ============================================================================
// Negative tests
// ============================================================================

TEST_CASE("encode_wrap: wrong payload type for batch", "[batch_dispatch][negative]") {
    auto session = make_batch_session();

    // Pass an Event when records is expected
    batch_test::Event ev;
    ev.set_code(0);
    auto result = session->encode_wrap(batch_test::records::TYPE_ID, std::any{ev});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("encode_wrap: wrong payload type for per-record", "[batch_dispatch][negative]") {
    auto session = make_batch_session();

    // Pass a records when Event is expected
    batch_test::records recs;
    auto result = session->encode_wrap(batch_test::Event::TYPE_ID, std::any{recs});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("encode_wrap: unknown type_id", "[batch_dispatch][negative]") {
    auto session = make_batch_session();
    batch_test::records recs;
    auto result = session->encode_wrap(0xDEAD, std::any{recs});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode: unknown discriminator value", "[batch_dispatch][negative]") {
    // msg-type=3 (not in enum: 1=records, 2=events)
    conduit::io::BitWriter w;
    w.write_u8(3);          // unknown msg-type
    w.write_u16(3 + 1);    // len = 4 (3 header + 1 body byte)
    w.write_u8(0xFF);       // arbitrary body
    auto bytes_result = w.finish();
    REQUIRE(bytes_result.has_value());

    auto session = make_batch_session();
    auto result = session->decode_frame(*bytes_result);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode: truncated container header", "[batch_dispatch][negative]") {
    // Only 2 bytes: msg-type + 1 byte of len (missing second len byte)
    std::vector<uint8_t> bytes = {0x01, 0x00};

    auto session = make_batch_session();
    auto result = session->decode_frame(bytes);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode: body shorter than declared length", "[batch_dispatch][negative]") {
    // msg-type=1, len=100 (declares 97 bytes of body), but we only provide 2 body bytes
    conduit::io::BitWriter w;
    w.write_u8(1);
    w.write_u16(100);  // claims 97 bytes of body
    w.write_u8(0x00);
    w.write_u8(0x00);
    auto bytes_result = w.finish();
    REQUIRE(bytes_result.has_value());

    auto session = make_batch_session();
    auto result = session->decode_frame(*bytes_result);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("decode: empty frame returns empty messages", "[batch_dispatch][negative]") {
    std::vector<uint8_t> empty;
    auto session = make_batch_session();
    auto result = session->decode_frame(empty);
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("decode: record body not multiple of record size", "[batch_dispatch][negative]") {
    // Record is 6 bytes (2 + 4). Send 5 bytes of body — partial record.
    conduit::io::BitWriter w;
    w.write_u8(1);                 // msg-type = records
    w.write_u16(3 + 5);           // len = 8 (3 header + 5 body)
    w.write_u8(0x00);
    w.write_u8(0x01);
    w.write_u8(0x00);
    w.write_u8(0x00);
    w.write_u8(0x00);             // 5 bytes = incomplete record
    auto bytes_result = w.finish();
    REQUIRE(bytes_result.has_value());

    auto session = make_batch_session();
    auto result = session->decode_frame(*bytes_result);
    // Should fail: sub_reader has 5 bytes, Record::decode reads 6
    CHECK_FALSE(result.has_value());
}
