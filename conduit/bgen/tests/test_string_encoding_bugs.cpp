// SPDX-License-Identifier: MIT
// Tests demonstrating string encoding data corruption cases

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
// Bug 1: Trim strips both '\0' AND ' ' regardless of padding type
//
// The generated trim code always strips both null bytes AND spaces:
//   while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
//       s.pop_back();
//
// For null-padded fields, only trailing '\0' should be stripped.
// For space-padded fields, only trailing ' ' should be stripped.
// The current code incorrectly strips BOTH, corrupting valid data.
// ============================================================================

TEST_CASE("BUG: null-padded string with trailing spaces loses spaces on roundtrip",
          "[bug][string][trim]") {
    // name-str is null-padded (padding="null", trim="right")
    // Valid input: "Hello   " (with 3 trailing spaces)
    // Expected: trailing spaces preserved (they're data, not padding)
    // Actual: trailing spaces stripped (treated as padding)
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

    // BUG: This fails! The trailing spaces are incorrectly stripped.
    // decoded->name().value() == "Hello" instead of "Hello   "
    CHECK(decoded->name().value() == "Hello   ");
}

TEST_CASE("BUG: null-padded string with single trailing space loses space",
          "[bug][string][trim]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Test ");  // 5 chars, 1 trailing space

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // BUG: trailing space stripped
    CHECK(decoded->name().value() == "Test ");
}

TEST_CASE("BUG: null-padded bounded-str with trailing spaces loses spaces",
          "[bug][string][trim]") {
    // bounded-str is null-padded (padding="ascii"/default null, trim="right")
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_bounded().set_value("Data   ");  // 7 chars, 3 trailing spaces

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // BUG: trailing spaces stripped
    CHECK(decoded->bounded().value() == "Data   ");
}

TEST_CASE("BUG: space-padded string with embedded trailing nulls loses nulls",
          "[bug][string][trim]") {
    // label-str is space-padded (padding="space", trim="both")
    // Valid input with trailing null in the content
    string_features::StringMsg msg;
    msg.set_id(0);

    // Set a string that happens to end with a null byte
    std::string val = "AB";
    val += '\0';
    msg.mutable_label().set_value(val);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Wire: 'A', 'B', '\0', ' ', ' ', ...  (null then space padding)
    CHECK(bytes[22] == 'A');
    CHECK(bytes[23] == 'B');
    CHECK(bytes[24] == '\0');  // This is content, not padding
    CHECK(bytes[25] == ' ');   // This is space padding

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // BUG: The trailing null (which is content) is stripped along with spaces.
    // decoded->label().value() == "AB" instead of "AB\0"
    CHECK(decoded->label().value().size() == 3);
}

// ============================================================================
// Bug 2: Packed 6-bit character encoding silently corrupts lowercase letters
//
// The 6-bit encoding masks each char to lower 6 bits: ch & 0x3F
// On decode, values < 32 get 0x40 added to recover uppercase letters.
// But lowercase letters (0x61-0x7A) when masked give 0x21-0x3A,
// which are >= 32 and NOT adjusted, resulting in punctuation characters.
//
// 'a' (0x61) -> 0x21 -> '!' (0x21)
// 'b' (0x62) -> 0x22 -> '"' (0x22)
// 'z' (0x7A) -> 0x3A -> ':' (0x3A)
// ============================================================================

TEST_CASE("BUG: packed 6-bit encoding corrupts lowercase letters",
          "[bug][string][packed]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("abcd");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // BUG: lowercase "abcd" is silently corrupted to "!\"#$"
    // No error is reported — data is silently mangled.
    auto val = decoded->packed().value();
    CHECK(val.substr(0, 4) == "abcd");
}

TEST_CASE("BUG: packed 6-bit encoding corrupts mixed case",
          "[bug][string][packed]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("AbCd");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    auto val = decoded->packed().value();
    // 'A' -> OK (uppercase preserved)
    // 'b' -> CORRUPTED to '"'
    // 'C' -> OK (uppercase preserved)
    // 'd' -> CORRUPTED to '$'
    CHECK(val[0] == 'A');
    CHECK(val[1] == 'b');  // BUG: actually becomes '"'
    CHECK(val[2] == 'C');
    CHECK(val[3] == 'd');  // BUG: actually becomes '$'
}

// ============================================================================
// Bug 3: Terminated string trim also strips content characters
//
// The trim-right logic in terminated strings strips both '\0' and ' '.
// A newline-terminated or CRLF-terminated string ending in spaces will
// have those spaces silently removed.
// ============================================================================

TEST_CASE("BUG: null-terminated string with trailing spaces loses spaces",
          "[bug][string][terminated][trim]") {
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

    // BUG: trailing space in null-terminated string is stripped
    CHECK(decoded->null_term() == "Hello ");
}

TEST_CASE("BUG: newline-terminated string with trailing spaces loses spaces",
          "[bug][string][terminated][trim]") {
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

    // BUG: trailing spaces stripped from newline-terminated string
    CHECK(decoded->newline_term() == "Data   ");
}

// ============================================================================
// Bug 4: EBCDIC encoding is lossy for extended ASCII (>0x7F)
//
// Characters with values 0x80-0xFF are mapped through the ascii_to_ebcdic
// table and then back through ebcdic_to_ascii. Many of these mappings are
// not bijective (multiple ASCII values map to the same EBCDIC code).
// ============================================================================

TEST_CASE("BUG: EBCDIC roundtrip loses data for extended ASCII chars",
          "[bug][string][ebcdic]") {
    using namespace conduit::string;

    // Test roundtrip for all 256 byte values
    int corruption_count = 0;
    for (int i = 0; i < 256; ++i) {
        std::string input(1, static_cast<char>(static_cast<uint8_t>(i)));
        auto encoded = from_ascii(input, Encoding::EBCDIC);
        auto decoded = to_ascii(encoded, Encoding::EBCDIC);
        if (decoded != input) {
            corruption_count++;
        }
    }

    // For the standard printable ASCII range (0x20-0x7E), roundtrip should work
    for (int i = 0x20; i < 0x7F; ++i) {
        std::string input(1, static_cast<char>(static_cast<uint8_t>(i)));
        auto encoded = from_ascii(input, Encoding::EBCDIC);
        auto decoded = to_ascii(encoded, Encoding::EBCDIC);
        INFO("char 0x" << std::hex << i << " '" << input << "'");
        CHECK(decoded == input);
    }

    // Document that non-printable/extended chars ARE corrupted
    // (this is expected behavior for EBCDIC but should be documented)
    CHECK(corruption_count > 0);
}

// ============================================================================
// Verify: null-padded string WITHOUT trailing spaces roundtrips correctly
// (to show the bug is specifically about trailing space handling)
// ============================================================================

TEST_CASE("null-padded string without trailing spaces roundtrips correctly (control)",
          "[string][trim][control]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("Hello");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Hello");  // This works fine
}

TEST_CASE("space-padded string without trailing nulls roundtrips correctly (control)",
          "[string][trim][control]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_label().set_value("Test");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value() == "Test");  // This works fine
}
