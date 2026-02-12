// SPDX-License-Identifier: MIT
// Conduit - Stream Framer

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/traits/session_traits.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace conduit::transceiver {

// ============================================================================
// StreamFramer: Extract complete protocol frames from a byte stream
//
// One framer per peer. Not thread-safe (only the I/O thread feeds it).
//
// Algorithm:
//   1. Append incoming bytes to internal buffer.
//   2. Find sync pattern at buffer start; discard bytes before sync.
//   3. If sync is empty, use length-prefix mode only (no sync search).
//   4. Check buffer size >= min_frame_header_size; if not, need more data.
//   5. Call extract_frame_length() to get total frame length.
//   6. If buffer has enough bytes, extract frame, remove from buffer, repeat.
//   7. Return all extracted frames.
// ============================================================================

class StreamFramer {
public:
    explicit StreamFramer(traits::ISession& session);
    StreamFramer(traits::ISession& session, size_t max_buffer_size);

    // Push new data and extract any complete frames.
    [[nodiscard]] Result<std::vector<std::vector<uint8_t>>>
        push_data(std::span<const uint8_t> data);

    // Reset internal buffer state.
    void reset();

    // Number of bytes currently buffered (partial frame data).
    [[nodiscard]] size_t buffered_bytes() const noexcept;

    void set_max_buffer_size(size_t max_size) noexcept { max_buffer_size_ = max_size; }
    [[nodiscard]] size_t max_buffer_size() const noexcept { return max_buffer_size_; }

private:
    traits::ISession& session_;
    std::vector<uint8_t> buffer_;
    size_t offset_ = 0;  // Read position into buffer_
    size_t max_buffer_size_ = 1048576;  // 1 MB default
};

} // namespace conduit::transceiver
