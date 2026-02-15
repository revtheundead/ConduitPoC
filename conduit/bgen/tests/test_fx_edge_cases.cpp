// SPDX-License-Identifier: MIT
// Bgen tests - FX block edge cases
//
// Tests FX truncation scenarios and nested FX blocks (fx_advanced fixture).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "fx_block/messages.hpp"
#include "fx_advanced/messages.hpp"
#include "fx_choice/messages.hpp"

// ============================================================================
// FX truncation: partial item data
// ============================================================================

TEST_CASE("FX decode with FX=1 and partial item1 data fails", "[fx][errors]") {
    // header(8) + FX=1(1) + only 8 bits of item1 (needs 16)
    conduit::io::BitWriter w;
    w.write_u8(0x00);     // header
    w.write_bits(1, 1);   // FX bit = 1
    w.write_u8(0xAA);     // only 1 byte of item1 (needs 2)
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("FX decode with FX=1, item1 ok, but item2 truncated fails", "[fx][errors]") {
    // header(8) + FX=1(1) + item1(16) + only 8 bits of item2 (needs 32)
    conduit::io::BitWriter w;
    w.write_u8(0x00);      // header
    w.write_bits(1, 1);    // FX bit = 1
    w.write_bits(0x1234, 16);  // item1
    w.write_u8(0xFF);      // only 1 byte of item2 (needs 4)
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = fx_block::FxMessage::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("FX decode single byte buffer (just header, no FX bit) fails", "[fx][errors]") {
    std::vector<uint8_t> data = {0x42};
    auto decoded = fx_block::FxMessage::decode_bytes(data);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// FX Advanced: nested FX blocks
// ============================================================================

TEST_CASE("FxAdvanced no FX items roundtrip", "[roundtrip][fx][nested]") {
    fx_advanced::FxAdvancedMsg msg;
    msg.set_header(0xBB);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_advanced::FxAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xBB);
    CHECK_FALSE(decoded->has_scaled_temp());
    CHECK_FALSE(decoded->has_status());
    CHECK_FALSE(decoded->has_label());
}

TEST_CASE("FxAdvanced outer extent only (no nested FX)", "[roundtrip][fx][nested]") {
    fx_advanced::FxAdvancedMsg msg;
    msg.set_header(0x11);
    msg.set_scaled_temp(100.0);  // int16 raw = (100+40)/0.01 = 14000, fits in int16
    msg.set_status(fx_advanced::fx_status::active);
    msg.set_label("Test");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_advanced::FxAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x11);

    // Outer extent fields
    REQUIRE(decoded->has_scaled_temp());
    CHECK_THAT(decoded->scaled_temp(), Catch::Matchers::WithinRel(100.0, 0.001));
    REQUIRE(decoded->has_status());
    CHECK(decoded->status() == fx_advanced::fx_status::active);
    REQUIRE(decoded->has_label());
    CHECK(decoded->label() == "Test");

    // Inner (nested) FX items should NOT be present
    CHECK_FALSE(decoded->has_sub());
    CHECK_FALSE(decoded->has_values());
}

TEST_CASE("FxAdvanced both extents populated", "[roundtrip][fx][nested]") {
    fx_advanced::FxAdvancedMsg msg;
    msg.set_header(0x22);
    msg.set_scaled_temp(-100.0);  // raw = (-100+40)/0.01 = -6000, fits in int16
    msg.set_status(fx_advanced::fx_status::complete);
    msg.set_label("Full");

    // Nested FX items
    fx_advanced::FxSubStruct sub;
    sub.set_a(0xAA);
    sub.set_b(0xBB);
    msg.set_sub(sub);
    msg.set_values({1, 2, 3, 4});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_advanced::FxAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x22);

    // Outer extent
    REQUIRE(decoded->has_scaled_temp());
    CHECK_THAT(decoded->scaled_temp(), Catch::Matchers::WithinRel(-100.0, 0.001));
    REQUIRE(decoded->has_status());
    CHECK(decoded->status() == fx_advanced::fx_status::complete);
    REQUIRE(decoded->has_label());
    CHECK(decoded->label() == "Full");

    // Inner extent
    REQUIRE(decoded->has_sub());
    CHECK(decoded->sub().a() == 0xAA);
    CHECK(decoded->sub().b() == 0xBB);
    REQUIRE(decoded->has_values());
    auto& vals = decoded->values();
    REQUIRE(vals.size() == 4);
    CHECK(vals[0] == 1);
    CHECK(vals[1] == 2);
    CHECK(vals[2] == 3);
    CHECK(vals[3] == 4);
}

