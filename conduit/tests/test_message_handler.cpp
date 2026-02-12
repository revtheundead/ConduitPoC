// SPDX-License-Identifier: MIT
// Conduit - MessageHandler Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/transceiver/handler.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>
#include <cstdint>
#include <string>

using namespace conduit;
using namespace conduit::transceiver;

// ============================================================================
// Mock message types
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
    int value = 0;

    VoidResult encode(io::BitWriter& /*w*/) const { return {}; }
    static Result<MockMsgB> decode(io::BitReader& /*r*/) { return MockMsgB{}; }
};

struct MockMsgC {
    static constexpr uint64_t TYPE_ID = 300;
    static constexpr std::string_view TYPE_NAME = "MockMsgC";
    int value = 0;

    VoidResult encode(io::BitWriter& /*w*/) const { return {}; }
    static Result<MockMsgC> decode(io::BitReader& /*r*/) { return MockMsgC{}; }
};

static_assert(traits::Message<MockMsgA>);
static_assert(traits::Message<MockMsgB>);
static_assert(traits::Message<MockMsgC>);

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("MessageHandler: fluent chaining returns self", "[message_handler]") {
    MessageHandler handler;
    auto* p1 = &handler.on<MockMsgA>(std::function<void(const MockMsgA&)>(
        [](const MockMsgA&) {}));
    auto* p2 = &handler.on<MockMsgB>(std::function<void(const MockMsgB&)>(
        [](const MockMsgB&) {}));
    CHECK(p1 == &handler);
    CHECK(p2 == &handler);
}

TEST_CASE("MessageHandler: typed handler receives correct value", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int received_value = 0;
    MessageHandler handler;
    handler.on<MockMsgA>(std::function<void(const MockMsgA&)>(
        [&](const MockMsgA& msg) { received_value = msg.value; }));

    registry.install_handler(peer, handler);

    MockMsgA msg;
    msg.value = 42;
    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(msg));

    CHECK(result == DispatchResult::Handled);
    CHECK(received_value == 42);
}

TEST_CASE("MessageHandler: typed dispatch isolation", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int a_count = 0;
    int b_count = 0;

    MessageHandler handler;
    handler
        .on<MockMsgA>(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++a_count; }))
        .on<MockMsgB>(std::function<void(const MockMsgB&)>(
            [&](const MockMsgB&) { ++b_count; }));

    registry.install_handler(peer, handler);

    // Dispatch only MockMsgA
    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(a_count == 1);
    CHECK(b_count == 0);
}

TEST_CASE("MessageHandler: on_group multiple type_ids", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    std::vector<uint64_t> received_type_ids;

    MessageHandler handler;
    handler.on_group({MockMsgA::TYPE_ID, MockMsgB::TYPE_ID},
        [&](uint64_t tid, const std::any&) {
            received_type_ids.push_back(tid);
        });

    registry.install_handler(peer, handler);

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));

    REQUIRE(received_type_ids.size() == 2);
    CHECK(received_type_ids[0] == MockMsgA::TYPE_ID);
    CHECK(received_type_ids[1] == MockMsgB::TYPE_ID);
}

TEST_CASE("MessageHandler: on_any catches unhandled types", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int typed_count = 0;
    uint64_t catch_all_tid = 0;

    MessageHandler handler;
    handler
        .on<MockMsgA>(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++typed_count; }))
        .on_any([&](uint64_t tid, const std::any&) {
            catch_all_tid = tid;
        });

    registry.install_handler(peer, handler);

    // Dispatch MockMsgB — should go to catch-all
    registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));

    CHECK(typed_count == 0);
    CHECK(catch_all_tid == MockMsgB::TYPE_ID);
}

