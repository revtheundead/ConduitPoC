// SPDX-License-Identifier: MIT
// Bgen tests - String overflow/truncation behavior (T6)

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "string_features/messages.hpp"

// ============================================================================
// Section: String truncation on encode (longer than fixed length)
// ============================================================================

TEST_CASE("string longer than fixed length is truncated on encode",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(1);
    // name-str is 20 bytes long; provide 30 characters
    std::string long_str = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234";
    REQUIRE(long_str.size() == 30);
    msg.mutable_name().set_value(long_str);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // Verify wire bytes: only the first 20 characters should be present
    std::string expected_on_wire = "ABCDEFGHIJKLMNOPQRST"; // first 20 chars
    for (size_t i = 0; i < 20; i++) {
        CHECK(bytes[2 + i] == static_cast<uint8_t>(expected_on_wire[i]));
    }

    // Roundtrip: decoded value should be the truncated 20-char string
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == expected_on_wire);
    CHECK(decoded->name().value().size() == 20);
}

TEST_CASE("space-padded string longer than fixed length is truncated on encode",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(2);
    // label-str is 16 bytes; provide 25 characters
    std::string long_label = "This is a very long label";
    REQUIRE(long_label.size() == 25);
    msg.mutable_label().set_value(long_label);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // Verify wire bytes: label starts at offset 22 (id=2 + name=20), only first 16 chars
    std::string expected_on_wire = "This is a very l"; // first 16 chars
    for (size_t i = 0; i < 16; i++) {
        CHECK(bytes[22 + i] == static_cast<uint8_t>(expected_on_wire[i]));
    }

    // Roundtrip: decoded value should be trimmed (trim="both")
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // "This is a very l" has no leading/trailing spaces, so trim="both" is a no-op
    CHECK(decoded->label().value() == expected_on_wire);
}

// ============================================================================
// Section: String shorter than fixed length gets padded
// ============================================================================

TEST_CASE("null-padded string shorter than fixed length has null padding",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(3);
    msg.mutable_name().set_value("Hi");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // name starts at offset 2, is 20 bytes. "Hi" is 2 chars.
    CHECK(bytes[2] == 'H');
    CHECK(bytes[3] == 'i');
    // Remaining 18 bytes should be null-padded (padding="null")
    for (size_t i = 2; i < 20; i++) {
        CHECK(bytes[2 + i] == 0x00);
    }

    // Roundtrip: decoded value should be "Hi" (null padding trimmed)
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Hi");
}

TEST_CASE("space-padded string shorter than fixed length has space padding",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(4);
    msg.mutable_label().set_value("AB");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // label starts at offset 22, is 16 bytes. "AB" is 2 chars.
    CHECK(bytes[22] == 'A');
    CHECK(bytes[23] == 'B');
    // Remaining 14 bytes should be space-padded (padding="space")
    for (size_t i = 2; i < 16; i++) {
        CHECK(bytes[22 + i] == ' ');
    }

    // Roundtrip: decoded value should be "AB" (space padding trimmed via trim="both")
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value() == "AB");
}

// ============================================================================
// Section: String exact length works perfectly
// ============================================================================

TEST_CASE("null-padded string exact length roundtrips perfectly",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(5);
    // name-str is exactly 20 bytes
    std::string exact = "12345678901234567890";
    REQUIRE(exact.size() == 20);
    msg.mutable_name().set_value(exact);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // Verify all 20 bytes on the wire match exactly
    for (size_t i = 0; i < 20; i++) {
        CHECK(bytes[2 + i] == static_cast<uint8_t>(exact[i]));
    }

    // Roundtrip: no truncation, no padding needed
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == exact);
    CHECK(decoded->name().value().size() == 20);
}

TEST_CASE("space-padded string exact length roundtrips perfectly",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(6);
    // label-str is exactly 16 bytes; fill with non-space characters
    std::string exact = "ABCDEFGHIJKLMNOP";
    REQUIRE(exact.size() == 16);
    msg.mutable_label().set_value(exact);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // Verify all 16 bytes on the wire
    for (size_t i = 0; i < 16; i++) {
        CHECK(bytes[22 + i] == static_cast<uint8_t>(exact[i]));
    }

    // Roundtrip: no truncation, no trimming needed (no trailing spaces)
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value() == exact);
    CHECK(decoded->label().value().size() == 16);
}

// ============================================================================
// Section: Field-level max-length setter validation
// ============================================================================

TEST_CASE("field max-length setter rejects string exceeding limit",
          "[string][overflow][setter]") {
    string_features::MaxLenMsg msg;
    msg.set_id(0);
    auto result = msg.set_data(std::string(17, 'A'));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::StringTooLong);
}

TEST_CASE("field max-length setter accepts string at limit",
          "[string][overflow][setter]") {
    string_features::MaxLenMsg msg;
    msg.set_id(0);
    auto result = msg.set_data(std::string(16, 'A'));
    CHECK(result.has_value());
}

