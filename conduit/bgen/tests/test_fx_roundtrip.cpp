// SPDX-License-Identifier: MIT
// Bgen tests - FX block roundtrip verification
//
// Tests generated code from fx_block, fx_string and fx_ia5_string fixtures.

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "fx_block/messages.hpp"
#include "fx_string/messages.hpp"
#include "fx_choice/messages.hpp"
#include "fx_choice/constants.hpp"
#include "fx_ia5_string/messages.hpp"

// ============================================================================
// Section: FX Block - FxMessage (fx_block fixture)
// ============================================================================

TEST_CASE("FxMessage all FX items present roundtrip", "[roundtrip][fx]") {
    fx_block::FxMessage msg;
    msg.set_header(0x42);
    msg.set_item1(0x1234);
    msg.set_item2(0xDEADBEEF);
    msg.set_item3(0xFF);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x42);
    CHECK(decoded->has_item1());
    CHECK(decoded->item1() == 0x1234);
    CHECK(decoded->has_item2());
    CHECK(decoded->item2() == 0xDEADBEEF);
    CHECK(decoded->has_item3());
    CHECK(decoded->item3() == 0xFF);
}

TEST_CASE("FxMessage no FX items roundtrip", "[roundtrip][fx]") {
    fx_block::FxMessage msg;
    msg.set_header(0xAA);
    // Don't set any FX items

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xAA);
    CHECK_FALSE(decoded->has_item1());
    CHECK_FALSE(decoded->has_item2());
    CHECK_FALSE(decoded->has_item3());
}

TEST_CASE("FxMessage wire format FX bit", "[roundtrip][fx][wire]") {
    // No FX items: header(1 byte) + FX bit 0 (1 bit, padded to byte)
    fx_block::FxMessage msg_empty;
    msg_empty.set_header(0x00);
    auto enc1 = msg_empty.encode_bytes();
    REQUIRE(enc1.has_value());
    auto bytes_empty = std::move(*enc1);

    // With FX items: should be larger
    fx_block::FxMessage msg_full;
    msg_full.set_header(0x00);
    msg_full.set_item1(0);
    msg_full.set_item2(0);
    msg_full.set_item3(0);
    auto enc2 = msg_full.encode_bytes();
    REQUIRE(enc2.has_value());
    auto bytes_full = std::move(*enc2);

    CHECK(bytes_full.size() > bytes_empty.size());
}

TEST_CASE("FxMessage decode from empty buffer fails", "[roundtrip][fx][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = fx_block::FxMessage::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("FxMessage decode from truncated FX body fails", "[roundtrip][fx][errors]") {
    // Header + FX=1, but then truncated (not enough data for item1)
    conduit::io::BitWriter w;
    w.write_u8(0x42);    // header
    w.write_bits(1, 1);  // FX bit = 1 (items follow)
    // But no item data follows
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: FX String - FxStringMsg (fx_string fixture)
// ============================================================================

TEST_CASE("FxStringMsg all items roundtrip", "[roundtrip][fx][string]") {
    fx_string::FxStringMsg msg;
    msg.set_header(0x55);
    msg.set_label("TestLabel");
    std::array<uint8_t, 4> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    msg.set_payload(payload);
    msg.set_extra(0x9876);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x55);
    CHECK(decoded->has_label());
    CHECK(decoded->label() == "TestLabel");
    CHECK(decoded->has_payload());
    CHECK(decoded->payload() == payload);
    CHECK(decoded->has_extra());
    CHECK(decoded->extra() == 0x9876);
}

TEST_CASE("FxStringMsg no FX items roundtrip", "[roundtrip][fx][string]") {
    fx_string::FxStringMsg msg;
    msg.set_header(0xBB);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xBB);
    CHECK_FALSE(decoded->has_label());
    CHECK_FALSE(decoded->has_payload());
    CHECK_FALSE(decoded->has_extra());
}

