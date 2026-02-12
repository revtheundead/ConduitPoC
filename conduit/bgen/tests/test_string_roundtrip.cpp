// SPDX-License-Identifier: MIT
// Bgen tests - String features roundtrip verification

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "string_features/messages.hpp"

// ============================================================================
// Section: String types (string_features fixture)
// ============================================================================

TEST_CASE("fixed-length null-padded string roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(1);
    msg.set_name({});
    msg.mutable_name().set_value("Hello");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "Hello");
}

TEST_CASE("fixed-length space-padded string roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(2);
    msg.mutable_label().set_value("Test");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().value() == "Test");
}

TEST_CASE("packed 6-bit character string roundtrip", "[roundtrip][string]") {
    // 6-bit encoding masks each char to lower 6 bits (values 0-63).
    // Use characters whose ASCII values fit in 6 bits: digits (0x30-0x39)
    // and some punctuation like ':' (0x3A), '<' (0x3C).
    string_features::StringMsg msg;
    msg.set_id(3);
    msg.mutable_packed().set_value("0123");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto val = decoded->packed().value();
    CHECK(val.substr(0, 4) == "0123");
}

TEST_CASE("string with max-length type roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(4);
    msg.mutable_bounded().set_value("Short");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bounded().value() == "Short");
}

TEST_CASE("empty string roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(5);
    msg.mutable_name().set_value("");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value().empty());
}

TEST_CASE("full-length string roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(6);
    msg.mutable_name().set_value("12345678901234567890"); // exactly 20 chars
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->name().value() == "12345678901234567890");
}

TEST_CASE("all-spaces string trimmed to empty", "[roundtrip][string]") {
    // label-str has trim="both" and padding="space", so all-spaces should trim to empty
    string_features::StringMsg msg;
    msg.set_id(7);
    msg.mutable_label().set_value("                "); // 16 spaces (full length)
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    // After trim="both", all spaces should be trimmed away
    CHECK(decoded->label().value().empty());
}

// ============================================================================
// Section: Wire-byte verification
// ============================================================================

TEST_CASE("packed 6-bit wire bytes", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_packed().set_value("0123");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // packed-str starts at offset 70 (id=2 + name=20 + label=16 + bounded=32)
    // 8 chars × 6 bits each, MSB-first. '0'=0x30, '1'=0x31, '2'=0x32, '3'=0x33
    // masked to 6 bits: 0x30, 0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00
    // Packed into 6 bytes:
    //   byte70: 0x30<<2 | 0x31>>4 = 0xC3
    //   byte71: (0x31&0xF)<<4 | 0x32>>2 = 0x1C
    //   byte72: (0x32&0x3)<<6 | 0x33     = 0xB3
    //   byte73..75: remaining zero chars → 0x00
    CHECK(bytes[70] == 0xC3);
    CHECK(bytes[71] == 0x1C);
    CHECK(bytes[72] == 0xB3);
    CHECK(bytes[73] == 0x00);
    CHECK(bytes[74] == 0x00);
    CHECK(bytes[75] == 0x00);
}

TEST_CASE("full-length name wire bytes", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_name().set_value("12345678901234567890"); // exactly 20 chars
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // name starts at offset 2 (after id=2 bytes), 20 bytes long
    std::string expected = "12345678901234567890";
    for (size_t i = 0; i < 20; i++) {
        CHECK(bytes[2 + i] == static_cast<uint8_t>(expected[i]));
    }
}

TEST_CASE("bounded string wire bytes", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_bounded().set_value("Short");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 140);

    // bounded starts at offset 38 (id=2 + name=20 + label=16), 32 bytes long
    std::string expected = "Short";
    for (size_t i = 0; i < 5; i++) {
        CHECK(bytes[38 + i] == static_cast<uint8_t>(expected[i]));
    }
    // Remaining 27 bytes should be null-padded
    for (size_t i = 5; i < 32; i++) {
        CHECK(bytes[38 + i] == 0x00);
    }
}

// ============================================================================
// Section: Error-path tests
// ============================================================================

