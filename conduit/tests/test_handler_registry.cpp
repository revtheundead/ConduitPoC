// SPDX-License-Identifier: MIT
// Conduit - HandlerRegistry Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/handler.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Mock message types satisfying traits::Message concept
// ============================================================================

namespace {

struct MockMsgA {
    static constexpr uint64_t TYPE_ID = 100;
    static constexpr std::string_view TYPE_NAME = "MockMsgA";
    int value = 0;

    VoidResult encode(io::BitWriter& /*w*/) const { return {}; }
    static Result<MockMsgA> decode(io::BitReader& /*r*/) { return MockMsgA{}; }
};

struct MockMsgB {
    static constexpr uint64_t TYPE_ID = 200;
    static constexpr std::string_view TYPE_NAME = "MockMsgB";
    std::string text;

    VoidResult encode(io::BitWriter& /*w*/) const { return {}; }
    static Result<MockMsgB> decode(io::BitReader& /*r*/) { return MockMsgB{}; }
};

// Verify concepts
static_assert(traits::Message<MockMsgA>);
static_assert(traits::Message<MockMsgB>);

} // anonymous namespace

TEST_CASE("HandlerRegistry: register and dispatch matching type", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int received_value = 0;
    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA& msg) { received_value = msg.value; })));

    MockMsgA msg;
    msg.value = 42;
    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(msg));

    CHECK(result == DispatchResult::Handled);
    CHECK(received_value == 42);
}

TEST_CASE("HandlerRegistry: dispatch non-matching type returns false", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [](const MockMsgA&) {})));

    // Dispatch with MockMsgB type_id — no handler registered for it
    auto result = registry.dispatch(peer, MockMsgB::TYPE_ID,
                                    std::any(MockMsgB{}));
    CHECK(result == DispatchResult::NotFound);
}

TEST_CASE("HandlerRegistry: per-peer isolation", "[handler]") {
    HandlerRegistry registry;
    PeerId peer_a(1);
    PeerId peer_b(2);

    int a_count = 0;
    int b_count = 0;

    registry.register_handler(peer_a,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++a_count; })));

    registry.register_handler(peer_b,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++b_count; })));

    // Dispatch to peer_a
    registry.dispatch(peer_a, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(a_count == 1);
    CHECK(b_count == 0);

    // Dispatch to peer_b
    registry.dispatch(peer_b, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(a_count == 1);
    CHECK(b_count == 1);
}

TEST_CASE("HandlerRegistry: global handler called for any peer", "[handler]") {
    HandlerRegistry registry;

    int call_count = 0;
    registry.register_handler_all(
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++call_count; })));

    PeerId peer1(1);
    PeerId peer2(2);

    registry.dispatch(peer1, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    registry.dispatch(peer2, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(call_count == 2);
}

TEST_CASE("HandlerRegistry: per-peer overrides global", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int global_count = 0;
    int peer_count = 0;

    registry.register_handler_all(
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++global_count; })));

    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++peer_count; })));

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(peer_count == 1);
    CHECK(global_count == 0);  // Per-peer wins
}

TEST_CASE("HandlerRegistry: global handler used when no per-peer handler", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);
    PeerId other(2);

    int global_count = 0;

    registry.register_handler_all(
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++global_count; })));

    // Peer 1 has no per-peer handler; should fall back to global
    registry.dispatch(other, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(global_count == 1);
}

TEST_CASE("HandlerRegistry: unregister per-peer handler", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int call_count = 0;
    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++call_count; })));

    // Dispatch once — should work
    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(call_count == 1);

    // Unregister
    bool removed = registry.unregister_handler(peer, MockMsgA::TYPE_ID);
    CHECK(removed);

    // Dispatch again — should not fire
    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(result == DispatchResult::NotFound);
    CHECK(call_count == 1);

    // Unregister again — should return false (already removed)
    bool removed_again = registry.unregister_handler(peer, MockMsgA::TYPE_ID);
    CHECK_FALSE(removed_again);
}

TEST_CASE("HandlerRegistry: unregister global handler", "[handler]") {
    HandlerRegistry registry;

    int call_count = 0;
    registry.register_handler_all(
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++call_count; })));

    PeerId peer(1);
    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(call_count == 1);

    // Unregister global
    bool removed = registry.unregister_handler_all(MockMsgA::TYPE_ID);
    CHECK(removed);

    // Dispatch again — should not fire
    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(result == DispatchResult::NotFound);
    CHECK(call_count == 1);

    // Unregister again — should return false
    bool removed_again = registry.unregister_handler_all(MockMsgA::TYPE_ID);
    CHECK_FALSE(removed_again);
}

TEST_CASE("HandlerRegistry: dispatch survives type mismatch", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    // Register handler expecting MockMsgA
    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [](const MockMsgA&) {})));

    // Dispatch with correct type_id but wrong payload type (int instead of MockMsgA).
    // This should NOT throw; dispatch should catch bad_any_cast and return Error.
    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(42));
    CHECK(result == DispatchResult::Error);
}

// ============================================================================
// Additional handler registry tests
// ============================================================================

TEST_CASE("HandlerRegistry: per-peer catch-all fires for unmatched type", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    uint64_t catch_all_tid = 0;
    registry.set_catch_all(peer, [&](uint64_t tid, const std::any&) {
        catch_all_tid = tid;
    });

    auto result = registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));
    CHECK(result == DispatchResult::Handled);
    CHECK(catch_all_tid == MockMsgB::TYPE_ID);
}

TEST_CASE("HandlerRegistry: per-peer catch-all overrides global catch-all", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int peer_catch_all_count = 0;
    int global_catch_all_count = 0;

    registry.set_catch_all(peer, [&](uint64_t, const std::any&) {
        ++peer_catch_all_count;
    });
    registry.set_catch_all([&](uint64_t, const std::any&) {
        ++global_catch_all_count;
    });

    registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));

    CHECK(peer_catch_all_count == 1);
    CHECK(global_catch_all_count == 0);
}

TEST_CASE("HandlerRegistry: handler > catch-all priority", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int handler_count = 0;
    int catch_all_count = 0;

    // Register a handler for MockMsgA::TYPE_ID
    registry.register_handler(peer,
        ErasedHandler(MockMsgA::TYPE_ID,
            [&](const std::any&) { ++handler_count; }));

    // Catch-all for same peer
    registry.set_catch_all(peer, [&](uint64_t, const std::any&) {
        ++catch_all_count;
    });

    // Handler should take priority over catch-all
    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(handler_count == 1);
    CHECK(catch_all_count == 0);

    // Dispatch an unregistered type — catch-all should fire
    registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));
    CHECK(catch_all_count == 1);
}

TEST_CASE("HandlerRegistry: install_handler replaces previous", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int v1_count = 0;
    int v2_count = 0;

    // Install v1
    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++v1_count; })));

    // Install v2 (replaces v1)
    registry.register_handler(peer,
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++v2_count; })));

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(v1_count == 0);
    CHECK(v2_count == 1);
}

TEST_CASE("HandlerRegistry: concurrent dispatch safety", "[handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    std::atomic<int> total_count{0};
    registry.register_handler_all(
        ErasedHandler(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { total_count.fetch_add(1); })));

    constexpr int threads = 4;
    constexpr int per_thread = 1000;

    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&]() {
            for (int j = 0; j < per_thread; ++j) {
                registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
            }
        });
    }

    for (auto& t : workers) t.join();

    CHECK(total_count.load() == threads * per_thread);
}
