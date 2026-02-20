// SPDX-License-Identifier: MIT
// Bgen tests - Inline field type parity tests

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "inline_field_types/messages.hpp"

TEST_CASE("inline float32 roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(3.14f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(false);
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
    msg.set_active(false);
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
    msg.set_active(false);
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
    msg.set_raw_data(0x01020304);
    msg.set_active(false);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->raw_data() == 0x01020304);
}

TEST_CASE("inline bool roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(true);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->active() == true);
}

TEST_CASE("inline bool false roundtrip", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_active(false);
    msg.set_mode3a(0);
    msg.set_tag(0);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_field_types::InlineMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->active() == false);
}

TEST_CASE("inline bool getter returns bool type", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_active(true);
    // Verify getter returns bool
    static_assert(std::is_same_v<decltype(msg.active()), bool>,
                  "active() should return bool");
}

TEST_CASE("inline base=int is signed", "[inline_field_types]") {
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(-100);
    msg.set_callsign("");
    msg.set_active(false);
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
    msg.set_active(false);
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
    msg.set_active(false);
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
    msg.set_active(false);
    msg.set_mode3a(0777);  // 511 decimal = 0777 octal
    msg.set_tag(0);

    auto str = msg.to_string();
    // format="octal" outputs "mode3a=0777" (std::oct prefix)
    CHECK(str.find("mode3a=0777") != std::string::npos);
}

TEST_CASE("bool to_string displays true/false", "[inline_field_types]") {
    // Bool fields in to_string should render as "true"/"false", not "1"/"0".
    inline_field_types::InlineMsg msg;
    msg.set_temperature(0.0f);
    msg.set_latitude(0.0);
    msg.set_offset(0);
    msg.set_callsign("");
    msg.set_mode3a(0);
    msg.set_tag(0);

    // Test with active=true
    msg.set_active(true);
    auto str_true = msg.to_string();
    CHECK(str_true.find("active=true") != std::string::npos);
    // Should NOT display as numeric "1"
    CHECK(str_true.find("active=1") == std::string::npos);

    // Test with active=false
    msg.set_active(false);
    auto str_false = msg.to_string();
    CHECK(str_false.find("active=false") != std::string::npos);
    // Should NOT display as numeric "0"
    CHECK(str_false.find("active=0") == std::string::npos);
}
