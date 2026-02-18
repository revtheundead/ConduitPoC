// SPDX-License-Identifier: MIT
// Tests for auto="config" on message fields (not just frame fields)

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "msg_config/messages.hpp"
#include "msg_config/sessions.hpp"
#include "msg_config/protocol.hpp"

#include "msg_config_inline/messages.hpp"
#include "msg_config_inline/sessions.hpp"
#include "msg_config_inline/protocol.hpp"

// ============================================================================
// msg_config: config fields on both frame AND message fields
// ============================================================================

TEST_CASE("msg_config: session Config struct has all config keys", "[config][msg]") {
    // Config struct should include system-id (frame), station-id (Telemetry),
    // and operator-id (Command)
    msg_config::FrameSession::Config config;
    config.system_id = 1;
    config.station_id = 2;
    config.operator_id = 300;

    auto session = msg_config::create_frame_session(config);
    REQUIRE(session != nullptr);
}

TEST_CASE("msg_config: Telemetry message-level config field set during encode", "[config][msg]") {
    msg_config::FrameSession::Config config;
    config.system_id = 7;
    config.station_id = 42;
    config.operator_id = 0;  // unused by Telemetry

    auto session = msg_config::create_frame_session(config);

    msg_config::Telemetry msg;
    msg.set_value(1000);

    auto encoded = session->encode_wrap(msg_config::Telemetry::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Wire layout: [system-id:1][msg-type:1][length:2][station-id:1][value:2] = 7 bytes
    REQUIRE(encoded->bytes.size() == 7);
    CHECK(encoded->bytes[0] == 7);   // system-id from config (frame-level)
    CHECK(encoded->bytes[1] == 1);   // msg-type = Telemetry::ID_VALUE
    CHECK(encoded->bytes[4] == 42);  // station-id from config (message-level)

    // Decode and verify
    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_id == msg_config::Telemetry::TYPE_ID);
    auto* payload = std::any_cast<msg_config::Telemetry>(&dm.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->station_id() == 42);
    CHECK(payload->value() == 1000);
}

TEST_CASE("msg_config: Command message-level config field (uint16) set during encode", "[config][msg]") {
    msg_config::FrameSession::Config config;
    config.system_id = 3;
    config.station_id = 0;   // unused by Command
    config.operator_id = 500;

    auto session = msg_config::create_frame_session(config);

    msg_config::Command msg;
    msg.set_code(99);

    auto encoded = session->encode_wrap(msg_config::Command::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Wire layout: [system-id:1][msg-type:1][length:2][operator-id:2][code:1] = 7 bytes
    REQUIRE(encoded->bytes.size() == 7);
    CHECK(encoded->bytes[0] == 3);   // system-id from config (frame-level)
    CHECK(encoded->bytes[1] == 2);   // msg-type = Command::ID_VALUE

    // Decode and verify operator-id was set
    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<msg_config::Command>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->operator_id() == 500);
    CHECK(payload->code() == 99);
}

TEST_CASE("msg_config: Heartbeat uses only frame-level config", "[config][msg]") {
    msg_config::FrameSession::Config config;
    config.system_id = 15;
    config.station_id = 0;
    config.operator_id = 0;

    auto session = msg_config::create_frame_session(config);

    msg_config::Heartbeat msg;
    msg.set_seq(42);

    auto encoded = session->encode_wrap(msg_config::Heartbeat::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Wire layout: [system-id:1][msg-type:1][length:2][seq:2] = 6 bytes
    REQUIRE(encoded->bytes.size() == 6);
    CHECK(encoded->bytes[0] == 15);  // system-id from config (frame-level)
    CHECK(encoded->bytes[1] == 3);   // msg-type = Heartbeat::ID_VALUE

    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<msg_config::Heartbeat>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->seq() == 42);
}

