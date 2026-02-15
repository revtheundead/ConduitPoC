// SPDX-License-Identifier: MIT
// ============================================================================
// SentryLink Example — TCP Server + Client
//
// Demonstrates Conduit's session and transport layers with the SentryLink
// protocol. Because SentryLink's leaf body types are BMDL structs (not
// messages), this example drives the session encode/decode API directly
// atop TCP transports — showing the full codec path without raw bytes.
//
//   - TCP server accepting a connection
//   - TCP client connecting to the server
//   - Session-level encode/decode (typed messages, no raw bytes in user code)
//   - All 4 message types: HeartbeatBody, SensorBody, ConfigBody, AlertBody
//   - Enums, scaled fields, bit-packed flags, strings, constraints
//   - Auto-increment sequence numbers
//   - Bidirectional communication
//
// The protocol defines direction constraints:
//   - HeartbeatBody:  receive-only (device → controller)
//   - SensorBody:     receive-only (device → controller)
//   - AlertBody:      receive-only (device → controller)
//   - ConfigBody:     send-only    (controller → device)
//
// In this example:
//   - The "server" acts as the controller (receives heartbeat/sensor/alert,
//     sends config)
//   - The "client" acts as the device (sends heartbeat/sensor/alert,
//     receives config)
// ============================================================================

#include <sentry_link/sessions.hpp>
#include <sentry_link/constants.hpp>
#include <sentry_link/messages.hpp>

#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/stream_framer.hpp>

#include <any>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

// ============================================================================
// Helpers
// ============================================================================

static std::mutex g_print_mutex;

template<typename... Args>
static void log(const char* tag, [[maybe_unused]] const char* fmt, Args... args) {
    std::lock_guard lock(g_print_mutex);
    std::printf("[%-8s] ", tag);
    if constexpr (sizeof...(args) == 0) {
        std::fputs(fmt, stdout);
    } else {
        std::printf(fmt, args...);
    }
    std::printf("\n");
    std::fflush(stdout);
}

static std::string strip_nulls(const std::string& s) {
    auto pos = s.find('\0');
    return (pos != std::string::npos) ? s.substr(0, pos) : s;
}

// ============================================================================
// Verification counters — track what the server received
// ============================================================================

struct ServerStats {
    std::atomic<int> heartbeats_received{0};
    std::atomic<int> sensors_received{0};
    std::atomic<int> alerts_received{0};
    std::atomic<bool> heartbeat_ok{false};
    std::atomic<bool> sensor_ok{false};
    std::atomic<bool> alert_ok{false};
};

struct ClientStats {
    std::atomic<int> configs_received{0};
    std::atomic<bool> config_ok{false};
};

// ============================================================================
// Build test messages — exercises every field in each message type
// ============================================================================

static sentry_link::HeartbeatBody make_heartbeat() {
    sentry_link::HeartbeatBody hb;
    hb.set_timestamp(1700000000);       // Unix epoch: Nov 14 2023
    hb.set_uptime_hours(2048);          // ~85 days
    hb.set_status(sentry_link::device_status::online);
    (void)hb.set_cpu_load(73);           // 73% (constrained: max=100)
    return hb;
}

static sentry_link::SensorBody make_sensor_celsius() {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(1001);
    sb.set_timestamp(1700000100);

    sentry_link::SensorFlags flags;
    flags.set_channel(5);               // 4-bit: sensor channel 5
    flags.set_precision(3);             // 2-bit: high precision
    flags.set_saturated(0);             // 1-bit: not saturated
    flags.set_valid(1);                 // 1-bit: reading is valid
    sb.set_flags(flags);

    // Scaled field (scale=0.01): set_raw_value takes the physical value.
    // The codec transparently encodes/decodes via the scale factor.
    // 23.45 °C as the physical value:
    sb.set_raw_value(23.45);
    sb.set_unit_code(1);                // 1 = celsius
    return sb;
}

