// SPDX-License-Identifier: MIT
// Bgen tests - Large protocol stress roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "stress_large/protocol.hpp"

// ============================================================================
// Section: 50-field struct roundtrip
// ============================================================================

TEST_CASE("BigRecord 50-field roundtrip", "[roundtrip][stress]") {
    stress_large::BigRecord rec;
    rec.set_f01(0x01);
    rec.set_f02(0x0202);
    rec.set_f03(0x03030303);
    rec.set_f04(-1);
    rec.set_f05(-200);
    rec.set_f06(-300000);
    rec.set_f07(0x07);
    rec.set_f08(0x0808);
    rec.set_f09(0x09090909);
    rec.set_f10(0x0A);
    rec.set_f11(0x0B0B);
    rec.set_f12(0x0C0C0C0C);
    rec.set_f13(-13);
    rec.set_f14(-1414);
    rec.set_f15(-151515);
    rec.set_f16(0x10);
    rec.set_f17(0x1111);
    rec.set_f18(0x12121212);
    rec.set_f19(0x13);
    rec.set_f20(0x1414);
    rec.set_f21(0x15151515);
    rec.set_f22(-22);
    rec.set_f23(-2323);
    rec.set_f24(-242424);
    rec.set_f25(0x19);
    rec.set_f26(0x1A1A);
    rec.set_f27(0x1B1B1B1B);
    rec.set_f28(0x1C);
    rec.set_f29(0x1D1D);
    rec.set_f30(0x1E1E1E1E);
    rec.set_f31(-31);
    rec.set_f32(-3232);
    rec.set_f33(-333333);
    rec.set_f34(0x22);
    rec.set_f35(0x2323);
    rec.set_f36(0x24242424);
    rec.set_f37(0x25);
    rec.set_f38(0x2626);
    rec.set_f39(0x27272727);
    rec.set_f40(-40);
    rec.set_f41(-4141);
    rec.set_f42(-424242);
    rec.set_f43(0x2B);
    rec.set_f44(0x2C2C);
    rec.set_f45(0x2D2D2D2D);
    rec.set_f46(0x2E);
    rec.set_f47(0x2F2F);
    rec.set_f48(0x30303030);
    rec.set_f49(-49);
    rec.set_f50(-5050);

    conduit::io::BitWriter w;
    auto enc_result = rec.encode(w);
    REQUIRE(enc_result.has_value());
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());

    conduit::io::BitReader r(*finish_result);
    auto dec = stress_large::BigRecord::decode(r);
    REQUIRE(dec.has_value());

    CHECK(dec->f01() == 0x01);
    CHECK(dec->f02() == 0x0202);
    CHECK(dec->f03() == 0x03030303);
    CHECK(dec->f04() == -1);
    CHECK(dec->f05() == -200);
    CHECK(dec->f50() == -5050);
}

// ============================================================================
// Section: 20-case choice roundtrip
// ============================================================================

TEST_CASE("StressMsg with case-a roundtrip", "[roundtrip][stress]") {
    stress_large::StressMsg msg;
    stress_large::BigRecord hdr;
    hdr.set_f01(1);
    msg.set_header(hdr);
    msg.set_tag(stress_large::ItemTag::TagA);
    stress_large::CaseA body;
    body.set_val(0xDEADBEEF);
    msg.set_payload(stress_large::payloadVariant{body});

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = stress_large::StressMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == stress_large::ItemTag::TagA);
    auto& decoded_body = std::get<stress_large::CaseA>(dec->payload());
    CHECK(decoded_body.val() == 0xDEADBEEF);
}

TEST_CASE("StressMsg with case-t roundtrip", "[roundtrip][stress]") {
    stress_large::StressMsg msg;
    stress_large::BigRecord hdr;
    hdr.set_f01(20);
    msg.set_header(hdr);
    msg.set_tag(stress_large::ItemTag::TagT);
    stress_large::CaseT body;
    body.set_val(-999999);
    msg.set_payload(stress_large::payloadVariant{body});

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = stress_large::StressMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == stress_large::ItemTag::TagT);
    auto& decoded_body = std::get<stress_large::CaseT>(dec->payload());
    CHECK(decoded_body.val() == -999999);
}

// ============================================================================
// Section: 5-level nested struct roundtrip
// ============================================================================

TEST_CASE("NestedMsg 5-level deep roundtrip", "[roundtrip][stress]") {
    stress_large::Level5 l5;
    l5.set_val(0x55);

    stress_large::Level4 l4;
    l4.set_inner(l5);
    l4.set_val(0x44);

    stress_large::Level3 l3;
    l3.set_inner(l4);
    l3.set_val(0x3333);

    stress_large::Level2 l2;
    l2.set_inner(l3);
    l2.set_val(0x22222222);

    stress_large::NestedMsg msg;
    msg.set_inner(l2);
    msg.set_val(0x11111111);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = stress_large::NestedMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());

    CHECK(dec->val() == 0x11111111);
    CHECK(dec->inner().val() == 0x22222222);
    CHECK(dec->inner().inner().val() == 0x3333);
    CHECK(dec->inner().inner().inner().val() == 0x44);
    CHECK(dec->inner().inner().inner().inner().val() == 0x55);
}

// ============================================================================
// Section: Sustained encode/decode iterations
// ============================================================================

TEST_CASE("Sustained 10000 encode/decode iterations", "[roundtrip][stress][sustained]") {
    for (int i = 0; i < 10000; ++i) {
        stress_large::NestedMsg msg;
        stress_large::Level5 l5;
        l5.set_val(static_cast<uint8_t>(i & 0xFF));
        stress_large::Level4 l4;
        l4.set_inner(l5);
        l4.set_val(static_cast<uint8_t>((i >> 8) & 0xFF));
        stress_large::Level3 l3;
        l3.set_inner(l4);
        l3.set_val(static_cast<uint16_t>(i));
        stress_large::Level2 l2;
        l2.set_inner(l3);
        l2.set_val(static_cast<uint32_t>(i));
        msg.set_inner(l2);
        msg.set_val(static_cast<uint32_t>(i * 7));

        auto enc = msg.encode_bytes();
        REQUIRE(enc.has_value());
        auto dec = stress_large::NestedMsg::decode_bytes(*enc);
        REQUIRE(dec.has_value());
        CHECK(dec->val() == static_cast<uint32_t>(i * 7));
    }
}
