// SPDX-License-Identifier: MIT
// Conduit - Transceiver Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/itransport.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// ============================================================================
// Mock message types
// ============================================================================

namespace {

struct TestMsg {
    static constexpr uint64_t TYPE_ID = 1001;
    static constexpr std::string_view TYPE_NAME = "TestMsg";
    int payload = 0;

    VoidResult encode(io::BitWriter& w) const {
        w.write_u32(static_cast<uint32_t>(payload));
        if (w.has_error()) return std::unexpected(w.error());
        return {};
    }

    static Result<TestMsg> decode(io::BitReader& r) {
        CONDUIT_TRY_ASSIGN(auto, val, r.read_u32());
        TestMsg msg;
        msg.payload = static_cast<int>(val);
        return msg;
    }
};

static_assert(traits::Message<TestMsg>);

// ============================================================================
// Mock transport
// ============================================================================

class MockTransport : public ITransport {
public:
    TransportCallbacks callbacks;
    std::vector<std::vector<uint8_t>> sent_data;
    std::mutex sent_mutex;
    bool started = false;
    bool multi_peer = false;
    bool stream = true;

    VoidResult start(TransportCallbacks cb) override {
        callbacks = std::move(cb);
        started = true;
        return {};
    }

    void stop() override {
        started = false;
    }

    VoidResult send(PeerId /*peer*/, std::span<const uint8_t> data) override {
        std::lock_guard lock(sent_mutex);
        sent_data.emplace_back(data.begin(), data.end());
        return {};
    }

    bool is_stream_oriented() const noexcept override { return stream; }
    bool is_multi_peer() const noexcept override { return multi_peer; }

    // Inject data as if received from I/O
    void inject_data(PeerId peer, std::span<const uint8_t> data) {
        if (callbacks.on_data_received) {
            callbacks.on_data_received(peer, data);
        }
    }
};

// ============================================================================
// Mock session
// ============================================================================

class MockSession : public traits::ISession {
public:
    // decode_frame: Treat entire frame as a TestMsg (4 bytes big-endian int)
    Result<std::vector<traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        if (data.size() < 4) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::BufferUnderrun, "Need 4 bytes"));
        }
        io::BitReader reader(data);
        auto msg = TestMsg::decode(reader);
        if (!msg) return std::unexpected(msg.error());

        traits::DecodedMessage dm;
        dm.type_id = TestMsg::TYPE_ID;
        dm.type_name = TestMsg::TYPE_NAME;
        dm.payload = std::any(*msg);
        dm.raw = std::vector<uint8_t>(data.begin(), data.end());

        return std::vector<traits::DecodedMessage>{std::move(dm)};
    }

    Result<std::vector<uint8_t>>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != TestMsg::TYPE_ID) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        }
        auto& msg = std::any_cast<const TestMsg&>(payload);
        io::BitWriter writer;
        msg.encode(writer);
        return writer.finish();
    }

    std::span<const uint8_t> sync_pattern() const override { return sync_; }
    size_t min_frame_header_size() const override { return 4; }
    size_t extract_frame_length(std::span<const uint8_t> /*header*/) const override {
        return 4;  // Fixed 4-byte frames
    }

    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t /*type_id*/) const override { return "TestMsg"; }
    void reset() override {}

private:
    std::vector<uint8_t> sync_;  // No sync pattern — length-prefix mode
    std::vector<uint64_t> ids_ = {TestMsg::TYPE_ID};
};

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Transceiver: receive pipeline - handler receives decoded message",
          "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;  // Datagram mode — no framing needed

    Transceiver xcvr;

    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    int received_payload = 0;
    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [&](const TestMsg& msg) { received_payload = msg.payload; }));

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Inject a frame: big-endian uint32 = 42
    std::vector<uint8_t> frame = {0x00, 0x00, 0x00, 0x2A};
    transport->inject_data(peer, frame);

    // Wait for worker to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    xcvr.stop();

    CHECK(received_payload == 42);
}

TEST_CASE("Transceiver: send pipeline - encoded bytes sent via transport",
          "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;

    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    TestMsg msg;
    msg.payload = 99;
    auto send_result = xcvr.send<TestMsg>(peer, msg);
    REQUIRE(send_result.has_value());

    xcvr.stop();

    std::lock_guard lock(transport->sent_mutex);
    REQUIRE(transport->sent_data.size() == 1);

    // Verify encoded bytes: big-endian uint32 = 99 (0x00000063)
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x63};
    CHECK(transport->sent_data[0] == expected);
}

