// SPDX-License-Identifier: MIT
// Conduit - StreamFramer Tests with bgen-generated Sessions

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <sentry_link/sessions.hpp>
#include <cstdint>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Helpers: encode a leaf message into its full frame bytes
// ============================================================================

static std::vector<uint8_t> encode_sentry_heartbeat(uint32_t ts, uint16_t uptime, uint8_t cpu) {
    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(ts);
    hb.set_uptime_hours(uptime);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(cpu);
    auto session = sentry_link::create_frame_session();
    auto result = session->encode_wrap(sentry_link::HeartbeatBody::TYPE_ID, std::any(hb));
    REQUIRE(result.has_value());
    return *result;
}

static std::vector<uint8_t> encode_sentry_alert(const std::string& msg) {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1000);
    alert.set_source_id(1);
    alert.set_severity(sentry_link::severity_level::warning);
    alert.set_category(5);
    alert.set_alert_code(0x1234);
    alert.set_message(msg);
    auto session = sentry_link::create_frame_session();
    auto result = session->encode_wrap(sentry_link::AlertBody::TYPE_ID, std::any(alert));
    REQUIRE(result.has_value());
    return *result;
}

static std::vector<uint8_t> encode_sentry_sensor(uint16_t sensor_id, double raw_value) {
    sentry_link::SensorBody sensor;
    sensor.set_sensor_id(sensor_id);
    sensor.set_timestamp(500);
    sentry_link::SensorFlags flags;
    flags.set_channel(3);
    flags.set_precision(2);
    flags.set_saturated(0);
    flags.set_valid(1);
    sensor.set_flags(flags);
    sensor.set_raw_value(raw_value);
    sensor.set_unit_code(1);
    auto session = sentry_link::create_frame_session();
    auto result = session->encode_wrap(sentry_link::SensorBody::TYPE_ID, std::any(sensor));
    REQUIRE(result.has_value());
    return *result;
}

// ============================================================================
// Tests
//
// Note: Only sentry_link is used for StreamFramer tests because its length
// field contains the total frame size (header + body). The session_protocol's
// length field contains only the body size, which the framer interprets as
// total frame length, causing incorrect extraction.
// ============================================================================

TEST_CASE("StreamFramer generated: sentry_link sync=0xAA55 extracts HeartbeatBody frame",
          "[stream_framer][generated]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    auto frame_bytes = encode_sentry_heartbeat(1700000000, 100, 42);
    REQUIRE(frame_bytes.size() == 14);

    auto result = framer.push_data(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0].size() == 14);
    CHECK((*result)[0] == frame_bytes);
}

TEST_CASE("StreamFramer generated: sentry_link two frames concatenated",
          "[stream_framer][generated]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    auto hb_bytes = encode_sentry_heartbeat(1000, 50, 30);
    auto alert_bytes = encode_sentry_alert("TEST");

    // Concatenate
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), hb_bytes.begin(), hb_bytes.end());
    combined.insert(combined.end(), alert_bytes.begin(), alert_bytes.end());

    auto result = framer.push_data(combined);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].size() == 14);
    CHECK((*result)[1].size() == 47);
}

TEST_CASE("StreamFramer generated: sentry_link HeartbeatBody 3-part split",
          "[stream_framer][generated]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    auto frame_bytes = encode_sentry_heartbeat(0xAABBCCDD, 200, 75);
    REQUIRE(frame_bytes.size() == 14);

    // Split into 3 parts
    size_t part1_end = 4;
    size_t part2_end = 9;

    std::vector<uint8_t> p1(frame_bytes.begin(), frame_bytes.begin() + static_cast<ptrdiff_t>(part1_end));
    std::vector<uint8_t> p2(frame_bytes.begin() + static_cast<ptrdiff_t>(part1_end), frame_bytes.begin() + static_cast<ptrdiff_t>(part2_end));
    std::vector<uint8_t> p3(frame_bytes.begin() + static_cast<ptrdiff_t>(part2_end), frame_bytes.end());

    auto r1 = framer.push_data(p1);
    REQUIRE(r1.has_value());
    CHECK(r1->empty());

    auto r2 = framer.push_data(p2);
    REQUIRE(r2.has_value());
    CHECK(r2->empty());

    auto r3 = framer.push_data(p3);
    REQUIRE(r3.has_value());
    REQUIRE(r3->size() == 1);
    CHECK((*r3)[0] == frame_bytes);
}

TEST_CASE("StreamFramer generated: sentry_link garbage recovery",
          "[stream_framer][generated]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    auto sensor_bytes = encode_sentry_sensor(0x1234, 23.45);

    // Prepend garbage
    std::vector<uint8_t> data;
    for (int i = 0; i < 10; ++i) data.push_back(0xFF);
    data.insert(data.end(), sensor_bytes.begin(), sensor_bytes.end());

    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0] == sensor_bytes);
}

TEST_CASE("StreamFramer generated: sentry_link frame decoded after framing",
          "[stream_framer][generated]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    auto frame_bytes = encode_sentry_heartbeat(42, 10, 50);

    auto result = framer.push_data(frame_bytes);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    // Verify the extracted frame can be decoded
    auto decoded = session->decode_frame((*result)[0]);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);
    CHECK((*decoded)[0].type_name == "HeartbeatBody");

    auto& body = std::any_cast<const sentry_link::HeartbeatBody&>((*decoded)[0].payload);
    CHECK(body.timestamp() == 42);
    CHECK(body.cpu_load() == 50);
}
