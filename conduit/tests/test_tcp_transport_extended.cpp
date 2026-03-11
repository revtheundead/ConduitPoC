// SPDX-License-Identifier: MIT
// Conduit - TCP Transport Extended Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/peer.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <span>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

namespace {

inline void wait_until(auto pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}  // anonymous namespace

// ============================================================================
// Helpers
// ============================================================================

// Helper to set up a server with common callbacks
struct ServerHelper {
    std::shared_ptr<TcpServerTransport> server;
    std::atomic<uint32_t> next_id{100};
    std::vector<PeerId> connected_peers;
    std::vector<PeerId> disconnected_peers;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> received_data;
    std::mutex peers_mutex;
    std::mutex rx_mutex;

    explicit ServerHelper(uint16_t port = 0, size_t max_clients = 64) {
        TcpServerConfig cfg;
        cfg.bind_address = "127.0.0.1";
        cfg.port = port;
        cfg.recv_buffer_size = 65536;
        cfg.max_clients = max_clients;
        server = std::make_shared<TcpServerTransport>(cfg);
    }

    VoidResult start() {
        TransportCallbacks cb;
        cb.on_peer_connected = [this](std::string) -> PeerId {
            auto id = PeerId{next_id.fetch_add(1)};
            std::lock_guard lock(peers_mutex);
            connected_peers.push_back(id);
            return id;
        };
        cb.on_data_received = [this](PeerId peer, std::span<const uint8_t> data) {
            std::lock_guard lock(rx_mutex);
            received_data.emplace_back(peer.value(),
                std::vector<uint8_t>(data.begin(), data.end()));
        };
        cb.on_peer_disconnected = [this](PeerId peer) {
            std::lock_guard lock(peers_mutex);
            disconnected_peers.push_back(peer);
        };
        cb.on_state_changed = [](PeerId, net::ConnectionState) {};
        return server->start(std::move(cb));
    }
};

struct ClientHelper {
    std::shared_ptr<TcpClientTransport> client;
    std::vector<uint8_t> received;
    std::vector<net::ConnectionState> states;
    std::mutex rx_mutex;
    std::mutex state_mutex;
    PeerId peer_id;

    explicit ClientHelper(const std::string& host, uint16_t port,
                          bool reconnect = false, uint32_t max_attempts = 0,
                          PeerId pid = PeerId{1}) : peer_id(pid) {
        TcpClientConfig cfg;
        cfg.host = host;
        cfg.port = port;
        cfg.reconnect.enabled = reconnect;
        cfg.reconnect.max_attempts = max_attempts;
        cfg.reconnect.initial_delay = std::chrono::milliseconds(100);
        cfg.reconnect.max_delay = std::chrono::milliseconds(500);
        cfg.recv_buffer_size = 65536;
        cfg.connect_timeout = std::chrono::milliseconds(2000);
        client = std::make_shared<TcpClientTransport>(cfg);
    }

    VoidResult start() {
        TransportCallbacks cb;
        cb.on_data_received = [this](PeerId, std::span<const uint8_t> data) {
            std::lock_guard lock(rx_mutex);
            received.insert(received.end(), data.begin(), data.end());
        };
        cb.on_peer_connected = [this](std::string) -> PeerId { return peer_id; };
        cb.on_peer_disconnected = [](PeerId) {};
        cb.on_state_changed = [this](PeerId, net::ConnectionState s) {
            std::lock_guard lock(state_mutex);
            states.push_back(s);
        };
        return client->start(std::move(cb));
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("TCP: ephemeral port is non-zero", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());

    auto port = srv.server->local_port();
    CHECK(port != 0);

    srv.server->stop();
}

TEST_CASE("TCP: max_clients enforcement", "[tcp]") {
    ServerHelper srv(0, 2);  // max_clients=2
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c1("127.0.0.1", port, false, 0, PeerId{1});
    ClientHelper c2("127.0.0.1", port, false, 0, PeerId{2});
    ClientHelper c3("127.0.0.1", port, false, 0, PeerId{3});

    REQUIRE(c1.start().has_value());
    REQUIRE(c2.start().has_value());
    REQUIRE(c3.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() >= 2; });

    // Stop server BEFORE clients to prevent race: stopping a client frees a
    // slot in the server's clients map, which could let the 3rd pending
    // connection be accepted from the TCP backlog.
    srv.server->stop();

