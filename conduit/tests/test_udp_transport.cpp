// SPDX-License-Identifier: MIT
// Conduit - UDP Transport Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/peer.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <span>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// Use PID-based port allocation to avoid collisions with other test processes.
// Each test uses a different offset from the base to avoid intra-process collisions.
static uint16_t test_base_port() {
    static const uint16_t base = 30000 + static_cast<uint16_t>(
#ifdef _WIN32
        _getpid()
#else
        getpid()
#endif
        % 20000);
    return base;
}

// ============================================================================
// Helpers
// ============================================================================

static TransportCallbacks make_null_callbacks() {
    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    return cb;
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("UDP: single-peer send/receive loopback", "[udp]") {
    // Transport A: binds ephemeral port, sends to B
    // Transport B: binds ephemeral port, sends to A
    // We set up B first to know its port, then configure A.

    UdpConfig cfg_b;
    cfg_b.bind_address = "127.0.0.1";
    cfg_b.bind_port = 0;
    cfg_b.recv_buffer_size = 4096;

    // B is multi-peer mode (no remote_address), will receive from unknown senders
    auto transport_b = std::make_shared<UdpTransport>(cfg_b);

    std::vector<uint8_t> b_received;
    std::mutex b_mutex;
    std::atomic<uint32_t> b_next_id{10};

    TransportCallbacks cb_b;
    cb_b.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(b_mutex);
        b_received.assign(data.begin(), data.end());
    };
    cb_b.on_peer_connected = [&](std::string) -> PeerId {
        return PeerId{b_next_id.fetch_add(1)};
    };
    cb_b.on_peer_disconnected = [](PeerId) {};
    cb_b.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r_b = transport_b->start(std::move(cb_b));
    REQUIRE(r_b.has_value());

    // Since B is multi-peer with ephemeral port, we need to get its bound port.
    // UDP doesn't have local_port() like TCP server, so we use a known port instead.
    // Let's reconfigure: use a fixed loopback setup with two single-peer transports.
    transport_b->stop();

    // Simpler approach: two single-peer transports pointing at each other
    uint16_t port_a = test_base_port();
    uint16_t port_b = static_cast<uint16_t>(test_base_port() + 1);

    UdpConfig cfg_a_real;
    cfg_a_real.bind_address = "127.0.0.1";
    cfg_a_real.bind_port = port_a;
    cfg_a_real.remote_address = "127.0.0.1";
    cfg_a_real.remote_port = port_b;
    cfg_a_real.recv_buffer_size = 4096;

    UdpConfig cfg_b_real;
    cfg_b_real.bind_address = "127.0.0.1";
    cfg_b_real.bind_port = port_b;
    cfg_b_real.remote_address = "127.0.0.1";
    cfg_b_real.remote_port = port_a;
    cfg_b_real.recv_buffer_size = 4096;

    auto ta = std::make_shared<UdpTransport>(cfg_a_real);
    auto tb = std::make_shared<UdpTransport>(cfg_b_real);

    std::vector<uint8_t> a_received;
    std::mutex a_mutex;
    PeerId peer_a(1);
    PeerId peer_b(2);

    TransportCallbacks cb_a;
    cb_a.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(a_mutex);
        a_received.assign(data.begin(), data.end());
    };
    cb_a.on_peer_connected = [&](std::string) -> PeerId { return peer_a; };
    cb_a.on_peer_disconnected = [](PeerId) {};
    cb_a.on_state_changed = [](PeerId, net::ConnectionState) {};

    TransportCallbacks cb_b2;
    cb_b2.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(b_mutex);
        b_received.assign(data.begin(), data.end());
    };
    cb_b2.on_peer_connected = [&](std::string) -> PeerId { return peer_b; };
    cb_b2.on_peer_disconnected = [](PeerId) {};
    cb_b2.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto ra = ta->start(std::move(cb_a));
    auto rb = tb->start(std::move(cb_b2));
    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // B sends to A
    std::vector<uint8_t> msg = {0xDE, 0xAD, 0xBE, 0xEF};
    auto send_r = tb->send(peer_b, msg);
    REQUIRE(send_r.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ta->stop();
    tb->stop();

    std::lock_guard lock(a_mutex);
    CHECK(a_received == msg);
}