TEST_CASE("truncated StringMsg fails decode", "[error][string]") {
    // StringMsg needs 140 bytes; provide only 10
    std::vector<uint8_t> data(10, 0x00);
    auto decoded = string_features::StringMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("empty StringMsg fails decode", "[error][string]") {
    std::vector<uint8_t> data;
    auto decoded = string_features::StringMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

// ============================================================================
// Section: Encode size verification
// ============================================================================

TEST_CASE("StringMsg wire size", "[wire][string]") {
    string_features::StringMsg msg;
    msg.set_id(1);
    msg.mutable_name().set_value("Test");
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 140);
}

// ============================================================================
// Section: InlineStringMsg tests
// ============================================================================

TEST_CASE("InlineStringMsg roundtrip", "[roundtrip][string]") {
    string_features::InlineStringMsg msg;
    msg.set_id(5);
    msg.mutable_inline_name().set_value("TestName");
    msg.set_prefix_str(42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::InlineStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->id() == 5);
    CHECK(decoded->inline_name().value() == "TestName");
    CHECK(decoded->prefix_str() == 42);
}

TEST_CASE("InlineStringMsg wire size", "[wire][string]") {
    string_features::InlineStringMsg msg;
    msg.set_id(5);
    msg.mutable_inline_name().set_value("TestName");
    msg.set_prefix_str(42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 22);
}

TEST_CASE("InlineStringMsg truncated decode fails", "[error][string]") {
    std::vector<uint8_t> data = {0x05, 0x41, 0x42, 0x43, 0x44};
    auto decoded = string_features::InlineStringMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("term_str non-empty content roundtrip", "[roundtrip][string]") {
    string_features::StringMsg msg;
    msg.set_id(0);
    msg.mutable_term().set_value("Hello World");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::StringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->term().value() == "Hello World");

    // term_str starts at offset 76 (id(2)+name(20)+label(16)+bounded(32)+packed(6))
    // Verify ASCII bytes + null padding
    std::string expected = "Hello World";
    for (size_t i = 0; i < expected.size(); i++) {
        CHECK(bytes[76 + i] == static_cast<uint8_t>(expected[i]));
    }
    // Remaining bytes should be null-padded
    for (size_t i = expected.size(); i < 64; i++) {
        CHECK(bytes[76 + i] == 0x00);
    }
}

// ============================================================================
// Section: Terminated strings (null, newline, crlf)
// ============================================================================

TEST_CASE("null-terminated string roundtrip", "[roundtrip][string][terminated]") {
    string_features::TermStringMsg msg;
    msg.set_id(1);
    msg.set_null_term("Hello");
    msg.set_newline_term("World");
    msg.set_crlf_term("Test");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->null_term() == "Hello");
    CHECK(decoded->newline_term() == "World");
    CHECK(decoded->crlf_term() == "Test");
}

TEST_CASE("newline-terminated string wire format", "[roundtrip][string][terminated]") {
    string_features::TermStringMsg msg;
    msg.set_id(2);
    msg.set_null_term("A");
    msg.set_newline_term("NL");
    msg.set_crlf_term("CR");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // id(1 byte) + "A" + 0x00 + "NL" + 0x0A + "CR" + 0x0D + 0x0A
    // offset 0: id = 2
    CHECK(bytes[0] == 0x02);
    // offset 1-2: "A" + null terminator
    CHECK(bytes[1] == 'A');
    CHECK(bytes[2] == 0x00);
    // offset 3-5: "NL" + newline terminator
    CHECK(bytes[3] == 'N');
    CHECK(bytes[4] == 'L');
    CHECK(bytes[5] == 0x0A);
    // offset 6-9: "CR" + CRLF terminator
    CHECK(bytes[6] == 'C');
    CHECK(bytes[7] == 'R');
    CHECK(bytes[8] == 0x0D);
    CHECK(bytes[9] == 0x0A);

    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->null_term() == "A");
    CHECK(decoded->newline_term() == "NL");
    CHECK(decoded->crlf_term() == "CR");
}

TEST_CASE("empty terminated strings roundtrip", "[roundtrip][string][terminated]") {
    string_features::TermStringMsg msg;
    msg.set_id(3);
    msg.set_null_term("");
    msg.set_newline_term("");
    msg.set_crlf_term("");

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // id(1) + null(1) + newline(1) + crlf(2) = 5 bytes
    CHECK(bytes.size() == 5);
    CHECK(bytes[0] == 3);
    CHECK(bytes[1] == 0x00);  // null terminator only
    CHECK(bytes[2] == 0x0A);  // newline terminator only
    CHECK(bytes[3] == 0x0D);  // CR
    CHECK(bytes[4] == 0x0A);  // LF

    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->null_term().empty());
    CHECK(decoded->newline_term().empty());
    CHECK(decoded->crlf_term().empty());
}

TEST_CASE("crlf-terminated string with embedded CR roundtrip", "[roundtrip][string][terminated]") {
    // A string with an isolated CR (not followed by LF) should be preserved
    string_features::TermStringMsg msg;
    msg.set_id(4);
    msg.set_null_term("X");
    msg.set_newline_term("Y");
    // Encode a string with an embedded CR that is NOT followed by LF
    msg.set_crlf_term(std::string("A\rB"));

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_features::TermStringMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->crlf_term() == std::string("A\rB"));
}
