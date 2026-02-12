// SPDX-License-Identifier: MIT
// Bgen tests - Constraint tightening (field narrows range vs wider field)

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "constraint_tighten/messages.hpp"

// ============================================================================
// Section: Tightened constraint — narrow field has tighter range than wide
// ============================================================================

TEST_CASE("Tightened constraint: valid value in narrow range", "[constraints][tighten]") {
    constraint_tighten::TightenedMsg msg;
    auto r1 = msg.set_narrow(100);  // Within 10-200
    REQUIRE(r1.has_value());
    auto r2 = msg.set_wide(500);    // Within 0-1000
    REQUIRE(r2.has_value());
    msg.set_tag(0x42);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = constraint_tighten::TightenedMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->narrow() == 100);
    CHECK(dec->wide() == 500);
    CHECK(dec->tag() == 0x42);
}

TEST_CASE("Tightened constraint: setter rejects below field min", "[constraints][tighten]") {
    constraint_tighten::TightenedMsg msg;
    auto r = msg.set_narrow(5);  // Below field min of 10
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("Tightened constraint: setter rejects above field max", "[constraints][tighten]") {
    constraint_tighten::TightenedMsg msg;
    auto r = msg.set_narrow(201);  // Above field max of 200
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("Tightened constraint: boundary values 10 and 200 pass", "[constraints][tighten]") {
    // Min boundary
    {
        constraint_tighten::TightenedMsg msg;
        auto r = msg.set_narrow(10);
        REQUIRE(r.has_value());
        REQUIRE(msg.set_wide(0).has_value());
        msg.set_tag(0);
        auto enc = msg.encode_bytes();
        REQUIRE(enc.has_value());
        auto dec = constraint_tighten::TightenedMsg::decode_bytes(*enc);
        REQUIRE(dec.has_value());
        CHECK(dec->narrow() == 10);
    }
    // Max boundary
    {
        constraint_tighten::TightenedMsg msg;
        auto r = msg.set_narrow(200);
        REQUIRE(r.has_value());
        REQUIRE(msg.set_wide(1000).has_value());
        msg.set_tag(0xFF);
        auto enc = msg.encode_bytes();
        REQUIRE(enc.has_value());
        auto dec = constraint_tighten::TightenedMsg::decode_bytes(*enc);
        REQUIRE(dec.has_value());
        CHECK(dec->narrow() == 200);
    }
}

TEST_CASE("Tightened constraint: wide field accepts full range", "[constraints][tighten]") {
    constraint_tighten::TightenedMsg msg;
    REQUIRE(msg.set_narrow(100).has_value());
    REQUIRE(msg.set_wide(0).has_value());     // Field min
    msg.set_tag(0);
    auto enc1 = msg.encode_bytes();
    REQUIRE(enc1.has_value());

    REQUIRE(msg.set_wide(1000).has_value());  // Field max
    auto enc2 = msg.encode_bytes();
    REQUIRE(enc2.has_value());
}

TEST_CASE("Tightened constraint: wide field rejects above max", "[constraints][tighten]") {
    constraint_tighten::TightenedMsg msg;
    auto r = msg.set_wide(1001);  // Above max of 1000
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("Tightened constraint: decode rejects narrow violation from wire", "[constraints][tighten]") {
    // Manually encode with narrow=5 (below field constraint 10)
    conduit::io::BitWriter w;
    w.write_u16(5, conduit::io::Endian::Big);   // narrow (violates min=10)
    w.write_u16(100, conduit::io::Endian::Big);  // wide
    w.write_u8(0x01);                             // tag
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto dec = constraint_tighten::TightenedMsg::decode_bytes(data);
    REQUIRE_FALSE(dec.has_value());
    CHECK(dec.error().code() == conduit::ErrorCode::ConstraintViolation);
}