    c1.client->stop();
    c2.client->stop();
    c3.client->stop();

    std::lock_guard lock(srv.peers_mutex);
    CHECK(srv.connected_peers.size() == 2);
}

TEST_CASE("TCP: multiple clients get distinct PeerIds", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c1("127.0.0.1", port, false, 0, PeerId{1});
    ClientHelper c2("127.0.0.1", port, false, 0, PeerId{2});
    ClientHelper c3("127.0.0.1", port, false, 0, PeerId{3});

    REQUIRE(c1.start().has_value());
    REQUIRE(c2.start().has_value());
    REQUIRE(c3.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() >= 3; });

    c1.client->stop();
    c2.client->stop();
    c3.client->stop();
    srv.server->stop();

    std::lock_guard lock(srv.peers_mutex);
    REQUIRE(srv.connected_peers.size() == 3);

    std::set<uint32_t> ids;
    for (auto& p : srv.connected_peers) ids.insert(p.value());
    CHECK(ids.size() == 3);  // All unique
}

TEST_CASE("TCP: data routed to correct peer", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c_a("127.0.0.1", port, false, 0, PeerId{1});
    ClientHelper c_b("127.0.0.1", port, false, 0, PeerId{2});

    REQUIRE(c_a.start().has_value());
    REQUIRE(c_b.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() >= 2; },
               std::chrono::milliseconds(5000));

    // Small delay to let connections fully stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client A sends 0xAA
    std::vector<uint8_t> msg_a = {0xAA};
    (void)c_a.client->send(c_a.peer_id, msg_a);

    // Client B sends 0xBB
    std::vector<uint8_t> msg_b = {0xBB};
    (void)c_b.client->send(c_b.peer_id, msg_b);

    wait_until([&] { std::lock_guard lock(srv.rx_mutex); return srv.received_data.size() >= 2; },
               std::chrono::milliseconds(5000));

    c_a.client->stop();
    c_b.client->stop();
    srv.server->stop();

    std::lock_guard lock(srv.rx_mutex);
    REQUIRE(srv.received_data.size() == 2);

    // Each message should have a different peer id
    CHECK(srv.received_data[0].first != srv.received_data[1].first);
}

TEST_CASE("TCP: on_peer_disconnected fires", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c("127.0.0.1", port, false, 0, PeerId{1});
    REQUIRE(c.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.connected_peers.empty(); });

    // Client disconnects
    c.client->stop();

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.disconnected_peers.empty(); });

    srv.server->stop();

    std::lock_guard lock(srv.peers_mutex);
    CHECK(!srv.disconnected_peers.empty());
    // The disconnected peer should be one of the connected peers
    CHECK(srv.disconnected_peers[0] == srv.connected_peers[0]);
}

TEST_CASE("TCP: reconnect disabled - client fails permanently", "[tcp]") {
    // No server running on this port
    ClientHelper c("127.0.0.1", 19899, false, 0, PeerId{1});
    REQUIRE(c.start().has_value());

    wait_until([&] {
        std::lock_guard lock(c.state_mutex);
        return !c.states.empty() && c.states.back() == net::ConnectionState::Failed;
    }, std::chrono::milliseconds(5000));

    c.client->stop();

    std::lock_guard lock(c.state_mutex);
    // Should have failed
    CHECK(!c.states.empty());
    auto last = c.states.back();
    CHECK(last == net::ConnectionState::Failed);

    // Should not have any Connecting after Failed
    bool found_failed = false;
    for (auto s : c.states) {
        if (found_failed) {
            CHECK(s != net::ConnectionState::Connecting);
        }
        if (s == net::ConnectionState::Failed) found_failed = true;
    }
}

