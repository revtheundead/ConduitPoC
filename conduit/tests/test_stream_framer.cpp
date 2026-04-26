// SPDX-License-Identifier: MIT
// Conduit - StreamFramer Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/traits/session_traits.hpp>
#include <cstdint>
#include <span>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Mock ISession for framing tests
// ============================================================================

class MockFramerSession : public traits::ISession {
public:
    std::vector<uint8_t> sync;
    size_t min_header = 4;
    size_t fixed_frame_length = 0;  // 0 = read from header

    Result<std::vector<traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> /*data*/) override {
        return std::vector<traits::DecodedMessage>{};
    }

    Result<traits::EncodeResult>
    encode_wrap(uint64_t /*type_id*/, const std::any& /*payload*/) override {
        return traits::EncodeResult{{}, {}};
    }

    std::span<const uint8_t> sync_pattern() const override {
        return sync;
    }

    size_t min_frame_header_size() const override {
        return min_header;
    }

    size_t extract_frame_length(std::span<const uint8_t> header) const override {
        if (fixed_frame_length > 0) return fixed_frame_length;
        // Length field sits right after the sync pattern
        size_t offset = sync.size();
        if (header.size() < offset + 2) return 0;
        return (static_cast<size_t>(header[offset]) << 8) | header[offset + 1];
    }

    std::span<const uint64_t> leaf_type_ids() const override {
        return {};
    }

    std::string_view type_name(uint64_t /*type_id*/) const override {
        return "";
    }

    void reset() override {}
};

TEST_CASE("StreamFramer: complete frame in one push", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    // Frame: sync(2) + length(2) = header says 6 total, so 2 more bytes of payload
    std::vector<uint8_t> data = {0xAA, 0xBB, 0x00, 0x06, 0x01, 0x02};
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0] == data);
    CHECK(framer.buffered_bytes() == 0);
}

TEST_CASE("StreamFramer: frame split across two pushes", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    // Push partial frame
    std::vector<uint8_t> part1 = {0xAA, 0xBB, 0x00};
    auto r1 = framer.push_data(part1);
    REQUIRE(r1.has_value());
    CHECK(r1->empty());
    CHECK(framer.buffered_bytes() == 3);

    // Push remaining
    std::vector<uint8_t> part2 = {0x06, 0x01, 0x02};
    auto r2 = framer.push_data(part2);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x06, 0x01, 0x02};
    CHECK((*r2)[0] == expected);
    CHECK(framer.buffered_bytes() == 0);
}

TEST_CASE("StreamFramer: two frames in one push", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    std::vector<uint8_t> data = {
        0xAA, 0xBB, 0x00, 0x05, 0x01,       // Frame 1 (5 bytes)
        0xAA, 0xBB, 0x00, 0x04,              // Frame 2 (4 bytes, header-only)
    };

    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    std::vector<uint8_t> frame1 = {0xAA, 0xBB, 0x00, 0x05, 0x01};
    std::vector<uint8_t> frame2 = {0xAA, 0xBB, 0x00, 0x04};
    CHECK((*result)[0] == frame1);
    CHECK((*result)[1] == frame2);
}

TEST_CASE("StreamFramer: garbage before sync discarded", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    std::vector<uint8_t> data = {
        0xFF, 0xFF, 0x00,                    // Garbage
        0xAA, 0xBB, 0x00, 0x05, 0x01,       // Valid frame (5 bytes)
    };

    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x05, 0x01};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: partial header returns empty", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    // Only sync + 1 byte header (need 4 bytes min)
    std::vector<uint8_t> data = {0xAA, 0xBB, 0x00};
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    CHECK(result->empty());
    CHECK(framer.buffered_bytes() == 3);
}

TEST_CASE("StreamFramer: no-sync protocol (length-prefix only)", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {};  // No sync pattern
    session.min_header = 2;

    StreamFramer framer(session);

    // Length is in bytes [0..1], says frame is 5 bytes total
    std::vector<uint8_t> data = {0x00, 0x05, 0x01, 0x02, 0x03};
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0] == data);
}

