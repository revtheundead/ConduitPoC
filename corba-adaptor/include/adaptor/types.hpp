// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Common Types

#pragma once

#include <CorbaAdaptorC.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace adaptor {

// ============================================================================
// Byte buffer alias
// ============================================================================

using ByteBuffer = std::vector<uint8_t>;

// ============================================================================
// Peer identification
// ============================================================================

enum class PeerId : uint8_t {
    tcp_peer,
    corba_peer
};

[[nodiscard]] inline const char* to_string(PeerId id) noexcept {
    switch (id) {
        case PeerId::tcp_peer:   return "tcp-peer";
        case PeerId::corba_peer: return "corba-peer";
    }
    return "unknown";
}

// ============================================================================
// Internal data representation (IDL-free within the adaptor)
// ============================================================================

struct InternalPacket {
    PeerId                                      source{};
    std::chrono::steady_clock::time_point       received_at{};
    std::string                                 label;
    ByteBuffer                                  payload;

    /// Convert to the CORBA DataPacket for distribution via the supplier.
    [[nodiscard]] CorbaAdaptor::DataPacket to_corba() const {
        CorbaAdaptor::DataPacket pkt;
        pkt.header.timestamp_us = static_cast<CORBA::ULongLong>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                received_at.time_since_epoch()).count());
        pkt.header.source = (source == PeerId::tcp_peer)
            ? CorbaAdaptor::PEER_TCP
            : CorbaAdaptor::PEER_CORBA;
        pkt.header.label = CORBA::string_dup(label.c_str());

        pkt.payload.length(static_cast<CORBA::ULong>(payload.size()));
        std::copy(payload.begin(), payload.end(), pkt.payload.get_buffer());
        return pkt;
    }
};

// ============================================================================
// Callback types used internally
// ============================================================================

/// Called when a peer delivers data to the adaptor.
using DataReceivedCallback = std::function<void(InternalPacket)>;

/// Called when a peer's connection state changes.
using PeerStateCallback = std::function<void(PeerId, bool /*connected*/)>;

} // namespace adaptor