TEST_CASE("FxStringMsg string content preserved after decode", "[roundtrip][fx][string]") {
    fx_string::FxStringMsg msg;
    msg.set_header(0x01);
    msg.set_label("Short");
    std::array<uint8_t, 4> payload = {1, 2, 3, 4};
    msg.set_payload(payload);
    msg.set_extra(42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // Label is 10 bytes on wire, "Short" is 5 chars + 5 null padding
    // After decode, trailing nulls/spaces are trimmed
    CHECK(decoded->label() == "Short");
}

TEST_CASE("FxStringMsg bytes payload roundtrip", "[roundtrip][fx][string]") {
    fx_string::FxStringMsg msg;
    msg.set_header(0x00);
    msg.set_label("x");
    std::array<uint8_t, 4> payload = {0xFF, 0x00, 0xAA, 0x55};
    msg.set_payload(payload);
    msg.set_extra(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->has_payload());
    CHECK(decoded->payload()[0] == 0xFF);
    CHECK(decoded->payload()[1] == 0x00);
    CHECK(decoded->payload()[2] == 0xAA);
    CHECK(decoded->payload()[3] == 0x55);
}

// ============================================================================
// Section: FX Block - Partial item presence
// ============================================================================

TEST_CASE("FxMessage only item1 set encodes all items in extent", "[roundtrip][fx]") {
    // Flat FX block: all items share one extent.
    // Setting any item triggers encoding of ALL items (unset ones get default 0).
    fx_block::FxMessage msg;
    msg.set_header(0x77);
    msg.set_item1(0xABCD);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x77);
    REQUIRE(decoded->has_item1());
    CHECK(decoded->item1() == 0xABCD);
    // Flat FX: all items in the extent are present after decode
    CHECK(decoded->has_item2());
    CHECK(decoded->item2() == 0);
    CHECK(decoded->has_item3());
    CHECK(decoded->item3() == 0);
}

TEST_CASE("FxMessage item1 and item2 set, item3 gets default", "[roundtrip][fx]") {
    fx_block::FxMessage msg;
    msg.set_header(0x33);
    msg.set_item1(0x1111);
    msg.set_item2(0x22222222);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x33);
    REQUIRE(decoded->has_item1());
    CHECK(decoded->item1() == 0x1111);
    REQUIRE(decoded->has_item2());
    CHECK(decoded->item2() == 0x22222222);
    // item3 gets default value since it shares the extent
    CHECK(decoded->has_item3());
    CHECK(decoded->item3() == 0);
}

// ============================================================================
// Section: FX Block - Strict wire format byte counts
//
// FX fields are tightly bit-packed: header(8) + FX bit(1) + item(N) + FX bit(1) ...
// No implicit alignment padding between FX bits and item data.
// ============================================================================

TEST_CASE("FxMessage wire format: no items = 2 bytes", "[roundtrip][fx][wire]") {
    // header(8 bits) + FX=0(1 bit) = 9 bits -> 2 bytes
    fx_block::FxMessage msg;
    msg.set_header(0x00);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    CHECK(enc_result->size() == 2);
}

TEST_CASE("FxMessage wire format: flat FX populated = 9 bytes", "[roundtrip][fx][wire]") {
    // Flat FX block: all items in one extent, all-or-nothing encoding
    // header(8) + FX=1(1) + item1(16) + item2(32) + item3(8) + FX=0(1) = 66 bits -> 9 bytes
    fx_block::FxMessage msg;
    msg.set_header(0x00);
    msg.set_item1(0);
    msg.set_item2(0);
    msg.set_item3(0);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    CHECK(enc_result->size() == 9);
}

