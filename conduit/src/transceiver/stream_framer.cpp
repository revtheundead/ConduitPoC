// SPDX-License-Identifier: MIT
// Conduit - Stream Framer (implementation)

#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/logging/logger.hpp>
#include <algorithm>

namespace conduit::transceiver {

StreamFramer::StreamFramer(traits::ISession& session)
    : session_(session) {}

StreamFramer::StreamFramer(traits::ISession& session, size_t max_buffer_size)
    : session_(session), max_buffer_size_(max_buffer_size) {}

Result<std::vector<std::vector<uint8_t>>>
StreamFramer::push_data(std::span<const uint8_t> data) {
    // Check if data fits; only compact if needed to avoid O(n) shift on every call
    if (buffer_.size() + data.size() > max_buffer_size_) {
        if (offset_ > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<ptrdiff_t>(offset_));
            offset_ = 0;
        }
        if (buffer_.size() + data.size() > max_buffer_size_) {
            reset();
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::BufferOverrun,
                              "StreamFramer buffer exceeds max size"));
        }
    }

    buffer_.insert(buffer_.end(), data.begin(), data.end());

    std::vector<std::vector<uint8_t>> frames;
    auto sync = session_.sync_pattern();
    auto min_header = session_.min_frame_header_size();

    // Guard against min_header == 0 which would cause UB (zero-length span)
    if (min_header == 0) {
        LOG_WARN("StreamFramer: session reports min_frame_header_size=0, clamping to 1");
        min_header = 1;
    }

    auto available = [&]() -> size_t {
        return (offset_ <= buffer_.size()) ? buffer_.size() - offset_ : 0;
    };
    auto begin = [&]() { return buffer_.begin() + static_cast<ptrdiff_t>(offset_); };

    while (true) {
        // Step 2/3: Find sync pattern (if non-empty)
        if (!sync.empty()) {
            // Search for sync pattern starting at offset_
            auto it = std::search(begin(), buffer_.end(),
                                  sync.begin(), sync.end());
            if (it == buffer_.end()) {
                // No sync found — keep only the last (sync.size()-1) bytes
                // which could be a partial sync match
                if (available() > sync.size() - 1) {
                    offset_ = buffer_.size() - (sync.size() - 1);
                }
                break;
            }
            if (it != begin()) {
                // Discard garbage before sync
                offset_ = static_cast<size_t>(it - buffer_.begin());
            }
        }

        // Step 4: Need at least min_frame_header_size bytes
        if (available() < min_header) {
            break;
        }

        // Step 5: Extract frame length from header
        auto header_span = std::span<const uint8_t>(buffer_.data() + offset_, min_header);
        size_t frame_length = session_.extract_frame_length(header_span);

        if (frame_length > max_buffer_size_) {
            // Corrupt frame length — skip past sync and retry
            if (!sync.empty()) {
                if (available() >= sync.size()) {
                    offset_ += sync.size();
                } else {
                    break;
                }
            } else if (available() > 0) {
                offset_ += 1;
            } else {
                break;
            }
            continue;
        }

        if (frame_length == 0) {
            if (!sync.empty()) {
                if (available() >= sync.size()) {
                    offset_ += sync.size();
                } else {
                    break;
                }
            } else if (available() > 0) {
                LOG_WARN("StreamFramer: frame_length=0 with no sync, discarding 1 byte");
                offset_ += 1;
            } else {
                break;
            }
            continue;
        }

        // Step 6: Check if we have the complete frame
        if (available() < frame_length) {
            break;
        }

        // Extract frame
        frames.emplace_back(begin(),
                            begin() + static_cast<ptrdiff_t>(frame_length));
        offset_ += frame_length;
    }

    // Compact buffer when offset is significant
    if (offset_ > 0 && (offset_ > buffer_.size() / 2 || !frames.empty())) {
        buffer_.erase(buffer_.begin(),
                      buffer_.begin() + static_cast<ptrdiff_t>(offset_));
        offset_ = 0;
    }

    return frames;
}

void StreamFramer::reset() {
    buffer_.clear();
    offset_ = 0;
}

size_t StreamFramer::buffered_bytes() const noexcept {
    return (offset_ <= buffer_.size()) ? buffer_.size() - offset_ : 0;
}

} // namespace conduit::transceiver
