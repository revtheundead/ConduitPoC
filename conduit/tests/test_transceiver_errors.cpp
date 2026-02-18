// SPDX-License-Identifier: MIT
// Conduit - Transceiver on_error() Callback Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/itransport.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>
#include <atomic>
#include <chrono>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// ============================================================================
// Mock message type
// ============================================================================

namespace {

struct ErrTestMsg {
    static constexpr uint64_t TYPE_ID = 2001;
    static constexpr std::string_view TYPE_NAME = "ErrTestMsg";
    int value = 0;

    VoidResult encode(io::BitWriter& w) const {
        w.write_u32(static_cast<uint32_t>(value));
        if (w.has_error()) return std::unexpected(w.error());
        return {};
    }

    static Result<ErrTestMsg> decode(io::BitReader& r) {
        CONDUIT_TRY_ASSIGN(auto, val, r.read_u32());
        ErrTestMsg msg;
        msg.value = static_cast<int>(val);
        return msg;
    }
};

static_assert(traits::Message<ErrTestMsg>);

// ============================================================================
// Mock transport (datagram mode for simplicity)
// ============================================================================

class ErrorTestTransport : public ITransport {
public:
    TransportCallbacks callbacks;
    bool started = false;

    VoidResult start(TransportCallbacks cb) override {
        callbacks = std::move(cb);
        started = true;
        return {};
    }

    void stop() override { started = false; }

    VoidResult send(PeerId, std::span<const uint8_t>) override { return {}; }

    bool is_stream_oriented() const noexcept override { return false; }
    bool is_multi_peer() const noexcept override { return false; }

    void inject_data(PeerId peer, std::span<const uint8_t> data) {
        if (callbacks.on_data_received) {
            callbacks.on_data_received(peer, data);
        }
    }
};

// ============================================================================
// Mock session
// ============================================================================

class ErrorTestSession : public traits::ISession {
public:
    Result<std::vector<traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        if (data.size() < 4) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::BufferUnderrun, "Need 4 bytes"));
        }
        io::BitReader reader(data);
        auto msg = ErrTestMsg::decode(reader);
        if (!msg) return std::unexpected(msg.error());

        traits::DecodedMessage dm;
        dm.type_id = ErrTestMsg::TYPE_ID;
        dm.type_name = ErrTestMsg::TYPE_NAME;
        dm.payload = std::any(*msg);
        dm.raw = std::vector<uint8_t>(data.begin(), data.end());
        return std::vector<traits::DecodedMessage>{std::move(dm)};
    }

    Result<traits::EncodeResult>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != ErrTestMsg::TYPE_ID) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        }
        auto& msg = std::any_cast<const ErrTestMsg&>(payload);
        io::BitWriter writer;
        msg.encode(writer);
        auto bytes = writer.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return traits::EncodeResult{std::move(*bytes), {}};
    }

    std::span<const uint8_t> sync_pattern() const override { return sync_; }
    size_t min_frame_header_size() const override { return 4; }
    size_t extract_frame_length(std::span<const uint8_t>) const override {
        return 4;
    }

    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t) const override { return "ErrTestMsg"; }
    void reset() override {}

private:
    std::vector<uint8_t> sync_;
    std::vector<uint64_t> ids_ = {ErrTestMsg::TYPE_ID};
};

// ============================================================================
// Mock transport (stream-oriented mode)
// ============================================================================

class ErrorStreamTransport : public ITransport {
public:
    TransportCallbacks callbacks;
    bool started = false;

    VoidResult start(TransportCallbacks cb) override {
        callbacks = std::move(cb);
        started = true;
        return {};
    }

    void stop() override { started = false; }

    VoidResult send(PeerId, std::span<const uint8_t>) override { return {}; }

    bool is_stream_oriented() const noexcept override { return true; }
    bool is_multi_peer() const noexcept override { return false; }

    void inject_data(PeerId peer, std::span<const uint8_t> data) {
        if (callbacks.on_data_received) {
            callbacks.on_data_received(peer, data);
        }
    }
};

// ============================================================================
// Mock session for stream-oriented transport (with sync pattern)
// ============================================================================