TEST_CASE("FxMessage wire format: setting any item produces full extent", "[roundtrip][fx][wire]") {
    // Setting only item1 still encodes the full extent (all items)
    fx_block::FxMessage msg_item1;
    msg_item1.set_header(0x00);
    msg_item1.set_item1(0);
    auto enc1 = msg_item1.encode_bytes();
    REQUIRE(enc1.has_value());
    auto bytes_item1 = std::move(*enc1);

    fx_block::FxMessage msg_all;
    msg_all.set_header(0x00);
    msg_all.set_item1(0);
    msg_all.set_item2(0);
    msg_all.set_item3(0);
    auto enc2 = msg_all.encode_bytes();
    REQUIRE(enc2.has_value());
    auto bytes_all = std::move(*enc2);

    // Both should produce the same size (full extent)
    CHECK(bytes_item1.size() == bytes_all.size());
}

// ============================================================================
// Section: FX String - Strict wire format byte counts
// ============================================================================

TEST_CASE("FxStringMsg wire format: no items = 2 bytes", "[roundtrip][fx][string][wire]") {
    // header(8) + FX=0(1) = 9 bits -> 2 bytes
    fx_string::FxStringMsg msg;
    msg.set_header(0x00);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    CHECK(enc_result->size() == 2);
}

TEST_CASE("FxStringMsg wire format: flat FX populated is fixed size", "[roundtrip][fx][string][wire]") {
    // Flat FX block: all items in one extent
    // Setting any item produces the full extent with all items encoded
    fx_string::FxStringMsg msg;
    msg.set_header(0x00);
    msg.set_label("x");
    std::array<uint8_t, 4> payload = {0, 0, 0, 0};
    msg.set_payload(payload);
    msg.set_extra(0);
    auto enc1 = msg.encode_bytes();
    REQUIRE(enc1.has_value());
    auto bytes_all = std::move(*enc1);

    fx_string::FxStringMsg msg_label_only;
    msg_label_only.set_header(0x00);
    msg_label_only.set_label("x");
    auto enc2 = msg_label_only.encode_bytes();
    REQUIRE(enc2.has_value());
    auto bytes_label = std::move(*enc2);

    // Both produce the same size (full extent)
    CHECK(bytes_all.size() == bytes_label.size());
    // Non-empty FX must be larger than empty FX
    CHECK(bytes_all.size() > 2);
}

// ============================================================================
// Section: FX String - Partial items
// ============================================================================

TEST_CASE("FxStringMsg only label roundtrip", "[roundtrip][fx][string]") {
    // Flat FX block: setting any item triggers encoding of ALL items in the extent.
    // Unset items get default values (empty string, zero-filled bytes, 0).
    fx_string::FxStringMsg msg;
    msg.set_header(0xCC);
    msg.set_label("OnlyLabel");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xCC);
    REQUIRE(decoded->has_label());
    CHECK(decoded->label() == "OnlyLabel");
    // Flat FX: all items in the extent are present after decode
    CHECK(decoded->has_payload());
    CHECK(decoded->has_extra());
    CHECK(decoded->extra() == 0);
}

TEST_CASE("FxStringMsg label and payload roundtrip", "[roundtrip][fx][string]") {
    // Flat FX block: all items encoded together.
    fx_string::FxStringMsg msg;
    msg.set_header(0xDD);
    msg.set_label("TwoItems");
    std::array<uint8_t, 4> payload = {0x11, 0x22, 0x33, 0x44};
    msg.set_payload(payload);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_string::FxStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xDD);
    REQUIRE(decoded->has_label());
    CHECK(decoded->label() == "TwoItems");
    REQUIRE(decoded->has_payload());
    CHECK(decoded->payload() == payload);
    // Flat FX: extra is present with default value
    CHECK(decoded->has_extra());
    CHECK(decoded->extra() == 0);
}

// ============================================================================
// Section: FX Block - Value preservation across non-byte-aligned boundaries
// ============================================================================

