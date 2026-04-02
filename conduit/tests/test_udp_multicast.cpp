// SPDX-License-Identifier: MIT
// Conduit - UDP Multicast Transport Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/peer.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// PID-based port allocation with offset to avoid collision with unicast UDP tests.
static uint16_t mcast_base_port() {
    static const uint16_t base = 40000 + static_cast<uint16_t>(
#ifdef _WIN32
        _getpid()
#else
        getpid()
#endif
        % 20000);
    return base;
}

static constexpr const char* TEST_MCAST_GROUP = "239.255.0.1";

// ============================================================================
// Helpers
// ============================================================================

// Probe whether the OS allows joining a multicast group.
// CI runners (containers, restricted VMs) may not support IP_ADD_MEMBERSHIP.
static bool multicast_available() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    auto sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { cached = 0; return false; }
#else
    auto sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { cached = 0; return false; }
#endif

    // Bind to an ephemeral port (required before IP_ADD_MEMBERSHIP on some OSes)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
#ifdef _WIN32
    ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#else
    ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif

    struct ip_mreq mreq{};
    inet_pton(AF_INET, TEST_MCAST_GROUP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

#ifdef _WIN32
    int ok = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                        reinterpret_cast<const char*>(&mreq), sizeof(mreq));
    ::closesocket(sock);
#else
    int ok = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    ::close(sock);
#endif

    cached = (ok == 0) ? 1 : 0;
    return cached != 0;
}

static TransportCallbacks make_collecting_callbacks(
    std::vector<uint8_t>& received,
    std::mutex& mtx,
    std::atomic<uint32_t>& next_id)
{
    TransportCallbacks cb;
    cb.on_data_received = [&](PeerId, std::span<const uint8_t> data) {
        std::lock_guard lock(mtx);
        received.assign(data.begin(), data.end());
    };
    cb.on_peer_connected = [&](std::string) -> PeerId {
        return PeerId{next_id.fetch_add(1)};
    };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};
    return cb;
}

// ============================================================================
// Happy-path tests (skipped when multicast is not available)
// ============================================================================

