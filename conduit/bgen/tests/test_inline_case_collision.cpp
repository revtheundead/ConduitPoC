// SPDX-License-Identifier: MIT
// Tests for inline case type naming collision prevention.
// Both MsgAlpha and MsgBeta define inline cases named "TypeA" and "TypeB",
// which are generated as MsgAlpha_TypeA, MsgAlpha_TypeB, MsgBeta_TypeA, MsgBeta_TypeB.

#include <catch2/catch_test_macros.hpp>

#include "inline_case_collision/messages.hpp"

TEST_CASE("inline case collision: MsgAlpha TypeA roundtrip", "[inline_case][collision]") {
    inline_case_collision::MsgAlpha msg;
    msg.set_tag(1);

    inline_case_collision::MsgAlpha_TypeA ta;
    ta.set_alpha_val(0xDEADBEEF);
    msg.set_payload(ta);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = inline_case_collision::MsgAlpha::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 1);
    auto* a = std::get_if<inline_case_collision::MsgAlpha_TypeA>(&dec->payload());
    REQUIRE(a != nullptr);
    CHECK(a->alpha_val() == 0xDEADBEEF);
}

TEST_CASE("inline case collision: MsgBeta TypeA roundtrip", "[inline_case][collision]") {
    inline_case_collision::MsgBeta msg;
    msg.set_tag(1);

    inline_case_collision::MsgBeta_TypeA ta;
    ta.set_beta_x(100);
    ta.set_beta_y(200);
    msg.set_payload(ta);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());

    auto dec = inline_case_collision::MsgBeta::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 1);
    auto* a = std::get_if<inline_case_collision::MsgBeta_TypeA>(&dec->payload());
    REQUIRE(a != nullptr);
    CHECK(a->beta_x() == 100);
    CHECK(a->beta_y() == 200);
}

TEST_CASE("inline case collision: distinct class types", "[inline_case][collision]") {
    // Verify MsgAlpha_TypeA and MsgBeta_TypeA are distinct types with different fields
    inline_case_collision::MsgAlpha_TypeA alpha_ta;
    alpha_ta.set_alpha_val(42);
    CHECK(alpha_ta.alpha_val() == 42);

    inline_case_collision::MsgBeta_TypeA beta_ta;
    beta_ta.set_beta_x(10);
    beta_ta.set_beta_y(20);
    CHECK(beta_ta.beta_x() == 10);
    CHECK(beta_ta.beta_y() == 20);

    // TypeB variants are also distinct
    inline_case_collision::MsgAlpha_TypeB alpha_tb;
    alpha_tb.set_alpha_flag(0xFF);
    CHECK(alpha_tb.alpha_flag() == 0xFF);

    inline_case_collision::MsgBeta_TypeB beta_tb;
    beta_tb.set_beta_code(0x1234);
    CHECK(beta_tb.beta_code() == 0x1234);
}
