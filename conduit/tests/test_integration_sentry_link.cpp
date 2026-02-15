// SPDX-License-Identifier: MIT
// Conduit - Integration Tests: sentry_link (bgen-generated)

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <sentry_link/sessions.hpp>
#include <any>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("sentry_link: send ConfigBody preserves all fields",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::ConfigBody cfg;
    cfg.set_device_name("TestDev");
    sentry_link::FirmwareVersion fw;
    fw.set_major(1);
    fw.set_minor(2);
    fw.set_patch(3);
    cfg.set_firmware(fw);
    cfg.set_sample_rate(8000);
    cfg.set_mode(sentry_link::device_mode::active);

    auto encoded = session->encode_wrap(sentry_link::ConfigBody::TYPE_ID, std::any(cfg));
    REQUIRE(encoded.has_value());

    // Decode and verify
    auto frame = sentry_link::Frame::decode_bytes(*encoded);
    REQUIRE(frame.has_value());
    CHECK(frame->sync() == 0xAA55);

    REQUIRE(std::holds_alternative<sentry_link::ConfigBody>(frame->payload()));
    auto& body = std::get<sentry_link::ConfigBody>(frame->payload());
    // String may be null-padded, check prefix
    CHECK(body.device_name().substr(0, 7) == "TestDev");
    CHECK(body.firmware().major() == 1);
    CHECK(body.firmware().minor() == 2);
    CHECK(body.firmware().patch() == 3);
    CHECK(body.sample_rate() == 8000);
}

TEST_CASE("sentry_link: receive HeartbeatBody with exact values",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1700000000);
    hb.set_uptime_hours(100);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(42);

    auto encoded = session->encode_wrap(sentry_link::HeartbeatBody::TYPE_ID, std::any(hb));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const sentry_link::HeartbeatBody&>((*decoded)[0].payload);
    CHECK(body.timestamp() == 1700000000);
    CHECK(body.uptime_hours() == 100);
    CHECK(body.status() == sentry_link::device_status::online);
    CHECK(body.cpu_load() == 42);
}

TEST_CASE("sentry_link: receive SensorBody with nested bit-fields",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::SensorBody sensor;
    sensor.set_sensor_id(0x1234);
    sensor.set_timestamp(999);
    sentry_link::SensorFlags flags;
    flags.set_channel(5);
    flags.set_precision(3);
    flags.set_saturated(0);
    flags.set_valid(1);
    sensor.set_flags(flags);
    sensor.set_raw_value(23.45);
    sensor.set_unit_code(1);

    auto encoded = session->encode_wrap(sentry_link::SensorBody::TYPE_ID, std::any(sensor));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const sentry_link::SensorBody&>((*decoded)[0].payload);
    CHECK(body.sensor_id() == 0x1234);
    CHECK(body.flags().channel() == 5);
    CHECK(body.flags().precision() == 3);
    CHECK(body.flags().saturated() == 0);
    CHECK(body.flags().valid() == 1);
}

TEST_CASE("sentry_link: receive AlertBody with string and enum",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::AlertBody alert;
    alert.set_timestamp(2000);
    alert.set_source_id(10);
    alert.set_severity(sentry_link::severity_level::critical);
    alert.set_category(7);
    alert.set_alert_code(0xABCD);
    alert.set_message("OVERTEMP");

    auto encoded = session->encode_wrap(sentry_link::AlertBody::TYPE_ID, std::any(alert));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const sentry_link::AlertBody&>((*decoded)[0].payload);
    CHECK(body.severity() == sentry_link::severity_level::critical);
    CHECK(body.alert_code() == 0xABCD);
    // String is null-padded to 32 bytes, check prefix
    CHECK(body.message().substr(0, 8) == "OVERTEMP");
}

TEST_CASE("sentry_link: dispatch all 4 types",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1);
    hb.set_uptime_hours(0);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(0);

    sentry_link::SensorBody sensor;
    sensor.set_sensor_id(1);
    sensor.set_timestamp(0);
    sentry_link::SensorFlags sf;
    sensor.set_flags(sf);
    sensor.set_raw_value(0.0);
    sensor.set_unit_code(1);

    sentry_link::ConfigBody config;
    config.set_device_name("dev");
    sentry_link::FirmwareVersion fw;
    config.set_firmware(fw);
    config.set_sample_rate(1000);

    sentry_link::AlertBody alert;
    alert.set_timestamp(0);
    alert.set_source_id(0);
    alert.set_severity(sentry_link::severity_level::info);
    alert.set_category(0);
    alert.set_alert_code(0);
    alert.set_message("test");

    auto e_hb = session->encode_wrap(sentry_link::HeartbeatBody::TYPE_ID, std::any(hb));
    auto e_sensor = session->encode_wrap(sentry_link::SensorBody::TYPE_ID, std::any(sensor));
    auto e_config = session->encode_wrap(sentry_link::ConfigBody::TYPE_ID, std::any(config));
    auto e_alert = session->encode_wrap(sentry_link::AlertBody::TYPE_ID, std::any(alert));

    REQUIRE(e_hb.has_value());
    REQUIRE(e_sensor.has_value());
    REQUIRE(e_config.has_value());
    REQUIRE(e_alert.has_value());

    session->reset();

    auto d_hb = session->decode_frame(*e_hb);
    auto d_sensor = session->decode_frame(*e_sensor);
    auto d_config = session->decode_frame(*e_config);
    auto d_alert = session->decode_frame(*e_alert);

    REQUIRE(d_hb.has_value());
    REQUIRE(d_sensor.has_value());
    REQUIRE(d_config.has_value());
    REQUIRE(d_alert.has_value());

    CHECK((*d_hb)[0].type_name == "HeartbeatBody");
    CHECK((*d_sensor)[0].type_name == "SensorBody");
    CHECK((*d_config)[0].type_name == "ConfigBody");
    CHECK((*d_alert)[0].type_name == "AlertBody");
}