TEST_CASE("FxAdvanced nested extent only (inner without outer) triggers full outer", "[roundtrip][fx][nested]") {
    // Setting any inner FX item must also trigger the outer extent
    // because the inner FX bit is inside the outer extent
    fx_advanced::FxAdvancedMsg msg;
    msg.set_header(0x33);

    fx_advanced::FxSubStruct sub;
    sub.set_a(0x11);
    sub.set_b(0x22);
    msg.set_sub(sub);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = fx_advanced::FxAdvancedMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x33);

    // Outer extent items should be present with defaults (all-or-nothing per extent)
    CHECK(decoded->has_scaled_temp());
    CHECK(decoded->has_status());
    CHECK(decoded->has_label());

    // Inner extent items
    REQUIRE(decoded->has_sub());
    CHECK(decoded->sub().a() == 0x11);
    CHECK(decoded->sub().b() == 0x22);
    // values is in same inner extent, so also present with defaults
    CHECK(decoded->has_values());
}

TEST_CASE("FxAdvanced decode from empty buffer fails", "[fx][nested][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = fx_advanced::FxAdvancedMsg::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Wire format: nested FX has two FX bits
// ============================================================================

TEST_CASE("FxAdvanced wire format: no items = 2 bytes", "[fx][nested][wire]") {
    // header(8) + FX=0(1) = 9 bits -> 2 bytes
    fx_advanced::FxAdvancedMsg msg;
    msg.set_header(0x00);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 2);
}

TEST_CASE("FxAdvanced wire format: outer extent only is larger than empty", "[fx][nested][wire]") {
    fx_advanced::FxAdvancedMsg msg_empty;
    msg_empty.set_header(0x00);
    auto enc_result_empty = msg_empty.encode_bytes();
    REQUIRE(enc_result_empty.has_value());
    auto empty_bytes = std::move(*enc_result_empty);

    fx_advanced::FxAdvancedMsg msg_outer;
    msg_outer.set_header(0x00);
    msg_outer.set_scaled_temp(0);
    auto enc_result_outer = msg_outer.encode_bytes();
    REQUIRE(enc_result_outer.has_value());
    auto outer_bytes = std::move(*enc_result_outer);

    CHECK(outer_bytes.size() > empty_bytes.size());
}

TEST_CASE("FxAdvanced wire format: both extents larger than outer only", "[fx][nested][wire]") {
    fx_advanced::FxAdvancedMsg msg_outer;
    msg_outer.set_header(0x00);
    msg_outer.set_scaled_temp(0);
    auto enc_result_outer = msg_outer.encode_bytes();
    REQUIRE(enc_result_outer.has_value());
    auto outer_bytes = std::move(*enc_result_outer);

    fx_advanced::FxAdvancedMsg msg_both;
    msg_both.set_header(0x00);
    msg_both.set_scaled_temp(0);
    fx_advanced::FxSubStruct sub;
    sub.set_a(0);
    sub.set_b(0);
    msg_both.set_sub(sub);
    auto enc_result_both = msg_both.encode_bytes();
    REQUIRE(enc_result_both.has_value());
    auto both_bytes = std::move(*enc_result_both);

    CHECK(both_bytes.size() > outer_bytes.size());
}

// ============================================================================
// FX Choice: fx_choice fixture — choice inside FX block
// ============================================================================

TEST_CASE("FxChoice: CaseA roundtrip", "[fx][choice][roundtrip]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x42);
    msg.set_item1(1000);
    msg.set_tag(fx_choice::tag_type::case_a);
    fx_choice::CaseABody body;
    body.set_x(0xABCD);
    msg.set_payload(body);
    msg.set_item3(99);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x42);
    REQUIRE(decoded->has_item1());
    CHECK(decoded->item1() == 1000);
    REQUIRE(decoded->has_tag());
    CHECK(decoded->tag() == fx_choice::tag_type::case_a);
    REQUIRE(decoded->has_payload());
    auto* case_a = std::get_if<fx_choice::CaseABody>(&decoded->payload());
    REQUIRE(case_a != nullptr);
    CHECK(case_a->x() == 0xABCD);
    REQUIRE(decoded->has_item3());
    CHECK(decoded->item3() == 99);
}

TEST_CASE("FxChoice: CaseB roundtrip", "[fx][choice][roundtrip]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x55);
    msg.set_item1(2000);
    msg.set_tag(fx_choice::tag_type::case_b);
    fx_choice::CaseBBody body;
    body.set_y(0xDEADBEEF);
    msg.set_payload(body);
    msg.set_item3(7);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x55);
    REQUIRE(decoded->has_tag());
    CHECK(decoded->tag() == fx_choice::tag_type::case_b);
    REQUIRE(decoded->has_payload());
    auto* case_b = std::get_if<fx_choice::CaseBBody>(&decoded->payload());
    REQUIRE(case_b != nullptr);
    CHECK(case_b->y() == 0xDEADBEEF);
    REQUIRE(decoded->has_item3());
    CHECK(decoded->item3() == 7);
}

TEST_CASE("FxChoice: no FX items roundtrip", "[fx][choice][roundtrip]") {
    fx_choice::FxChoiceMsg msg;
    msg.set_header(0x00);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = fx_choice::FxChoiceMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0x00);
    CHECK_FALSE(decoded->has_item1());
    CHECK_FALSE(decoded->has_tag());
    CHECK_FALSE(decoded->has_payload());
    CHECK_FALSE(decoded->has_item3());
}
