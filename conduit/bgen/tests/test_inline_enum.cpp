// SPDX-License-Identifier: MIT
// Bgen tests - Inline enum roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "inline_enum/messages.hpp"

TEST_CASE("Inline enum roundtrip", "[inline_enum]") {
    inline_enum::InlineEnumMsg msg;
    msg.set_mode(inline_enum::InlineEnumMsg_Mode::active);
    msg.set_priority(inline_enum::InlineEnumMsg_Priority::high);
    msg.set_data(0x1234);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = inline_enum::InlineEnumMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->mode() == inline_enum::InlineEnumMsg_Mode::active);
    CHECK(dec->priority() == inline_enum::InlineEnumMsg_Priority::high);
    CHECK(dec->data() == 0x1234);
}

TEST_CASE("Inline enum all values roundtrip", "[inline_enum]") {
    // Test each mode value
    for (int i = 0; i <= 2; i++) {
        inline_enum::InlineEnumMsg msg;
        msg.set_mode(static_cast<inline_enum::InlineEnumMsg_Mode>(i));
        msg.set_priority(inline_enum::InlineEnumMsg_Priority::low);
        msg.set_data(0);

        auto enc = msg.encode_bytes();
        REQUIRE(enc.has_value());
        auto dec = inline_enum::InlineEnumMsg::decode_bytes(*enc);
        REQUIRE(dec.has_value());
        CHECK(static_cast<int>(dec->mode()) == i);
    }
}

TEST_CASE("Inline enum to_string", "[inline_enum]") {
    inline_enum::InlineEnumMsg msg;
    msg.set_mode(inline_enum::InlineEnumMsg_Mode::standby);
    msg.set_priority(inline_enum::InlineEnumMsg_Priority::medium);
    msg.set_data(42);

    auto str = msg.to_string();
    CHECK(str.find("standby") != std::string::npos);
    CHECK(str.find("medium") != std::string::npos);
}

TEST_CASE("Inline enum default initialization", "[inline_enum]") {
    // Default should be first enum value
    inline_enum::InlineEnumMsg msg;
    CHECK(msg.mode() == inline_enum::InlineEnumMsg_Mode::off);
    CHECK(msg.priority() == inline_enum::InlineEnumMsg_Priority::low);
}

TEST_CASE("Inline enum accessor returns by value", "[inline_enum]") {
    // Inline enum accessors should return by value, not by const reference,
    // since enums are small scalar types.  Verify with static_assert on the
    // return type of mode() and priority().
    inline_enum::InlineEnumMsg msg;
    static_assert(std::is_same_v<decltype(msg.mode()), inline_enum::InlineEnumMsg_Mode>,
                  "mode() should return InlineEnumMsg_Mode by value");
    static_assert(std::is_same_v<decltype(msg.priority()), inline_enum::InlineEnumMsg_Priority>,
                  "priority() should return InlineEnumMsg_Priority by value");
    // Note: optional inline enum accessor test is skipped because the
    // inline_enum fixture has no FX block or optional inline enum fields.
}