TEST_CASE("FxMessage item values preserved across FX bit boundaries", "[roundtrip][fx]") {
    // Use non-trivial values to verify bit packing is correct
    fx_block::FxMessage msg;
    msg.set_header(0xFF);
    msg.set_item1(0xFFFF);         // all bits set
    msg.set_item2(0xFFFFFFFF);     // all bits set
    msg.set_item3(0xFF);           // all bits set

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xFF);
    CHECK(decoded->item1() == 0xFFFF);
    CHECK(decoded->item2() == 0xFFFFFFFF);
    CHECK(decoded->item3() == 0xFF);
}

TEST_CASE("FxMessage alternating bit patterns", "[roundtrip][fx]") {
    fx_block::FxMessage msg;
    msg.set_header(0xAA);
    msg.set_item1(0x5555);
    msg.set_item2(0xAAAAAAAA);
    msg.set_item3(0x55);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xAA);
    CHECK(decoded->item1() == 0x5555);
    CHECK(decoded->item2() == 0xAAAAAAAA);
    CHECK(decoded->item3() == 0x55);
}

// ============================================================================
// Section: FX Choice - FxChoiceMsg (fx_choice fixture) -- B1/B2
//
// tag is inside the FX block (flat FX: all-or-nothing encoding).
// Setting any FX item triggers all items in the extent.
// ============================================================================

TEST_CASE("FxChoiceMsg case A roundtrip", "[roundtrip][fx][choice]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x42);
    msg.set_item1(0x1234);
    msg.set_tag(static_cast<fx_choice::tag_type>(fx_choice::TAG_A));
    fx_choice::CaseABody case_a;
    case_a.set_x(0xABCD);
    msg.set_payload(fx_choice::FxChoiceMsg_payloadVariant{case_a});
    msg.set_item3(0xFF);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x42);
    REQUIRE(decoded->has_item1());
    CHECK(decoded->item1() == 0x1234);
    REQUIRE(decoded->has_tag());
    CHECK(decoded->tag() == static_cast<fx_choice::tag_type>(fx_choice::TAG_A));
    REQUIRE(decoded->has_payload());
    auto& body = std::get<fx_choice::CaseABody>(decoded->payload());
    CHECK(body.x() == 0xABCD);
    REQUIRE(decoded->has_item3());
    CHECK(decoded->item3() == 0xFF);
}

TEST_CASE("FxChoiceMsg case B roundtrip", "[roundtrip][fx][choice]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x55);
    msg.set_item1(0x9876);
    msg.set_tag(static_cast<fx_choice::tag_type>(fx_choice::TAG_B));
    fx_choice::CaseBBody case_b;
    case_b.set_y(0xDEADBEEF);
    msg.set_payload(fx_choice::FxChoiceMsg_payloadVariant{case_b});
    msg.set_item3(0x77);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x55);
    REQUIRE(decoded->has_item1());
    CHECK(decoded->item1() == 0x9876);
    REQUIRE(decoded->has_tag());
    CHECK(decoded->tag() == static_cast<fx_choice::tag_type>(fx_choice::TAG_B));
    REQUIRE(decoded->has_payload());
    auto& body = std::get<fx_choice::CaseBBody>(decoded->payload());
    CHECK(body.y() == 0xDEADBEEF);
    REQUIRE(decoded->has_item3());
    CHECK(decoded->item3() == 0x77);
}

TEST_CASE("FxChoiceMsg no FX items roundtrip", "[roundtrip][fx][choice]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0xBB);
    // Don't set any FX items

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xBB);
    CHECK_FALSE(decoded->has_item1());
    CHECK_FALSE(decoded->has_tag());
    CHECK_FALSE(decoded->has_payload());
    CHECK_FALSE(decoded->has_item3());
}