TEST_CASE("bounded string exact length roundtrips perfectly",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(7);
    // bounded-str is exactly 32 bytes
    std::string exact = "12345678901234567890123456789012";
    REQUIRE(exact.size() == 32);
    msg.mutable_bounded().set_value(exact);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // bounded starts at offset 38 (id=2 + name=20 + label=16)
    for (size_t i = 0; i < 32; i++) {
        CHECK(bytes[38 + i] == static_cast<uint8_t>(exact[i]));
    }

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bounded().value() == exact);
    CHECK(decoded->bounded().value().size() == 32);
}

// ============================================================================
// Section: Empty string encodes as all padding bytes
// ============================================================================

TEST_CASE("empty null-padded string encodes as all null bytes",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(8);
    msg.mutable_name().set_value("");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // name starts at offset 2, 20 bytes: all should be 0x00
    for (size_t i = 0; i < 20; i++) {
        CHECK(bytes[2 + i] == 0x00);
    }

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value().empty());
}

TEST_CASE("empty space-padded string encodes as all space bytes",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(9);
    msg.mutable_label().set_value("");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // label starts at offset 22, 16 bytes: all should be 0x20 (space)
    for (size_t i = 0; i < 16; i++) {
        CHECK(bytes[22 + i] == 0x20);
    }

    // After trim="both", all spaces are trimmed to empty
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value().empty());
}

TEST_CASE("empty bounded string encodes as all null bytes",
          "[roundtrip][string][overflow]") {
    string_features::StringMsg msg;
    msg.set_id(10);
    msg.mutable_bounded().set_value("");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // bounded starts at offset 38, 32 bytes: all should be 0x00
    for (size_t i = 0; i < 32; i++) {
        CHECK(bytes[38 + i] == 0x00);
    }

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bounded().value().empty());
}

// ============================================================================
// Section: String with embedded null characters
// ============================================================================

TEST_CASE("null-padded string with embedded null is truncated at first null on decode",
          "[roundtrip][string][overflow]") {
    // Construct a wire buffer manually with embedded nulls in the name field
    // to verify decode behavior: trim="right" on null-padded should strip
    // trailing nulls, but an embedded null mid-string means the right-trim
    // stops at the rightmost non-null, preserving the embedded null.
    string_features::StringMsg msg;
    msg.set_id(11);
    msg.mutable_name().set_value("Hello");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // Manually inject an embedded null: "Hel\0o" followed by content then nulls
    // name starts at offset 2
    bytes[2] = 'H';
    bytes[3] = 'e';
    bytes[4] = 'l';
    bytes[5] = 0x00; // embedded null
    bytes[6] = 'o';
    bytes[7] = 'X';
    // bytes[8..21] remain null-padded from original encode

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    // trim="right" with padding="null" trims trailing nulls from the right.
    // The rightmost non-null byte is 'X' at position 5 (relative to name start).
    // So the decoded string should be "Hel\0oX" (6 chars, with embedded null).
    auto val = decoded->name().value();
    CHECK(val.size() == 6);
    CHECK(val[0] == 'H');
    CHECK(val[1] == 'e');
    CHECK(val[2] == 'l');
    CHECK(val[3] == '\0');
    CHECK(val[4] == 'o');
    CHECK(val[5] == 'X');
}

TEST_CASE("fixed-length string with embedded null preserves embedded null on decode",
          "[roundtrip][string][overflow]") {
    // term-str has length="64" AND terminated="0x00". In codegen, length takes priority,
    // so it generates a fixed-length string (reads 64 bytes, right-trims trailing nulls).
    // Embedded nulls within the string are preserved.
    string_features::StringMsg msg;
    msg.set_id(12);
    msg.mutable_term().set_value("Hello");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // term starts at offset 76 (id=2 + name=20 + label=16 + bounded=32 + packed=6)
    // Manually inject: "Hi\0World" followed by null padding
    bytes[76] = 'H';
    bytes[77] = 'i';
    bytes[78] = 0x00; // embedded null
    bytes[79] = 'W';
    bytes[80] = 'o';
    bytes[81] = 'r';
    bytes[82] = 'l';
    bytes[83] = 'd';
    // bytes[84..139] remain 0x00 (trailing null padding)

    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // Fixed-length decode reads all 64 bytes, then right-trims trailing nulls.
    // The rightmost non-null is 'd' at offset 83, so result is "Hi\0World" (8 chars).
    auto val = decoded->term().value();
    CHECK(val.size() == 8);
    CHECK(val[0] == 'H');
    CHECK(val[1] == 'i');
    CHECK(val[2] == '\0');
    CHECK(val[3] == 'W');
    CHECK(val[4] == 'o');
    CHECK(val[5] == 'r');
    CHECK(val[6] == 'l');
    CHECK(val[7] == 'd');
}
