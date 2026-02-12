// SPDX-License-Identifier: MIT
// Conduit - Common Type Aliases

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace conduit {

// ============================================================================
// Time Types
// ============================================================================

using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::milliseconds;
using SteadyTimestamp = std::chrono::steady_clock::time_point;

// ============================================================================
// Byte Type Aliases
// ============================================================================

using byte_t = uint8_t;

} // namespace conduit
