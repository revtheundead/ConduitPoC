// SPDX-License-Identifier: MIT
// Conduit - Peer Identity and Message Envelope Types

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/net/connection_state.hpp>
#include <conduit/traits/session_traits.hpp>
#include <cstdint>
#include <format>
#include <functional>
#include <ostream>
#include <string>

namespace conduit::transceiver {

// ============================================================================
// PeerId: Opaque peer identity
// ============================================================================

class PeerId {
public:
    constexpr PeerId() noexcept = default;
    constexpr explicit PeerId(uint32_t id) noexcept : id_(id) {}

    [[nodiscard]] constexpr uint32_t value() const noexcept { return id_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return id_ != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }

    constexpr bool operator==(const PeerId&) const noexcept = default;
    constexpr auto operator<=>(const PeerId&) const noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, const PeerId& p) {
        return os << "Peer(" << p.id_ << ")";
    }

    [[nodiscard]] std::string to_string() const {
        return std::format("Peer({})", id_);
    }

private:
    uint32_t id_ = 0;
};

} // namespace conduit::transceiver

// std::formatter support for PeerId
template<>
struct std::formatter<conduit::transceiver::PeerId> : std::formatter<uint32_t> {
    auto format(conduit::transceiver::PeerId p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "Peer({})", p.value());
    }
};

// Hash support for PeerId
template<>
struct std::hash<conduit::transceiver::PeerId> {
    size_t operator()(conduit::transceiver::PeerId id) const noexcept {
        return std::hash<uint32_t>{}(id.value());
    }
};

namespace conduit::transceiver {

// ============================================================================
// InboundMessage: Decoded message envelope for the dispatch queue
// ============================================================================

struct InboundMessage {
    PeerId peer;
    traits::DecodedMessage decoded;
};

// ============================================================================
// ConnectionStateCallback
// ============================================================================

using ConnectionStateCallback = std::function<void(PeerId, net::ConnectionState)>;

// Opaque identifier returned by on_state_change() for later removal.
enum class CallbackId : uint32_t {};

} // namespace conduit::transceiver