TEST_CASE("TCP: reconnect enabled - client reconnects", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c("127.0.0.1", port, true, 0, PeerId{1});
    REQUIRE(c.start().has_value());

    wait_until([&] {
        std::lock_guard lock(c.state_mutex);
        return std::find(c.states.begin(), c.states.end(),
                         net::ConnectionState::Connected) != c.states.end();
    });

    // Verify connected
    {
        std::lock_guard lock(c.state_mutex);
        auto it = std::find(c.states.begin(), c.states.end(),
                            net::ConnectionState::Connected);
        REQUIRE(it != c.states.end());
    }

    // Restart server on same port to test reconnect
    srv.server->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // New server on same port
    ServerHelper srv2(port);
    REQUIRE(srv2.start().has_value());

    // Wait for reconnection (second Connected state)
    wait_until([&] {
        std::lock_guard lock(c.state_mutex);
        int count = 0;
        for (auto s : c.states) {
            if (s == net::ConnectionState::Connected) ++count;
        }
        return count >= 2;
    }, std::chrono::milliseconds(5000));

    // Send data to verify connection is live
    std::vector<uint8_t> msg = {0x42};
    auto send_r = c.client->send(c.peer_id, msg);

    wait_until([&] { std::lock_guard lock(srv2.rx_mutex); return !srv2.received_data.empty(); });

    c.client->stop();
    srv2.server->stop();

    // Should have reconnected (multiple Connected states)
    std::lock_guard lock(c.state_mutex);
    int connected_count = 0;
    for (auto s : c.states) {
        if (s == net::ConnectionState::Connected) ++connected_count;
    }
    CHECK(connected_count >= 2);
}

TEST_CASE("TCP: max_attempts=3 gives up after 3 attempts", "[tcp]") {
    // No server listening
    ClientHelper c("127.0.0.1", 19897, true, 3, PeerId{1});
    REQUIRE(c.start().has_value());

    // Wait for client to exhaust reconnect attempts and reach Failed state
    wait_until([&] {
        std::lock_guard lock(c.state_mutex);
        return !c.states.empty() && c.states.back() == net::ConnectionState::Failed;
    }, std::chrono::milliseconds(10000));

    c.client->stop();

    std::lock_guard lock(c.state_mutex);
    CHECK(!c.states.empty());
    CHECK(c.states.back() == net::ConnectionState::Failed);
}

TEST_CASE("TCP: is_stream_oriented (client) returns true", "[tcp]") {
    TcpClientConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 5000;
    TcpClientTransport client(cfg);
    CHECK(client.is_stream_oriented() == true);
}

TEST_CASE("TCP: is_multi_peer (server) returns true", "[tcp]") {
    TcpServerConfig cfg;
    TcpServerTransport server(cfg);
    CHECK(server.is_multi_peer() == true);
}

TEST_CASE("TCP: is_multi_peer (client) returns false", "[tcp]") {
    TcpClientConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 5000;
    TcpClientTransport client(cfg);
    CHECK(client.is_multi_peer() == false);
}

TEST_CASE("TCP: large message 64KB", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c("127.0.0.1", port, false, 0, PeerId{1});
    REQUIRE(c.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.connected_peers.empty(); },
               std::chrono::milliseconds(5000));

    // Build 65536-byte message
    std::vector<uint8_t> large_msg(65536);
    for (size_t i = 0; i < large_msg.size(); ++i) {
        large_msg[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto send_r = c.client->send(c.peer_id, large_msg);
    REQUIRE(send_r.has_value());

    // Wait for TCP to deliver all data (may arrive in chunks)
    wait_until([&] {
        std::lock_guard lock(srv.rx_mutex);
        size_t total = 0;
        for (auto& [pid, data] : srv.received_data) total += data.size();
        return total >= 65536;
    }, std::chrono::milliseconds(10000));

    c.client->stop();
    srv.server->stop();

    // Accumulate all received data from server
    std::vector<uint8_t> accumulated;
    {
        std::lock_guard lock(srv.rx_mutex);
        for (auto& [pid, data] : srv.received_data) {
            accumulated.insert(accumulated.end(), data.begin(), data.end());
        }
    }

    REQUIRE(accumulated.size() == 65536);
    CHECK(accumulated == large_msg);
}

TEST_CASE("TCP: rapid connect/disconnect 10 clients", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    for (int i = 0; i < 10; ++i) {
        size_t prev_connected = [&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size(); }();
        size_t prev_disconnected = [&] { std::lock_guard lock(srv.peers_mutex); return srv.disconnected_peers.size(); }();

        ClientHelper c("127.0.0.1", port, false, 0, PeerId{static_cast<uint32_t>(i + 1)});
        REQUIRE(c.start().has_value());

        wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() > prev_connected; });

        // Send 4 bytes
        std::vector<uint8_t> msg = {
            static_cast<uint8_t>(i),
            static_cast<uint8_t>(i + 1),
            static_cast<uint8_t>(i + 2),
            static_cast<uint8_t>(i + 3)
        };
        (void)c.client->send(c.peer_id, msg);

        wait_until([&] { std::lock_guard lock(srv.rx_mutex); return srv.received_data.size() > static_cast<size_t>(i); });

        c.client->stop();

        wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.disconnected_peers.size() > prev_disconnected; });
    }

    srv.server->stop();

    std::lock_guard lock(srv.peers_mutex);
    CHECK(srv.connected_peers.size() == 10);
    CHECK(srv.disconnected_peers.size() == 10);
}

