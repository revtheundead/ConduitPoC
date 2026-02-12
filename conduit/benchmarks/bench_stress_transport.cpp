// SPDX-License-Identifier: MIT
// Conduit - Stress transport benchmarks
//
// Drives 100K messages through actual TCP/UDP loopback to measure
// end-to-end throughput including codec, framing, and I/O.
// Uses manual std::chrono timing and std::fprintf for results.

#include <catch2/catch_test_macros.hpp>

#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/net/connection_state.hpp>

#include <stress/messages.hpp>
#include <stress/sessions.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

static constexpr size_t TRANSPORT_BATCH = 100000;

// ============================================================================
// Helpers
// ============================================================================

namespace {

bool wait_for_connection(Transceiver& tx, int timeout_ms = 5000) {
    auto ids = tx.peer_ids();
    if (ids.empty()) return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        bool all_connected = true;
        for (auto id : ids) {
            if (tx.peer_state(id) != net::ConnectionState::Connected) {
                all_connected = false;
                break;
            }
        }
        if (all_connected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_for_count(const std::atomic<size_t>& counter, size_t target, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (counter.load(std::memory_order_acquire) < target &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return counter.load(std::memory_order_acquire) >= target;
}

static std::atomic<uint16_t> next_udp_port{39200};

void print_result(const char* label, size_t count, double elapsed_ms, size_t bytes) {
    double msgs_per_sec = (elapsed_ms > 0) ? (static_cast<double>(count) / elapsed_ms * 1000.0) : 0;
    double mb_per_sec = (elapsed_ms > 0) ? (static_cast<double>(bytes) / 1024.0 / 1024.0 / elapsed_ms * 1000.0) : 0;
    std::fprintf(stdout, "  %-36s %6zu msgs in %8.1f ms => %8.0f msgs/s, %5.1f MB/s\n",
                 label, count, elapsed_ms, msgs_per_sec, mb_per_sec);
}

// Message factories (same as bench_stress_codec.cpp, kept local)

stress::Ping make_ping() {
    stress::Ping p;
    p.set_sequence(12345);
    p.set_timestamp(1700000000);
    p.set_priority(stress::priority_level::high);
    stress::status_flags f;
    f.set_active(true);
    f.set_calibrated(true);
    f.set_gps_lock(true);
    p.set_flags(f);
    p.set_tag(0xBEEF);
    return p;
}

stress::TelemetryReport make_telemetry() {
    stress::TelemetryReport tr;
    tr.set_sequence(67890);
    tr.set_timestamp(1700001000);
    tr.set_source_id(42);
    tr.set_sensor(stress::sensor_type::acceleration);

    stress::Coordinate pos;
    pos.set_x(100000);
    pos.set_y(-200000);
    pos.set_z(5000);
    tr.set_position(pos);

    stress::Velocity vel;
    vel.set_vx(150);
    vel.set_vy(-75);
    vel.set_vz(10);
    tr.set_velocity(vel);

    stress::temperature temp;
    temp.set_value(23.5);
    tr.set_temperature(temp);

    stress::pressure_hpa pres;
    pres.set_value(1013.25);
    tr.set_pressure(pres);

    stress::angle_deg hdg;
    hdg.set_value(270.0);
    tr.set_heading(hdg);

    stress::status_flags st;
    st.set_active(true);
    st.set_recording(true);
    tr.set_status(st);

    tr.set_label("SENSOR-ALPHA");
    return tr;
}

stress::SurveillanceRecord make_surveillance() {
    stress::SurveillanceRecord sr;
    sr.set_track_id(1001);
    sr.set_timestamp(1700002000);
    sr.set_report_class(stress::report_class::periodic);

    auto& it = sr.mutable_items();
    it.set_source_id(42);

    stress::GeoPosition gp;
    stress::wgs84 lat; lat.set_value(41.015137);
    stress::wgs84 lon; lon.set_value(28.979530);
    stress::altitude_fl alt; alt.set_value(350.0);
    gp.set_latitude(lat);
    gp.set_longitude(lon);
    gp.set_altitude(alt);
    it.set_geo_position(gp);

    stress::Coordinate cart;
    cart.set_x(50000);
    cart.set_y(-30000);
    cart.set_z(1400);
    it.set_cartesian(cart);

    stress::Velocity vel;
    vel.set_vx(200);
    vel.set_vy(-100);
    vel.set_vz(5);
    it.set_velocity(vel);

    stress::angle_deg hdg;
    hdg.set_value(135.0);
    it.set_heading(hdg);

    stress::TrackQuality tq;
    tq.set_confidence(12);
    tq.set_freshness(8);
    tq.set_source(3);
    it.set_quality(tq);

    stress::ExtendedStatus es;
    es.set_mode(5);
    es.set_reliability(10);
    es.set_secondary_mode(2);
    es.set_aux_flags(7);
    it.set_status(es);

    it.set_label("TRK1001");

    stress::PeriodicPayload pp;
    pp.set_interval_ms(1000);
    pp.set_counter(9999);
    sr.set_payload(pp);

    return sr;
}

// Estimate wire size for throughput reporting
size_t wire_size_of(const stress::Ping& msg) {
    return stress::StressFrame::wrap(msg).encode_bytes().value().size();
}

size_t wire_size_of(const stress::TelemetryReport& msg) {
    return stress::StressFrame::wrap(msg).encode_bytes().value().size();
}

size_t wire_size_of(const stress::SurveillanceRecord& msg) {
    return stress::StressFrame::wrap(msg).encode_bytes().value().size();
}

// TCP setup: returns (server, client, server_transport)
struct TcpPair {
    Transceiver server;
    Transceiver client;
    std::shared_ptr<TcpServerTransport> server_transport;

    TcpPair(size_t queue_capacity = 200000, size_t workers = 1)
        : server(make_server_config(queue_capacity, workers))
        , client() {
        TcpServerConfig srv_cfg;
        srv_cfg.bind_address = "127.0.0.1";
        srv_cfg.port = 0;
        server_transport = std::make_shared<TcpServerTransport>(srv_cfg);

        auto sr = server.add_peer("clients", stress::create_stress_frame_session, server_transport);
        (void)sr;
    }

    void start_and_connect() {
        auto sr = server.start();
        REQUIRE(sr.has_value());

        uint16_t port = server_transport->local_port();
        REQUIRE(port != 0);

        TcpClientConfig cli_cfg;
        cli_cfg.host = "127.0.0.1";
        cli_cfg.port = port;
        cli_cfg.reconnect.enabled = false;
        cli_cfg.connect_timeout = std::chrono::milliseconds(5000);

        auto cli_transport = std::make_shared<TcpClientTransport>(cli_cfg);
        auto cr = client.add_peer("server",
            std::make_unique<stress::StressFrameSession>(), cli_transport);
        REQUIRE(cr.has_value());

        auto csr = client.start();
        REQUIRE(csr.has_value());

        REQUIRE(wait_for_connection(client));
    }

    void stop() {
        client.stop();
        server.stop();
    }

private:
    static TransceiverConfig make_server_config(size_t cap, size_t workers) {
        TransceiverConfig cfg;
        cfg.rx_queue.capacity = cap;
        cfg.rx_queue.drop_policy = queue::DropPolicy::DropOldest;
        cfg.worker.thread_count = workers;
        return cfg;
    }
};

// UDP setup — both sides in single-peer mode (fixed ports, point-to-point)
struct UdpPair {
    Transceiver receiver;
    Transceiver sender;
    uint16_t port;

    UdpPair(size_t queue_capacity = 200000)
        : receiver(make_config(queue_capacity))
        , sender()
        , port(next_udp_port.fetch_add(2)) {

        uint16_t rx_port = port;
        uint16_t tx_port = static_cast<uint16_t>(port + 1);

        UdpConfig rx_cfg;
        rx_cfg.bind_address = "127.0.0.1";
        rx_cfg.bind_port = rx_port;
        rx_cfg.remote_address = "127.0.0.1";
        rx_cfg.remote_port = tx_port;
        auto rx_transport = std::make_shared<UdpTransport>(rx_cfg);

        auto rr = receiver.add_peer("sender",
            std::make_unique<stress::StressFrameSession>(), rx_transport);
        (void)rr;

        UdpConfig tx_cfg;
        tx_cfg.bind_address = "127.0.0.1";
        tx_cfg.bind_port = tx_port;
        tx_cfg.remote_address = "127.0.0.1";
        tx_cfg.remote_port = rx_port;
        auto tx_transport = std::make_shared<UdpTransport>(tx_cfg);

        auto sr = sender.add_peer("receiver",
            std::make_unique<stress::StressFrameSession>(), tx_transport);
        (void)sr;
    }

    void start() {
        auto rr = receiver.start();
        REQUIRE(rr.has_value());
        auto sr = sender.start();
        REQUIRE(sr.has_value());
        // UDP is connectionless — give a brief moment for sockets to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void stop() {
        sender.stop();
        receiver.stop();
    }

private:
    static TransceiverConfig make_config(size_t cap) {
        TransceiverConfig cfg;
        cfg.rx_queue.capacity = cap;
        cfg.rx_queue.drop_policy = queue::DropPolicy::DropOldest;
        return cfg;
    }
};

} // namespace

// ============================================================================
// TCP throughput
// ============================================================================

TEST_CASE("Stress: TCP throughput", "[stress][tcp]") {
    std::fprintf(stdout, "\nStress: TCP throughput\n");
    std::fprintf(stdout, "-------------------------------------------\n");

    SECTION("Simple (100K Ping)") {
        TcpPair pair;
        std::atomic<size_t> count{0};
        pair.server.on<stress::Ping>(std::function<void(const stress::Ping&)>(
            [&](const stress::Ping&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start_and_connect();

        auto ping = make_ping();
        size_t msg_bytes = wire_size_of(ping);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.client.send<stress::Ping>(ping);
        }
        wait_for_count(count, TRANSPORT_BATCH, 30000);
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        print_result("Simple (100K Ping):", received, ms, received * msg_bytes);
        CHECK(received == TRANSPORT_BATCH);

        pair.stop();
    }

    SECTION("Medium (100K TelemetryReport)") {
        TcpPair pair;
        std::atomic<size_t> count{0};
        pair.server.on<stress::TelemetryReport>(std::function<void(const stress::TelemetryReport&)>(
            [&](const stress::TelemetryReport&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start_and_connect();

        auto telem = make_telemetry();
        size_t msg_bytes = wire_size_of(telem);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.client.send<stress::TelemetryReport>(telem);
        }
        wait_for_count(count, TRANSPORT_BATCH, 30000);
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        print_result("Medium (100K Telemetry):", received, ms, received * msg_bytes);
        CHECK(received == TRANSPORT_BATCH);

        pair.stop();
    }

    SECTION("Complex (100K SurveillanceRecord)") {
        TcpPair pair;
        std::atomic<size_t> count{0};
        pair.server.on<stress::SurveillanceRecord>(std::function<void(const stress::SurveillanceRecord&)>(
            [&](const stress::SurveillanceRecord&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start_and_connect();

        auto surv = make_surveillance();
        size_t msg_bytes = wire_size_of(surv);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.client.send<stress::SurveillanceRecord>(surv);
        }
        wait_for_count(count, TRANSPORT_BATCH, 60000);
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        print_result("Complex (100K Surv):", received, ms, received * msg_bytes);
        CHECK(received == TRANSPORT_BATCH);

        pair.stop();
    }

    SECTION("Mixed (100K random)") {
        TcpPair pair;
        std::atomic<size_t> count{0};
        auto inc = [&](auto&) { count.fetch_add(1, std::memory_order_relaxed); };
        pair.server.on<stress::Ping>(std::function<void(const stress::Ping&)>(inc));
        pair.server.on<stress::TelemetryReport>(std::function<void(const stress::TelemetryReport&)>(inc));
        pair.server.on<stress::SurveillanceRecord>(std::function<void(const stress::SurveillanceRecord&)>(inc));
        pair.start_and_connect();

        auto ping  = make_ping();
        auto telem = make_telemetry();
        auto surv  = make_surveillance();
        size_t avg_bytes = (wire_size_of(ping) + wire_size_of(telem) + wire_size_of(surv)) / 3;

        std::mt19937 rng(42);
        std::discrete_distribution<int> dist({40.0, 35.0, 25.0});

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            switch (dist(rng)) {
                case 0: (void)pair.client.send<stress::Ping>(ping); break;
                case 1: (void)pair.client.send<stress::TelemetryReport>(telem); break;
                default: (void)pair.client.send<stress::SurveillanceRecord>(surv); break;
            }
        }
        wait_for_count(count, TRANSPORT_BATCH, 30000);
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        print_result("Mixed (100K random):", received, ms, received * avg_bytes);
        CHECK(received == TRANSPORT_BATCH);

        pair.stop();
    }

    std::fprintf(stdout, "\n");
}

// ============================================================================
// UDP throughput
// ============================================================================

TEST_CASE("Stress: UDP throughput", "[stress][udp]") {
    std::fprintf(stdout, "\nStress: UDP throughput\n");
    std::fprintf(stdout, "-------------------------------------------\n");

    SECTION("Simple (100K Ping)") {
        UdpPair pair;
        std::atomic<size_t> count{0};
        pair.receiver.on<stress::Ping>(std::function<void(const stress::Ping&)>(
            [&](const stress::Ping&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start();

        auto ping = make_ping();

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.sender.send<stress::Ping>(ping);
        }
        // Wait up to 5 seconds, then report whatever arrived
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        double drop_pct = 100.0 * (1.0 - static_cast<double>(received) / TRANSPORT_BATCH);
        std::fprintf(stdout, "  %-36s %6zu/%zu msgs in %8.1f ms (%.1f%% drop)\n",
                     "Simple (100K Ping):", received, TRANSPORT_BATCH, ms, drop_pct);
        CHECK(received > 0);

        pair.stop();
    }

    SECTION("Medium (100K TelemetryReport)") {
        UdpPair pair;
        std::atomic<size_t> count{0};
        pair.receiver.on<stress::TelemetryReport>(std::function<void(const stress::TelemetryReport&)>(
            [&](const stress::TelemetryReport&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start();

        auto telem = make_telemetry();

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.sender.send<stress::TelemetryReport>(telem);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        double drop_pct = 100.0 * (1.0 - static_cast<double>(received) / TRANSPORT_BATCH);
        std::fprintf(stdout, "  %-36s %6zu/%zu msgs in %8.1f ms (%.1f%% drop)\n",
                     "Medium (100K Telemetry):", received, TRANSPORT_BATCH, ms, drop_pct);
        CHECK(received > 0);

        pair.stop();
    }

    SECTION("Complex (100K SurveillanceRecord)") {
        UdpPair pair;
        std::atomic<size_t> count{0};
        pair.receiver.on<stress::SurveillanceRecord>(std::function<void(const stress::SurveillanceRecord&)>(
            [&](const stress::SurveillanceRecord&) { count.fetch_add(1, std::memory_order_relaxed); }));
        pair.start();

        auto surv = make_surveillance();

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.sender.send<stress::SurveillanceRecord>(surv);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        double drop_pct = 100.0 * (1.0 - static_cast<double>(received) / TRANSPORT_BATCH);
        std::fprintf(stdout, "  %-36s %6zu/%zu msgs in %8.1f ms (%.1f%% drop)\n",
                     "Complex (100K Surv):", received, TRANSPORT_BATCH, ms, drop_pct);
        CHECK(received > 0);

        pair.stop();
    }

    SECTION("Mixed (100K random)") {
        UdpPair pair;
        std::atomic<size_t> count{0};
        auto inc = [&](auto&) { count.fetch_add(1, std::memory_order_relaxed); };
        pair.receiver.on<stress::Ping>(std::function<void(const stress::Ping&)>(inc));
        pair.receiver.on<stress::TelemetryReport>(std::function<void(const stress::TelemetryReport&)>(inc));
        pair.receiver.on<stress::SurveillanceRecord>(std::function<void(const stress::SurveillanceRecord&)>(inc));
        pair.start();

        auto ping  = make_ping();
        auto telem = make_telemetry();
        auto surv  = make_surveillance();

        std::mt19937 rng(42);
        std::discrete_distribution<int> dist({40.0, 35.0, 25.0});

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            switch (dist(rng)) {
                case 0: (void)pair.sender.send<stress::Ping>(ping); break;
                case 1: (void)pair.sender.send<stress::TelemetryReport>(telem); break;
                default: (void)pair.sender.send<stress::SurveillanceRecord>(surv); break;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();
        double drop_pct = 100.0 * (1.0 - static_cast<double>(received) / TRANSPORT_BATCH);
        std::fprintf(stdout, "  %-36s %6zu/%zu msgs in %8.1f ms (%.1f%% drop)\n",
                     "Mixed (100K random):", received, TRANSPORT_BATCH, ms, drop_pct);
        CHECK(received > 0);

        pair.stop();
    }

    std::fprintf(stdout, "\n");
}

// ============================================================================
// TCP back-pressure
// ============================================================================

TEST_CASE("Stress: TCP back-pressure", "[stress][tcp][backpressure]") {
    std::fprintf(stdout, "\nStress: TCP back-pressure\n");
    std::fprintf(stdout, "-------------------------------------------\n");

    TransceiverConfig scfg;
    scfg.rx_queue.capacity = 1024;
    scfg.rx_queue.drop_policy = queue::DropPolicy::DropOldest;
    scfg.rx_queue.back_pressure_threshold = 0.8;

    TcpServerConfig srv_cfg;
    srv_cfg.bind_address = "127.0.0.1";
    srv_cfg.port = 0;
    auto server_transport = std::make_shared<TcpServerTransport>(srv_cfg);

    Transceiver server(std::move(scfg));
    auto sr = server.add_peer("clients", stress::create_stress_frame_session, server_transport);
    REQUIRE(sr.has_value());

    std::atomic<size_t> count{0};
    server.on<stress::Ping>(std::function<void(const stress::Ping&)>(
        [&](const stress::Ping&) {
            count.fetch_add(1, std::memory_order_relaxed);
        }));

    REQUIRE(server.start().has_value());
    uint16_t port = server_transport->local_port();

    TcpClientConfig cli_cfg;
    cli_cfg.host = "127.0.0.1";
    cli_cfg.port = port;
    cli_cfg.reconnect.enabled = false;
    cli_cfg.connect_timeout = std::chrono::milliseconds(5000);

    auto cli_transport = std::make_shared<TcpClientTransport>(cli_cfg);
    Transceiver client;
    auto cr = client.add_peer("server",
        std::make_unique<stress::StressFrameSession>(), cli_transport);
    REQUIRE(cr.has_value());
    REQUIRE(client.start().has_value());
    REQUIRE(wait_for_connection(client));

    auto ping = make_ping();
    size_t msg_bytes = wire_size_of(ping);

    auto t0 = std::chrono::high_resolution_clock::now();
    size_t sent = 0;
    for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
        auto r = client.send<stress::Ping>(ping);
        if (r.has_value()) ++sent;
    }
    // Wait for delivery
    wait_for_count(count, sent, 30000);
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    size_t received = count.load();
    size_t dropped = (sent > received) ? sent - received : 0;

    std::fprintf(stdout, "  Queue capacity: 1024, back-pressure threshold: 0.8\n");
    std::fprintf(stdout, "  Sent: %zu, Received: %zu, Dropped: %zu\n", sent, received, dropped);
    print_result("Back-pressure:", received, ms, received * msg_bytes);
    std::fprintf(stdout, "\n");

    CHECK(received > 0);

    client.stop();
    server.stop();
}

// ============================================================================
// TCP multi-worker
// ============================================================================

TEST_CASE("Stress: TCP multi-worker", "[stress][tcp][workers]") {
    std::fprintf(stdout, "\nStress: TCP multi-worker\n");
    std::fprintf(stdout, "-------------------------------------------\n");

    auto run_with_workers = [](size_t worker_count) {
        TcpPair pair(200000, worker_count);

        std::atomic<size_t> count{0};
        pair.server.on<stress::SurveillanceRecord>(
            std::function<void(const stress::SurveillanceRecord&)>(
            [&](const stress::SurveillanceRecord&) {
                count.fetch_add(1, std::memory_order_relaxed);
            }));
        pair.start_and_connect();

        auto surv = make_surveillance();
        size_t msg_bytes = wire_size_of(surv);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
            (void)pair.client.send<stress::SurveillanceRecord>(surv);
        }
        wait_for_count(count, TRANSPORT_BATCH, 60000);
        auto t1 = std::chrono::high_resolution_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t received = count.load();

        char label[64];
        std::snprintf(label, sizeof(label), "%zu worker(s):", worker_count);
        print_result(label, received, ms, received * msg_bytes);

        CHECK(received == TRANSPORT_BATCH);
        pair.stop();
    };

    SECTION("1 worker thread") { run_with_workers(1); }
    SECTION("2 worker threads") { run_with_workers(2); }
    SECTION("4 worker threads") { run_with_workers(4); }

    std::fprintf(stdout, "\n");
}

// ============================================================================
// TCP burst
// ============================================================================

TEST_CASE("Stress: TCP burst", "[stress][tcp][burst]") {
    std::fprintf(stdout, "\nStress: TCP burst\n");
    std::fprintf(stdout, "-------------------------------------------\n");

    TcpPair pair;
    std::atomic<size_t> count{0};
    pair.server.on<stress::Ping>(std::function<void(const stress::Ping&)>(
        [&](const stress::Ping&) { count.fetch_add(1, std::memory_order_relaxed); }));
    pair.start_and_connect();

    auto ping = make_ping();
    size_t msg_bytes = wire_size_of(ping);

    // Measure raw send rate (fire-and-forget)
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t sent = 0;
    for (size_t i = 0; i < TRANSPORT_BATCH; ++i) {
        auto r = pair.client.send<stress::Ping>(ping);
        if (r.has_value()) ++sent;
    }
    auto t_send = std::chrono::high_resolution_clock::now();

    double send_ms = std::chrono::duration<double, std::milli>(t_send - t0).count();
    double send_rate = (send_ms > 0) ? (static_cast<double>(sent) / send_ms * 1000.0) : 0;

    std::fprintf(stdout, "  Send phase: %zu msgs in %.1f ms => %.0f msgs/s (encode + socket write)\n",
                 sent, send_ms, send_rate);

    // Wait for receive side
    wait_for_count(count, sent, 30000);
    auto t_recv = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(t_recv - t0).count();
    size_t received = count.load();
    print_result("End-to-end:", received, total_ms, received * msg_bytes);
    std::fprintf(stdout, "\n");

    CHECK(received == sent);

    pair.stop();
}
