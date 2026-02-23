// SPDX-License-Identifier: MIT
// Tests for string encoding edge cases: trim/padding interaction, packed chars,
// and EBCDIC roundtrip behavior.

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/string/encoding.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "string_features/messages.hpp"
#include "ebcdic_strings/messages.hpp"

// ============================================================================
// Null-padded fields: trim should only strip '\0', preserving trailing spaces
// ============================================================================

TEST_CASE("null-padded string preserves trailing spaces on roundtrip",
          "[string][trim]") {
    // name-str is null-padded (padding="null", trim="right")
    // Trailing spaces are content, not padding — they must survive roundtrip
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Hello   ");  // 8 chars, 3 trailing spaces

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Verify wire bytes: "Hello   " + 12 null bytes
    CHECK(bytes[2] == 'H');
    CHECK(bytes[3] == 'e');
    CHECK(bytes[4] == 'l');
    CHECK(bytes[5] == 'l');
    CHECK(bytes[6] == 'o');
    CHECK(bytes[7] == ' ');  // trailing space 1
    CHECK(bytes[8] == ' ');  // trailing space 2
    CHECK(bytes[9] == ' ');  // trailing space 3
    CHECK(bytes[10] == 0x00);  // null padding starts here

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Hello   ");
}

TEST_CASE("null-padded string preserves single trailing space",
          "[string][trim]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Test ");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Test ");
}

TEST_CASE("null-padded bounded-str preserves trailing spaces",
          "[string][trim]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_bounded().set_value("Data   ");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bounded().value() == "Data   ");
}

// ============================================================================
// Space-padded fields: trim should only strip ' ', preserving trailing nulls
// ============================================================================

TEST_CASE("space-padded string preserves embedded trailing null",
          "[string][trim]") {
    // label-str is space-padded (padding="space", trim="both")
    string_features::StringMsg msg;
    msg.set_id(0);

    std::string val = "AB";
    val += '\0';
    msg.mutable_label().set_value(val);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Wire: 'A', 'B', '\0', ' ', ' ', ...  (null is content, spaces are padding)
    CHECK(bytes[22] == 'A');
    CHECK(bytes[23] == 'B');
    CHECK(bytes[24] == '\0');
    CHECK(bytes[25] == ' ');

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value().size() == 3);
}

// ============================================================================
// Packed 6-bit character encoding: lowercase converts to uppercase
// ============================================================================

TEST_CASE("packed 6-bit encoding converts lowercase to uppercase",
          "[string][packed]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("abcd");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // Lowercase input is converted to uppercase on encode (6-bit IA-5 standard)
    auto val = decoded->packed().value();
    CHECK(val.substr(0, 4) == "ABCD");
}

TEST_CASE("packed 6-bit encoding normalizes mixed case to uppercase",
          "[string][packed]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("AbCd");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto val = decoded->packed().value();
    CHECK(val[0] == 'A');
    CHECK(val[1] == 'B');  // 'b' converted to 'B'
    CHECK(val[2] == 'C');
    CHECK(val[3] == 'D');  // 'd' converted to 'D'
}

// ============================================================================
// Terminated strings: trim with default null padding preserves trailing spaces
// ============================================================================

TEST_CASE("null-terminated string preserves trailing spaces",
          "[string][terminated][trim]") {
    string_features::TermStringMsg msg;
    msg.set_id(1);
    msg.set_null_term("Hello ");  // trailing space is content
    msg.set_newline_term("World");
    msg.set_crlf_term("Test");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->null_term() == "Hello ");
}

TEST_CASE("newline-terminated string preserves trailing spaces",
          "[string][terminated][trim]") {
    string_features::TermStringMsg msg;
    msg.set_id(2);
    msg.set_null_term("A");
    msg.set_newline_term("Data   ");  // trailing spaces are content
    msg.set_crlf_term("B");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->newline_term() == "Data   ");
}

// ============================================================================
// EBCDIC encoding: printable ASCII roundtrips correctly
// ============================================================================

TEST_CASE("EBCDIC roundtrip for printable ASCII",
          "[string][ebcdic]") {
    using namespace conduit::string;

    // Printable ASCII range (0x20-0x7E) must roundtrip perfectly
    for (int i = 0x20; i < 0x7F; ++i) {
        std::string input(1, static_cast<char>(static_cast<uint8_t>(i)));
        auto encoded = from_ascii(input, Encoding::EBCDIC);
        auto decoded = to_ascii(encoded, Encoding::EBCDIC);
        INFO("char 0x" << std::hex << i << " '" << input << "'");
        CHECK(decoded == input);
    }
}

TEST_CASE("EBCDIC extended ASCII is lossy (expected limitation)",
          "[string][ebcdic]") {
    using namespace conduit::string;

    // Extended ASCII (0x80-0xFF) is inherently lossy through EBCDIC — this is
    // expected behavior, not a bug, since EBCDIC is designed for 7-bit ASCII.
    int corruption_count = 0;
    for (int i = 0; i < 256; ++i) {
        std::string input(1, static_cast<char>(static_cast<uint8_t>(i)));
        auto encoded = from_ascii(input, Encoding::EBCDIC);
        auto decoded = to_ascii(encoded, Encoding::EBCDIC);
        if (decoded != input) {
            corruption_count++;
        }
    }
    CHECK(corruption_count > 0);
}

// ============================================================================
// Control tests: basic roundtrips still work
// ============================================================================

TEST_CASE("null-padded string without trailing spaces roundtrips correctly",
          "[string][trim]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Hello");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Hello");
}

TEST_CASE("space-padded string without trailing nulls roundtrips correctly",
          "[string][trim]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_label().set_value("Test");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value() == "Test");
}

TEST_CASE("packed 6-bit uppercase roundtrips correctly",
          "[string][packed]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("ABCD1234");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->packed().value() == "ABCD1234");
}