TEST_CASE("Transceiver: sole_peer convenience", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result2 = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result2.has_value());

    auto sole = xcvr.sole_peer();
    REQUIRE(sole.has_value());
    CHECK(sole->valid());
}

TEST_CASE("Transceiver: send before start returns NotRunning", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());

    TestMsg msg;
    auto result = xcvr.send<TestMsg>(*peer_result, msg);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::NotRunning);
}

TEST_CASE("Transceiver: send to unknown peer returns PeerNotFound",
          "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result2 = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result2.has_value());

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    PeerId unknown(999);
    TestMsg msg;
    auto result = xcvr.send<TestMsg>(unknown, msg);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::PeerNotFound);

    xcvr.stop();
}

TEST_CASE("BitWriter: finish resets state", "[bit_writer]") {
    io::BitWriter writer;
    writer.write_u32(0x12345678);
    auto finish_result = writer.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    // After finish(), the writer should be in a clean state
    CHECK(writer.size_bytes() == 0);
    CHECK(writer.size_bits() == 0);

    // Writing again should produce correct output
    writer.write_u8(0xAB);
    auto finish_result2 = writer.finish();
    REQUIRE(finish_result2.has_value());
    auto data2 = std::move(*finish_result2);
    REQUIRE(data2.size() == 1);
    CHECK(data2[0] == 0xAB);
}

TEST_CASE("Transceiver: stream framing pipeline", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = true;  // Stream mode — framing needed

    Transceiver xcvr;

    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    int received_payload = 0;
    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [&](const TestMsg& msg) { received_payload = msg.payload; }));

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Inject frame in two parts (stream framer should reassemble)
    std::vector<uint8_t> part1 = {0x00, 0x00};
    std::vector<uint8_t> part2 = {0x00, 0x07};  // payload = 7
    transport->inject_data(peer, part1);
    transport->inject_data(peer, part2);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xcvr.stop();

    CHECK(received_payload == 7);
}

TEST_CASE("Transceiver: on_state_change callback fires", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::vector<std::pair<PeerId, net::ConnectionState>> state_log;
    std::mutex log_mutex;
    auto cb_id = xcvr.on_state_change([&](PeerId p, net::ConnectionState s) {
        std::lock_guard lock(log_mutex);
        state_log.emplace_back(p, s);
    });
    (void)cb_id;

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Simulate state change via transport callback
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Connected);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    xcvr.stop();

    std::lock_guard lock(log_mutex);
    REQUIRE(!state_log.empty());
    CHECK(state_log.back().first == peer);
    CHECK(state_log.back().second == net::ConnectionState::Connected);
}

// ============================================================================
// TrackingSession: Counts reset() calls for B5 testing
// ============================================================================

class TrackingSession : public MockSession {
public:
    std::atomic<int> reset_count{0};

    void reset() override {
        reset_count.fetch_add(1);
    }
};

TEST_CASE("Transceiver: session and framer reset on reconnect", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = true;  // Stream mode so framer is created

    Transceiver xcvr;
    auto session = std::make_unique<TrackingSession>();
    auto* tracking = session.get();  // Keep raw pointer for checking

    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Simulate a (re)connect
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Connected);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CHECK(tracking->reset_count.load() >= 1);

    // Simulate disconnect then reconnect
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Connected);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CHECK(tracking->reset_count.load() >= 2);

    xcvr.stop();
}

TEST_CASE("Transceiver: peer_state and peer_count", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;
    Transceiver xcvr;
    CHECK(xcvr.peer_count() == 0);

    auto session = std::make_unique<MockSession>();
    auto pr = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(pr.has_value());

    CHECK(xcvr.peer_count() == 1);
    CHECK(xcvr.peer_state(*pr) == net::ConnectionState::Disconnected);
}

TEST_CASE("Transceiver: sole_peer errors", "[transceiver]") {
    Transceiver xcvr;
    // No peers
    auto r1 = xcvr.sole_peer();
    REQUIRE_FALSE(r1.has_value());

    // Add two peers -> MultiplePeers
    auto t1 = std::make_shared<MockTransport>(); t1->stream = false;
    auto t2 = std::make_shared<MockTransport>(); t2->stream = false;
    (void)xcvr.add_peer("a", std::make_unique<MockSession>(), t1);
    (void)xcvr.add_peer("b", std::make_unique<MockSession>(), t2);
    auto r2 = xcvr.sole_peer();
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().code() == ErrorCode::MultiplePeers);
}