TEST_CASE("UDP multicast: send/receive loopback", "[udp][multicast]") {
    if (!multicast_available()) SKIP("Multicast not available on this host");

    uint16_t port = mcast_base_port();

    UdpConfig cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.bind_port = port;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_loop = true;
    cfg.multicast_ttl = 1;

    UdpTransport transport(cfg);

    std::vector<uint8_t> received;
    std::mutex mtx;
    std::atomic<uint32_t> next_id{1};

    auto cb = make_collecting_callbacks(received, mtx, next_id);
    auto r = transport.start(std::move(cb));
    REQUIRE(r.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send to the multicast group — with loopback enabled, we should receive it
    std::vector<uint8_t> msg = {0xCA, 0xFE, 0xBA, 0xBE};
    auto sr = transport.send(PeerId{1}, msg);
    REQUIRE(sr.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    transport.stop();

    std::lock_guard lock(mtx);
    CHECK(received == msg);
}

TEST_CASE("UDP multicast: two receivers get same datagram", "[udp][multicast]") {
    if (!multicast_available()) SKIP("Multicast not available on this host");

    uint16_t port = static_cast<uint16_t>(mcast_base_port() + 1);

    UdpConfig cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.bind_port = port;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_loop = true;
    cfg.multicast_ttl = 1;

    UdpTransport receiver1(cfg);
    UdpTransport receiver2(cfg);

    std::vector<uint8_t> recv1, recv2;
    std::mutex mtx1, mtx2;
    std::atomic<uint32_t> id1{10}, id2{20};

    auto cb1 = make_collecting_callbacks(recv1, mtx1, id1);
    auto cb2 = make_collecting_callbacks(recv2, mtx2, id2);

    auto r1 = receiver1.start(std::move(cb1));
    auto r2 = receiver2.start(std::move(cb2));
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send from receiver1 (both should get it via loopback + multicast)
    std::vector<uint8_t> msg = {0x01, 0x02, 0x03};
    auto sr = receiver1.send(PeerId{10}, msg);
    REQUIRE(sr.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    receiver1.stop();
    receiver2.stop();

    {
        std::lock_guard lock(mtx1);
        CHECK(recv1 == msg);
    }
    {
        std::lock_guard lock(mtx2);
        CHECK(recv2 == msg);
    }
}

TEST_CASE("UDP multicast: peer tracking via on_peer_connected", "[udp][multicast]") {
    if (!multicast_available()) SKIP("Multicast not available on this host");

    uint16_t port = static_cast<uint16_t>(mcast_base_port() + 2);

    UdpConfig cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.bind_port = port;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_loop = true;

    UdpTransport transport(cfg);

    std::atomic<int> peer_count{0};
    std::string last_endpoint;
    std::mutex ep_mutex;

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [&](std::string endpoint) -> PeerId {
        {
            std::lock_guard lock(ep_mutex);
            last_endpoint = std::move(endpoint);
        }
        return PeerId{static_cast<uint32_t>(peer_count.fetch_add(1) + 1)};
    };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    REQUIRE(r.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send a datagram so the I/O loop sees a sender
    std::vector<uint8_t> msg = {0xAA};
    (void)transport.send(PeerId{1}, msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    transport.stop();

    // Should have registered at least one peer (ourselves via loopback)
    CHECK(peer_count.load() >= 1);
    {
        std::lock_guard lock(ep_mutex);
        CHECK(!last_endpoint.empty());
    }
}

TEST_CASE("UDP multicast: custom TTL and interface", "[udp][multicast]") {
    if (!multicast_available()) SKIP("Multicast not available on this host");

    uint16_t port = static_cast<uint16_t>(mcast_base_port() + 3);

    UdpConfig cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.bind_port = port;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_ttl = 4;
    cfg.multicast_interface = "0.0.0.0";
    cfg.multicast_loop = true;

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    CHECK(r.has_value());

    transport.stop();
}

TEST_CASE("UDP multicast: stop leaves group cleanly", "[udp][multicast]") {
    if (!multicast_available()) SKIP("Multicast not available on this host");

    uint16_t port = static_cast<uint16_t>(mcast_base_port() + 4);

    UdpConfig cfg;
    cfg.bind_address = "0.0.0.0";
    cfg.bind_port = port;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_loop = true;

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    REQUIRE(r.has_value());

    transport.stop();

    // Should be able to start again after stop (group was properly left)
    TransportCallbacks cb2;
    cb2.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb2.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb2.on_peer_disconnected = [](PeerId) {};
    cb2.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r2 = transport.start(std::move(cb2));
    CHECK(r2.has_value());

    transport.stop();
}

TEST_CASE("UDP multicast: is_multi_peer returns true", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.bind_port = 5000;

    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == true);
}

TEST_CASE("UDP multicast: is_multi_peer true even with remote_address set", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;
    cfg.bind_port = 5000;

    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == true);
}

// ============================================================================
// Error-path tests (no multicast I/O needed — test validation logic only)
// ============================================================================

TEST_CASE("UDP multicast: non-multicast address rejected", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.bind_port = static_cast<uint16_t>(mcast_base_port() + 10);
    cfg.multicast_group = "192.168.1.1";  // Not a multicast address

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    CHECK(!r.has_value());
}

TEST_CASE("UDP multicast: malformed group address rejected", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.bind_port = static_cast<uint16_t>(mcast_base_port() + 11);
    cfg.multicast_group = "not-an-ip";

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    CHECK(!r.has_value());
}

TEST_CASE("UDP multicast: bind_port 0 rejected", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.bind_port = 0;
    cfg.multicast_group = TEST_MCAST_GROUP;

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    CHECK(!r.has_value());
}

TEST_CASE("UDP multicast: invalid interface address rejected", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.bind_port = static_cast<uint16_t>(mcast_base_port() + 12);
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.multicast_interface = "bad-iface";

    UdpTransport transport(cfg);

    TransportCallbacks cb;
    cb.on_data_received = [](PeerId, std::span<const uint8_t>) {};
    cb.on_peer_connected = [](std::string) -> PeerId { return PeerId{1}; };
    cb.on_peer_disconnected = [](PeerId) {};
    cb.on_state_changed = [](PeerId, net::ConnectionState) {};

    auto r = transport.start(std::move(cb));
    CHECK(!r.has_value());
}
