// SPDX-License-Identifier: MIT
// Conduit - Error Implementation

#include <conduit/core/error.hpp>
#include <format>
#include <iomanip>
#include <sstream>

namespace conduit {

// ============================================================================
// Category Checks
// ============================================================================

bool Error::is_decode_error() const noexcept {
    int v = static_cast<int>(code_);
    return v >= 100 && v < 200;
}

bool Error::is_encode_error() const noexcept {
    int v = static_cast<int>(code_);
    return v >= 200 && v < 300;
}

bool Error::is_connection_error() const noexcept {
    int v = static_cast<int>(code_);
    return v >= 300 && v < 400;
}

bool Error::is_transceiver_error() const noexcept {
    int v = static_cast<int>(code_);
    return v >= 400 && v < 500;
}

bool Error::is_recoverable() const noexcept {
    switch (code_) {
        case ErrorCode::BufferUnderrun:
        case ErrorCode::InvalidFieldValue:
        case ErrorCode::UnknownEnumValue:
        case ErrorCode::UnknownDiscriminator:
        case ErrorCode::ConnectionRefused:
        case ErrorCode::ConnectionTimeout:
        case ErrorCode::ConnectionReset:
        case ErrorCode::ConnectionClosed:
        case ErrorCode::QueueFull:
        case ErrorCode::Timeout:
            return true;
        default:
            return false;
    }
}

ErrorSeverity Error::severity() const noexcept {
    int v = static_cast<int>(code_);
    if (v >= 100 && v < 200) return ErrorSeverity::Warning;   // Decode
    if (v >= 200 && v < 300) return ErrorSeverity::Warning;   // Encode
    if (v >= 300 && v < 400) return ErrorSeverity::Error;     // Connection
    if (v >= 400 && v < 500) return ErrorSeverity::Error;     // Transceiver
    if (v >= 900)            return ErrorSeverity::Critical;   // Internal
    return ErrorSeverity::Error;
}

// ============================================================================
// Context
// ============================================================================

Error Error::with_context(std::string ctx) const {
    Error copy = *this;
    if (copy.context_.empty()) {
        copy.context_ = std::move(ctx);
    } else {
        ctx.reserve(ctx.size() + 3 + copy.context_.size());
        ctx += " > ";
        ctx += copy.context_;
        copy.context_ = std::move(ctx);
    }
    return copy;
}

// ============================================================================
// Formatting
// ============================================================================

std::string Error::format() const {
    std::ostringstream oss;

    auto time_t = std::chrono::system_clock::to_time_t(timestamp());
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << " [" << code_to_string(code_) << "] " << message_;
    if (!context_.empty()) {
        oss << " (in " << context_ << ")";
    }
    oss << " (at " << location_.file_name()
        << ":" << location_.line()
        << " in " << location_.function_name() << ")";

    return oss.str();
}

std::string Error::format_short() const {
    if (context_.empty()) {
        return std::format("[{}] {}", code_to_string(code_), message_);
    }
    return std::format("[{}] {} (in {})", code_to_string(code_), message_, context_);
}

// ============================================================================
// Code/Severity to String
// ============================================================================

std::string_view Error::code_to_string(ErrorCode code) noexcept {
    switch (code) {
        // Decode
        case ErrorCode::BufferUnderrun:           return "BUFFER_UNDERRUN";
        case ErrorCode::InvalidFieldValue:        return "INVALID_FIELD_VALUE";
        case ErrorCode::ConstraintViolation:      return "CONSTRAINT_VIOLATION";
        case ErrorCode::UnknownEnumValue:         return "UNKNOWN_ENUM_VALUE";
        case ErrorCode::UnknownDiscriminator:     return "UNKNOWN_DISCRIMINATOR";
        case ErrorCode::ExactConsumptionFailed:   return "EXACT_CONSUMPTION_FAILED";
        case ErrorCode::NegativeLength:           return "NEGATIVE_LENGTH";
        case ErrorCode::DecodingFailed:           return "DECODING_FAILED";
        case ErrorCode::UnknownTypeId:            return "UNKNOWN_TYPE_ID";
        case ErrorCode::MaxLengthExceeded:       return "MAX_LENGTH_EXCEEDED";
        case ErrorCode::StringEncodingError:     return "STRING_ENCODING_ERROR";
        case ErrorCode::ConstraintViolationDeferred: return "CONSTRAINT_VIOLATION_DEFERRED";

        // Encode
        case ErrorCode::EncodeConstraintViolation: return "ENCODE_CONSTRAINT_VIOLATION";
        case ErrorCode::MissingRequiredField:      return "MISSING_REQUIRED_FIELD";
        case ErrorCode::EncodingFailed:            return "ENCODING_FAILED";
        case ErrorCode::BufferOverrun:             return "BUFFER_OVERRUN";
        case ErrorCode::StringTooLong:             return "STRING_TOO_LONG";
        case ErrorCode::DirectionViolation:        return "DIRECTION_VIOLATION";

        // Connection
        case ErrorCode::ConnectionRefused:  return "CONNECTION_REFUSED";
        case ErrorCode::ConnectionTimeout:  return "CONNECTION_TIMEOUT";
        case ErrorCode::ConnectionReset:    return "CONNECTION_RESET";
        case ErrorCode::ConnectionClosed:   return "CONNECTION_CLOSED";
        case ErrorCode::HostNotFound:       return "HOST_NOT_FOUND";
        case ErrorCode::NetworkUnreachable: return "NETWORK_UNREACHABLE";
        case ErrorCode::SocketError:        return "SOCKET_ERROR";

        // Transceiver
        case ErrorCode::NotRunning:             return "NOT_RUNNING";
        case ErrorCode::AlreadyRunning:         return "ALREADY_RUNNING";
        case ErrorCode::PeerNotFound:           return "PEER_NOT_FOUND";
        case ErrorCode::QueueFull:              return "QUEUE_FULL";
        case ErrorCode::InvalidConfig:          return "INVALID_CONFIG";
        case ErrorCode::MultiplePeers:          return "MULTIPLE_PEERS";
        case ErrorCode::UnsupportedMessageType: return "UNSUPPORTED_MESSAGE_TYPE";

        // Internal
        case ErrorCode::InternalError:    return "INTERNAL_ERROR";
        case ErrorCode::NotImplemented:   return "NOT_IMPLEMENTED";
        case ErrorCode::InvalidArgument:  return "INVALID_ARGUMENT";
        case ErrorCode::InvalidState:     return "INVALID_STATE";
        case ErrorCode::Timeout:          return "TIMEOUT";
    }
    return "UNKNOWN";
}

std::string_view Error::severity_to_string(ErrorSeverity sev) noexcept {
    switch (sev) {
        case ErrorSeverity::Debug:    return "DEBUG";
        case ErrorSeverity::Info:     return "INFO";
        case ErrorSeverity::Warning:  return "WARNING";
        case ErrorSeverity::Error:    return "ERROR";
        case ErrorSeverity::Critical: return "CRITICAL";
        case ErrorSeverity::Fatal:    return "FATAL";
    }
    return "UNKNOWN";
}

} // namespace conduit
