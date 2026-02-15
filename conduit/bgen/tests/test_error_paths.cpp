// SPDX-License-Identifier: MIT
// Bgen tests - Error path / negative tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/core/error.hpp>
#include <cstdint>
#include <vector>

#include "all_types/messages.hpp"
#include "boundary_types/messages.hpp"
#include "constraints/messages.hpp"

// ============================================================================
// Section: Truncated input
// ============================================================================

TEST_CASE("truncated input - partial uint16 fails decode", "[error]") {
    // AllTypesMessage starts with u8 + u16, provide only 2 bytes (need 3)
    std::vector<uint8_t> data = {0x42, 0xAB}; // u8=0x42, half of u16
    auto decoded = all_types::AllTypesMessage::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("empty input fails decode", "[error]") {
    std::vector<uint8_t> data = {};
    auto decoded = all_types::AllTypesMessage::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("single byte input for multi-field message", "[error]") {
    std::vector<uint8_t> data = {0xFF};
    auto decoded = all_types::AllTypesMessage::decode_bytes(data);
    REQUIRE(!decoded.has_value());
}

// ============================================================================
// Section: Invalid enum value
// ============================================================================

TEST_CASE("unknown enum value in wire data", "[error]") {
    // Build wire data for BoundaryMsg with invalid enum at the level field position
    // BoundaryMsg: 1+3+7+8+16+32+64+8+16+32+64+16 = 267 bits before level (4 bits)
    // Instead of constructing the full message, encode a valid one and corrupt the enum byte
    boundary_types::BoundaryMsg msg;
    msg.set_flag(0);
    msg.set_small(0);
    msg.set_medium(0);
    msg.set_byte_val(0);
    msg.set_word(0);
    msg.set_dword(0);
    msg.set_qword(0);
    msg.set_signed_byte(0);
    msg.set_signed_word(0);
    msg.set_signed_dword(0);
    msg.set_signed_qword(0);
    msg.mutable_temp().set_raw(0);
    msg.set_level(boundary_types::nybble_enum::off);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // Corrupt the level field to value 15 (not in enum).
    // The level field is the last 4 data bits, packed MSB-first into the
    // high nibble of the final byte (the low nibble is padding zeros).
    if (!bytes.empty()) {
        bytes.back() = (bytes.back() & 0x0F) | 0xF0;
    }

    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::UnknownEnumValue);
}

// ============================================================================
// Section: Constraint violation errors
// ============================================================================

TEST_CASE("constraint violation on decode - equals mismatch", "[error][constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0x0000, conduit::io::Endian::Big); // magic should be 0xBEEF
    w.write_u8(50);
    w.write_u16(100, conduit::io::Endian::Big);
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ConstraintViolation);
}

TEST_CASE("constraint violation on decode - range exceeded", "[error][constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(200); // percent max=100
    w.write_u16(100, conduit::io::Endian::Big);
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ConstraintViolation);
}

// ============================================================================
// Section: Decode with excess trailing data (T5 test gap)
//
// Verifies behavior when extra bytes are appended after a valid message.
// The decoder reads exactly the expected number of bytes and ignores trailing
// data -- decode should succeed.
// ============================================================================

TEST_CASE("decode with excess trailing bytes succeeds", "[error][trailing]") {
    // Encode a valid ConstraintMsg
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    REQUIRE(msg.set_percent(50).has_value());
    msg.set_deferred_val(100);
    msg.set_payload(0x12345678);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 9);

    // Append extra trailing bytes
    bytes.push_back(0xFF);
    bytes.push_back(0xAA);
    bytes.push_back(0x55);
    REQUIRE(bytes.size() == 12);

    // Decode should succeed (decoder reads 9 bytes, ignores the trailing 3)
    auto decoded = constraints::ConstraintMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->magic() == 0xBEEF);
    CHECK(decoded->percent() == 50);
    CHECK(decoded->deferred_val() == 100);
    CHECK(decoded->payload() == 0x12345678);
}

