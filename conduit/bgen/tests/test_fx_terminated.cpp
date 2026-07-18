// SPDX-License-Identifier: MIT
// Bgen tests — FX-terminated arrays (count="fx"), FX optional-count unwrapping,
// and enum-typed message-id fields.
//
// Covers:
//   Issue 1: count="fx" arrays repeat while the FX continuation bit is set.
//   Issue 2: an array whose count-from references an optional field in the same
//            FX extent must dereference the optional before casting to a count.
//   Issue 3: an enum-typed auto="id" frame field compiles and dispatches.

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "fx_terminated/messages.hpp"
#include "fx_optional_count/messages.hpp"
#include "frame_enum_id/messages.hpp"
#include "frame_enum_id/sessions.hpp"
#include "frame_enum_id/protocol.hpp"
#include "scale_identity/messages.hpp"

#include <type_traits>
#include <utility>

// ============================================================================
// Issue 1: count="fx" FX-terminated arrays
// ============================================================================

TEST_CASE("fx-terminated: typed element roundtrip", "[fx][fx-terminated][roundtrip]") {
    fx_terminated::TypedRepeat msg;
    msg.set_header(0x2A);
    msg.mutable_octets() = {1, 2, 3, 4};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    // header (1 byte) + 4 units of (7 data + 1 FX) = 1 + 4 = 5 bytes
    CHECK(enc->size() == 5);

    auto dec = fx_terminated::TypedRepeat::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->header() == 0x2A);
    REQUIRE(dec->octets().size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(dec->octets()[i] == static_cast<uint8_t>(i + 1));
    }
}

TEST_CASE("fx-terminated: single element roundtrip", "[fx][fx-terminated][roundtrip]") {
    fx_terminated::TypedRepeat msg;
    msg.set_header(0x01);
    msg.mutable_octets() = {0x7F};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    // header (1 byte) + 1 unit (8 bits) = 2 bytes
    CHECK(enc->size() == 2);

    auto dec = fx_terminated::TypedRepeat::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    REQUIRE(dec->octets().size() == 1);
    CHECK(dec->octets()[0] == 0x7F);
}

TEST_CASE("fx-terminated: FX continuation bit governs element count",
          "[fx][fx-terminated][wire]") {
    // Two elements: header=0x00, then a=1 (0b001) FX=1, then a=2 (0b010) FX=0.
    fx_terminated::TypedRepeat two;
    two.set_header(0x00);
    two.mutable_octets() = {0x01, 0x02};
    auto enc2 = two.encode_bytes();
    REQUIRE(enc2.has_value());
    CHECK(enc2->size() == 3);
    // Last unit's FX bit (LSB of final byte) must be 0.
    CHECK(((*enc2)[2] & 0x01) == 0);
    // First unit's FX bit (LSB of second byte) must be 1.
    CHECK(((*enc2)[1] & 0x01) == 1);
}

TEST_CASE("fx-terminated: inline struct element roundtrip",
          "[fx][fx-terminated][roundtrip]") {
    fx_terminated::InlineRepeat msg;
    msg.set_header(0xAB);
    fx_terminated::InlineRepeat_extentsElement e1;
    e1.set_a(5);
    e1.set_b(9);
    fx_terminated::InlineRepeat_extentsElement e2;
    e2.set_a(2);
    e2.set_b(7);
    msg.mutable_extents() = {e1, e2};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    // header (1 byte) + 2 units of 8 bits = 3 bytes
    CHECK(enc->size() == 3);

    auto dec = fx_terminated::InlineRepeat::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->header() == 0xAB);
    REQUIRE(dec->extents().size() == 2);
    CHECK(dec->extents()[0].a() == 5);
    CHECK(dec->extents()[0].b() == 9);
    CHECK(dec->extents()[1].a() == 2);
    CHECK(dec->extents()[1].b() == 7);
}

// ============================================================================
// Issue 2: array count-from referencing an optional field in the same FX extent
// ============================================================================

