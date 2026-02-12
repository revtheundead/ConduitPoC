// SPDX-License-Identifier: MIT
// Conduit - Session Traits (ISession Interface)

#pragma once

#include <conduit/core/error.hpp>
#include <any>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
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

    // Wrap leaf message -> entry-point wire bytes
    [[nodiscard]] virtual Result<std::vector<uint8_t>>
        encode_wrap(uint64_t type_id, const std::any& payload) = 0;

    // Stream framing metadata
    [[nodiscard]] virtual std::span<const uint8_t> sync_pattern() const = 0;
    [[nodiscard]] virtual size_t min_frame_header_size() const = 0;
    [[nodiscard]] virtual size_t extract_frame_length(
        std::span<const uint8_t> header) const = 0;

    // Type introspection
    [[nodiscard]] virtual std::span<const uint64_t> leaf_type_ids() const = 0;
    [[nodiscard]] virtual std::string_view type_name(uint64_t type_id) const = 0;

    // Direction introspection (for transceiver-level send blocking)
    [[nodiscard]] virtual bool is_receive_only(uint64_t /*type_id*/) const { return false; }

    virtual void reset() = 0;
};

} // namespace conduit::traits