TEST_CASE("MessageHandler: last handler for same type_id wins", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int typed_count = 0;
    int group_count = 0;
    int catch_all_count = 0;

    // Both typed and group handlers have the same type_id (MockMsgA::TYPE_ID).
    // install_handler uses insert_or_assign: the last handler added for a given
    // type_id replaces the previous one.  Group is added after typed, so group wins.
    MessageHandler handler;
    handler
        .on<MockMsgA>(std::function<void(const MockMsgA&)>(
            [&](const MockMsgA&) { ++typed_count; }))
        .on_group({MockMsgA::TYPE_ID},
            [&](uint64_t, const std::any&) { ++group_count; })
        .on_any([&](uint64_t, const std::any&) { ++catch_all_count; });

    registry.install_handler(peer, handler);

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    // Group was registered last for this type_id, so it replaces the typed handler
    CHECK(typed_count == 0);
    CHECK(group_count == 1);
    CHECK(catch_all_count == 0);
}

TEST_CASE("MessageHandler: priority - group > catch-all", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int group_count = 0;
    int catch_all_count = 0;

    MessageHandler handler;
    handler
        .on_group({MockMsgA::TYPE_ID},
            [&](uint64_t, const std::any&) { ++group_count; })
        .on_any([&](uint64_t, const std::any&) { ++catch_all_count; });

    registry.install_handler(peer, handler);

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(group_count == 1);
    CHECK(catch_all_count == 0);
}

TEST_CASE("MessageHandler: catch-all fires for unregistered type", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    uint64_t catch_all_tid = 0;

    MessageHandler handler;
    handler
        .on<MockMsgA>(std::function<void(const MockMsgA&)>(
            [](const MockMsgA&) {}))
        .on_any([&](uint64_t tid, const std::any&) {
            catch_all_tid = tid;
        });

    registry.install_handler(peer, handler);

    // Dispatch MockMsgC — no typed handler, should go to catch-all
    registry.dispatch(peer, MockMsgC::TYPE_ID, std::any(MockMsgC{}));

    CHECK(catch_all_tid == MockMsgC::TYPE_ID);
}

TEST_CASE("MessageHandler: empty handler dispatch returns false", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    MessageHandler handler;  // No handlers registered
    registry.install_handler(peer, handler);

    auto result = registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(result == DispatchResult::NotFound);
}

TEST_CASE("MessageHandler: on_any only catches all types", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int catch_all_count = 0;

    MessageHandler handler;
    handler.on_any([&](uint64_t, const std::any&) { ++catch_all_count; });

    registry.install_handler(peer, handler);

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    registry.dispatch(peer, MockMsgB::TYPE_ID, std::any(MockMsgB{}));
    registry.dispatch(peer, MockMsgC::TYPE_ID, std::any(MockMsgC{}));

    CHECK(catch_all_count == 3);
}

TEST_CASE("MessageHandler: per-peer installation isolation", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer1(1);
    PeerId peer2(2);

    int peer1_count = 0;

    MessageHandler handler;
    handler.on<MockMsgA>(std::function<void(const MockMsgA&)>(
        [&](const MockMsgA&) { ++peer1_count; }));

    registry.install_handler(peer1, handler);

    // Dispatch from peer2 — should not be handled (no handler installed for peer2)
    auto result = registry.dispatch(peer2, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(result == DispatchResult::NotFound);
    CHECK(peer1_count == 0);

    // Dispatch from peer1 — should be handled
    result = registry.dispatch(peer1, MockMsgA::TYPE_ID, std::any(MockMsgA{}));
    CHECK(result == DispatchResult::Handled);
    CHECK(peer1_count == 1);
}

TEST_CASE("MessageHandler: install replaces previous handler", "[message_handler]") {
    HandlerRegistry registry;
    PeerId peer(1);

    int v1_count = 0;
    int v2_count = 0;

    // Install v1
    MessageHandler handler_v1;
    handler_v1.on<MockMsgA>(std::function<void(const MockMsgA&)>(
        [&](const MockMsgA&) { ++v1_count; }));
    registry.install_handler(peer, handler_v1);

    // Install v2 (replaces v1 for same type)
    MessageHandler handler_v2;
    handler_v2.on<MockMsgA>(std::function<void(const MockMsgA&)>(
        [&](const MockMsgA&) { ++v2_count; }));
    registry.install_handler(peer, handler_v2);

    registry.dispatch(peer, MockMsgA::TYPE_ID, std::any(MockMsgA{}));

    CHECK(v1_count == 0);
    CHECK(v2_count == 1);
}