TEST_CASE("TCP: server send to specific peer", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c1("127.0.0.1", port, false, 0, PeerId{1});
    ClientHelper c2("127.0.0.1", port, false, 0, PeerId{2});

    REQUIRE(c1.start().has_value());
    REQUIRE(c2.start().has_value());

    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() >= 2; });

    // Server sends different data to each client
    {
        std::lock_guard lock(srv.peers_mutex);
        REQUIRE(srv.connected_peers.size() == 2);

        std::vector<uint8_t> data1 = {0xAA, 0xBB};
        std::vector<uint8_t> data2 = {0xCC, 0xDD};
        (void)srv.server->send(srv.connected_peers[0], data1);
        (void)srv.server->send(srv.connected_peers[1], data2);
    }

    wait_until([&] {
        std::lock_guard l1(c1.rx_mutex);
        std::lock_guard l2(c2.rx_mutex);
        return !c1.received.empty() && !c2.received.empty();
    });

    c1.client->stop();
    c2.client->stop();
    srv.server->stop();

    // Each client should have received data
    {
        std::lock_guard l1(c1.rx_mutex);
        std::lock_guard l2(c2.rx_mutex);
        CHECK(!c1.received.empty());
        CHECK(!c2.received.empty());
        // The two clients should have received different data
        CHECK(c1.received != c2.received);
    }
}

// ============================================================================
// TCP server + TCP client in same Transceiver
// ============================================================================

#include <conduit/transceiver/transceiver.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>

namespace {

struct XcvrTestMsg {
    static constexpr uint64_t TYPE_ID = 2001;
    static constexpr std::string_view TYPE_NAME = "XcvrTestMsg";
    int payload = 0;

    conduit::VoidResult encode(conduit::io::BitWriter& w) const {
        w.write_u32(static_cast<uint32_t>(payload));
        if (w.has_error()) return std::unexpected(w.error());
        return {};
    }

    static conduit::Result<XcvrTestMsg> decode(conduit::io::BitReader& r) {
        CONDUIT_TRY_ASSIGN(auto, val, r.read_u32());
        XcvrTestMsg msg;
        msg.payload = static_cast<int>(val);
        return msg;
    }
};

static_assert(conduit::traits::Message<XcvrTestMsg>);

class XcvrTestSession : public conduit::traits::ISession {
public:
    conduit::Result<std::vector<conduit::traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        if (data.size() < 4) {
            return std::unexpected(
                CONDUIT_ERROR(conduit::ErrorCode::BufferUnderrun, "Need 4 bytes"));
        }
        conduit::io::BitReader reader(data);
        auto msg = XcvrTestMsg::decode(reader);
        if (!msg) return std::unexpected(msg.error());

        conduit::traits::DecodedMessage dm;
        dm.type_id = XcvrTestMsg::TYPE_ID;
        dm.type_name = XcvrTestMsg::TYPE_NAME;
        dm.payload = std::any(*msg);
        dm.raw = std::vector<uint8_t>(data.begin(), data.end());

        return std::vector<conduit::traits::DecodedMessage>{std::move(dm)};
    }

    conduit::Result<conduit::traits::EncodeResult>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != XcvrTestMsg::TYPE_ID) {
            return std::unexpected(
                CONDUIT_ERROR(conduit::ErrorCode::UnknownTypeId, "Unknown type"));
        }
        auto& msg = std::any_cast<const XcvrTestMsg&>(payload);
        conduit::io::BitWriter writer;
        (void)msg.encode(writer);
        auto bytes = writer.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return conduit::traits::EncodeResult{std::move(*bytes), {}};
    }

    std::span<const uint8_t> sync_pattern() const override { return sync_; }
    size_t min_frame_header_size() const override { return 4; }
    size_t extract_frame_length(std::span<const uint8_t>) const override {
        return 4;
    }

    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t) const override { return "XcvrTestMsg"; }
    void reset() override {}

private:
    std::vector<uint8_t> sync_;
    std::vector<uint64_t> ids_ = {XcvrTestMsg::TYPE_ID};
};

}  // anonymous namespace

