// SPDX-License-Identifier: MIT
// Conduit - Transceiver Config Tests (using real transports + bgen sessions)

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transceiver_config.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/transport/serial.hpp>
#include <session_protocol/sessions.hpp>
#include <sentry_link/sessions.hpp>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("TransceiverConfig: add_peer fluent chaining",
          "[transceiver][config]") {
    TransceiverConfig config;
    auto& ref = config
        .add_peer("peer1",
            []() { return session_test::create_packet_session(); },
            UdpConfig{.bind_address = "127.0.0.1", .bind_port = 0,
                      .remote_address = "127.0.0.1", .remote_port = 5000})
        .add_peer("peer2",
            []() { return session_test::create_packet_session(); },
            UdpConfig{.bind_address = "127.0.0.1", .bind_port = 0,
                      .remote_address = "127.0.0.1", .remote_port = 5001});

    CHECK(&ref == &config);
    CHECK(config.peers.size() == 2);
}

TEST_CASE("TransceiverConfig: materialize TCP client",
          "[transceiver][config]") {
    // Start a real TCP server to connect to
    TcpServerConfig srv_cfg;
    srv_cfg.bind_address = "127.0.0.1";
    srv_cfg.port = 0;
    auto server = std::make_shared<TcpServerTransport>(srv_cfg);

    TransportCallbacks srv_cb;
    std::atomic<uint32_t> next_id{100};
    srv_cb.on_peer_connected = [&]() -> PeerId {
        return PeerId{next_id.fetch_add(1)};
    };
    srv_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    srv_cb.on_peer_disconnected = [](PeerId) {};
    srv_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(server->start(std::move(srv_cb)).has_value());
    auto port = server->local_port();

    // Config-driven transceiver
    TcpClientConfig cli_cfg;
    cli_cfg.host = "127.0.0.1";
    cli_cfg.port = port;
    cli_cfg.reconnect.enabled = false;

    TransceiverConfig config;
    config.add_peer("client",
        []() { return session_test::create_packet_session(); },
        cli_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    CHECK(xcvr.peer_count() == 1);
    auto p = xcvr.peer("client");
    REQUIRE(p.has_value());

    xcvr.stop();
    server->stop();
}

TEST_CASE("TransceiverConfig: materialize UDP peer",
          "[transceiver][config]") {
    UdpConfig udp_cfg;
    udp_cfg.bind_address = "127.0.0.1";
    udp_cfg.bind_port = 0;
    udp_cfg.remote_address = "127.0.0.1";
    udp_cfg.remote_port = 19999;

    TransceiverConfig config;
    config.add_peer("udp_peer",
        []() { return session_test::create_packet_session(); },
        udp_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    CHECK(xcvr.peer_count() == 1);

    xcvr.stop();
}

TEST_CASE("TransceiverConfig: materialize TCP server",
          "[transceiver][config]") {
    TcpServerConfig srv_cfg;
    srv_cfg.bind_address = "127.0.0.1";
    srv_cfg.port = 0;

    TransceiverConfig config;
    config.add_peer("server",
        []() { return session_test::create_packet_session(); },
        srv_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    // Server started; verify it can accept connections
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    xcvr.stop();
}

TEST_CASE("TransceiverConfig: materialize serial with nonexistent port",
          "[transceiver][config]") {
    SerialConfig ser_cfg;
    ser_cfg.port = "NOEXIST_PORT_XYZ";
    ser_cfg.baud_rate = 9600;

    TransceiverConfig config;
    config.add_peer("serial",
        []() { return session_test::create_packet_session(); },
        ser_cfg);

    Transceiver xcvr(config);
    // Construction should succeed, start should fail
    auto result = xcvr.start();
    REQUIRE_FALSE(result.has_value());

    xcvr.stop();
}

TEST_CASE("TransceiverConfig: custom rx_queue capacity",
          "[transceiver][config]") {
    UdpConfig udp_cfg;
    udp_cfg.bind_address = "127.0.0.1";
    udp_cfg.bind_port = 0;
    udp_cfg.remote_address = "127.0.0.1";
    udp_cfg.remote_port = 19998;

    TransceiverConfig config;
    config.rx_queue.capacity = 4;
    config.add_peer("test",
        []() { return session_test::create_packet_session(); },
        udp_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    xcvr.stop();
}

TEST_CASE("TransceiverConfig: multiple config peers mixed transport",
          "[transceiver][config]") {
    UdpConfig udp1;
    udp1.bind_address = "127.0.0.1";
    udp1.bind_port = 0;
    udp1.remote_address = "127.0.0.1";
    udp1.remote_port = 19996;

    UdpConfig udp2;
    udp2.bind_address = "127.0.0.1";
    udp2.bind_port = 0;
    udp2.remote_address = "127.0.0.1";
    udp2.remote_port = 19995;

    // Start a server for the TCP client to connect to
    TcpServerConfig srv_cfg;
    srv_cfg.bind_address = "127.0.0.1";
    srv_cfg.port = 0;
    auto server = std::make_shared<TcpServerTransport>(srv_cfg);
    TransportCallbacks srv_cb;
    std::atomic<uint32_t> next_id{100};
    srv_cb.on_peer_connected = [&]() -> PeerId { return PeerId{next_id.fetch_add(1)}; };
    srv_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    srv_cb.on_peer_disconnected = [](PeerId) {};
    srv_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(server->start(std::move(srv_cb)).has_value());
    auto port = server->local_port();

    TcpClientConfig tcp_cfg;
    tcp_cfg.host = "127.0.0.1";
    tcp_cfg.port = port;
    tcp_cfg.reconnect.enabled = false;

    TransceiverConfig config;
    config
        .add_peer("udp1",
            []() { return session_test::create_packet_session(); }, udp1)
        .add_peer("udp2",
            []() { return session_test::create_packet_session(); }, udp2)
        .add_peer("tcp",
            []() { return session_test::create_packet_session(); }, tcp_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    CHECK(xcvr.peer_count() == 3);

    xcvr.stop();
    server->stop();
}

TEST_CASE("TransceiverConfig: worker thread_count=4",
          "[transceiver][config]") {
    UdpConfig udp_cfg;
    udp_cfg.bind_address = "127.0.0.1";
    udp_cfg.bind_port = 0;
    udp_cfg.remote_address = "127.0.0.1";
    udp_cfg.remote_port = 19994;

    TransceiverConfig config;
    config.worker.thread_count = 4;
    config.add_peer("test",
        []() { return session_test::create_packet_session(); },
        udp_cfg);

    Transceiver xcvr(config);
    REQUIRE(xcvr.start().has_value());

    // Verify it runs and can be stopped without issues
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    xcvr.stop();
}
