// SPDX-License-Identifier: MIT
// Bgen tests - Inline field type parity tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "inline_field_types/messages.hpp"

TEST_CASE("inline float32 roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(3.14f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK_THAT(dec->temperature(), Catch::Matchers::WithinRel(3.14f, 0.001f));
}

TEST_CASE("inline float64 roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(2.718281828);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK_THAT(dec->latitude(), Catch::Matchers::WithinRel(2.718281828, 1e-9));
}

TEST_CASE("inline string with IA5 encoding roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("ABCDEF");
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->callsign() == "ABCDEF");
}

TEST_CASE("inline bytes roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.mutable_raw_data() = {0x01, 0x02, 0x03, 0x04};
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->raw_data()[0] == 0x01);
    CHECK(dec->raw_data()[1] == 0x02);
    CHECK(dec->raw_data()[2] == 0x03);
    CHECK(dec->raw_data()[3] == 0x04);
}

TEST_CASE("inline bool roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(1);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->active() == 1);
}

TEST_CASE("inline base=int is signed", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(-100);
    msg.set_callsign("");
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->offset() == -100);
}

TEST_CASE("inline string padding and right-trim", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("AB");  // 2 chars in 8-byte field, space-padded
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->callsign() == "AB");  // right-trimmed back to "AB"
}

TEST_CASE("inline string full-length roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("ABCDEFGH");  // exactly 8 chars
    msg.set_active(0);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->callsign() == "ABCDEFGH");
}

TEST_CASE("inline format=octal in to_string", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(0);
    msg.set_mode3a(0777);  // 511 decimal = 0777 octal
    msg.set_tag(0);

    auto str = msg.to_string();
    // format="octal" outputs "mode3a=0777" (std::oct prefix)
    CHECK(str.find("mode3a=0777") != std::string::npos);
}
