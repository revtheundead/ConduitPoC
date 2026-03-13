// SPDX-License-Identifier: MIT
// Conduit - TCP Transport Integration Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/peer.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

TEST_CASE("TCP loopback: server + client bidirectional",
          "[integration][tcp]") {
    auto wait_until = [](auto pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) -> bool {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return pred();
    };

    // --- Server setup ---
    TcpServerConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.port = 0;  // Ephemeral port — resolved via local_port() after start
    server_cfg.recv_buffer_size = 4096;

    auto server = std::make_shared<TcpServerTransport>(server_cfg);

    std::atomic<uint32_t> next_id{100};
    std::vector<uint8_t> server_received;
    std::mutex server_rx_mutex;
    std::atomic<uint32_t> client_peer_on_server_id{0};

    TransportCallbacks server_cb;
    server_cb.on_peer_connected = [&](std::string) -> PeerId {
        auto id = next_id.fetch_add(1);
        client_peer_on_server_id.store(id, std::memory_order_release);
        return PeerId{id};
    };
    server_cb.on_data_received = [&](PeerId /*peer*/,
                                     std::span<const uint8_t> data) {
        std::lock_guard lock(server_rx_mutex);
        server_received.insert(server_received.end(), data.begin(), data.end());
    };
    server_cb.on_peer_disconnected = [](PeerId) {};
    server_cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto srv_result = server->start(std::move(server_cb));
    REQUIRE(srv_result.has_value());

    auto bound_port = server->local_port();
    REQUIRE(bound_port != 0);

    // --- Client setup ---
    TcpClientConfig client_cfg;
    client_cfg.host = "127.0.0.1";
    client_cfg.port = bound_port;
    client_cfg.reconnect.enabled = false;
    client_cfg.recv_buffer_size = 4096;

    auto client = std::make_shared<TcpClientTransport>(client_cfg);

    std::vector<uint8_t> client_received;
    std::mutex client_rx_mutex;
    PeerId client_peer(1);

    TransportCallbacks client_cb;
    client_cb.on_data_received = [&](PeerId /*peer*/,
                                     std::span<const uint8_t> data) {
        std::lock_guard lock(client_rx_mutex);
        client_received.insert(client_received.end(), data.begin(), data.end());
    };
    client_cb.on_peer_connected = [&](std::string) -> PeerId { return client_peer; };
    client_cb.on_peer_disconnected = [](PeerId) {};
    client_cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto cli_result = client->start(std::move(client_cb));
    REQUIRE(cli_result.has_value());

    // Wait for connection
    REQUIRE(wait_until([&] {
        return client_peer_on_server_id.load(std::memory_order_acquire) != 0;
    }));

    PeerId client_peer_on_server{client_peer_on_server_id.load(std::memory_order_acquire)};

    // Client sends to server
    std::vector<uint8_t> msg1 = {0x01, 0x02, 0x03, 0x04};
    auto send1 = client->send(client_peer, msg1);
    REQUIRE(send1.has_value());

    // Wait for server to receive data
    REQUIRE(wait_until([&] { std::lock_guard lock(server_rx_mutex); return !server_received.empty(); }));

    // Server sends back to client
    std::vector<uint8_t> msg2 = {0x0A, 0x0B, 0x0C};
    auto send2 = server->send(client_peer_on_server, msg2);
    REQUIRE(send2.has_value());

    // Wait for client to receive data
    REQUIRE(wait_until([&] { std::lock_guard lock(client_rx_mutex); return !client_received.empty(); }));

    client->stop();
    server->stop();

    // Verify
    {
        std::lock_guard lock(server_rx_mutex);
        CHECK(server_received == msg1);
    }
    {
        std::lock_guard lock(client_rx_mutex);
        CHECK(client_received == msg2);
    }
}