TEST_CASE("sentry_link: stream framing HeartbeatBody split at byte 5",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(42);
    hb.set_uptime_hours(10);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(50);

    auto encoded = session->encode_wrap(sentry_link::HeartbeatBody::TYPE_ID, std::any(hb));
    REQUIRE(encoded.has_value());
    auto& bytes = *encoded;
    REQUIRE(bytes.size() == 14);

    // Split at byte 5
    std::vector<uint8_t> part1(bytes.begin(), bytes.begin() + 5);
    std::vector<uint8_t> part2(bytes.begin() + 5, bytes.end());

    auto r1 = framer.push_data(part1);
    REQUIRE(r1.has_value());
    CHECK(r1->empty());

    auto r2 = framer.push_data(part2);
    REQUIRE(r2.has_value());
    REQUIRE(r2->size() == 1);
}

TEST_CASE("sentry_link: wrong sync recovery",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();
    StreamFramer framer(*session);

    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(100);
    hb.set_uptime_hours(5);
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(25);

    auto encoded = session->encode_wrap(sentry_link::HeartbeatBody::TYPE_ID, std::any(hb));
    REQUIRE(encoded.has_value());

    // Prepend wrong sync bytes
    std::vector<uint8_t> data = {0xDE, 0xAD};
    data.insert(data.end(), encoded->begin(), encoded->end());

    auto result = framer.push_data(data);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0] == *encoded);
}

TEST_CASE("sentry_link: send+loopback roundtrip ConfigBody",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::ConfigBody cfg;
    cfg.set_device_name("LoopDev");
    sentry_link::FirmwareVersion fw;
    fw.set_major(3);
    fw.set_minor(1);
    fw.set_patch(99);
    cfg.set_firmware(fw);
    cfg.set_sample_rate(4000);
    cfg.set_mode(sentry_link::device_mode::passive);

    auto encoded = session->encode_wrap(sentry_link::ConfigBody::TYPE_ID, std::any(cfg));
    REQUIRE(encoded.has_value());

    session->reset();
    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const sentry_link::ConfigBody&>((*decoded)[0].payload);
    CHECK(body.device_name().substr(0, 7) == "LoopDev");
    CHECK(body.firmware().major() == 3);
    CHECK(body.firmware().minor() == 1);
    CHECK(body.firmware().patch() == 99);
    CHECK(body.sample_rate() == 4000);
}

TEST_CASE("sentry_link: auto-sequence increments",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::ConfigBody cfg;
    cfg.set_device_name("seq");
    sentry_link::FirmwareVersion fw;
    cfg.set_firmware(fw);
    cfg.set_sample_rate(1000);

    auto e0 = session->encode_wrap(sentry_link::ConfigBody::TYPE_ID, std::any(cfg));
    auto e1 = session->encode_wrap(sentry_link::ConfigBody::TYPE_ID, std::any(cfg));
    REQUIRE(e0.has_value());
    REQUIRE(e1.has_value());

    auto f0 = sentry_link::Frame::decode_bytes(*e0);
    auto f1 = sentry_link::Frame::decode_bytes(*e1);
    REQUIRE(f0.has_value());
    REQUIRE(f1.has_value());

    CHECK(f1->sequence() == f0->sequence() + 1);
}

TEST_CASE("sentry_link: scaled field roundtrip",
          "[integration][sentry_link]") {
    auto session = sentry_link::create_frame_session();

    sentry_link::SensorBody sensor;
    sensor.set_sensor_id(1);
    sensor.set_timestamp(0);
    sentry_link::SensorFlags sf;
    sf.set_channel(0);
    sf.set_precision(0);
    sf.set_saturated(0);
    sf.set_valid(1);
    sensor.set_flags(sf);
    sensor.set_raw_value(23.45);
    sensor.set_unit_code(1);

    auto encoded = session->encode_wrap(sentry_link::SensorBody::TYPE_ID, std::any(sensor));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& body = std::any_cast<const sentry_link::SensorBody&>((*decoded)[0].payload);
    // Scale is 0.01, so roundtrip should be within 0.01 tolerance
    CHECK(std::abs(body.raw_value() - 23.45) < 0.02);
}