static sentry_link::SensorBody make_sensor_humidity() {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(2002);
    sb.set_timestamp(1700000200);

    sentry_link::SensorFlags flags;
    flags.set_channel(8);
    flags.set_precision(1);             // low precision
    flags.set_saturated(1);             // saturated reading!
    flags.set_valid(1);
    sb.set_flags(flags);

    // 95.50% RH as physical value
    sb.set_raw_value(95.50);
    sb.set_unit_code(2);                // 2 = percent-RH
    return sb;
}

static sentry_link::SensorBody make_sensor_pressure() {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(3003);
    sb.set_timestamp(1700000300);

    sentry_link::SensorFlags flags;
    flags.set_channel(0);               // channel 0
    flags.set_precision(2);
    flags.set_saturated(0);
    flags.set_valid(1);
    sb.set_flags(flags);

    // 1013.25 hPa as physical value
    sb.set_raw_value(1013.25);
    sb.set_unit_code(3);                // 3 = hPa
    return sb;
}

static sentry_link::SensorBody make_sensor_negative() {
    sentry_link::SensorBody sb;
    sb.set_sensor_id(4004);
    sb.set_timestamp(1700000400);

    sentry_link::SensorFlags flags;
    flags.set_channel(15);              // max channel (4-bit)
    flags.set_precision(0);             // lowest precision
    flags.set_saturated(0);
    flags.set_valid(0);                 // invalid reading
    sb.set_flags(flags);

    // Negative temperature: -40.00 °C as physical value
    sb.set_raw_value(-40.00);
    sb.set_unit_code(1);                // celsius
    return sb;
}

static sentry_link::ConfigBody make_config() {
    sentry_link::ConfigBody cfg;
    cfg.set_device_name("SentryProbe-7");   // 16-byte null-padded ASCII string

    sentry_link::FirmwareVersion fw;
    fw.set_major(3);
    fw.set_minor(12);
    fw.set_patch(2048);                     // uint16 patch
    cfg.set_firmware(fw);

    cfg.set_mode(sentry_link::device_mode::calibration);  // 2-bit enum
    cfg.set_log_level(5);                   // 3-bit: 0=off, 7=trace
    cfg.set_auto_report(1);                 // 1-bit flag
    cfg.set_compression(0);                 // 1-bit flag
    // 1 reserved bit is auto-handled by the codec
    cfg.set_sample_rate(16000);             // uint16 Hz
    return cfg;
}

static sentry_link::AlertBody make_alert_critical() {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1700001000);
    alert.set_source_id(42);
    alert.set_severity(sentry_link::severity_level::critical);  // 3-bit enum
    alert.set_category(7);                  // 5-bit: alert category
    alert.set_alert_code(0x1234);
    alert.set_message("OVERTEMP SENSOR 5");  // 32-byte null-padded ASCII
    return alert;
}

static sentry_link::AlertBody make_alert_info() {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1700002000);
    alert.set_source_id(0);
    alert.set_severity(sentry_link::severity_level::info);
    alert.set_category(0);
    alert.set_alert_code(0x0001);
    alert.set_message("System startup complete");
    return alert;
}

static sentry_link::AlertBody make_alert_warning() {
    sentry_link::AlertBody alert;
    alert.set_timestamp(1700003000);
    alert.set_source_id(99);
    alert.set_severity(sentry_link::severity_level::warning);
    alert.set_category(31);                 // max 5-bit value
    alert.set_alert_code(0xFFFF);           // max uint16
    alert.set_message("Battery low: 15%");
    return alert;
}

// ============================================================================
// Verify received messages match what was sent
// ============================================================================

static void verify_heartbeat(const sentry_link::HeartbeatBody& hb, ServerStats& stats) {
    auto expected = make_heartbeat();
    bool ok = true;
    ok &= (hb.timestamp() == expected.timestamp());
    ok &= (hb.uptime_hours() == expected.uptime_hours());
    ok &= (hb.status() == expected.status());
    ok &= (hb.cpu_load() == expected.cpu_load());
    stats.heartbeat_ok = ok;
    stats.heartbeats_received++;

    log("SERVER", "Heartbeat: ts=%u uptime=%u status=%d cpu=%u%% [%s]",
        hb.timestamp(), hb.uptime_hours(),
        static_cast<int>(hb.status()), hb.cpu_load(),
        ok ? "OK" : "MISMATCH");
}

