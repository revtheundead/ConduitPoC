// SPDX-License-Identifier: MIT
// Conduit - Reconnection Policy

#pragma once

#include <chrono>
#include <cstdint>

namespace conduit::transceiver::transport {

// ============================================================================
// ReconnectPolicy: Exponential backoff configuration for client transports
// ============================================================================

struct ReconnectPolicy {
    bool enabled = true;
    std::chrono::milliseconds initial_delay{1000};
    std::chrono::milliseconds max_delay{30000};
    double backoff_multiplier = 2.0;
    uint32_t max_attempts = 0;  // 0 = unlimited
};

} // namespace conduit::transceiver::transport