class ErrorStreamSession : public traits::ISession {
public:
    Result<std::vector<traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t> data) override {
        if (data.size() < 4) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::BufferUnderrun, "Need 4 bytes"));
        }
        io::BitReader reader(data);
        auto msg = ErrTestMsg::decode(reader);
        if (!msg) return std::unexpected(msg.error());

        traits::DecodedMessage dm;
        dm.type_id = ErrTestMsg::TYPE_ID;
        dm.type_name = ErrTestMsg::TYPE_NAME;
        dm.payload = std::any(*msg);
        dm.raw = std::vector<uint8_t>(data.begin(), data.end());
        return std::vector<traits::DecodedMessage>{std::move(dm)};
    }

    Result<traits::EncodeResult>
    encode_wrap(uint64_t type_id, const std::any& payload) override {
        if (type_id != ErrTestMsg::TYPE_ID) {
            return std::unexpected(
                CONDUIT_ERROR(ErrorCode::UnknownTypeId, "Unknown type"));
        }
        auto& msg = std::any_cast<const ErrTestMsg&>(payload);
        io::BitWriter writer;
        msg.encode(writer);
        auto bytes = writer.finish();
        if (!bytes) return std::unexpected(bytes.error());
        return traits::EncodeResult{std::move(*bytes), {}};
    }

    std::span<const uint8_t> sync_pattern() const override { return sync_; }
    size_t min_frame_header_size() const override { return 4; }
    size_t extract_frame_length(std::span<const uint8_t>) const override {
        return 4;
    }

    std::span<const uint64_t> leaf_type_ids() const override { return ids_; }
    std::string_view type_name(uint64_t) const override { return "ErrTestMsg"; }
    void reset() override {}

private:
    std::vector<uint8_t> sync_ = {0xAA, 0xBB};
    std::vector<uint64_t> ids_ = {ErrTestMsg::TYPE_ID};
};

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("on_error fires for decode failures", "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("radar-1",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::mutex mtx;
    std::vector<ErrorEvent> events;
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        std::lock_guard lock(mtx);
        events.push_back(e);
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // Inject malformed data (too short for 4-byte frame)
    std::vector<uint8_t> bad_data = {0x01, 0x02};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(events.size() == 1);
    CHECK(events[0].error.is_decode_error());
    CHECK(events[0].peer == peer);
    CHECK(events[0].peer_name == "radar-1");
    CHECK(events[0].error.has_context());
}

