#ifndef BGEN11_STREAM_FRAMER_HPP
#define BGEN11_STREAM_FRAMER_HPP

// Stream-oriented framing helper.  Ported from Conduit's
// conduit/transceiver/stream_framer.* to header-only C++11 so it can
// live inside the bgen11 runtime alongside the ISession it drives.
//
// Usage:
//   bgen11::StreamFramer framer(*session);
//   auto frames = framer.push_data(incoming_chunk);
//   if (frames) for (auto& f : *frames) session->decode_frame(f);
//
// Algorithm (unchanged from Conduit):
//   1. Append incoming bytes to the internal buffer.
//   2. If session has a sync_pattern(), find it; discard bytes before.
//   3. Wait for at least min_frame_header_size() bytes.
//   4. Call extract_frame_length(header) to learn total frame length.
//   5. Wait until the whole frame is buffered, then slice it out.
//   6. Repeat until the buffer cannot produce another complete frame.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../compat11/span.hpp"
#include "error.hpp"
#include "session_traits.hpp"

namespace bgen11 {

class StreamFramer {
public:
    explicit StreamFramer(traits::ISession& session)
        : session_(&session), offset_(0), max_buffer_size_(1048576) {}

    StreamFramer(traits::ISession& session, std::size_t max_buffer_size)
        : session_(&session), offset_(0), max_buffer_size_(max_buffer_size) {}

    Result<std::vector<std::vector<uint8_t> > >
    push_data(cpp11::span<const uint8_t> data) {
        // If appending would overflow the max buffer, try compacting; if
        // still over, drop the buffer and return an error.
        if (buffer_.size() + data.size() > max_buffer_size_) {
            if (offset_ > 0) {
                buffer_.erase(buffer_.begin(),
                              buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
                offset_ = 0;
            }
            if (buffer_.size() + data.size() > max_buffer_size_) {
                reset();
                return cpp11::make_unexpected(Error(
                    ErrorCode_BufferOverrun,
                    "StreamFramer buffer exceeds max size"));
            }
        }

        buffer_.insert(buffer_.end(), data.data(), data.data() + data.size());

        std::vector<std::vector<uint8_t> > frames;

        cpp11::span<const uint8_t> sync = session_->sync_pattern();
        std::size_t min_header = session_->min_frame_header_size();
        if (min_header == 0) min_header = 1;  // guard against degenerate value

        while (true) {
            // Step 2/3: find sync pattern if the session defines one.
            if (!sync.empty()) {
                std::vector<uint8_t>::iterator it = std::search(
                    buffer_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    buffer_.end(),
                    sync.data(), sync.data() + sync.size());
                if (it == buffer_.end()) {
                    // Keep the last sync.size()-1 bytes in case the sync
                    // straddles the next chunk.
                    std::size_t keep = sync.size() - 1;
                    if (available() > keep) {
                        offset_ = buffer_.size() - keep;
                    }
                    break;
                }
                if (it != buffer_.begin() + static_cast<std::ptrdiff_t>(offset_)) {
                    offset_ = static_cast<std::size_t>(it - buffer_.begin());
                }
            }

            // Step 4: wait for the header.
            if (available() < min_header) break;

            // Step 5: ask the session how long this frame is.
            cpp11::span<const uint8_t> header(
                buffer_.data() + offset_, min_header);
            std::size_t frame_length = session_->extract_frame_length(header);

            if (frame_length > 0 && frame_length < min_header) {
                if (!advance_past_corrupt(sync)) break;
                continue;
            }
            if (frame_length > max_buffer_size_) {
                if (!advance_past_corrupt(sync)) break;
                continue;
            }
            if (frame_length == 0) {
                if (!advance_past_corrupt(sync)) break;
                continue;
            }

            // Step 6: need the whole frame before slicing.
            if (available() < frame_length) break;

            frames.push_back(std::vector<uint8_t>(
                buffer_.begin() + static_cast<std::ptrdiff_t>(offset_),
                buffer_.begin() + static_cast<std::ptrdiff_t>(offset_ + frame_length)));
            offset_ += frame_length;
        }

        // Compact the buffer when offset_ grew significant.
        if (offset_ > 0 && (offset_ > buffer_.size() / 2 || !frames.empty())) {
            buffer_.erase(buffer_.begin(),
                          buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
            offset_ = 0;
        }

        return frames;
    }

    void reset() {
        buffer_.clear();
        offset_ = 0;
    }

    std::size_t buffered_bytes() const {
        return (offset_ <= buffer_.size()) ? buffer_.size() - offset_ : 0;
    }

    void set_max_buffer_size(std::size_t n) { max_buffer_size_ = n; }
    std::size_t max_buffer_size() const { return max_buffer_size_; }

private:
    std::size_t available() const {
        return (offset_ <= buffer_.size()) ? buffer_.size() - offset_ : 0;
    }

    // Advance offset_ past a corrupt / too-small / zero-length frame.
    // Returns false if we need more bytes first.
    bool advance_past_corrupt(cpp11::span<const uint8_t> sync) {
        if (!sync.empty()) {
            if (available() >= sync.size()) {
                offset_ += sync.size();
                return true;
            }
            return false;
        }
        if (available() > 0) {
            offset_ += 1;
            return true;
        }
        return false;
    }

    traits::ISession*     session_;
    std::vector<uint8_t>  buffer_;
    std::size_t           offset_;
    std::size_t           max_buffer_size_;
};

} // namespace bgen11

#endif