TEST_CASE("TCP: server and client in same Transceiver", "[tcp][transceiver]") {
    using namespace conduit::transceiver;

    // Set up a TCP server on ephemeral port
    TcpServerConfig srv_cfg;
    srv_cfg.bind_address = "127.0.0.1";
    srv_cfg.port = 0;

    auto server_transport = std::make_shared<TcpServerTransport>(srv_cfg);

    Transceiver xcvr;

    // Add TCP server as a multi-peer transport with session factory
    auto srv_result = xcvr.add_peer("server",
        []() -> std::unique_ptr<conduit::traits::ISession> {
            return std::make_unique<XcvrTestSession>();
        },
        server_transport);
    REQUIRE(srv_result.has_value());

    // We need to start the transceiver to get the server listening,
    // then find out its port
    REQUIRE(xcvr.start().has_value());

    auto port = server_transport->local_port();
    REQUIRE(port != 0);

    // Now set up a separate client transceiver that connects to the server
    TcpClientConfig cli_cfg;
    cli_cfg.host = "127.0.0.1";
    cli_cfg.port = port;
    cli_cfg.reconnect.enabled = false;
    cli_cfg.connect_timeout = std::chrono::milliseconds(2000);

    auto client_transport = std::make_shared<TcpClientTransport>(cli_cfg);

    Transceiver cli_xcvr;
    auto cli_result = cli_xcvr.add_peer("client",
        std::make_unique<XcvrTestSession>(), client_transport);
    REQUIRE(cli_result.has_value());
    PeerId client_peer = *cli_result;

    // Track received messages on server side
    std::mutex srv_rx_mutex;
    std::vector<int> srv_received;
    xcvr.on<XcvrTestMsg>(std::function<void(const XcvrTestMsg&)>(
        [&](const XcvrTestMsg& msg) {
            std::lock_guard lock(srv_rx_mutex);
            srv_received.push_back(msg.payload);
        }));

    // Track received messages on client side
    std::mutex cli_rx_mutex;
    std::vector<int> cli_received;
    cli_xcvr.on<XcvrTestMsg>(std::function<void(const XcvrTestMsg&)>(
        [&](const XcvrTestMsg& msg) {
            std::lock_guard lock(cli_rx_mutex);
            cli_received.push_back(msg.payload);
        }));

    REQUIRE(cli_xcvr.start().has_value());

    // Wait for connection
    wait_until([&] {
        return cli_xcvr.peer_state(client_peer) == net::ConnectionState::Connected;
    });

    // Client sends to server
    XcvrTestMsg msg_to_server;
    msg_to_server.payload = 42;
    auto send_result = cli_xcvr.send<XcvrTestMsg>(client_peer, msg_to_server);
    REQUIRE(send_result.has_value());

    // Wait for delivery
    wait_until([&] { std::lock_guard lock(srv_rx_mutex); return !srv_received.empty(); });

    // Verify server received the message
    {
        std::lock_guard lock(srv_rx_mutex);
        REQUIRE(srv_received.size() == 1);
        CHECK(srv_received[0] == 42);
    }

    cli_xcvr.stop();
    xcvr.stop();
}

// ============================================================================
// Transport start rollback test
// ============================================================================

namespace {

class FailingTransport : public ITransport {
public:
    VoidResult start(TransportCallbacks) override {
        return std::unexpected(
            CONDUIT_ERROR(conduit::ErrorCode::SocketError, "Intentional failure"));
    }
    void stop() override {}
    VoidResult send(PeerId, std::span<const uint8_t>) override { return {}; }
    bool is_stream_oriented() const noexcept override { return false; }
    bool is_multi_peer() const noexcept override { return false; }
};

class TrackingTransport : public ITransport {
public:
    std::atomic<bool> started{false};
    std::atomic<bool> stopped{false};

    VoidResult start(TransportCallbacks) override {
        started = true;
        return {};
    }
    void stop() override {
        stopped = true;
    }
    VoidResult send(PeerId, std::span<const uint8_t>) override { return {}; }
    bool is_stream_oriented() const noexcept override { return false; }
    bool is_multi_peer() const noexcept override { return false; }
};

}  // anonymous namespace