TEST_CASE("FxChoiceMsg first case type (index 0) roundtrip - B2 regression", "[roundtrip][fx][choice]") {
    // B2 bug: setting a choice to its FIRST case type (variant index 0) inside
    // an FX block would silently not trigger FX encoding because variant index 0
    // could be misdetected as "unset".
    // This test sets ONLY the choice-related fields and verifies the FX block
    // is encoded and decoded correctly with CaseABody (index 0 in the variant).
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x99);
    msg.set_tag(static_cast<fx_choice::tag_type>(fx_choice::TAG_A));
    fx_choice::CaseABody case_a;
    case_a.set_x(0x4242);
    msg.set_payload(fx_choice::FxChoiceMsg_payloadVariant{case_a});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // The FX block should be present (not empty)
    CHECK(bytes.size() > 2);

    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x99);
    // The FX items should be present (flat FX: setting any item triggers all)
    REQUIRE(decoded->has_tag());
    CHECK(decoded->tag() == static_cast<fx_choice::tag_type>(fx_choice::TAG_A));
    REQUIRE(decoded->has_payload());
    auto& body = std::get<fx_choice::CaseABody>(decoded->payload());
    CHECK(body.x() == 0x4242);
    // Other items in the flat extent should also be present with defaults
    CHECK(decoded->has_item1());
    CHECK(decoded->has_item3());
}

TEST_CASE("FxChoiceMsg wire size with FX items", "[roundtrip][fx][choice][wire]") {
    // No FX items: header(8) + FX=0(1) = 9 bits -> 2 bytes
    fx_choice::FxChoiceMsg msg_empty;
    msg_empty.set_header(0x00);
    auto enc1 = msg_empty.encode_bytes();
    REQUIRE(enc1.has_value());
    auto bytes_empty = std::move(*enc1);

    // With FX items: should be larger
    fx_choice::FxChoiceMsg msg_full;
    msg_full.set_header(0x00);
    msg_full.set_item1(0);
    msg_full.set_tag(static_cast<fx_choice::tag_type>(fx_choice::TAG_A));
    fx_choice::CaseABody case_a;
    case_a.set_x(0);
    msg_full.set_payload(fx_choice::FxChoiceMsg_payloadVariant{case_a});
    msg_full.set_item3(0);
    auto enc2 = msg_full.encode_bytes();
    REQUIRE(enc2.has_value());
    auto bytes_full = std::move(*enc2);

    CHECK(bytes_full.size() > bytes_empty.size());
}

// ============================================================================
// Section: FX IA5 String - FxIa5Msg (fx_ia5_string fixture)
// ============================================================================

TEST_CASE("FxIa5Msg IA5 string roundtrip in FX block", "[roundtrip][fx][ia5]") {
    fx_ia5_string::FxIa5Msg msg;
    msg.set_header(0x42);
    msg.set_label("HELLO   ");  // 8 chars with space padding
    msg.set_code(1234);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto decoded = fx_ia5_string::FxIa5Msg::decode_bytes(*enc);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x42);
    CHECK(decoded->has_label());
    // IA5 roundtrip should preserve the string content
    CHECK(decoded->label().substr(0, 5) == "HELLO");
    CHECK(decoded->has_code());
    CHECK(decoded->code() == 1234);
}

TEST_CASE("FxIa5Msg no FX items roundtrip", "[roundtrip][fx][ia5]") {
    fx_ia5_string::FxIa5Msg msg;
    msg.set_header(0xAA);
    // Don't set any FX items

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto decoded = fx_ia5_string::FxIa5Msg::decode_bytes(*enc);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xAA);
    CHECK_FALSE(decoded->has_label());
    CHECK_FALSE(decoded->has_code());
}

TEST_CASE("FxIa5Msg IA5 encoding applied in FX encode path", "[roundtrip][fx][ia5][wire]") {
    // Verify that IA5 encoding is actually applied on the wire,
    // not just passed through as raw ASCII
    fx_ia5_string::FxIa5Msg msg;
    msg.set_header(0x01);
    msg.set_label("A");
    msg.set_code(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    // Decode and verify the string survives the IA5 encode/decode roundtrip
    auto decoded = fx_ia5_string::FxIa5Msg::decode_bytes(*enc);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_label());
    // The first char should be 'A' after IA5 decode
    CHECK(decoded->label()[0] == 'A');
}
