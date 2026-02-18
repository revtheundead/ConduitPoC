// SPDX-License-Identifier: MIT
// Conduit - Transceiver Multi-Peer Unit Tests

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

namespace {

inline void wait_until(auto pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}  // anonymous namespace

// ============================================================================
// Mock message type
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
    mutable std::mutex sent_mutex;
    bool started = false;
    bool multi_peer_mode = false;
    bool stream = false;

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
    bool is_multi_peer() const noexcept override { return multi_peer_mode; }

    void inject_data(PeerId peer, std::span<const uint8_t> data) {
        if (callbacks.on_data_received) {
            callbacks.on_data_received(peer, data);
        }
    }

    bool has_sent_data() const {
        std::lock_guard lock(sent_mutex);
        return !sent_data.empty();
    }

    size_t sent_count() const {
        std::lock_guard lock(sent_mutex);
        return sent_data.size();
    }
};

// ============================================================================
// Mock session
// ============================================================================

class MockSession : public traits::ISession {
public:
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
        return 4;
    }

    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t /*type_id*/) const override { return "TestMsg"; }
    void reset() override {}

private:
    std::vector<uint8_t> sync_;
    std::vector<uint64_t> ids_ = {TestMsg::TYPE_ID};
};

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Transceiver multi-peer: two peers on separate transports",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    CHECK(xcvr.peer_count() == 2);
    CHECK(*r_alpha != *r_beta);  // Distinct PeerIds
}

TEST_CASE("Transceiver multi-peer: peer() by name",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    auto found_alpha = xcvr.peer("alpha");
    auto found_beta = xcvr.peer("beta");
    auto found_gamma = xcvr.peer("gamma");

    REQUIRE(found_alpha.has_value());
    REQUIRE(found_beta.has_value());
    CHECK(*found_alpha == *r_alpha);
    CHECK(*found_beta == *r_beta);
    CHECK_FALSE(found_gamma.has_value());
}

TEST_CASE("Transceiver multi-peer: sole_peer() with 2 peers returns error",
          "[transceiver][multi_peer]") {
    auto t1 = std::make_shared<MockTransport>();
    auto t2 = std::make_shared<MockTransport>();

    Transceiver xcvr;
    (void)xcvr.add_peer("a", std::make_unique<MockSession>(), t1);
    (void)xcvr.add_peer("b", std::make_unique<MockSession>(), t2);

    auto r = xcvr.sole_peer();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == ErrorCode::MultiplePeers);
}

TEST_CASE("Transceiver multi-peer: per-peer handler isolation",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    PeerId alpha = *r_alpha;
    PeerId beta = *r_beta;

    int alpha_count = 0;
    int beta_count = 0;

    xcvr.on<TestMsg>(alpha, std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { ++alpha_count; }));
    xcvr.on<TestMsg>(beta, std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { ++beta_count; }));

    REQUIRE(xcvr.start().has_value());

    // Inject data to alpha only
    std::vector<uint8_t> frame = {0x00, 0x00, 0x00, 0x2A};
    t_alpha->inject_data(alpha, frame);

    wait_until([&] { return alpha_count >= 1; });
    xcvr.stop();

    CHECK(alpha_count == 1);
    CHECK(beta_count == 0);
}

TEST_CASE("Transceiver multi-peer: send routes to correct transport",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    REQUIRE(xcvr.start().has_value());

    TestMsg msg;
    msg.payload = 123;
    auto send_result = xcvr.send<TestMsg>(*r_alpha, msg);
    REQUIRE(send_result.has_value());

    xcvr.stop();

    CHECK(t_alpha->has_sent_data());
    CHECK_FALSE(t_beta->has_sent_data());
}

TEST_CASE("Transceiver multi-peer: add_peer with session factory",
          "[transceiver][multi_peer]") {
    auto transport = std::make_shared<MockTransport>();
    transport->multi_peer_mode = true;

    Transceiver xcvr;
    auto r = xcvr.add_peer("server",
        []() -> std::unique_ptr<traits::ISession> {
            return std::make_unique<MockSession>();
        },
        transport);
    REQUIRE(r.has_value());

    REQUIRE(xcvr.start().has_value());

    // Simulate two peers connecting
    PeerId dyn1, dyn2;
    if (transport->callbacks.on_peer_connected) {
        dyn1 = transport->callbacks.on_peer_connected("10.0.0.1:5001");
        dyn2 = transport->callbacks.on_peer_connected("10.0.0.2:5002");
    }

    CHECK(dyn1.valid());
    CHECK(dyn2.valid());
    CHECK(dyn1 != dyn2);

    xcvr.stop();
}

