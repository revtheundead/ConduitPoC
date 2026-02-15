// SPDX-License-Identifier: MIT
// Conduit - Error Handling

#pragma once

#include <conduit/core/types.hpp>
#include <chrono>
#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace conduit {

// ============================================================================
// Error Codes
// ============================================================================

enum class ErrorCode {
    // Decode errors (100-199)
    BufferUnderrun = 100,
    InvalidFieldValue = 101,
    ConstraintViolation = 102,
    UnknownEnumValue = 103,
    UnknownDiscriminator = 104,
    ExactConsumptionFailed = 105,
    NegativeLength = 106,
    DecodingFailed = 107,
    UnknownTypeId = 108,
    MaxLengthExceeded = 109,
    StringEncodingError = 110,
    ConstraintViolationDeferred = 111,

    // Encode errors (200-299)
    EncodeConstraintViolation = 200,
    MissingRequiredField = 201,
    EncodingFailed = 202,
    BufferOverrun = 203,
    StringTooLong = 204,
    DirectionViolation = 205,

    // Connection errors (300-399)
    ConnectionRefused = 300,
    ConnectionTimeout = 301,
    ConnectionReset = 302,
    ConnectionClosed = 303,
    HostNotFound = 304,
    NetworkUnreachable = 305,
    SocketError = 306,

    // Transceiver errors (400-499)
    NotRunning = 400,
    AlreadyRunning = 401,
    PeerNotFound = 402,
    QueueFull = 403,
    InvalidConfig = 404,
    MultiplePeers = 405,
    UnsupportedMessageType = 406,
    BatchNotSupported = 407,       // Session does not support batch encoding

    // Internal errors (900-999)
    InternalError = 900,
    NotImplemented = 901,
    InvalidArgument = 902,
    InvalidState = 903,
    Timeout = 904,
};

// ============================================================================
// Error Severity
// ============================================================================

enum class ErrorSeverity {
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Fatal
};

// ============================================================================
// Error Class
// ============================================================================

class Error {
public:
    Error() = default;

    Error(ErrorCode code,
          std::string message,
          std::source_location location = std::source_location::current())
        : code_(code)
        , message_(std::move(message))
        , location_(location)
        , timestamp_(std::chrono::system_clock::now()) {}

    // Accessors
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::source_location& location() const noexcept { return location_; }
    [[nodiscard]] Timestamp timestamp() const noexcept {
        return timestamp_.value_or(Timestamp{});
    }
    [[nodiscard]] const std::string& context() const noexcept { return context_; }
    [[nodiscard]] bool has_context() const noexcept { return !context_.empty(); }

    // Returns a copy of this error with context set
    [[nodiscard]] Error with_context(std::string ctx) const;

    // Category checks
    [[nodiscard]] bool is_decode_error() const noexcept;
    [[nodiscard]] bool is_encode_error() const noexcept;
    [[nodiscard]] bool is_connection_error() const noexcept;
    [[nodiscard]] bool is_transceiver_error() const noexcept;
    [[nodiscard]] bool is_recoverable() const noexcept;

    // Derived properties
    [[nodiscard]] ErrorSeverity severity() const noexcept;

    // Formatting
    [[nodiscard]] std::string format() const;
    [[nodiscard]] std::string format_short() const;

    // Code/severity to string
    [[nodiscard]] static std::string_view code_to_string(ErrorCode code) noexcept;
    [[nodiscard]] static std::string_view severity_to_string(ErrorSeverity sev) noexcept;

    // Comparison
    [[nodiscard]] bool operator==(const Error& other) const noexcept {
        return code_ == other.code_;
    }

private:
    ErrorCode code_ = ErrorCode::InternalError;
    std::string message_;
    std::string context_;
    std::source_location location_;
    std::optional<Timestamp> timestamp_;
};

// ============================================================================
// Result Types
// ============================================================================

template<typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

// ============================================================================
// Convenience Macros
// ============================================================================

// Create an error with current source location
#define CONDUIT_ERROR(code, msg) \
    ::conduit::Error(code, msg, std::source_location::current())

// TRY macro: early return on error (propagates error, discards value).
// Works with both Result<T> and VoidResult.
// Use CONDUIT_TRY_ASSIGN(type, var, expr) when you need the unwrapped value.
#define CONDUIT_TRY(expr) \
    do { \
        auto&& _conduit_result_ = (expr); \
        if (!_conduit_result_) { \
            return std::unexpected(_conduit_result_.error()); \
        } \
    } while(0)

// Cross-platform TRY that assigns to a variable.
// Note: Must be used within a braced block (not directly inside if/else/for).
#define CONDUIT_TRY_ASSIGN(type, var, expr) \
    auto _conduit_try_##var = (expr); \
    if (!_conduit_try_##var) { \
        return std::unexpected(_conduit_try_##var.error()); \
    } \
    type var = std::move(*_conduit_try_##var)

// TRY with context: early return on error, attaching context string
#define CONDUIT_TRY_CONTEXT(expr, ctx_str) \
    do { \
        auto&& _conduit_ctx_result_ = (expr); \
        if (!_conduit_ctx_result_) { \
            return std::unexpected(_conduit_ctx_result_.error().with_context(ctx_str)); \
        } \
    } while(0)

// Ensure a condition or return an error
#define CONDUIT_ENSURE(cond, code, msg) \
    do { \
        if (!(cond)) { \
            return std::unexpected(CONDUIT_ERROR(code, msg)); \
        } \
    } while(0)

} // namespace conduit