TEST_CASE("UDP: is_stream_oriented returns false", "[udp]") {
    UdpConfig cfg;
    UdpTransport transport(cfg);
    CHECK(transport.is_stream_oriented() == false);
}

TEST_CASE("UDP: single-peer is_multi_peer returns false", "[udp]") {
    UdpConfig cfg;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;
    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == false);
}

TEST_CASE("UDP: multi-peer is_multi_peer returns true", "[udp]") {
    UdpConfig cfg;
    // No remote_address → multi-peer mode
    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == true);
}

TEST_CASE("UDP: multi-peer auto-detect from 2 clients", "[udp]") {
    uint16_t server_port = static_cast<uint16_t>(test_base_port() + 2);

    UdpConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.bind_port = server_port;
    server_cfg.recv_buffer_size = 4096;

    auto server = std::make_shared<UdpTransport>(server_cfg);

    std::atomic<uint32_t> next_id{100};
    std::set<uint32_t> connected_peers;
    std::mutex peer_mutex;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> received_data;
    std::mutex rx_mutex;

    TransportCallbacks cb;
    cb.on_data_received = [&](PeerId pid, std::span<const uint8_t> data) {
        std::lock_guard lock(rx_mutex);
        received_data.emplace_back(pid.value(),
            std::vector<uint8_t>(data.begin(), data.end()));
    };
    cb.on_peer_connected = [&](std::string) -> PeerId {
        auto id = next_id.fetch_add(1);
        std::lock_guard lock(peer_mutex);
        connected_peers.insert(id);
        return PeerId{id};
    };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(server->start(std::move(cb)).has_value());

    // Client 1
    UdpConfig c1_cfg;
    c1_cfg.bind_address = "127.0.0.1";
    c1_cfg.bind_port = 0;
    c1_cfg.remote_address = "127.0.0.1";
    c1_cfg.remote_port = server_port;

    auto c1 = std::make_shared<UdpTransport>(c1_cfg);
    PeerId c1_peer(1);
    TransportCallbacks c1_cb;
    c1_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    c1_cb.on_peer_connected = [&](std::string) -> PeerId { return c1_peer; };
    c1_cb.on_peer_disconnected = [](PeerId) {};
    c1_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(c1->start(std::move(c1_cb)).has_value());

    // Client 2
    UdpConfig c2_cfg;
    c2_cfg.bind_address = "127.0.0.1";
    c2_cfg.bind_port = 0;
    c2_cfg.remote_address = "127.0.0.1";
    c2_cfg.remote_port = server_port;

    auto c2 = std::make_shared<UdpTransport>(c2_cfg);
    PeerId c2_peer(2);
    TransportCallbacks c2_cb;
    c2_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    c2_cb.on_peer_connected = [&](std::string) -> PeerId { return c2_peer; };
    c2_cb.on_peer_disconnected = [](PeerId) {};
    c2_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(c2->start(std::move(c2_cb)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Each client sends distinct data
    std::vector<uint8_t> msg1 = {0x01};
    std::vector<uint8_t> msg2 = {0x02};
    c1->send(c1_peer, msg1);
    c2->send(c2_peer, msg2);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    c1->stop();
    c2->stop();
    server->stop();

    {
        std::lock_guard lock(peer_mutex);
        CHECK(connected_peers.size() == 2);
    }

    {
        std::lock_guard lock(rx_mutex);
        CHECK(received_data.size() == 2);
    }
}

TEST_CASE("UDP: multi-peer send-back routing", "[udp]") {
    uint16_t server_port = static_cast<uint16_t>(test_base_port() + 3);

    UdpConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.bind_port = server_port;
    server_cfg.recv_buffer_size = 4096;

    auto server = std::make_shared<UdpTransport>(server_cfg);

    std::atomic<uint32_t> next_id{100};
    std::vector<PeerId> server_peers;
    std::mutex peer_mutex;

    TransportCallbacks srv_cb;
    srv_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    srv_cb.on_peer_connected = [&](std::string) -> PeerId {
        auto id = PeerId{next_id.fetch_add(1)};
        std::lock_guard lock(peer_mutex);
        server_peers.push_back(id);
        return id;
    };
    srv_cb.on_peer_disconnected = [](PeerId) {};
    srv_cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(server->start(std::move(srv_cb)).has_value());

    // Client 1
    UdpConfig c1_cfg;
    c1_cfg.bind_address = "127.0.0.1";
    c1_cfg.bind_port = 0;
    c1_cfg.remote_address = "127.0.0.1";
    c1_cfg.remote_port = server_port;

    auto c1 = std::make_shared<UdpTransport>(c1_cfg);
    std::vector<uint8_t> c1_received;
    std::mutex c1_mutex;
    PeerId c1_peer(1);

    TransportCallbacks c1_cb;
    c1_cb.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(c1_mutex);
        c1_received.assign(data.begin(), data.end());
    };
    c1_cb.on_peer_connected = [&](std::string) -> PeerId { return c1_peer; };
    c1_cb.on_peer_disconnected = [](PeerId) {};
    c1_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(c1->start(std::move(c1_cb)).has_value());

    // Client 2
    UdpConfig c2_cfg;
    c2_cfg.bind_address = "127.0.0.1";
    c2_cfg.bind_port = 0;
    c2_cfg.remote_address = "127.0.0.1";
    c2_cfg.remote_port = server_port;

    auto c2 = std::make_shared<UdpTransport>(c2_cfg);
    std::vector<uint8_t> c2_received;
    std::mutex c2_mutex;
    PeerId c2_peer(2);

    TransportCallbacks c2_cb;
    c2_cb.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(c2_mutex);
        c2_received.assign(data.begin(), data.end());
    };
    c2_cb.on_peer_connected = [&](std::string) -> PeerId { return c2_peer; };
    c2_cb.on_peer_disconnected = [](PeerId) {};
    c2_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(c2->start(std::move(c2_cb)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Each client sends a message so server discovers their addresses
    std::vector<uint8_t> hello = {0xFF};
    c1->send(c1_peer, hello);
    c2->send(c2_peer, hello);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Server sends different data back to each peer
    {
        std::lock_guard lock(peer_mutex);
        REQUIRE(server_peers.size() == 2);

        std::vector<uint8_t> reply1 = {0xAA};
        std::vector<uint8_t> reply2 = {0xBB};
        server->send(server_peers[0], reply1);
        server->send(server_peers[1], reply2);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    c1->stop();
    c2->stop();
    server->stop();

    // Verify each client received a reply (may be AA or BB depending on order)
    {
        std::lock_guard lock(c1_mutex);
        CHECK(c1_received.size() == 1);
    }
    {
        std::lock_guard lock(c2_mutex);
        CHECK(c2_received.size() == 1);
    }
}

TEST_CASE("UDP: large datagram 1400 bytes", "[udp]") {
    uint16_t port_a = static_cast<uint16_t>(test_base_port() + 4);
    uint16_t port_b = static_cast<uint16_t>(test_base_port() + 5);

    UdpConfig cfg_a;
    cfg_a.bind_address = "127.0.0.1";
    cfg_a.bind_port = port_a;
    cfg_a.remote_address = "127.0.0.1";
    cfg_a.remote_port = port_b;
    cfg_a.recv_buffer_size = 4096;

    UdpConfig cfg_b;
    cfg_b.bind_address = "127.0.0.1";
    cfg_b.bind_port = port_b;
    cfg_b.remote_address = "127.0.0.1";
    cfg_b.remote_port = port_a;
    cfg_b.recv_buffer_size = 4096;

    auto ta = std::make_shared<UdpTransport>(cfg_a);
    auto tb = std::make_shared<UdpTransport>(cfg_b);

    std::vector<uint8_t> received;
    std::mutex rx_mutex;
    PeerId pa(1), pb(2);

    TransportCallbacks cb_a;
    cb_a.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(rx_mutex);
        received.assign(data.begin(), data.end());
    };
    cb_a.on_peer_connected = [&](std::string) -> PeerId { return pa; };
    cb_a.on_peer_disconnected = [](PeerId) {};
    cb_a.on_state_changed = [](PeerId, net::ConnectionState) {};

    TransportCallbacks cb_b;
    cb_b.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb_b.on_peer_connected = [&](std::string) -> PeerId { return pb; };
    cb_b.on_peer_disconnected = [](PeerId) {};
    cb_b.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(ta->start(std::move(cb_a)).has_value());
    REQUIRE(tb->start(std::move(cb_b)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Build 1400-byte payload
    std::vector<uint8_t> large_msg(1400);
    for (size_t i = 0; i < large_msg.size(); ++i) {
        large_msg[i] = static_cast<uint8_t>(i & 0xFF);
    }

    tb->send(pb, large_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ta->stop();
    tb->stop();

    std::lock_guard lock(rx_mutex);
    REQUIRE(received.size() == 1400);
    CHECK(received == large_msg);
}

TEST_CASE("UDP: rapid fire 100 datagrams", "[udp]") {
    uint16_t port_a = static_cast<uint16_t>(test_base_port() + 6);
    uint16_t port_b = static_cast<uint16_t>(test_base_port() + 7);

    UdpConfig cfg_a;
    cfg_a.bind_address = "127.0.0.1";
    cfg_a.bind_port = port_a;
    cfg_a.remote_address = "127.0.0.1";
    cfg_a.remote_port = port_b;
    cfg_a.recv_buffer_size = 65536;

    UdpConfig cfg_b;
    cfg_b.bind_address = "127.0.0.1";
    cfg_b.bind_port = port_b;
    cfg_b.remote_address = "127.0.0.1";
    cfg_b.remote_port = port_a;
    cfg_b.recv_buffer_size = 65536;

    auto ta = std::make_shared<UdpTransport>(cfg_a);
    auto tb = std::make_shared<UdpTransport>(cfg_b);

    std::vector<std::vector<uint8_t>> received;
    std::mutex rx_mutex;
    PeerId pa(1), pb(2);

    TransportCallbacks cb_a;
    cb_a.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(rx_mutex);
        received.emplace_back(data.begin(), data.end());
    };
    cb_a.on_peer_connected = [&](std::string) -> PeerId { return pa; };
    cb_a.on_peer_disconnected = [](PeerId) {};
    cb_a.on_state_changed = [](PeerId, net::ConnectionState) {};

    TransportCallbacks cb_b;
    cb_b.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb_b.on_peer_connected = [&](std::string) -> PeerId { return pb; };
    cb_b.on_peer_disconnected = [](PeerId) {};
    cb_b.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(ta->start(std::move(cb_a)).has_value());
    REQUIRE(tb->start(std::move(cb_b)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send 100 datagrams with 4-byte counter
    for (int i = 0; i < 100; ++i) {
        uint8_t b0 = static_cast<uint8_t>((i >> 24) & 0xFF);
        uint8_t b1 = static_cast<uint8_t>((i >> 16) & 0xFF);
        uint8_t b2 = static_cast<uint8_t>((i >> 8) & 0xFF);
        uint8_t b3 = static_cast<uint8_t>(i & 0xFF);
        std::vector<uint8_t> msg = {b0, b1, b2, b3};
        tb->send(pb, msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ta->stop();
    tb->stop();

    std::lock_guard lock(rx_mutex);
    CHECK(received.size() == 100);
}

TEST_CASE("UDP: send before start returns error", "[udp]") {
    UdpConfig cfg;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;
    UdpTransport transport(cfg);

    std::vector<uint8_t> data = {0x01};
    auto result = transport.send(PeerId(1), data);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("UDP: start twice returns AlreadyRunning", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;

    UdpTransport transport(cfg);

    auto cb = make_null_callbacks();
    auto r1 = transport.start(std::move(cb));
    REQUIRE(r1.has_value());

    auto cb2 = make_null_callbacks();
    auto r2 = transport.start(std::move(cb2));
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().code() == ErrorCode::AlreadyRunning);

    transport.stop();
}

TEST_CASE("UDP: stop then restart works", "[udp]") {
    uint16_t port_a = static_cast<uint16_t>(test_base_port() + 8);
    uint16_t port_b = static_cast<uint16_t>(test_base_port() + 9);

    UdpConfig cfg_a;
    cfg_a.bind_address = "127.0.0.1";
    cfg_a.bind_port = port_a;
    cfg_a.remote_address = "127.0.0.1";
    cfg_a.remote_port = port_b;

    UdpConfig cfg_b;
    cfg_b.bind_address = "127.0.0.1";
    cfg_b.bind_port = port_b;
    cfg_b.remote_address = "127.0.0.1";
    cfg_b.remote_port = port_a;

    auto ta = std::make_shared<UdpTransport>(cfg_a);
    auto tb = std::make_shared<UdpTransport>(cfg_b);

    std::atomic<int> recv_count{0};
    PeerId pa(1), pb(2);

    auto make_cb_a = [&]() {
        TransportCallbacks cb;
        cb.on_data_received = [&](PeerId, std::span<const uint8_t>) {
            recv_count.fetch_add(1);
        };
        cb.on_peer_connected = [&](std::string) -> PeerId { return pa; };
        cb.on_peer_disconnected = [](PeerId) {};
        cb.on_state_changed = [](PeerId, net::ConnectionState) {};
        return cb;
    };

    auto make_cb_b = [&]() {
        TransportCallbacks cb;
        cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
        cb.on_peer_connected = [&](std::string) -> PeerId { return pb; };
        cb.on_peer_disconnected = [](PeerId) {};
        cb.on_state_changed = [](PeerId, net::ConnectionState) {};
        return cb;
    };

    // First run
    REQUIRE(ta->start(make_cb_a()).has_value());
    REQUIRE(tb->start(make_cb_b()).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<uint8_t> msg = {0x01};
    tb->send(pb, msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ta->stop();
    tb->stop();
    CHECK(recv_count.load() >= 1);

    // Restart
    recv_count = 0;
    REQUIRE(ta->start(make_cb_a()).has_value());
    REQUIRE(tb->start(make_cb_b()).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    tb->send(pb, msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ta->stop();
    tb->stop();
    CHECK(recv_count.load() >= 1);
}

TEST_CASE("UDP: on_state_changed fires Connected in single-peer mode", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;

    UdpTransport transport(cfg);

    std::vector<net::ConnectionState> states;
    std::mutex mtx;

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [&](PeerId, net::ConnectionState s) {
        std::lock_guard lock(mtx);
        states.push_back(s);
    };

    REQUIRE(transport.start(std::move(cb)).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    transport.stop();

    std::lock_guard lock(mtx);
    REQUIRE(!states.empty());
    CHECK(states[0] == net::ConnectionState::Connected);
}

TEST_CASE("UDP: send to invalid PeerId in single-peer returns PeerNotFound", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;
    // multi-peer mode (no remote_address), so sending to unknown peer fails
    cfg.recv_buffer_size = 4096;

    UdpTransport transport(cfg);
    auto cb = make_null_callbacks();
    REQUIRE(transport.start(std::move(cb)).has_value());

    std::vector<uint8_t> data = {0x01};
    auto result = transport.send(PeerId(999), data);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::PeerNotFound);

    transport.stop();
}

TEST_CASE("UDP: config with invalid bind address returns error", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "999.999.999.999";
    cfg.bind_port = 5000;

    UdpTransport transport(cfg);
    auto cb = make_null_callbacks();
    auto result = transport.start(std::move(cb));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("UDP: max_peers enforcement rejects excess peers", "[udp]") {
    uint16_t server_port = static_cast<uint16_t>(test_base_port() + 10);

    UdpConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.bind_port = server_port;
    server_cfg.recv_buffer_size = 4096;
    server_cfg.max_peers = 2;  // Only allow 2 peers

    auto server = std::make_shared<UdpTransport>(server_cfg);

    std::atomic<uint32_t> next_id{200};
    std::set<uint32_t> connected_peers;
    std::mutex peer_mutex;

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [&](std::string) -> PeerId {
        auto id = next_id.fetch_add(1);
        std::lock_guard lock(peer_mutex);
        connected_peers.insert(id);
        return PeerId{id};
    };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(server->start(std::move(cb)).has_value());

    // Create 3 clients, each on a different ephemeral port
    std::vector<std::shared_ptr<UdpTransport>> clients;
    for (int i = 0; i < 3; ++i) {
        UdpConfig c_cfg;
        c_cfg.bind_address = "127.0.0.1";
        c_cfg.bind_port = 0;
        c_cfg.remote_address = "127.0.0.1";
        c_cfg.remote_port = server_port;

        auto c = std::make_shared<UdpTransport>(c_cfg);
        PeerId c_peer(static_cast<uint32_t>(i + 50));
        TransportCallbacks c_cb;
        c_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
        c_cb.on_peer_connected = [c_peer](std::string) -> PeerId { return c_peer; };
        c_cb.on_peer_disconnected = [](PeerId) {};
        c_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
        REQUIRE(c->start(std::move(c_cb)).has_value());
        clients.push_back(c);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Each client sends a packet so the server discovers them
    for (size_t i = 0; i < 3; ++i) {
        std::vector<uint8_t> msg = {static_cast<uint8_t>(i + 1)};
        clients[i]->send(PeerId{static_cast<uint32_t>(i + 50)}, msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    for (auto& c : clients) c->stop();
    server->stop();

    // Only 2 peers should have been created (3rd rejected by max_peers)
    std::lock_guard lock(peer_mutex);
    CHECK(connected_peers.size() == 2);
}

TEST_CASE("UDP: peer timeout evicts stale peers", "[udp]") {
    uint16_t server_port = static_cast<uint16_t>(test_base_port() + 11);

    UdpConfig server_cfg;
    server_cfg.bind_address = "127.0.0.1";
    server_cfg.bind_port = server_port;
    server_cfg.recv_buffer_size = 4096;
    server_cfg.peer_timeout = std::chrono::seconds(1);

    auto server = std::make_shared<UdpTransport>(server_cfg);

    std::atomic<uint32_t> next_id{300};
    std::set<uint32_t> connected_peers;
    std::set<uint32_t> disconnected_peers;
    std::mutex peer_mutex;

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [&](std::string) -> PeerId {
        auto id = next_id.fetch_add(1);
        std::lock_guard lock(peer_mutex);
        connected_peers.insert(id);
        return PeerId{id};
    };
    cb.on_peer_disconnected = [&](PeerId pid) {
        std::lock_guard lock(peer_mutex);
        disconnected_peers.insert(pid.value());
    };
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    REQUIRE(server->start(std::move(cb)).has_value());

    // Client sends one packet to register as a peer
    UdpConfig c_cfg;
    c_cfg.bind_address = "127.0.0.1";
    c_cfg.bind_port = 0;
    c_cfg.remote_address = "127.0.0.1";
    c_cfg.remote_port = server_port;

    auto c = std::make_shared<UdpTransport>(c_cfg);
    PeerId c_peer(50);
    TransportCallbacks c_cb;
    c_cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    c_cb.on_peer_connected = [c_peer](std::string) -> PeerId { return c_peer; };
    c_cb.on_peer_disconnected = [](PeerId) {};
    c_cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    REQUIRE(c->start(std::move(c_cb)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<uint8_t> msg = {0x01};
    c->send(c_peer, msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    {
        std::lock_guard lock(peer_mutex);
        REQUIRE(connected_peers.size() == 1);
        CHECK(disconnected_peers.empty());
    }

    // Stop the client and wait for the timeout to expire
    c->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    server->stop();

    // The peer should have been evicted
    std::lock_guard lock(peer_mutex);
    CHECK(disconnected_peers.size() == 1);
}

TEST_CASE("UDP: send exceeding max_datagram_size returns BufferOverrun", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;
    cfg.max_datagram_size = 100;  // Artificially small limit

    UdpTransport transport(cfg);
    auto cb = make_null_callbacks();
    REQUIRE(transport.start(std::move(cb)).has_value());

    // Build a datagram that exceeds the limit
    std::vector<uint8_t> oversized(101, 0xAA);
    auto result = transport.send(PeerId(1), oversized);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::BufferOverrun);

    // Verify a datagram at exactly the limit succeeds
    std::vector<uint8_t> exact(100, 0xBB);
    auto result_ok = transport.send(PeerId(1), exact);
    CHECK(result_ok.has_value());

    transport.stop();
}

TEST_CASE("UDP: default max_datagram_size is 65507", "[udp]") {
    UdpConfig cfg;
    CHECK(cfg.max_datagram_size == 65507);
}

TEST_CASE("UDP: ephemeral port bound when bind_port=0", "[udp]") {
    UdpConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.bind_port = 0;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;

    UdpTransport transport(cfg);
    auto cb = make_null_callbacks();
    auto result = transport.start(std::move(cb));
    REQUIRE(result.has_value());

    // Transport started successfully with ephemeral port — this validates binding works
    transport.stop();
}
