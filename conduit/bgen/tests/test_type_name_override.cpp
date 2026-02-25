// SPDX-License-Identifier: MIT
// Tests for typeName attribute override on inline definitions.
// Verifies that typeName controls the generated C++ class name for
// inline choice cases, otherwise cases, inline structs, and array elements.

#include <catch2/catch_test_macros.hpp>

#include "type_name_override/messages.hpp"

// ============================================================================
// Choice case typeName roundtrip
// ============================================================================

TEST_CASE("typeName choice: HeartbeatPayload roundtrip", "[type_name][choice]") {
    type_name_override::ChoiceMsg msg;
    msg.set_tag(1);

    type_name_override::HeartbeatPayload hb;
    hb.set_timestamp(0xDEADBEEF);
    hb.set_sequence(42);
    msg.set_payload(hb);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = type_name_override::ChoiceMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 1);
    auto* p = std::get_if<type_name_override::HeartbeatPayload>(&dec->payload());
    REQUIRE(p != nullptr);
    CHECK(p->timestamp() == 0xDEADBEEF);
    CHECK(p->sequence() == 42);
}

TEST_CASE("typeName choice: PositionPayload roundtrip", "[type_name][choice]") {
    type_name_override::ChoiceMsg msg;
    msg.set_tag(2);

    type_name_override::PositionPayload pos;
    pos.set_lat(123456);
    pos.set_lon(654321);
    msg.set_payload(pos);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = type_name_override::ChoiceMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 2);
    auto* p = std::get_if<type_name_override::PositionPayload>(&dec->payload());
    REQUIRE(p != nullptr);
    CHECK(p->lat() == 123456);
    CHECK(p->lon() == 654321);
}

TEST_CASE("typeName choice: UnknownPayload otherwise roundtrip", "[type_name][choice]") {
    type_name_override::ChoiceMsg msg;
    msg.set_tag(99);

    // Decode a message with an unrecognized tag — should fall into otherwise
    std::vector<uint8_t> wire = {99, 0x42};  // tag=99, raw_byte=0x42
    auto dec = type_name_override::ChoiceMsg::decode_bytes(wire);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 99);
    auto* p = std::get_if<type_name_override::UnknownPayload>(&dec->payload());
    REQUIRE(p != nullptr);
    CHECK(p->raw_byte() == 0x42);
}

// ============================================================================
// Choice element typeName (variant alias override) roundtrip
// ============================================================================

TEST_CASE("typeName choice variant: BodyVariant alias roundtrip", "[type_name][choice]") {
    type_name_override::ChoiceVariantMsg msg;
    msg.set_kind(1);

    type_name_override::ChoiceVariantMsg_alpha alpha;
    alpha.set_x(0x1234);
    msg.set_body(alpha);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = type_name_override::ChoiceVariantMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->kind() == 1);
    auto* p = std::get_if<type_name_override::ChoiceVariantMsg_alpha>(&dec->body());
    REQUIRE(p != nullptr);
    CHECK(p->x() == 0x1234);
}

TEST_CASE("typeName choice variant: BodyVariant is the using-declaration name", "[type_name][choice]") {
    // This would fail to compile if the choice typeName override was not applied.
    // Without typeName, the variant alias would be ChoiceVariantMsg_BodyVariant.
    // With typeName="BodyVariant", it should be just BodyVariant.
    using V = type_name_override::BodyVariant;
    V v = type_name_override::ChoiceVariantMsg_alpha{};
    CHECK(std::holds_alternative<type_name_override::ChoiceVariantMsg_alpha>(v));
}

// ============================================================================
// Inline struct typeName roundtrip
// ============================================================================

TEST_CASE("typeName struct: MsgHeader roundtrip", "[type_name][struct]") {
    type_name_override::StructMsg msg;
    msg.set_version(1);
    msg.mutable_header().set_src(0x1234);
    msg.mutable_header().set_dst(0x5678);
    msg.set_data(0xAABBCCDD);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = type_name_override::StructMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->version() == 1);
    CHECK(dec->header().src() == 0x1234);
    CHECK(dec->header().dst() == 0x5678);
    CHECK(dec->data() == 0xAABBCCDD);
}

// ============================================================================
// Inline array element typeName roundtrip
// ============================================================================

TEST_CASE("typeName array: ArrayItem roundtrip", "[type_name][array]") {
    type_name_override::ArrayMsg msg;
    msg.set_count(2);

    type_name_override::ArrayItem item1;
    item1.set_id(1);
    item1.set_value(100);

    type_name_override::ArrayItem item2;
    item2.set_id(2);
    item2.set_value(200);

    msg.mutable_items().push_back(item1);
    msg.mutable_items().push_back(item2);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = type_name_override::ArrayMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->count() == 2);
    REQUIRE(dec->items().size() == 2);
    CHECK(dec->items()[0].id() == 1);
    CHECK(dec->items()[0].value() == 100);
    CHECK(dec->items()[1].id() == 2);
    CHECK(dec->items()[1].value() == 200);
}

// ============================================================================
// Verify class name is the typeName, not the auto-generated name
// ============================================================================

TEST_CASE("typeName produces correct class names", "[type_name]") {
    // These would fail to compile if the typeName was not applied correctly.
    // The auto-generated names would be ChoiceMsg_heartbeat, ChoiceMsg_position, etc.
    type_name_override::HeartbeatPayload hb;
    hb.set_timestamp(1);
    CHECK(hb.timestamp() == 1);

    type_name_override::PositionPayload pos;
    pos.set_lat(2);
    CHECK(pos.lat() == 2);

    type_name_override::UnknownPayload unk;
    unk.set_raw_byte(3);
    CHECK(unk.raw_byte() == 3);

    type_name_override::MsgHeader hdr;
    hdr.set_src(4);
    CHECK(hdr.src() == 4);

    type_name_override::ArrayItem item;
    item.set_id(5);
    CHECK(item.id() == 5);

    // Choice variant alias override: BodyVariant instead of ChoiceVariantMsg_BodyVariant
    type_name_override::BodyVariant body = type_name_override::ChoiceVariantMsg_alpha{};
    CHECK(std::holds_alternative<type_name_override::ChoiceVariantMsg_alpha>(body));
}