TEST_CASE("decode with single excess trailing byte succeeds", "[error][trailing]") {
    boundary_types::BoundaryMsg msg;
    msg.set_flag(1);
    msg.set_small(5);
    msg.set_medium(99);
    msg.set_byte_val(0x42);
    msg.set_word(0x1234);
    msg.set_dword(0xDEADBEEF);
    msg.set_qword(0x0102030405060708ULL);
    msg.set_signed_byte(-42);
    msg.set_signed_word(-1000);
    msg.set_signed_dword(-100000);
    msg.set_signed_qword(-1);
    msg.mutable_temp().set_value(25.0);
    msg.set_level(boundary_types::nybble_enum::high);
    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto expected_size = bytes.size();

    // Append one extra byte
    bytes.push_back(0xCC);

    auto decoded = boundary_types::BoundaryMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flag() == 1);
    CHECK(decoded->small() == 5);
    CHECK(decoded->byte_val() == 0x42);
    (void)expected_size;
}

// ============================================================================
// Section: Error code properties
// ============================================================================

TEST_CASE("new error codes have correct category", "[error]") {
    conduit::Error max_len(conduit::ErrorCode::MaxLengthExceeded, "test");
    CHECK(max_len.is_decode_error());

    conduit::Error enc_err(conduit::ErrorCode::StringEncodingError, "test");
    CHECK(enc_err.is_decode_error());

    conduit::Error deferred(conduit::ErrorCode::ConstraintViolationDeferred, "test");
    CHECK(deferred.is_decode_error());

    conduit::Error too_long(conduit::ErrorCode::StringTooLong, "test");
    CHECK(too_long.is_encode_error());
}

TEST_CASE("error code to string for new codes", "[error]") {
    CHECK(conduit::Error::code_to_string(conduit::ErrorCode::MaxLengthExceeded) == "MAX_LENGTH_EXCEEDED");
    CHECK(conduit::Error::code_to_string(conduit::ErrorCode::StringEncodingError) == "STRING_ENCODING_ERROR");
    CHECK(conduit::Error::code_to_string(conduit::ErrorCode::ConstraintViolationDeferred) == "CONSTRAINT_VIOLATION_DEFERRED");
    CHECK(conduit::Error::code_to_string(conduit::ErrorCode::StringTooLong) == "STRING_TOO_LONG");
}

// ============================================================================
// Section: Comprehensive error code coverage
//
// Validates every ErrorCode variant for:
//   - Correct round-trip via Error.code()
//   - Non-empty, non-"UNKNOWN" string from code_to_string()
//   - Correct severity category
// ============================================================================

namespace {

struct ErrorCodeExpectation {
    conduit::ErrorCode code;
    const char* expected_string;
    conduit::ErrorSeverity expected_severity;
    bool is_decode;
    bool is_encode;
    bool is_connection;
    bool is_transceiver;
};

const ErrorCodeExpectation all_error_codes[] = {
    // Decode errors (100-199) — severity: Warning
    {conduit::ErrorCode::BufferUnderrun,              "BUFFER_UNDERRUN",               conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::InvalidFieldValue,           "INVALID_FIELD_VALUE",           conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::ConstraintViolation,         "CONSTRAINT_VIOLATION",          conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::UnknownEnumValue,            "UNKNOWN_ENUM_VALUE",            conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::UnknownDiscriminator,        "UNKNOWN_DISCRIMINATOR",         conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::ExactConsumptionFailed,      "EXACT_CONSUMPTION_FAILED",      conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::NegativeLength,              "NEGATIVE_LENGTH",               conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::DecodingFailed,              "DECODING_FAILED",               conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::UnknownTypeId,               "UNKNOWN_TYPE_ID",               conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::MaxLengthExceeded,           "MAX_LENGTH_EXCEEDED",           conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::StringEncodingError,         "STRING_ENCODING_ERROR",         conduit::ErrorSeverity::Warning,  true,  false, false, false},
    {conduit::ErrorCode::ConstraintViolationDeferred, "CONSTRAINT_VIOLATION_DEFERRED", conduit::ErrorSeverity::Warning,  true,  false, false, false},

    // Encode errors (200-299) — severity: Warning
    {conduit::ErrorCode::EncodeConstraintViolation, "ENCODE_CONSTRAINT_VIOLATION", conduit::ErrorSeverity::Warning, false, true, false, false},
    {conduit::ErrorCode::MissingRequiredField,      "MISSING_REQUIRED_FIELD",      conduit::ErrorSeverity::Warning, false, true, false, false},
    {conduit::ErrorCode::EncodingFailed,            "ENCODING_FAILED",             conduit::ErrorSeverity::Warning, false, true, false, false},
    {conduit::ErrorCode::BufferOverrun,             "BUFFER_OVERRUN",              conduit::ErrorSeverity::Warning, false, true, false, false},
    {conduit::ErrorCode::StringTooLong,             "STRING_TOO_LONG",             conduit::ErrorSeverity::Warning, false, true, false, false},

    // Connection errors (300-399) — severity: Error
    {conduit::ErrorCode::ConnectionRefused,  "CONNECTION_REFUSED",  conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::ConnectionTimeout,  "CONNECTION_TIMEOUT",  conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::ConnectionReset,    "CONNECTION_RESET",    conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::ConnectionClosed,   "CONNECTION_CLOSED",   conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::HostNotFound,       "HOST_NOT_FOUND",      conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::NetworkUnreachable, "NETWORK_UNREACHABLE", conduit::ErrorSeverity::Error, false, false, true, false},
    {conduit::ErrorCode::SocketError,        "SOCKET_ERROR",        conduit::ErrorSeverity::Error, false, false, true, false},

    // Transceiver errors (400-499) — severity: Error
    {conduit::ErrorCode::NotRunning,             "NOT_RUNNING",              conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::AlreadyRunning,         "ALREADY_RUNNING",          conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::PeerNotFound,           "PEER_NOT_FOUND",           conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::QueueFull,              "QUEUE_FULL",               conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::InvalidConfig,          "INVALID_CONFIG",           conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::MultiplePeers,          "MULTIPLE_PEERS",           conduit::ErrorSeverity::Error, false, false, false, true},
    {conduit::ErrorCode::UnsupportedMessageType, "UNSUPPORTED_MESSAGE_TYPE", conduit::ErrorSeverity::Error, false, false, false, true},

    // Internal errors (900-999) — severity: Critical
    {conduit::ErrorCode::InternalError,   "INTERNAL_ERROR",   conduit::ErrorSeverity::Critical, false, false, false, false},
    {conduit::ErrorCode::NotImplemented,  "NOT_IMPLEMENTED",  conduit::ErrorSeverity::Critical, false, false, false, false},
    {conduit::ErrorCode::InvalidArgument, "INVALID_ARGUMENT", conduit::ErrorSeverity::Critical, false, false, false, false},
    {conduit::ErrorCode::InvalidState,    "INVALID_STATE",    conduit::ErrorSeverity::Critical, false, false, false, false},
    {conduit::ErrorCode::Timeout,         "TIMEOUT",          conduit::ErrorSeverity::Error, false, false, false, false},
};

} // anonymous namespace

