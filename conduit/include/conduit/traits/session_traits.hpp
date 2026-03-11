// SPDX-License-Identifier: MIT
// Conduit - Session Traits (ISession Interface)

#pragma once

#include <conduit/core/error.hpp>
#include <any>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace conduit::traits {

// ============================================================================
// DecodedMessage: Result of decoding a frame
//
// Internal: Used by ISession implementations and Transceiver.
// Users interact with typed messages via MessageHandler, never with DecodedMessage.
// ============================================================================

struct DecodedMessage {
    uint64_t type_id;                  // Unique per leaf type (compile-time hash)
    std::string_view type_name;        // e.g., "Cat048Record"
    std::any payload;                  // The typed message object
    std::vector<uint8_t> raw;          // Raw bytes for forwarding (owned copy)
};

// ============================================================================
// EncodeResult: Result of encoding a message
//
// Carries the encoded bytes plus metadata about auto-managed fields
// (id, length, timestamp, etc.) that were set during encoding.
// ============================================================================

struct EncodeResult {
    std::vector<uint8_t> bytes;
    std::vector<std::pair<std::string, std::string>> auto_fields; // name, value
};

// ============================================================================
// ISession: Bridge between generated code and transceiver
//
// Internal interface: Generated sessions implement this.
// The Transceiver owns ISession instances and calls them during frame processing.
// Users should not implement or interact with ISession directly.
//
// This is the single virtual dispatch boundary. The transceiver holds ISession
// pointers (one per peer) and calls these methods for framing, decoding, and
// encoding. Everything else is direct generated code.
// ============================================================================

class ISession {
public:
    virtual ~ISession() = default;

    // Decode entry-point bytes -> leaf messages
    [[nodiscard]] virtual Result<std::vector<DecodedMessage>>
        decode_frame(std::span<const uint8_t> data) = 0;

    // Wrap leaf message -> entry-point wire bytes + auto-field metadata
    [[nodiscard]] virtual Result<EncodeResult>
        encode_wrap(uint64_t type_id, const std::any& payload) = 0;

    // Stream framing metadata
    [[nodiscard]] virtual std::span<const uint8_t> sync_pattern() const = 0;
    [[nodiscard]] virtual size_t min_frame_header_size() const = 0;
    [[nodiscard]] virtual size_t extract_frame_length(
        std::span<const uint8_t> header) const = 0;

    // Type introspection
    [[nodiscard]] virtual std::span<const uint64_t> leaf_type_ids() const = 0;
    [[nodiscard]] virtual std::string_view type_name(uint64_t type_id) const = 0;

    // Encode multiple messages of the same type into a single frame.
    // Only supported for array-payload protocols (<payload count="*"/>).
    // Default: returns BatchNotSupported error.
    [[nodiscard]] virtual Result<EncodeResult>
        encode_batch(uint64_t type_id, std::span<const std::any> payloads) {
        (void)type_id; (void)payloads;
        return std::unexpected(
            Error(ErrorCode::BatchNotSupported,
                  "This session does not support batch encoding"));
    }

    // Direction introspection (for transceiver-level send blocking)
    [[nodiscard]] virtual bool is_receive_only(uint64_t /*type_id*/) const { return false; }

    virtual void reset() = 0;

    // Format a decoded message payload as a human-readable string.
    [[nodiscard]] virtual std::string format_message(uint64_t type_id, const std::any& payload) const {
        (void)type_id; (void)payload; return {};
    }

    // Format an outbound message with auto-field overrides (id, length, timestamp, etc.)
    [[nodiscard]] virtual std::string format_outbound(
        uint64_t type_id, const std::any& payload,
        std::span<const std::pair<std::string, std::string>> auto_fields) const {
        (void)auto_fields;
        return format_message(type_id, payload); // default: ignore auto fields
    }

    // Return the protocol name (e.g., "asterix", "sentry-link").
    [[nodiscard]] virtual std::string_view protocol_name() const { return "unknown"; }

    // Passthrough sessions defer message logging to the caller (Java/Python)
    // because the C++ side only sees raw frames and cannot format messages.
    [[nodiscard]] virtual bool defers_message_logging() const { return false; }
};

} // namespace conduit::traits
