// SPDX-License-Identifier: MIT
// Tests for auto="timestamp" — verifies timestamp is set during session encode

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>

#include "frame_timestamp/messages.hpp"
#include "frame_timestamp/sessions.hpp"

TEST_CASE("frame_timestamp: session sets timestamp on encode_wrap", "[frame][timestamp]") {
    auto session = frame_timestamp::create_ts_frame_session();
    REQUIRE(session != nullptr);

    auto before = static_cast<uint32_t>(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
        & 0xffffffffULL);

    frame_timestamp::Ping msg;
    msg.set_seq(42);
    auto encoded = session->encode_wrap(frame_timestamp::Ping::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto after = static_cast<uint32_t>(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
        & 0xffffffffULL);

    // Decode and check timestamp is in [before, after] range
    auto decoded = frame_timestamp::TsFrame::decode_bytes(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);

    uint32_t ts = decoded->ts();
    CHECK(ts != 0);
    // Timestamp should be within a reasonable range of the system clock
    // Allow for wraparound: if before > after, the 32-bit counter wrapped
    if (before <= after) {
        CHECK(ts >= before);
        CHECK(ts <= after);
    }
    // else: 32-bit wrap occurred during test — just check non-zero (already done)
}

TEST_CASE("frame_timestamp: reset does not affect timestamp", "[frame][timestamp]") {
    auto session = frame_timestamp::create_ts_frame_session();
    session->reset();

    frame_timestamp::Ping msg;
    msg.set_seq(1);
    auto encoded = session->encode_wrap(frame_timestamp::Ping::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = frame_timestamp::TsFrame::decode_bytes(*encoded);
    REQUIRE(decoded.has_value());
    // Timestamp is still set (reset only affects sequence counters, not timestamps)
    CHECK(decoded->ts() != 0);
}

TEST_CASE("frame_timestamp: direct frame roundtrip", "[frame][timestamp][roundtrip]") {
    frame_timestamp::Ping msg;
    msg.set_seq(9999);

    auto frame = frame_timestamp::TsFrame::wrap(msg);
    CHECK(frame.msg_type() == 1);

    // Set timestamp manually for direct frame test
    frame.set_ts(12345678);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Wire: [msg_type:1][ts:4][length:2][seq:2] = 9 bytes
    REQUIRE(bytes->size() == 9);

    auto decoded = frame_timestamp::TsFrame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg_type() == 1);
    CHECK(decoded->ts() == 12345678);
    CHECK(decoded->length() == 9);

    auto* payload = std::get_if<frame_timestamp::Ping>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 9999);
}
