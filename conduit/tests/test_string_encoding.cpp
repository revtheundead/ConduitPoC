// SPDX-License-Identifier: MIT
// Conduit - String Encoding Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/string/encoding.hpp>

using namespace conduit::string;

// ============================================================================
// to_ascii
// ============================================================================

TEST_CASE("to_ascii ASCII passthrough", "[string_encoding]") {
    auto result = to_ascii("Hello", Encoding::ASCII);
    CHECK(result == "Hello");
}

TEST_CASE("to_ascii UTF8 passthrough", "[string_encoding]") {
    auto result = to_ascii("Hello", Encoding::UTF8);
    CHECK(result == "Hello");
}

TEST_CASE("to_ascii IA5 masks high bit", "[string_encoding]") {
    // IA5 is 7-bit: high bit should be stripped
    std::string input;
    input += static_cast<char>(0xC1);  // 'A' with high bit set (0x41 | 0x80)
    input += static_cast<char>(0xC2);  // 'B' with high bit set

    auto result = to_ascii(input, Encoding::IA5);
    REQUIRE(result.size() == 2);
    CHECK(result[0] == 'A');
    CHECK(result[1] == 'B');
}

TEST_CASE("to_ascii IA5 preserves 7-bit values", "[string_encoding]") {
    auto result = to_ascii("ABC", Encoding::IA5);
    CHECK(result == "ABC");
}

TEST_CASE("to_ascii EBCDIC converts known chars", "[string_encoding]") {
    // EBCDIC 0xC1 = 'A', 0xC2 = 'B', 0xC3 = 'C' in Code Page 037
    std::string ebcdic_abc;
    ebcdic_abc += static_cast<char>(0xC1);
    ebcdic_abc += static_cast<char>(0xC2);
    ebcdic_abc += static_cast<char>(0xC3);

    auto result = to_ascii(ebcdic_abc, Encoding::EBCDIC);
    CHECK(result == "ABC");
}

TEST_CASE("to_ascii EBCDIC converts digits", "[string_encoding]") {
    // EBCDIC 0xF0-0xF9 = '0'-'9'
    std::string ebcdic_digits;
    for (uint8_t i = 0xF0; i <= 0xF9; ++i) {
        ebcdic_digits += static_cast<char>(i);
    }

    auto result = to_ascii(ebcdic_digits, Encoding::EBCDIC);
    CHECK(result == "0123456789");
}

TEST_CASE("to_ascii empty string", "[string_encoding]") {
    CHECK(to_ascii("", Encoding::ASCII).empty());
    CHECK(to_ascii("", Encoding::IA5).empty());
    CHECK(to_ascii("", Encoding::EBCDIC).empty());
    CHECK(to_ascii("", Encoding::UTF8).empty());
}

// ============================================================================
// from_ascii
// ============================================================================

TEST_CASE("from_ascii ASCII passthrough", "[string_encoding]") {
    auto result = from_ascii("Hello", Encoding::ASCII);
    CHECK(result == "Hello");
}

TEST_CASE("from_ascii UTF8 passthrough", "[string_encoding]") {
    auto result = from_ascii("Hello", Encoding::UTF8);
    CHECK(result == "Hello");
}

TEST_CASE("from_ascii IA5 masks high bit", "[string_encoding]") {
    std::string input;
    input += static_cast<char>(0xC1);  // high bit set

    auto result = from_ascii(input, Encoding::IA5);
    REQUIRE(result.size() == 1);
    CHECK(static_cast<uint8_t>(result[0]) == 0x41);  // high bit stripped
}

TEST_CASE("from_ascii EBCDIC converts known chars", "[string_encoding]") {
    auto result = from_ascii("ABC", Encoding::EBCDIC);
    REQUIRE(result.size() == 3);
    CHECK(static_cast<uint8_t>(result[0]) == 0xC1);
    CHECK(static_cast<uint8_t>(result[1]) == 0xC2);
    CHECK(static_cast<uint8_t>(result[2]) == 0xC3);
}

TEST_CASE("from_ascii empty string", "[string_encoding]") {
    CHECK(from_ascii("", Encoding::ASCII).empty());
    CHECK(from_ascii("", Encoding::IA5).empty());
    CHECK(from_ascii("", Encoding::EBCDIC).empty());
    CHECK(from_ascii("", Encoding::UTF8).empty());
}

// ============================================================================
// Round-trip
// ============================================================================

TEST_CASE("EBCDIC round-trip", "[string_encoding]") {
    std::string original = "Hello, World! 0123456789";
    auto encoded = from_ascii(original, Encoding::EBCDIC);
    auto decoded = to_ascii(encoded, Encoding::EBCDIC);
    CHECK(decoded == original);
}

TEST_CASE("IA5 round-trip for 7-bit ASCII", "[string_encoding]") {
    std::string original = "Hello 123";
    auto encoded = from_ascii(original, Encoding::IA5);
    auto decoded = to_ascii(encoded, Encoding::IA5);
    CHECK(decoded == original);
}

// ============================================================================
// Edge cases: non-printable, null bytes, all-bits-set
// ============================================================================

TEST_CASE("to_ascii handles null bytes", "[string_encoding]") {
    std::string input(4, '\0');
    auto result_ascii = to_ascii(input, Encoding::ASCII);
    CHECK(result_ascii.size() == 4);
    CHECK(result_ascii == input);

    auto result_ia5 = to_ascii(input, Encoding::IA5);
    CHECK(result_ia5.size() == 4);
    // IA5: 0x00 & 0x7F = 0x00
    for (char c : result_ia5) CHECK(c == '\0');
}

TEST_CASE("to_ascii EBCDIC handles all 256 byte values", "[string_encoding]") {
    // Ensure no crash for any byte value
    std::string all_bytes;
    all_bytes.reserve(256);
    for (int i = 0; i < 256; ++i) {
        all_bytes += static_cast<char>(static_cast<uint8_t>(i));
    }
    auto result = to_ascii(all_bytes, Encoding::EBCDIC);
    CHECK(result.size() == 256);
}

TEST_CASE("from_ascii EBCDIC handles all printable ASCII", "[string_encoding]") {
    std::string printable;
    for (char c = 0x20; c < 0x7F; ++c) {
        printable += c;
    }
    auto encoded = from_ascii(printable, Encoding::EBCDIC);
    auto roundtrip = to_ascii(encoded, Encoding::EBCDIC);
    CHECK(roundtrip == printable);
}

TEST_CASE("EBCDIC special characters round-trip", "[string_encoding]") {
    std::string special = "+-*/=@#$%^&(){}[]|\\:;\"'<>,.?!~`";
    auto encoded = from_ascii(special, Encoding::EBCDIC);
    auto decoded = to_ascii(encoded, Encoding::EBCDIC);
    CHECK(decoded == special);
}

TEST_CASE("IA5 high-bit characters are all masked", "[string_encoding]") {
    // All bytes 0x80-0xFF should have high bit stripped
    std::string high_bytes;
    for (int i = 0x80; i <= 0xFF; ++i) {
        high_bytes += static_cast<char>(static_cast<uint8_t>(i));
    }
    auto result = to_ascii(high_bytes, Encoding::IA5);
    REQUIRE(result.size() == high_bytes.size());
    for (size_t i = 0; i < result.size(); ++i) {
        CHECK(static_cast<uint8_t>(result[i]) < 0x80);
        CHECK(static_cast<uint8_t>(result[i]) ==
              (static_cast<uint8_t>(high_bytes[i]) & 0x7F));
    }
}