TEST_CASE("StreamFramer: frame_length=0 no-sync discards byte", "[stream_framer]") {
    // When sync is empty and extract_frame_length returns 0, the framer should
    // discard bytes to make forward progress instead of getting stuck forever.
    MockFramerSession session;
    session.sync = {};  // No sync pattern
    session.min_header = 2;
    session.fixed_frame_length = 0;  // Will cause extract_frame_length to return 0

    // Override to always return 0 for the test scenario
    // fixed_frame_length=0 and header [0x00, 0x00] => extract_frame_length reads 0

    StreamFramer framer(session);

    // Push data where the length field is 0 (first two bytes are 0x00, 0x00)
    // followed by a valid frame (0x00, 0x04, 0x01, 0x02)
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x04, 0x01, 0x02};
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());

    // The framer should have discarded the leading zero bytes and found the frame
    REQUIRE(result->size() == 1);
    std::vector<uint8_t> expected = {0x00, 0x04, 0x01, 0x02};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: max_buffer_size enforcement", "[stream_framer]") {
    SECTION("push_data exceeding max_buffer_size returns BufferOverrun and resets") {
        MockFramerSession session;
        session.sync = {0xAA, 0xBB};
        session.min_header = 4;

        StreamFramer framer(session, 16);  // Small max buffer
        CHECK(framer.max_buffer_size() == 16);

        // Push data that exceeds the 16-byte limit
        std::vector<uint8_t> big_data(20, 0xFF);
        auto result = framer.push_data(big_data);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::BufferOverrun);
        CHECK(framer.buffered_bytes() == 0);  // Buffer was reset
    }

    SECTION("frame_length > max_buffer_size is treated as corrupt") {
        MockFramerSession session;
        session.sync = {0xAA, 0xBB};
        session.min_header = 4;

        StreamFramer framer(session, 32);

        // Frame header claims length 1000 (0x03E8), exceeding max_buffer_size
        // After skipping corrupt frame, a valid frame follows
        std::vector<uint8_t> data = {
            0xAA, 0xBB, 0x03, 0xE8,             // Corrupt: length=1000
            0xAA, 0xBB, 0x00, 0x05, 0x01,       // Valid: length=5
        };
        auto result = framer.push_data(data);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);

        std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x05, 0x01};
        CHECK((*result)[0] == expected);
    }

    SECTION("custom max_buffer_size via constructor") {
        MockFramerSession session;
        session.sync = {};
        session.min_header = 2;

        StreamFramer framer(session, 64);
        CHECK(framer.max_buffer_size() == 64);

        // set_max_buffer_size also works
        framer.set_max_buffer_size(128);
        CHECK(framer.max_buffer_size() == 128);
    }
}

TEST_CASE("StreamFramer: reset clears buffer", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session);

    std::vector<uint8_t> data = {0xAA, 0xBB, 0x00};
    [[maybe_unused]] auto _ = framer.push_data(data);
    CHECK(framer.buffered_bytes() == 3);

    framer.reset();
    CHECK(framer.buffered_bytes() == 0);
}

// ============================================================================
// Error path tests
// ============================================================================

TEST_CASE("StreamFramer: BufferOverrun error on single oversized push", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 8); // Very small max buffer

    // Push more data than the max buffer allows in one call
    std::vector<uint8_t> big(16, 0x00);
    auto result = framer.push_data(big);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::BufferOverrun);
    CHECK(framer.buffered_bytes() == 0); // Buffer reset on overflow
}

TEST_CASE("StreamFramer: BufferOverrun from accumulated partial pushes", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 10);

    // First push: sync + incomplete header (buffered, no frame yet)
    std::vector<uint8_t> part1 = {0xAA, 0xBB, 0x00};
    auto r1 = framer.push_data(part1);
    REQUIRE(r1.has_value());
    CHECK(r1->empty());
    CHECK(framer.buffered_bytes() == 3);

    // Second push: pushes total past the 10-byte max
    std::vector<uint8_t> part2(9, 0xFF);
    auto r2 = framer.push_data(part2);
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().code() == ErrorCode::BufferOverrun);
    CHECK(framer.buffered_bytes() == 0);
}

TEST_CASE("StreamFramer: recovery after BufferOverrun error", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 16);

    // Trigger a BufferOverrun
    std::vector<uint8_t> oversized(20, 0x00);
    auto r1 = framer.push_data(oversized);
    REQUIRE_FALSE(r1.has_value());
    CHECK(r1.error().code() == ErrorCode::BufferOverrun);

    // After the error, the framer should be usable again (buffer was reset).
    // Feed a valid frame.
    std::vector<uint8_t> valid_frame = {0xAA, 0xBB, 0x00, 0x06, 0x01, 0x02};
    auto r2 = framer.push_data(valid_frame);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);
    CHECK((*r2)[0] == valid_frame);
    CHECK(framer.buffered_bytes() == 0);
}

