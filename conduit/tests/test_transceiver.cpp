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

    Result<traits::EncodeResult>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != TestMsg::TYPE_ID) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        }
        auto& msg = std::any_cast<const TestMsg&>(payload);
        io::BitWriter writer;
        msg.encode(writer);
        auto bytes = writer.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return traits::EncodeResult{std::move(*bytes), {}};
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

// ============================================================================
// Batch send tests
// ============================================================================

struct BatchTestMsg {
    static constexpr uint64_t TYPE_ID = 2001;
    static constexpr std::string_view TYPE_NAME = "BatchTestMsg";
    int value = 0;

    VoidResult encode(io::BitWriter& w) const {
        w.write_u32(static_cast<uint32_t>(value));
        if (w.has_error()) return std::unexpected(w.error());
        return {};
    }

    static Result<BatchTestMsg> decode(io::BitReader& r) {
        CONDUIT_TRY_ASSIGN(auto, val, r.read_u32());
        BatchTestMsg msg;
        msg.value = static_cast<int>(val);
        return msg;
    }
};

static_assert(traits::Message<BatchTestMsg>);

class MockBatchSession : public traits::ISession {
public:
    Result<std::vector<traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        io::BitReader r(data);
        std::vector<traits::DecodedMessage> msgs;
        while (r.remaining_bytes() >= 4) {
            auto msg = BatchTestMsg::decode(r);
            if (!msg) break;
            traits::DecodedMessage dm;
            dm.type_id = BatchTestMsg::TYPE_ID;
            dm.type_name = BatchTestMsg::TYPE_NAME;
            dm.payload = *msg;
            msgs.push_back(std::move(dm));
        }
        return msgs;
    }

    Result<traits::EncodeResult>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != BatchTestMsg::TYPE_ID)
            return std::unexpected(CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        auto& msg = std::any_cast<const BatchTestMsg&>(payload);
        io::BitWriter w;
        msg.encode(w);
        auto bytes = w.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return traits::EncodeResult{std::move(*bytes), {}};
    }

    Result<traits::EncodeResult>
    encode_batch(uint64_t type_id, std::span<const std::any> payloads) override {
        if (type_id != BatchTestMsg::TYPE_ID)
            return std::unexpected(CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        io::BitWriter w;
        for (const auto& p : payloads) {
            auto* msg = std::any_cast<BatchTestMsg>(&p);
            if (!msg)
                return std::unexpected(CONDUIT_ERROR(ErrorCode::InvalidArgument, "type mismatch"));
            msg->encode(w);
        }
        auto bytes = w.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return traits::EncodeResult{std::move(*bytes), {}};
    }

    std::span<const uint8_t> sync_pattern() const override { return {}; }
    size_t min_frame_header_size() const override { return 4; }
    size_t extract_frame_length(std::span<const uint8_t>) const override { return 4; }
    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t) const override { return "BatchTestMsg"; }
    void reset() override {}

private:
    std::vector<uint64_t> ids_ = {BatchTestMsg::TYPE_ID};
};

TEST_CASE("Transceiver: send_batch encodes multiple messages in one frame",
          "[transceiver][batch]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockBatchSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    std::vector<BatchTestMsg> msgs = {{.value = 10}, {.value = 20}, {.value = 30}};
    auto result = xcvr.send_batch<BatchTestMsg>(peer, std::span{msgs});
    REQUIRE(result.has_value());

    xcvr.stop();

    std::lock_guard lock(transport->sent_mutex);
    REQUIRE(transport->sent_data.size() == 1);
    // 3 messages * 4 bytes each = 12 bytes in a single transport send
    CHECK(transport->sent_data[0].size() == 12);
}

TEST_CASE("Transceiver: send_batch rejected for non-batch session",
          "[transceiver][batch]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();  // No encode_batch override
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    std::vector<TestMsg> msgs = {{.payload = 1}};
    auto result = xcvr.send_batch<TestMsg>(peer, std::span{msgs});
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == ErrorCode::BatchNotSupported);

    xcvr.stop();
}

// ============================================================================
// Direction-aware session: reports certain types as receive-only
// ============================================================================

struct RecvOnlyMsg {
    static constexpr uint64_t TYPE_ID = 3001;
    static constexpr std::string_view TYPE_NAME = "RecvOnlyMsg";
    int value = 0;

    VoidResult encode(io::BitWriter& w) const {
        w.write_u32(static_cast<uint32_t>(value));
        if (w.has_error()) return std::unexpected(w.error());
        return {};
    }

    static Result<RecvOnlyMsg> decode(io::BitReader& r) {
        CONDUIT_TRY_ASSIGN(auto, val, r.read_u32());
        RecvOnlyMsg msg;
        msg.value = static_cast<int>(val);
        return msg;
    }
};

static_assert(traits::Message<RecvOnlyMsg>);

class DirectionMockSession : public MockSession {
public:
    bool is_receive_only(uint64_t type_id) const override {
        return type_id == RecvOnlyMsg::TYPE_ID;
    }

    std::string_view type_name(uint64_t type_id) const override {
        if (type_id == RecvOnlyMsg::TYPE_ID) return "RecvOnlyMsg";
        return MockSession::type_name(type_id);
    }
};

// ============================================================================
// T1: DirectionViolation — send blocks receive-only types
// ============================================================================

