// SPDX-License-Identifier: MIT
// Conduit - Transceiver Statistics

#pragma once

#include <atomic>
#include <cstdint>

namespace conduit::transceiver {

// ============================================================================
// TransceiverStats: Atomic counters for observability
//
// All counters are monotonically increasing. Use snapshot() to get a
// consistent read; use reset() to zero all counters.
// ============================================================================

struct TransceiverStats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_dispatched{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> decode_errors{0};
    std::atomic<uint64_t> handler_errors{0};
    std::atomic<uint64_t> handler_timeouts{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> bytes_sent{0};

    struct Snapshot {
        uint64_t messages_received;
        uint64_t messages_dispatched;
        uint64_t messages_dropped;
        uint64_t decode_errors;
        uint64_t handler_errors;
        uint64_t handler_timeouts;
        uint64_t bytes_received;
        uint64_t bytes_sent;
    };

    Snapshot snapshot() const noexcept {
        return {
            messages_received.load(std::memory_order_relaxed),
            messages_dispatched.load(std::memory_order_relaxed),
            messages_dropped.load(std::memory_order_relaxed),
            decode_errors.load(std::memory_order_relaxed),
            handler_errors.load(std::memory_order_relaxed),
            handler_timeouts.load(std::memory_order_relaxed),
            bytes_received.load(std::memory_order_relaxed),
            bytes_sent.load(std::memory_order_relaxed),
        };
    }

    void reset() noexcept {
        messages_received.store(0, std::memory_order_relaxed);
        messages_dispatched.store(0, std::memory_order_relaxed);
        messages_dropped.store(0, std::memory_order_relaxed);
        decode_errors.store(0, std::memory_order_relaxed);
        handler_errors.store(0, std::memory_order_relaxed);
        handler_timeouts.store(0, std::memory_order_relaxed);
        bytes_received.store(0, std::memory_order_relaxed);
        bytes_sent.store(0, std::memory_order_relaxed);
    }
};

} // namespace conduit::transceiver