TEST_CASE("StreamFramer: corrupt frame_length exceeding max_buffer_size is skipped", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 32);

    // First frame has corrupt length (0x1000 = 4096, well over max 32).
    // The framer should skip past the corrupt sync and find the next valid frame.
    std::vector<uint8_t> data = {
        0xAA, 0xBB, 0x10, 0x00,             // Corrupt: length=4096
        0xAA, 0xBB, 0x00, 0x05, 0x42,       // Valid: length=5
    };
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x05, 0x42};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: multiple corrupt frames before a valid one", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 64);

    // Two corrupt frames followed by a valid one
    std::vector<uint8_t> data = {
        0xAA, 0xBB, 0xFF, 0xFF,             // Corrupt: length=65535
        0xAA, 0xBB, 0x7F, 0x00,             // Corrupt: length=32512
        0xAA, 0xBB, 0x00, 0x05, 0xDD,       // Valid: length=5
    };
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x05, 0xDD};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: frame_length=0 with sync skips past sync", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;
    session.fixed_frame_length = 0; // extract_frame_length will read from header

    StreamFramer framer(session);

    // First frame has length=0 (bytes after sync are 0x00, 0x00).
    // The framer should skip past the sync pattern and find the next valid frame.
    std::vector<uint8_t> data = {
        0xAA, 0xBB, 0x00, 0x00,             // Length=0 (skip)
        0xAA, 0xBB, 0x00, 0x06, 0x01, 0x02, // Valid: length=6
    };
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x06, 0x01, 0x02};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: recovery after reset from corrupt stream", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 16);

    // Push garbage that partially fills the buffer but yields no frames
    std::vector<uint8_t> garbage = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto r1 = framer.push_data(garbage);
    REQUIRE(r1.has_value());
    CHECK(r1->empty());
    // Garbage has no sync, so some bytes remain buffered (partial sync overlap)
    CHECK(framer.buffered_bytes() > 0);

    // Explicitly reset to clear the corrupt state
    framer.reset();
    CHECK(framer.buffered_bytes() == 0);

    // Now feed a valid frame — framer should work normally
    std::vector<uint8_t> valid = {0xAA, 0xBB, 0x00, 0x05, 0x99};
    auto r2 = framer.push_data(valid);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);

    std::vector<uint8_t> expected = {0xAA, 0xBB, 0x00, 0x05, 0x99};
    CHECK((*r2)[0] == expected);
}

TEST_CASE("StreamFramer: no-sync mode recovers from zero-length frames", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {};  // No sync pattern (length-prefix only)
    session.min_header = 2;

    StreamFramer framer(session);

    // Zero-length header followed by a valid frame.
    // The framer should discard bytes to make forward progress.
    std::vector<uint8_t> data = {
        0x00, 0x00,                          // Length=0 (discarded byte-by-byte)
        0x00, 0x05, 0x01, 0x02, 0x03,       // Valid: length=5
    };
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    std::vector<uint8_t> expected = {0x00, 0x05, 0x01, 0x02, 0x03};
    CHECK((*result)[0] == expected);
}

TEST_CASE("StreamFramer: interleaved errors and valid frames", "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 64);

    // Valid frame, then corrupt, then valid again — all in one push
    std::vector<uint8_t> data = {
        0xAA, 0xBB, 0x00, 0x04,             // Valid frame 1 (4 bytes, header only)
        0xAA, 0xBB, 0xFF, 0xFF,             // Corrupt: length=65535
        0xAA, 0xBB, 0x00, 0x05, 0xEE,       // Valid frame 2 (5 bytes)
    };
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    std::vector<uint8_t> frame1 = {0xAA, 0xBB, 0x00, 0x04};
    std::vector<uint8_t> frame2 = {0xAA, 0xBB, 0x00, 0x05, 0xEE};
    CHECK((*result)[0] == frame1);
    CHECK((*result)[1] == frame2);
}

// ============================================================================
// Defensive guard: a session reporting min_frame_header_size()==0 must not
// produce a zero-length header span (UB inside session.extract_frame_length).
// The framer clamps min_header up to 1 in that case.
// ============================================================================

TEST_CASE("StreamFramer: tolerates session reporting min_frame_header_size=0",
          "[stream_framer][error]") {
    MockFramerSession session;
    session.sync = {};            // No sync pattern
    session.min_header = 0;       // Misbehaving session
    session.fixed_frame_length = 1;  // Each input byte is one frame

    StreamFramer framer(session, 64);

    std::vector<uint8_t> data = {0x10, 0x20, 0x30};
    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
    CHECK((*result)[0] == std::vector<uint8_t>{0x10});
    CHECK((*result)[1] == std::vector<uint8_t>{0x20});
    CHECK((*result)[2] == std::vector<uint8_t>{0x30});
}

// ============================================================================
// buffered_bytes() reports remaining un-framed buffer correctly across resets
// and partial pushes.
// ============================================================================

TEST_CASE("StreamFramer: buffered_bytes tracks unconsumed input", "[stream_framer]") {
    MockFramerSession session;
    session.sync = {0xAA, 0xBB};
    session.min_header = 4;

    StreamFramer framer(session, 64);
    CHECK(framer.buffered_bytes() == 0);

    // Push a partial header — nothing extractable yet.
    std::vector<uint8_t> partial = {0xAA, 0xBB, 0x00};
    auto r1 = framer.push_data(partial);
    REQUIRE(r1.has_value());
    REQUIRE(r1->empty());
    CHECK(framer.buffered_bytes() == 3);

    // Reset clears the internal buffer.
    framer.reset();
    CHECK(framer.buffered_bytes() == 0);

    // Push a complete frame; afterwards buffered should be 0.
    std::vector<uint8_t> full = {0xAA, 0xBB, 0x00, 0x04};
    auto r2 = framer.push_data(full);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);
    CHECK(framer.buffered_bytes() == 0);
}