TEST_CASE("fx optional count: array count-from optional field roundtrip",
          "[fx][optional][roundtrip]") {
    fx_optional_count::RepMsg msg;
    msg.set_header(0x11);
    msg.set_rep(3);
    msg.mutable_items() = {10, 20, 30};

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = fx_optional_count::RepMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->header() == 0x11);
    REQUIRE(dec->has_rep());
    CHECK(dec->rep() == 3);
    REQUIRE(dec->has_items());
    REQUIRE(dec->items().size() == 3);
    CHECK(dec->items()[0] == 10);
    CHECK(dec->items()[1] == 20);
    CHECK(dec->items()[2] == 30);
}

TEST_CASE("fx optional count: absent FX extent roundtrip",
          "[fx][optional][roundtrip]") {
    fx_optional_count::RepMsg msg;
    msg.set_header(0x22);
    // rep/items left unset — the FX extent is omitted entirely.

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = fx_optional_count::RepMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->header() == 0x22);
    CHECK_FALSE(dec->has_rep());
}

// ============================================================================
// Issue 3: enum-typed auto="id" frame field
// ============================================================================

TEST_CASE("enum id: Heartbeat roundtrip via frame", "[frame][enum-id][roundtrip]") {
    frame_enum_id::Heartbeat msg;
    msg.set_ts(999);

    auto frame = frame_enum_id::EnumFrame::wrap(msg);
    CHECK(frame.msg_type() == frame_enum_id::msg_id::heartbeat);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());
    // [msg_type:1][length:2][ts:2] = 5 bytes
    REQUIRE(bytes->size() == 5);
    CHECK((*bytes)[0] == 1);  // msg_id::heartbeat == 1

    auto decoded = frame_enum_id::EnumFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == frame_enum_id::msg_id::heartbeat);

    auto* payload = std::get_if<frame_enum_id::Heartbeat>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->ts() == 999);
}

TEST_CASE("enum id: Status dispatches to the correct message",
          "[frame][enum-id][roundtrip]") {
    frame_enum_id::Status msg;
    msg.set_code(0xBEEF);

    auto frame = frame_enum_id::EnumFrame::wrap(msg);
    CHECK(frame.msg_type() == frame_enum_id::msg_id::status);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());
    CHECK((*bytes)[0] == 2);  // msg_id::status == 2

    auto decoded = frame_enum_id::EnumFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == frame_enum_id::msg_id::status);
    auto* payload = std::get_if<frame_enum_id::Status>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->code() == 0xBEEF);
}

// ============================================================================
// Identity scale (scale=1) must not promote a non-float field to double
// ============================================================================

TEST_CASE("scale=1 field keeps its integer type", "[scale][identity]") {
    scale_identity::ScaleMsg msg;
    // A scale of 1 with no offset is the identity: the accessor must be an
    // integer, not a double. Verified at compile time via the accessor type.
    using UnitScaledT = std::decay_t<decltype(std::declval<scale_identity::ScaleMsg>().unit_scaled())>;
    using InlineUnitT = std::decay_t<decltype(std::declval<scale_identity::ScaleMsg>().inline_unit())>;
    using RealScaledT = std::decay_t<decltype(std::declval<scale_identity::ScaleMsg>().real_scaled())>;
    STATIC_REQUIRE_FALSE(std::is_floating_point_v<UnitScaledT>);
    STATIC_REQUIRE_FALSE(std::is_floating_point_v<InlineUnitT>);
    // A genuine fractional scale still yields a double.
    STATIC_REQUIRE(std::is_floating_point_v<RealScaledT>);

    msg.set_unit_scaled(1000);
    msg.set_inline_unit(42);
    msg.set_real_scaled(3.5);
    msg.set_plain(7);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = scale_identity::ScaleMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->unit_scaled() == 1000);
    CHECK(dec->inline_unit() == 42);
    CHECK(dec->real_scaled() == 3.5);
    CHECK(dec->plain() == 7);
}