TEST_CASE("Transceiver multi-peer: global handler receives from all peers",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    std::mutex mtx;
    std::vector<int> received_values;

    xcvr.on<TestMsg>(std::function<void(const TestMsg&)>(
        [&](const TestMsg& msg) {
            std::lock_guard lock(mtx);
            received_values.push_back(msg.payload);
        }));

    REQUIRE(xcvr.start().has_value());

    // Inject from alpha (payload=10)
    std::vector<uint8_t> frame_a = {0x00, 0x00, 0x00, 0x0A};
    t_alpha->inject_data(*r_alpha, frame_a);

    // Inject from beta (payload=20)
    std::vector<uint8_t> frame_b = {0x00, 0x00, 0x00, 0x14};
    t_beta->inject_data(*r_beta, frame_b);

    wait_until([&] { std::lock_guard lock(mtx); return received_values.size() >= 2; });
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(received_values.size() == 2);
    // Sort to check values regardless of order
    std::sort(received_values.begin(), received_values.end());
    CHECK(received_values[0] == 10);
    CHECK(received_values[1] == 20);
}

TEST_CASE("Transceiver multi-peer: per-peer overrides global",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    int per_peer_count = 0;
    int global_count = 0;

    // Per-peer handler for alpha
    xcvr.on<TestMsg>(*r_alpha, std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { ++per_peer_count; }));

    // Global handler for all
    xcvr.on<TestMsg>(std::function<void(const TestMsg&)>(
        [&](const TestMsg&) { ++global_count; }));

    REQUIRE(xcvr.start().has_value());

    // Inject to alpha — per-peer should fire
    std::vector<uint8_t> frame = {0x00, 0x00, 0x00, 0x01};
    t_alpha->inject_data(*r_alpha, frame);

    wait_until([&] { return per_peer_count >= 1; });

    // Inject to beta — global should fire
    t_beta->inject_data(*r_beta, frame);

    wait_until([&] { return global_count >= 1; });
    xcvr.stop();

    CHECK(per_peer_count == 1);
    CHECK(global_count == 1);  // Only beta's message went to global
}

TEST_CASE("Transceiver multi-peer: peer_state per-peer",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    REQUIRE(xcvr.start().has_value());

    // Simulate alpha connecting
    t_alpha->callbacks.on_state_changed(*r_alpha, net::ConnectionState::Connected);

    wait_until([&] { return xcvr.peer_state(*r_alpha) == net::ConnectionState::Connected; });

    CHECK(xcvr.peer_state(*r_alpha) == net::ConnectionState::Connected);
    CHECK(xcvr.peer_state(*r_beta) == net::ConnectionState::Disconnected);

    xcvr.stop();
}

TEST_CASE("Transceiver multi-peer: send to multiple peers",
          "[transceiver][multi_peer]") {
    auto t_alpha = std::make_shared<MockTransport>();
    auto t_beta = std::make_shared<MockTransport>();

    Transceiver xcvr;
    auto r_alpha = xcvr.add_peer("alpha", std::make_unique<MockSession>(), t_alpha);
    auto r_beta = xcvr.add_peer("beta", std::make_unique<MockSession>(), t_beta);
    REQUIRE(r_alpha.has_value());
    REQUIRE(r_beta.has_value());

    REQUIRE(xcvr.start().has_value());

    TestMsg msg1; msg1.payload = 111;
    TestMsg msg2; msg2.payload = 222;

    auto s1 = xcvr.send<TestMsg>(*r_alpha, msg1);
    auto s2 = xcvr.send<TestMsg>(*r_beta, msg2);
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());

    xcvr.stop();

    REQUIRE(t_alpha->sent_count() == 1);
    REQUIRE(t_beta->sent_count() == 1);

    // Verify the payloads are different (different encoded bytes)
    CHECK(t_alpha->sent_data[0] != t_beta->sent_data[0]);
}