TEST_CASE("Transceiver: send receive-only type returns DirectionViolation",
          "[transceiver][direction]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<DirectionMockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    RecvOnlyMsg msg;
    msg.value = 99;
    auto result = xcvr.send<RecvOnlyMsg>(peer, msg);
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == ErrorCode::DirectionViolation);

    xcvr.stop();
}

TEST_CASE("Transceiver: send_batch receive-only type returns DirectionViolation",
          "[transceiver][direction][batch]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<DirectionMockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    std::vector<RecvOnlyMsg> msgs = {{.value = 1}};
    auto result = xcvr.send_batch<RecvOnlyMsg>(peer, std::span{msgs});
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == ErrorCode::DirectionViolation);

    xcvr.stop();
}

// ============================================================================
// T3: UnknownTypeId at transceiver level
// ============================================================================

TEST_CASE("Transceiver: send unknown type_id returns UnknownTypeId",
          "[transceiver]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    // RecvOnlyMsg has TYPE_ID=3001 which MockSession's encode_wrap doesn't know
    RecvOnlyMsg msg;
    msg.value = 42;
    auto result = xcvr.send<RecvOnlyMsg>(peer, msg);
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == ErrorCode::UnknownTypeId);

    xcvr.stop();
}

// ============================================================================
// T4: remove_handler and remove_state_change
// ============================================================================

TEST_CASE("Transceiver: remove_handler stops delivery", "[transceiver][handler]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> received{0};
    xcvr.on<TestMsg>(peer, std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { received.fetch_add(1); }));

    REQUIRE(xcvr.start().has_value());

    // Inject a frame — handler should fire
    std::vector<uint8_t> frame = {0x00, 0x00, 0x00, 0x01};
    transport->inject_data(peer, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(received.load() == 1);

    // Remove the handler
    bool removed = xcvr.remove_handler<TestMsg>(peer);
    CHECK(removed);

    // Inject another frame — handler should NOT fire
    transport->inject_data(peer, frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(received.load() == 1);

    xcvr.stop();
}

TEST_CASE("Transceiver: remove_state_change stops callbacks", "[transceiver][handler]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> state_changes{0};
    auto cb_id = xcvr.on_state_change([&](PeerId, net::ConnectionState) {
        state_changes.fetch_add(1);
    });

    REQUIRE(xcvr.start().has_value());

    // Fire a state change
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Connected);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(state_changes.load() >= 1);

    int before = state_changes.load();

    // Remove the callback
    bool removed = xcvr.remove_state_change(cb_id);
    CHECK(removed);

    // Fire another state change — should not increment
    transport->callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(state_changes.load() == before);

    xcvr.stop();
}

// ============================================================================
// T5: peer_ids and stats accessors
// ============================================================================

TEST_CASE("Transceiver: peer_ids returns all peer IDs", "[transceiver]") {
    Transceiver xcvr;
    auto t1 = std::make_shared<MockTransport>(); t1->stream = false;
    auto t2 = std::make_shared<MockTransport>(); t2->stream = false;

    auto p1 = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t1);
    auto p2 = xcvr.add_peer("beta", std::make_unique<MockSession>(), t2);
    REQUIRE(p1.has_value());
    REQUIRE(p2.has_value());

    auto ids = xcvr.peer_ids();
    REQUIRE(ids.size() == 2);

    // Both IDs should be present (order unspecified)
    bool has_p1 = std::find(ids.begin(), ids.end(), *p1) != ids.end();
    bool has_p2 = std::find(ids.begin(), ids.end(), *p2) != ids.end();
    CHECK(has_p1);
    CHECK(has_p2);
}

TEST_CASE("Transceiver: stats tracks bytes sent", "[transceiver][stats]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    CHECK(xcvr.stats().bytes_sent.load() == 0);

    TestMsg msg;
    msg.payload = 42;
    auto r = xcvr.send<TestMsg>(peer, msg);
    REQUIRE(r.has_value());

    CHECK(xcvr.stats().bytes_sent.load() == 4);  // TestMsg = 4 bytes

    xcvr.stop();
}

// ============================================================================
// T6: Empty batch send returns error
// ============================================================================

TEST_CASE("Transceiver: send_batch with empty span returns error",
          "[transceiver][batch]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = false;

    Transceiver xcvr;
    auto session = std::make_unique<MockBatchSession>();
    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    std::vector<BatchTestMsg> empty;
    auto result = xcvr.send_batch<BatchTestMsg>(peer, std::span{empty});
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);

    xcvr.stop();
}

// ============================================================================
// T12: ISession::reset() verification via tracking session
// ============================================================================

TEST_CASE("Transceiver: session reset increments on each reconnect", "[transceiver][reset]") {
    auto transport = std::make_shared<MockTransport>();
    transport->stream = true;

    Transceiver xcvr;
    auto session = std::make_unique<TrackingSession>();
    auto* tracking = session.get();

    auto peer_result = xcvr.add_peer("test", std::move(session), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    REQUIRE(xcvr.start().has_value());

    // Simulate 3 reconnect cycles
    for (int i = 0; i < 3; ++i) {
        transport->callbacks.on_state_changed(peer, net::ConnectionState::Connected);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        transport->callbacks.on_state_changed(peer, net::ConnectionState::Disconnected);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Each Connected event triggers a reset
    CHECK(tracking->reset_count.load() >= 3);

    xcvr.stop();
}

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