static void verify_sensor(const sentry_link::SensorBody& sb, ServerStats& stats) {
    stats.sensors_received++;

    log("SERVER", "Sensor #%u: raw=%.2f unit=%u ch=%u prec=%u sat=%u valid=%u",
        sb.sensor_id(), sb.raw_value(), sb.unit_code(),
        sb.flags().channel(), sb.flags().precision(),
        sb.flags().saturated(), sb.flags().valid());

    // Verify at least one sensor matches make_sensor_celsius() exactly
    if (sb.sensor_id() == 1001) {
        auto expected = make_sensor_celsius();
        bool ok = true;
        ok &= (sb.timestamp() == expected.timestamp());
        ok &= (sb.flags().channel() == expected.flags().channel());
        ok &= (sb.flags().precision() == expected.flags().precision());
        ok &= (sb.flags().saturated() == expected.flags().saturated());
        ok &= (sb.flags().valid() == expected.flags().valid());
        // raw_value is a scaled double — compare with tolerance
        ok &= (std::abs(sb.raw_value() - expected.raw_value()) < 0.02);
        ok &= (sb.unit_code() == expected.unit_code());
        stats.sensor_ok = ok;
    }
}

static void verify_alert(const sentry_link::AlertBody& alert, ServerStats& stats) {
    stats.alerts_received++;

    log("SERVER", "Alert: sev=%d cat=%u code=0x%04X msg=\"%s\"",
        static_cast<int>(alert.severity()), alert.category(),
        alert.alert_code(), strip_nulls(alert.message()).c_str());

    // Verify at least one alert matches make_alert_critical() exactly
    if (alert.alert_code() == 0x1234) {
        auto expected = make_alert_critical();
        bool ok = true;
        ok &= (alert.timestamp() == expected.timestamp());
        ok &= (alert.source_id() == expected.source_id());
        ok &= (alert.severity() == expected.severity());
        ok &= (alert.category() == expected.category());
        ok &= (strip_nulls(alert.message()) == strip_nulls(expected.message()));
        stats.alert_ok = ok;
    }
}

static void verify_config(const sentry_link::ConfigBody& cfg, ClientStats& stats) {
    auto expected = make_config();
    bool ok = true;
    ok &= (strip_nulls(cfg.device_name()) == strip_nulls(expected.device_name()));
    ok &= (cfg.firmware().major() == expected.firmware().major());
    ok &= (cfg.firmware().minor() == expected.firmware().minor());
    ok &= (cfg.firmware().patch() == expected.firmware().patch());
    ok &= (cfg.mode() == expected.mode());
    ok &= (cfg.log_level() == expected.log_level());
    ok &= (cfg.auto_report() == expected.auto_report());
    ok &= (cfg.compression() == expected.compression());
    ok &= (cfg.sample_rate() == expected.sample_rate());
    stats.config_ok = ok;
    stats.configs_received++;

    log("CLIENT", "Config: name=\"%s\" fw=%u.%u.%u mode=%d log=%u rate=%u [%s]",
        strip_nulls(cfg.device_name()).c_str(),
        cfg.firmware().major(), cfg.firmware().minor(), cfg.firmware().patch(),
        static_cast<int>(cfg.mode()), cfg.log_level(), cfg.sample_rate(),
        ok ? "OK" : "MISMATCH");
}

// ============================================================================
// Dispatch decoded messages by type_name
// ============================================================================

static void dispatch_server(const conduit::traits::DecodedMessage& dm, ServerStats& stats) {
    if (dm.type_name == "HeartbeatBody") {
        verify_heartbeat(std::any_cast<const sentry_link::HeartbeatBody&>(dm.payload), stats);
    } else if (dm.type_name == "SensorBody") {
        verify_sensor(std::any_cast<const sentry_link::SensorBody&>(dm.payload), stats);
    } else if (dm.type_name == "AlertBody") {
        verify_alert(std::any_cast<const sentry_link::AlertBody&>(dm.payload), stats);
    } else {
        log("SERVER", "Unknown message type: %.*s",
            static_cast<int>(dm.type_name.size()), dm.type_name.data());
    }
}