TEST_CASE("on_error fires for handler exceptions", "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("test",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    // Register handler that throws
    xcvr.on<ErrTestMsg>(peer, std::function<void(const ErrTestMsg&)>(
        [](const ErrTestMsg&) {
            throw std::runtime_error("intentional test error");
        }));

    std::mutex mtx;
    std::vector<ErrorEvent> events;
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        std::lock_guard lock(mtx);
        events.push_back(e);
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // Inject valid message
    std::vector<uint8_t> frame = {0x00, 0x00, 0x00, 0x01};
    transport->inject_data(peer, frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(events.size() >= 1);
    CHECK(events[0].error.code() == ErrorCode::InternalError);
    CHECK(events[0].peer == peer);
}

TEST_CASE("multiple on_error callbacks all fire", "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("test",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> count1{0}, count2{0}, count3{0};
    (void)xcvr.on_error([&](const ErrorEvent&) { count1.fetch_add(1); });
    (void)xcvr.on_error([&](const ErrorEvent&) { count2.fetch_add(1); });
    (void)xcvr.on_error([&](const ErrorEvent&) { count3.fetch_add(1); });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    std::vector<uint8_t> bad_data = {0x01};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    CHECK(count1.load() == 1);
    CHECK(count2.load() == 1);
    CHECK(count3.load() == 1);
}

TEST_CASE("remove_error_callback stops delivery", "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("test",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> removed_count{0};
    std::atomic<int> kept_count{0};
    auto cb_id = xcvr.on_error([&](const ErrorEvent&) { removed_count.fetch_add(1); });
    (void)xcvr.on_error([&](const ErrorEvent&) { kept_count.fetch_add(1); });

    bool removed = xcvr.remove_error_callback(cb_id);
    CHECK(removed);

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    std::vector<uint8_t> bad_data = {0x01};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    CHECK(removed_count.load() == 0);
    CHECK(kept_count.load() == 1);
}

TEST_CASE("error callback exception doesn't crash transceiver",
          "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("test",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> second_count{0};

    // First callback throws
    (void)xcvr.on_error([](const ErrorEvent&) {
        throw std::runtime_error("callback error");
    });
    // Second callback should still fire
    (void)xcvr.on_error([&](const ErrorEvent&) { second_count.fetch_add(1); });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    std::vector<uint8_t> bad_data = {0x01};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    // Second callback should still have been called despite first throwing
    CHECK(second_count.load() == 1);
}

TEST_CASE("ErrorEvent contains correct peer context", "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("radar-1",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::mutex mtx;
    ErrorEvent captured;
    bool got_event = false;
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        std::lock_guard lock(mtx);
        if (!got_event) {
            captured = e;
            got_event = true;
        }
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    std::vector<uint8_t> bad_data = {0x01};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(got_event);
    CHECK(captured.peer == peer);
    CHECK(captured.peer_name == "radar-1");
    CHECK(captured.error.is_decode_error());
    CHECK(!captured.error.message().empty());
}

// ============================================================================
// New tests
// ============================================================================

TEST_CASE("on_error fires for stream framing errors",
          "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorStreamTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("stream-peer",
        std::make_unique<ErrorStreamSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::mutex mtx;
    std::vector<ErrorEvent> events;
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        std::lock_guard lock(mtx);
        events.push_back(e);
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // The stream framer has a 1MB max buffer. Injecting a single chunk larger
    // than 1MB (with no sync match) triggers a BufferOverrun error from
    // push_data(), which fires on_error with context "framing".
    std::vector<uint8_t> big_data(1048577, 0xFF);  // 1MB + 1 byte, no sync {0xAA,0xBB}
    transport->inject_data(peer, big_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(events.size() >= 1);
    CHECK(events[0].peer == peer);
    CHECK(events[0].peer_name == "stream-peer");
    CHECK(events[0].error.has_context());
}

TEST_CASE("on_error error contains context string",
          "[transceiver][on_error]") {
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("ctx-peer",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::mutex mtx;
    ErrorEvent captured;
    bool got_event = false;
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        std::lock_guard lock(mtx);
        if (!got_event) {
            captured = e;
            got_event = true;
        }
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // Inject malformed data to trigger a decode error
    std::vector<uint8_t> bad_data = {0x01, 0x02};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    xcvr.stop();

    std::lock_guard lock(mtx);
    REQUIRE(got_event);
    CHECK(captured.error.has_context());
    // The context should contain "decode" (set by the datagram decode path)
    CHECK(captured.error.context().find("decode") != std::string::npos);
}

TEST_CASE("error callback can safely query transceiver state",
          "[transceiver][on_error]") {
    // Deadlock regression test: error callbacks are invoked outside all locks,
    // so calling peer_count() and is_running() from within the callback must
    // not deadlock.
    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr;

    auto peer_result = xcvr.add_peer("deadlock-test",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    std::atomic<int> callback_count{0};
    std::atomic<bool> queried_ok{false};
    (void)xcvr.on_error([&](const ErrorEvent&) {
        // These calls acquire peers_mutex_ and running_ respectively.
        // If error callbacks were fired under lock, this would deadlock.
        size_t count = xcvr.peer_count();
        bool running = xcvr.is_running();
        if (count >= 1 && running) {
            queried_ok.store(true);
        }
        callback_count.fetch_add(1);
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // Inject malformed data to trigger the error callback
    std::vector<uint8_t> bad_data = {0x01};
    transport->inject_data(peer, bad_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xcvr.stop();

    // If we reach here without deadlock, the test passes
    CHECK(callback_count.load() >= 1);
    CHECK(queried_ok.load());
}

TEST_CASE("on_error fires for queue overflow",
          "[transceiver][on_error]") {
    TransceiverConfig cfg;
    cfg.rx_queue.capacity = 1;
    cfg.rx_queue.drop_policy = queue::DropPolicy::DropNewest;

    auto transport = std::make_shared<ErrorTestTransport>();
    Transceiver xcvr(cfg);

    auto peer_result = xcvr.add_peer("overflow-peer",
        std::make_unique<ErrorTestSession>(), transport);
    REQUIRE(peer_result.has_value());
    PeerId peer = *peer_result;

    // Register a slow message handler that holds a queue slot for a while
    xcvr.on<ErrTestMsg>(peer, std::function<void(const ErrTestMsg&)>(
        [](const ErrTestMsg&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }));

    std::atomic<int> error_count{0};
    std::atomic<bool> got_queue_full{false};
    (void)xcvr.on_error([&](const ErrorEvent& e) {
        error_count.fetch_add(1);
        if (e.error.code() == ErrorCode::QueueFull) {
            got_queue_full.store(true);
        }
    });

    auto start = xcvr.start();
    REQUIRE(start.has_value());

    // Inject multiple valid 4-byte messages rapidly to overflow the queue
    std::vector<uint8_t> valid_msg = {0x00, 0x00, 0x00, 0x01};
    for (int i = 0; i < 10; ++i) {
        transport->inject_data(peer, valid_msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xcvr.stop();

    CHECK(error_count.load() >= 1);
    CHECK(got_queue_full.load());
}

TEST_CASE("remove_error_callback returns false for unknown id",
          "[transceiver][on_error]") {
    Transceiver xcvr;

    // Use an ID that was never registered
    CallbackId bogus_id = static_cast<CallbackId>(99999);
    bool removed = xcvr.remove_error_callback(bogus_id);
    CHECK_FALSE(removed);
}