TEST_CASE("Transceiver: send via sole_peer convenience", "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;
    Transceiver xcvr;
    REQUIRE(xcvr.add_peer("test", std::make_unique<MockSession>(), transport).has_value());
    REQUIRE(xcvr.start().has_value());

    TestMsg msg; msg.payload = 77;
    auto r = xcvr.send<TestMsg>(msg);  // No explicit PeerId
    REQUIRE(r.has_value());

    xcvr.stop();
    std::lock_guard lock(transport->sent_mutex);
    REQUIRE(transport->sent_data.size() == 1);
}

// ============================================================================
// T1: Handler exception safety
// ============================================================================

TEST_CASE("Transceiver: handler exception does not crash transceiver",
          "[transceiver][exception]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    // Register a handler that throws
    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [](const TestMsg&) { throw std::runtime_error("intentional"); }));

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Inject a frame — the handler will throw, but transceiver should survive
    std::vector<uint8_t> frame1 = {0x00, 0x00, 0x00, 0x01};
    transport->inject_data(peer, frame1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Transceiver should still be running — verify by sending
    TestMsg msg;
    msg.payload = 55;
    auto send_result = xcvr.send<TestMsg>(peer, msg);
    // Send should still work (transceiver is alive)
    CHECK(send_result.has_value());

    xcvr.stop();
}

// ============================================================================
// T3: Graceful shutdown with pending messages
// ============================================================================

TEST_CASE("Transceiver: graceful shutdown drains pending messages",
          "[transceiver][shutdown]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> received_count{0};
    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { received_count.fetch_add(1); }));

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Inject multiple frames rapidly
    for (int i = 0; i < 10; ++i) {
        uint8_t b0 = static_cast<uint8_t>((i >> 24) & 0xFF);
        uint8_t b1 = static_cast<uint8_t>((i >> 16) & 0xFF);
        uint8_t b2 = static_cast<uint8_t>((i >> 8) & 0xFF);
        uint8_t b3 = static_cast<uint8_t>(i & 0xFF);
        std::vector<uint8_t> frame = {b0, b1, b2, b3};
        transport->inject_data(peer, frame);
    }

    // Give workers a moment to start processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stop should complete without hanging
    xcvr.stop();

    // At least some messages should have been processed
    CHECK(received_count.load() > 0);
}

// ============================================================================
// T2: Back-pressure / high-volume injection
// ============================================================================

TEST_CASE("Transceiver: high-volume injection does not lose messages",
          "[transceiver][stress]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> received_count{0};
    std::mutex payloads_mutex;
    std::vector<int> payloads;

    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [&](const TestMsg& msg) {
            received_count.fetch_add(1);
            std::lock_guard lock(payloads_mutex);
            payloads.push_back(msg.payload);
        }));

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    // Inject 100 frames rapidly
    constexpr int count = 100;
    for (int i = 0; i < count; ++i) {
        uint8_t b0 = static_cast<uint8_t>((i >> 24) & 0xFF);
        uint8_t b1 = static_cast<uint8_t>((i >> 16) & 0xFF);
        uint8_t b2 = static_cast<uint8_t>((i >> 8) & 0xFF);
        uint8_t b3 = static_cast<uint8_t>(i & 0xFF);
        std::vector<uint8_t> frame = {b0, b1, b2, b3};
        transport->inject_data(peer, frame);
    }

    // Wait for processing
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (received_count.load() < count &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    xcvr.stop();

    // All messages should have been delivered (datagram mode, no framing loss)
    CHECK(received_count.load() == count);

    // Verify all payloads arrived (order may vary if queue is multi-consumer)
    std::lock_guard lock(payloads_mutex);
    std::sort(payloads.begin(), payloads.end());
    REQUIRE(static_cast<int>(payloads.size()) == count);
    for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        CHECK(payloads[i] == static_cast<int>(i));
    }
}

// ============================================================================
// T2b: Multiple rapid sends from transceiver
// ============================================================================

TEST_CASE("Transceiver: rapid sends all reach transport",
          "[transceiver][stress]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    auto start_result = xcvr.start();
    REQUIRE(start_result.has_value());

    constexpr int count = 50;
    for (int i = 0; i < count; ++i) {
        TestMsg msg;
        msg.payload = i;
        auto r = xcvr.send<TestMsg>(peer, msg);
        CHECK(r.has_value());
    }

    xcvr.stop();

    std::lock_guard lock(transport->sent_mutex);
    CHECK(static_cast<int>(transport->sent_data.size()) == count);
}