TEST_CASE("msg_config: config fields reported in auto_fields", "[config][msg]") {
    msg_config::FrameSession::Config config;
    config.system_id = 1;
    config.station_id = 2;
    config.operator_id = 3;

    auto session = msg_config::create_frame_session(config);

    // Telemetry: should report station-id and system-id in auto_fields
    msg_config::Telemetry msg;
    msg.set_value(0);
    auto encoded = session->encode_wrap(msg_config::Telemetry::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    bool found_station_id = false;
    bool found_system_id = false;
    for (const auto& [key, val] : encoded->auto_fields) {
        if (key == "station-id") {
            CHECK(val == "2");
            found_station_id = true;
        }
        if (key == "system-id") {
            CHECK(val == "1");
            found_system_id = true;
        }
    }
    CHECK(found_station_id);
    CHECK(found_system_id);
}

TEST_CASE("msg_config: different config values per session instance", "[config][msg]") {
    // Two sessions with different config values
    msg_config::FrameSession::Config config_a;
    config_a.system_id = 10;
    config_a.station_id = 20;
    config_a.operator_id = 0;

    msg_config::FrameSession::Config config_b;
    config_b.system_id = 50;
    config_b.station_id = 60;
    config_b.operator_id = 0;

    auto session_a = msg_config::create_frame_session(config_a);
    auto session_b = msg_config::create_frame_session(config_b);

    msg_config::Telemetry msg;
    msg.set_value(0);

    auto enc_a = session_a->encode_wrap(msg_config::Telemetry::TYPE_ID, msg);
    auto enc_b = session_b->encode_wrap(msg_config::Telemetry::TYPE_ID, msg);
    REQUIRE(enc_a.has_value());
    REQUIRE(enc_b.has_value());

    // Frame-level config: system-id at byte 0
    CHECK(enc_a->bytes[0] == 10);
    CHECK(enc_b->bytes[0] == 50);

    // Message-level config: station-id at byte 4
    CHECK(enc_a->bytes[4] == 20);
    CHECK(enc_b->bytes[4] == 60);
}

TEST_CASE("msg_config: direct frame encode preserves message field values", "[config][msg]") {
    // Direct frame usage (non-session) — message config fields are just regular fields
    msg_config::Telemetry msg;
    msg.set_station_id(99);
    msg.set_value(500);

    auto frame = msg_config::Frame::wrap(msg);
    frame.set_system_id(5);

    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = msg_config::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<msg_config::Telemetry>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->station_id() == 99);
    CHECK(payload->value() == 500);
}

// ============================================================================
// msg_config_inline: config fields inside an inlined struct
// ============================================================================

TEST_CASE("msg_config_inline: config fields from inlined struct", "[config][msg][inline]") {
    msg_config_inline::FrameSession::Config config;
    config.sac = 0x12;
    config.sic = 0x34;

    auto session = msg_config_inline::create_frame_session(config);
    REQUIRE(session != nullptr);

    msg_config_inline::Report msg;
    msg.set_value(999);

    auto encoded = session->encode_wrap(msg_config_inline::Report::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    // Wire layout: [msg-type:1][length:2][sac:1][sic:1][value:2] = 7 bytes
    REQUIRE(encoded->bytes.size() == 7);

    // Decode and verify config fields were set
    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<msg_config_inline::Report>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->sac() == 0x12);
    CHECK(payload->sic() == 0x34);
    CHECK(payload->value() == 999);
}

TEST_CASE("msg_config_inline: Status message unaffected by config", "[config][msg][inline]") {
    msg_config_inline::FrameSession::Config config;
    config.sac = 0xAA;
    config.sic = 0xBB;

    auto session = msg_config_inline::create_frame_session(config);

    msg_config_inline::Status msg;
    msg.set_code(77);

    auto encoded = session->encode_wrap(msg_config_inline::Status::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<msg_config_inline::Status>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->code() == 77);
}

TEST_CASE("msg_config_inline: config fields reported in auto_fields", "[config][msg][inline]") {
    msg_config_inline::FrameSession::Config config;
    config.sac = 1;
    config.sic = 2;

    auto session = msg_config_inline::create_frame_session(config);

    msg_config_inline::Report msg;
    msg.set_value(0);

    auto encoded = session->encode_wrap(msg_config_inline::Report::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    bool found_sac = false, found_sic = false;
    for (const auto& [key, val] : encoded->auto_fields) {
        if (key == "sac") { found_sac = true; CHECK(val == "1"); }
        if (key == "sic") { found_sic = true; CHECK(val == "2"); }
    }
    CHECK(found_sac);
    CHECK(found_sic);
}
