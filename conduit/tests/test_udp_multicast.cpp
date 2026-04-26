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
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
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

// Probe whether multicast loopback actually delivers data.
// Some CI runners allow IP_ADD_MEMBERSHIP but never deliver loopback packets.
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

    // Enable SO_REUSEADDR and bind to an ephemeral port
    int one = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        cached = 0; return false;
    }

    // Retrieve the bound port
    socklen_t alen = sizeof(addr);
    getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &alen);

    // Join multicast group
    struct ip_mreq mreq{};
    inet_pton(AF_INET, TEST_MCAST_GROUP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
#ifdef _WIN32
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0) {
        ::closesocket(sock); cached = 0; return false;
    }
    // Enable multicast loopback
    int loop = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
               reinterpret_cast<const char*>(&loop), sizeof(loop));
#else
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        ::close(sock); cached = 0; return false;
    }
    // Enable multicast loopback
    unsigned char loop = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
#endif

    // Send a test packet to the multicast group on the bound port
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    inet_pton(AF_INET, TEST_MCAST_GROUP, &dest.sin_addr);
    dest.sin_port = addr.sin_port;

    const char probe[] = "mcast_probe";
    sendto(sock, probe, sizeof(probe), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

    // Wait briefly for loopback delivery
#ifdef _WIN32
    DWORD tv = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{0, 500000}; // 500ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    char buf[64]{};
    auto n = recv(sock, buf, sizeof(buf), 0);

#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif

    cached = (n > 0) ? 1 : 0;
    return cached != 0;
}

// Probe whether multicast loopback fans out to multiple sockets bound to the
// same port via SO_REUSEADDR/SO_REUSEPORT. Containerised CI runners often
// allow basic loopback (single socket) but drop the second copy.
static bool multicast_fanout_available() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    if (!multicast_available()) { cached = 0; return false; }

    auto open_recv = [](sockaddr_in& bound) -> int {
#ifdef _WIN32
        auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return -1;
#else
        auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0) return -1;
#endif
        int one = 1;
#ifdef _WIN32
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&one), sizeof(one));
#else
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    #ifdef SO_REUSEPORT
        setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    #endif
#endif
        if (::bind(s, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)) != 0) {
#ifdef _WIN32
            ::closesocket(s);
#else
            ::close(s);
#endif
            return -1;
        }
        struct ip_mreq mreq{};
        inet_pton(AF_INET, TEST_MCAST_GROUP, &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
#ifdef _WIN32
        setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&mreq), sizeof(mreq));
        int loop = 1;
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
                   reinterpret_cast<const char*>(&loop), sizeof(loop));
        DWORD rcvto = 500;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&rcvto), sizeof(rcvto));
#else
        setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        unsigned char loop = 1;
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
        struct timeval tv{0, 500000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        return s;
    };

    sockaddr_in bound{};
    bound.sin_family = AF_INET;
    bound.sin_addr.s_addr = htonl(INADDR_ANY);
    bound.sin_port = 0;  // Let kernel pick for the first socket

    int s1 = open_recv(bound);
    if (s1 < 0) { cached = 0; return false; }

    socklen_t alen = sizeof(bound);
    getsockname(s1, reinterpret_cast<sockaddr*>(&bound), &alen);

    int s2 = open_recv(bound);  // Bind to the same port as s1
    if (s2 < 0) {
#ifdef _WIN32
        ::closesocket(s1);
#else
        ::close(s1);
#endif
        cached = 0; return false;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    inet_pton(AF_INET, TEST_MCAST_GROUP, &dest.sin_addr);
    dest.sin_port = bound.sin_port;
    const char probe[] = "fanout";
    sendto(s1, probe, sizeof(probe), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

    char buf[64];
    auto n1 = recv(s1, buf, sizeof(buf), 0);
    auto n2 = recv(s2, buf, sizeof(buf), 0);

#ifdef _WIN32
    ::closesocket(s1);
    ::closesocket(s2);
#else
    ::close(s1);
    ::close(s2);
#endif

    cached = (n1 > 0 && n2 > 0) ? 1 : 0;
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
    if (!multicast_fanout_available())
        SKIP("Multicast SO_REUSEPORT fan-out not supported on this host");

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

    // Poll for delivery to both receivers instead of one fixed sleep — slow
    // CI runners would otherwise flake.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        bool got1, got2;
        { std::lock_guard l(mtx1); got1 = (recv1 == msg); }
        { std::lock_guard l(mtx2); got2 = (recv2 == msg); }
        if (got1 && got2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

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

TEST_CASE("UDP multicast: is_multi_peer returns false (group is the peer)", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.bind_port = 5000;

    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == false);
}

TEST_CASE("UDP multicast: is_multi_peer false even with remote_address set", "[udp][multicast]") {
    UdpConfig cfg;
    cfg.multicast_group = TEST_MCAST_GROUP;
    cfg.remote_address = "127.0.0.1";
    cfg.remote_port = 5000;
    cfg.bind_port = 5000;

    UdpTransport transport(cfg);
    CHECK(transport.is_multi_peer() == false);
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