static void dispatch_client(const conduit::traits::DecodedMessage& dm, ClientStats& stats) {
    if (dm.type_name == "ConfigBody") {
        verify_config(std::any_cast<const sentry_link::ConfigBody&>(dm.payload), stats);
    } else {
        log("CLIENT", "Unknown message type: %.*s",
            static_cast<int>(dm.type_name.size()), dm.type_name.data());
    }
}

// ============================================================================
// Encode a leaf body through the session (wraps in Frame automatically)
// ============================================================================

static std::vector<uint8_t> encode_body(
    conduit::traits::ISession& session,
    uint64_t type_id,
    const std::any& payload,
    const char* label)
{
    auto result = session.encode_wrap(type_id, payload);
    if (!result) {
        log("ERROR", "encode_wrap failed for %s: %s", label,
            result.error().message().c_str());
        return {};
    }
    return std::move(*result);
}

// ============================================================================
// Lookup a leaf type_id from the session by name
// ============================================================================

static uint64_t find_type_id(conduit::traits::ISession& session, std::string_view name) {
    for (auto id : session.leaf_type_ids()) {
        if (session.type_name(id) == name) return id;
    }
    return 0;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace conduit::transceiver;
    using namespace conduit::transceiver::transport;
    using namespace conduit::net;
    namespace sl = sentry_link;

    log("MAIN", "SentryLink Example — TCP Server + Client");
    log("MAIN", "Protocol: %s v%s", "sentry-link", "1.0");
    log("MAIN", "Sync word: 0x%04X", sl::SYNC);
    log("MAIN", "Message types: Heartbeat(%u) Sensor(%u) Config(%u) Alert(%u)",
        sl::HeartbeatBody::ID_VALUE, sl::SensorBody::ID_VALUE,
        sl::ConfigBody::ID_VALUE, sl::AlertBody::ID_VALUE);
    log("MAIN", "");

    ServerStats server_stats;
    ClientStats client_stats;

    // ========================================================================
    // 1. Create TCP Server (controller)
    // ========================================================================

    log("SERVER", "Setting up TCP server on port 0 (ephemeral)...");

    TcpServerConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.port = 0;              // Let the OS pick a free port
    server_cfg.max_clients = 4;
    server_cfg.recv_buffer_size = 8192;
    auto server_transport = std::make_shared<TcpServerTransport>(server_cfg);

    // Session and framer for the server side
    auto server_session = sl::create_frame_session();
    auto server_framer = std::make_unique<StreamFramer>(*server_session);

    // Peer ID tracking
    std::atomic<bool> client_connected{false};
    PeerId connected_peer_id;
    std::mutex server_peer_mutex;

    // Start server transport with manual callbacks
    auto server_start = server_transport->start(TransportCallbacks{
        .on_data_received = [&](PeerId /*peer*/, std::span<const uint8_t> data) {
            // Feed data to the stream framer for frame extraction
            std::lock_guard lock(server_peer_mutex);
            auto frames = server_framer->push_data(data);
            if (frames) {
                for (auto& frame : *frames) {
                    auto decoded = server_session->decode_frame(frame);
                    if (decoded) {
                        for (auto& dm : *decoded) {
                            dispatch_server(dm, server_stats);
                        }
                    }
                }
            }
        },
        .on_peer_connected = [&](std::string) -> PeerId {
            auto id = PeerId(100);
            connected_peer_id = id;
            client_connected = true;
            log("SERVER", "Client connected (peer %u)", id.value());
            return id;
        },
        .on_peer_disconnected = [&](PeerId peer) {
            log("SERVER", "Client disconnected (peer %u)", peer.value());
        },
        .on_state_changed = [&](PeerId peer, ConnectionState state) {
            log("SERVER", "Peer %u state: %.*s",
                peer.value(),
                static_cast<int>(to_string(state).size()),
                to_string(state).data());
        }
    });

    if (!server_start) {
        log("SERVER", "Failed to start: %s", server_start.error().message().c_str());
        return 1;
    }

    uint16_t server_port = server_transport->local_port();
    log("SERVER", "Listening on 127.0.0.1:%u", server_port);

    // ========================================================================
    // 2. Create TCP Client (device)
    // ========================================================================

    log("CLIENT", "Setting up TCP client...");

    TcpClientConfig client_cfg;
    client_cfg.host = "127.0.0.1";
    client_cfg.port = server_port;
    client_cfg.recv_buffer_size = 8192;
    auto client_transport = std::make_shared<TcpClientTransport>(client_cfg);

    auto client_session = sl::create_frame_session();
    auto client_framer = std::make_unique<StreamFramer>(*client_session);
    PeerId client_peer_id;
    std::mutex client_peer_mutex;

    auto client_start = client_transport->start(TransportCallbacks{
        .on_data_received = [&](PeerId /*peer*/, std::span<const uint8_t> data) {
            std::lock_guard lock(client_peer_mutex);
            auto frames = client_framer->push_data(data);
            if (frames) {
                for (auto& frame : *frames) {
                    auto decoded = client_session->decode_frame(frame);
                    if (decoded) {
                        for (auto& dm : *decoded) {
                            dispatch_client(dm, client_stats);
                        }
                    }
                }
            }
        },
        .on_peer_connected = [&](std::string) -> PeerId {
            auto id = PeerId(200);
            client_peer_id = id;
            log("CLIENT", "Connected (peer %u)", id.value());
            return id;
        },
        .on_peer_disconnected = [&](PeerId peer) {
            log("CLIENT", "Disconnected (peer %u)", peer.value());
        },
        .on_state_changed = [&](PeerId peer, ConnectionState state) {
            log("CLIENT", "Peer %u state: %.*s",
                peer.value(),
                static_cast<int>(to_string(state).size()),
                to_string(state).data());
        }
    });

    if (!client_start) {
        log("CLIENT", "Failed to start: %s", client_start.error().message().c_str());
        return 1;
    }

    log("CLIENT", "Connecting to 127.0.0.1:%u...", server_port);

    // Wait for connection
    for (int i = 0; i < 50 && !client_connected; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!client_connected) {
        log("MAIN", "Connection timed out!");
        client_transport->stop();
        server_transport->stop();
        return 1;
    }

    log("MAIN", "Connection established. Exchanging messages...\n");

    // Give the system a moment to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Look up type IDs from the session metadata
    uint64_t hb_type_id    = find_type_id(*client_session, "HeartbeatBody");
    uint64_t sensor_type_id = find_type_id(*client_session, "SensorBody");
    uint64_t alert_type_id  = find_type_id(*client_session, "AlertBody");
    uint64_t config_type_id = find_type_id(*server_session, "ConfigBody");

    // ========================================================================
    // 3. Client sends all message types to server
    //    Uses session.encode_wrap() to wrap bodies into Frames, then sends
    //    the wire bytes via the transport. No raw byte construction.
    // ========================================================================

    auto client_send = [&](uint64_t type_id, const std::any& payload, const char* label) {
        auto bytes = encode_body(*client_session, type_id, payload, label);
        if (!bytes.empty()) {
            client_transport->send(client_peer_id, bytes);
        }
    };

    // --- Heartbeat (device → controller) ---
    {
        auto hb = make_heartbeat();
        log("CLIENT", "Sending HeartbeatBody: ts=%u status=%d cpu=%u%%",
            hb.timestamp(), static_cast<int>(hb.status()), hb.cpu_load());
        client_send(hb_type_id, hb, "HeartbeatBody");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // --- Sensor readings (4 different configurations) ---
    {
        auto s1 = make_sensor_celsius();
        log("CLIENT", "Sending SensorBody: id=%u raw=%.2f unit=celsius ch=%u",
            s1.sensor_id(), s1.raw_value(), s1.flags().channel());
        client_send(sensor_type_id, s1, "SensorBody");
    }
    {
        auto s2 = make_sensor_humidity();
        log("CLIENT", "Sending SensorBody: id=%u raw=%.2f unit=RH sat=%u",
            s2.sensor_id(), s2.raw_value(), s2.flags().saturated());
        client_send(sensor_type_id, s2, "SensorBody");
    }
    {
        auto s3 = make_sensor_pressure();
        log("CLIENT", "Sending SensorBody: id=%u raw=%.2f unit=hPa",
            s3.sensor_id(), s3.raw_value());
        client_send(sensor_type_id, s3, "SensorBody");
    }
    {
        auto s4 = make_sensor_negative();
        log("CLIENT", "Sending SensorBody: id=%u raw=%.2f (negative, invalid)",
            s4.sensor_id(), s4.raw_value());
        client_send(sensor_type_id, s4, "SensorBody");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // --- Alerts (3 different severities) ---
    {
        auto a1 = make_alert_critical();
        log("CLIENT", "Sending AlertBody: severity=critical code=0x%04X",
            a1.alert_code());
        client_send(alert_type_id, a1, "AlertBody");
    }
    {
        auto a2 = make_alert_info();
        log("CLIENT", "Sending AlertBody: severity=info code=0x%04X",
            a2.alert_code());
        client_send(alert_type_id, a2, "AlertBody");
    }
    {
        auto a3 = make_alert_warning();
        log("CLIENT", "Sending AlertBody: severity=warning code=0x%04X",
            a3.alert_code());
        client_send(alert_type_id, a3, "AlertBody");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ========================================================================
    // 4. Server sends config to client
    // ========================================================================

    {
        auto cfg = make_config();
        log("SERVER", "Sending ConfigBody: name=\"%s\" mode=%d rate=%u",
            strip_nulls(cfg.device_name()).c_str(),
            static_cast<int>(cfg.mode()), cfg.sample_rate());

        auto bytes = encode_body(*server_session, config_type_id, cfg, "ConfigBody");
        if (!bytes.empty()) {
            server_transport->send(connected_peer_id, bytes);
        }
    }

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ========================================================================
    // 5. Summary and verification
    // ========================================================================

    log("MAIN", "");
    log("MAIN", "=== Results ===");
    log("MAIN", "");
    log("MAIN", "Server received:");
    log("MAIN", "  Heartbeats: %d (verified: %s)",
        server_stats.heartbeats_received.load(),
        server_stats.heartbeat_ok ? "PASS" : "FAIL");
    log("MAIN", "  Sensors:    %d (verified: %s)",
        server_stats.sensors_received.load(),
        server_stats.sensor_ok ? "PASS" : "FAIL");
    log("MAIN", "  Alerts:     %d (verified: %s)",
        server_stats.alerts_received.load(),
        server_stats.alert_ok ? "PASS" : "FAIL");
    log("MAIN", "");
    log("MAIN", "Client received:");
    log("MAIN", "  Configs:    %d (verified: %s)",
        client_stats.configs_received.load(),
        client_stats.config_ok ? "PASS" : "FAIL");

    // ========================================================================
    // 6. Cleanup
    // ========================================================================

    log("MAIN", "");
    log("MAIN", "Stopping...");
    client_transport->stop();
    server_transport->stop();

    // Final pass/fail
    bool all_pass = true;
    all_pass &= (server_stats.heartbeats_received >= 1);
    all_pass &= server_stats.heartbeat_ok;
    all_pass &= (server_stats.sensors_received >= 4);
    all_pass &= server_stats.sensor_ok;
    all_pass &= (server_stats.alerts_received >= 3);
    all_pass &= server_stats.alert_ok;
    all_pass &= (client_stats.configs_received >= 1);
    all_pass &= client_stats.config_ok;

    log("MAIN", "");
    if (all_pass) {
        log("MAIN", "ALL CHECKS PASSED");
    } else {
        log("MAIN", "SOME CHECKS FAILED");
    }

    return all_pass ? 0 : 1;
}