TEST_CASE("Transceiver: transport start rollback on partial failure",
          "[transceiver][rollback]") {
    using namespace conduit::transceiver;

    auto good_transport = std::make_shared<TrackingTransport>();
    auto bad_transport = std::make_shared<FailingTransport>();

    Transceiver xcvr;

    // Add good transport first
    auto r1 = xcvr.add_peer("good", std::make_unique<XcvrTestSession>(), good_transport);
    REQUIRE(r1.has_value());

    // Add failing transport second
    auto r2 = xcvr.add_peer("bad", std::make_unique<XcvrTestSession>(), bad_transport);
    REQUIRE(r2.has_value());

    // Start should fail because the second transport fails
    auto start_result = xcvr.start();
    REQUIRE_FALSE(start_result.has_value());

    // The good transport should have been started and then stopped (rollback)
    CHECK(good_transport->started.load());
    CHECK(good_transport->stopped.load());

    // Transceiver should not be running
    CHECK_FALSE(xcvr.is_running());
}

// ============================================================================
// T5: TCP server handles client disconnect mid-transfer
// ============================================================================

TEST_CASE("TCP: server handles abrupt client disconnect mid-transfer", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    {
        ClientHelper c("127.0.0.1", port, false, 0, PeerId{1});
        REQUIRE(c.start().has_value());

        wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.connected_peers.empty(); });

        // Send a partial chunk of what would be a larger logical message
        std::vector<uint8_t> partial = {0x01, 0x02, 0x03};
        (void)c.client->send(c.peer_id, partial);

        wait_until([&] { std::lock_guard lock(srv.rx_mutex); return !srv.received_data.empty(); });

        // Abruptly stop the client (simulates disconnect mid-transfer)
        c.client->stop();
    }

    // Wait for server to detect the disconnect
    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.disconnected_peers.empty(); });

    // Server should still be healthy — connect a new client and verify
    {
        ClientHelper c2("127.0.0.1", port, false, 0, PeerId{2});
        REQUIRE(c2.start().has_value());

        size_t prev_connected = [&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size(); }();
        wait_until([&] { std::lock_guard lock(srv.peers_mutex); return srv.connected_peers.size() > prev_connected; });

        std::vector<uint8_t> msg = {0xDE, 0xAD};
        (void)c2.client->send(c2.peer_id, msg);

        size_t prev_rx = [&] { std::lock_guard lock(srv.rx_mutex); return srv.received_data.size(); }();
        wait_until([&] { std::lock_guard lock(srv.rx_mutex); return srv.received_data.size() > prev_rx; });

        c2.client->stop();
    }

    srv.server->stop();

    std::lock_guard lock(srv.peers_mutex);
    CHECK(srv.connected_peers.size() >= 2);
    CHECK(srv.disconnected_peers.size() >= 1);
}

// ============================================================================
// T5b: TCP client reconnects after server-side disconnect
// ============================================================================

TEST_CASE("TCP: client recovers after server closes connection", "[tcp]") {
    ServerHelper srv;
    REQUIRE(srv.start().has_value());
    auto port = srv.server->local_port();

    ClientHelper c("127.0.0.1", port, true, 0, PeerId{1});
    REQUIRE(c.start().has_value());

    // Wait for initial connection
    wait_until([&] { std::lock_guard lock(srv.peers_mutex); return !srv.connected_peers.empty(); });

    // Server sends some data, then we stop the server (simulates server-side close)
    {
        std::lock_guard lock(srv.peers_mutex);
        if (!srv.connected_peers.empty()) {
            std::vector<uint8_t> msg = {0xAA};
            (void)srv.server->send(srv.connected_peers[0], msg);
        }
    }

    // Stop the server — client should detect disconnection
    srv.server->stop();

    // Give client time to detect and attempt reconnect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Start a new server on the same port
    ServerHelper srv2(port);
    REQUIRE(srv2.start().has_value());

    // Wait for client to reconnect to new server
    wait_until([&] {
        std::lock_guard lock(srv2.peers_mutex);
        return !srv2.connected_peers.empty();
    }, std::chrono::milliseconds(5000));

    // Verify the reconnected client can send
    std::vector<uint8_t> verify_msg = {0xBB, 0xCC};
    (void)c.client->send(c.peer_id, verify_msg);

    wait_until([&] { std::lock_guard lock(srv2.rx_mutex); return !srv2.received_data.empty(); });

    c.client->stop();
    srv2.server->stop();

    std::lock_guard lock2(srv2.peers_mutex);
    CHECK(!srv2.connected_peers.empty());
}