TEST_CASE("all error codes: round-trip code, string, severity, and category", "[error]") {
    for (const auto& ec : all_error_codes) {
        CAPTURE(conduit::Error::code_to_string(ec.code));

        // Round-trip: constructing an Error preserves the code
        conduit::Error err(ec.code, "test message");
        CHECK(err.code() == ec.code);

        // code_to_string returns the expected non-empty, non-UNKNOWN string
        auto str = conduit::Error::code_to_string(ec.code);
        CHECK_FALSE(str.empty());
        CHECK(str != "UNKNOWN");
        CHECK(str == ec.expected_string);

        // Severity matches the expected category
        CHECK(err.severity() == ec.expected_severity);

        // Category predicates
        CHECK(err.is_decode_error() == ec.is_decode);
        CHECK(err.is_encode_error() == ec.is_encode);
        CHECK(err.is_connection_error() == ec.is_connection);
        CHECK(err.is_transceiver_error() == ec.is_transceiver);
    }
}

TEST_CASE("all error codes: format and format_short produce non-empty output", "[error]") {
    for (const auto& ec : all_error_codes) {
        conduit::Error err(ec.code, "test");
        CHECK_FALSE(err.format().empty());
        CHECK_FALSE(err.format_short().empty());

        // format_short includes the code string
        CHECK(err.format_short().find(ec.expected_string) != std::string::npos);
    }
}

TEST_CASE("all error codes: with_context preserves code and adds context", "[error]") {
    for (const auto& ec : all_error_codes) {
        conduit::Error err(ec.code, "original");
        auto ctx_err = err.with_context("some_context");
        CHECK(ctx_err.code() == ec.code);
        CHECK(ctx_err.has_context());
        CHECK(ctx_err.context() == "some_context");
    }
}

TEST_CASE("error code count matches expected total", "[error]") {
    // Guard: if a new ErrorCode is added but not included in all_error_codes[],
    // this count will go stale and remind you to update the table.
    constexpr size_t expected_count = 12 + 5 + 7 + 7 + 5; // decode + encode + connection + transceiver + internal
    CHECK(std::size(all_error_codes) == expected_count);
}
