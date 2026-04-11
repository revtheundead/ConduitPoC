// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Common Types
//
// Intentionally minimal: the adaptor now uses typed messages end-to-end via
// Conduit for the TCP peer, and typed messages via a Conduit `ISession` for
// the CORBA peer's byte stream. The only "raw" surface is the CORBA
// RawDataChannel; these aliases describe its callback contracts.

#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace adaptor {

// ============================================================================
// Callback types used internally
// ============================================================================

/// Called when a peer delivers raw bytes to the adaptor (CORBA peer only).
/// The span is valid only for the duration of the call.
using RawBytesCallback = std::function<void(std::span<const std::uint8_t>)>;

/// Called when a peer's connection state changes.
using PeerStateCallback = std::function<void(bool /*connected*/)>;

} // namespace adaptor
