// SPDX-License-Identifier: MIT
// Conduit - BoundedQueue Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/queue/bounded_queue.hpp>
#include <thread>
#include <string>

using namespace conduit::queue;

TEST_CASE("BoundedQueue basic push/pop", "[bounded_queue]") {
    BoundedQueue<int> q(4);

    CHECK(q.capacity() == 4);
    CHECK(q.empty());
    CHECK(q.size() == 0);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);

    CHECK(q.size() == 3);
    CHECK_FALSE(q.empty());
    CHECK_FALSE(q.full());

    auto v1 = q.try_pop();
    REQUIRE(v1.has_value());
    CHECK(*v1 == 1);

    auto v2 = q.try_pop();
    REQUIRE(v2.has_value());
    CHECK(*v2 == 2);

    auto v3 = q.try_pop();
    REQUIRE(v3.has_value());
    CHECK(*v3 == 3);

    CHECK(q.empty());
    CHECK(q.try_pop() == std::nullopt);
}

TEST_CASE("BoundedQueue DropOldest policy", "[bounded_queue]") {
    BoundedQueue<int> q(3, DropPolicy::DropOldest);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    CHECK(q.full());

    // Push when full: should drop oldest (1)
    q.try_push(4);
    CHECK(q.size() == 3);

    CHECK(*q.try_pop() == 2);
    CHECK(*q.try_pop() == 3);
    CHECK(*q.try_pop() == 4);
}

TEST_CASE("BoundedQueue DropNewest policy", "[bounded_queue]") {
    BoundedQueue<int> q(3, DropPolicy::DropNewest);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);

    // Push when full: should reject new item
    bool pushed = q.try_push(4);
    CHECK_FALSE(pushed);
    CHECK(q.size() == 3);

    CHECK(*q.try_pop() == 1);
    CHECK(*q.try_pop() == 2);
    CHECK(*q.try_pop() == 3);
}

TEST_CASE("BoundedQueue Block policy", "[bounded_queue]") {
    BoundedQueue<int> q(2, DropPolicy::Block);

    q.try_push(1);
    q.try_push(2);

    // try_push returns false when full in Block mode
    CHECK_FALSE(q.try_push(3));

    // But push with blocking should wait
    // Pop one item first to make room
    q.try_pop();
    CHECK(q.push(3));
}

TEST_CASE("BoundedQueue close", "[bounded_queue]") {
    BoundedQueue<int> q(4);

    q.try_push(1);
    q.try_push(2);
    q.close();

    CHECK(q.is_closed());

    // Can still pop existing items
    CHECK(*q.try_pop() == 1);
    CHECK(*q.try_pop() == 2);
    CHECK(q.try_pop() == std::nullopt);

    // Cannot push after close
    CHECK_FALSE(q.try_push(3));
}

TEST_CASE("BoundedQueue pop_batch", "[bounded_queue]") {
    BoundedQueue<int> q(8);

    for (int i = 0; i < 5; ++i) {
        q.try_push(i);
    }

    auto batch = q.pop_batch(3);
    CHECK(batch.size() == 3);
    CHECK(batch[0] == 0);
    CHECK(batch[1] == 1);
    CHECK(batch[2] == 2);
    CHECK(q.size() == 2);
}

TEST_CASE("BoundedQueue pop_batch more than available", "[bounded_queue]") {
    BoundedQueue<int> q(8);

    q.try_push(1);
    q.try_push(2);

    auto batch = q.pop_batch(10);
    CHECK(batch.size() == 2);
    CHECK(q.empty());
}

TEST_CASE("BoundedQueue stats", "[bounded_queue]") {
    BoundedQueue<int> q(3, DropPolicy::DropOldest);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    q.try_push(4);  // Drops oldest

    auto snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == 4);
    CHECK(snap.dropped == 1);
    CHECK(snap.current_size == 3);
    CHECK(snap.peak_size == 3);

    q.try_pop();
    q.try_pop();

    snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.dequeued == 2);
    CHECK(snap.current_size == 1);
}

TEST_CASE("BoundedQueue clear", "[bounded_queue]") {
    BoundedQueue<int> q(4);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    CHECK(q.size() == 3);

    q.clear();
    CHECK(q.empty());
    CHECK(q.size() == 0);
}

TEST_CASE("BoundedQueue minimum capacity", "[bounded_queue]") {
    // Capacity 0 should be bumped to 1
    BoundedQueue<int> q(0);
    CHECK(q.capacity() == 1);

    q.try_push(42);
    CHECK(*q.try_pop() == 42);
}

TEST_CASE("BoundedQueue with move-only types", "[bounded_queue]") {
    BoundedQueue<std::unique_ptr<int>> q(4);

    q.try_push(std::make_unique<int>(42));
    q.try_push(std::make_unique<int>(99));

    auto v = q.try_pop();
    REQUIRE(v.has_value());
    CHECK(**v == 42);
}

TEST_CASE("BoundedQueue concurrent push/pop", "[bounded_queue]") {
    BoundedQueue<int> q(1024, DropPolicy::Block);
    constexpr int count = 1000;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            q.push(i);
        }
    });

    std::thread consumer([&] {
        int expected = 0;
        while (expected < count) {
            auto val = q.pop_for(std::chrono::milliseconds(100));
            if (val.has_value()) {
                REQUIRE(*val == expected);
                ++expected;
            }
        }
    });

    producer.join();
    consumer.join();

    CHECK(q.empty());
    auto snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == count);
    CHECK(snap.dequeued == count);
    CHECK(snap.dropped == 0);
}

TEST_CASE("BoundedQueue blocking pop unblocks on close", "[bounded_queue]") {
    BoundedQueue<int> q(4);

    std::thread t([&] {
        // This should block until close()
        auto val = q.pop();
        CHECK_FALSE(val.has_value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.close();
    t.join();
}

TEST_CASE("BoundedQueue pop_for returns nullopt on timeout", "[bounded_queue]") {
    BoundedQueue<int> q(4);
    // Queue is empty — pop_for should return nullopt after timeout
    auto val = q.pop_for(std::chrono::milliseconds(20));
    CHECK_FALSE(val.has_value());
}

TEST_CASE("BoundedQueue push_for timeout", "[bounded_queue]") {
    BoundedQueue<int> q(1, DropPolicy::Block);
    q.try_push(1);  // Fill queue

    auto start = std::chrono::steady_clock::now();
    bool pushed = q.push_for(2, std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK_FALSE(pushed);
    CHECK(elapsed >= std::chrono::milliseconds(40));
}

TEST_CASE("BoundedQueue MPMC stress: 4 producers 4 consumers", "[bounded_queue]") {
    constexpr int items_per_producer = 500;
    constexpr int num_producers = 4;
    constexpr int num_consumers = 4;
    constexpr int total_items = items_per_producer * num_producers;

    BoundedQueue<int> q(64, DropPolicy::Block);

    std::atomic<int> total_consumed{0};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Producers
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < items_per_producer; ++i) {
                q.push(p * items_per_producer + i);
            }
        });
    }

    // Consumers
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&] {
            while (true) {
                auto val = q.pop_for(std::chrono::milliseconds(200));
                if (!val.has_value()) {
                    // Timeout or closed — check if done
                    if (total_consumed.load() >= total_items) break;
                    if (q.is_closed()) break;
                    continue;
                }
                total_consumed.fetch_add(1);
            }
        });
    }

    // Wait for all producers
    for (auto& t : producers) t.join();

    // Wait until all items consumed or timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (total_consumed.load() < total_items &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    q.close();
    for (auto& t : consumers) t.join();

    CHECK(total_consumed.load() == total_items);

    auto snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == total_items);
    CHECK(snap.dropped == 0);
}

TEST_CASE("BoundedQueue blocking push unblocks on close", "[bounded_queue]") {
    BoundedQueue<int> q(1, DropPolicy::Block);
    q.try_push(1);  // Fill queue

    std::thread t([&] {
        // This should block until close()
        bool pushed = q.push(99);
        CHECK_FALSE(pushed);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.close();
    t.join();
}

TEST_CASE("BoundedQueue push_for on closed queue returns false", "[bounded_queue]") {
    BoundedQueue<int> q(4, DropPolicy::Block);
    q.close();

    bool pushed = q.push_for(42, std::chrono::milliseconds(50));
    CHECK_FALSE(pushed);
}

TEST_CASE("BoundedQueue pop_batch on empty queue", "[bounded_queue]") {
    BoundedQueue<int> q(8);

    auto batch = q.pop_batch(10);
    CHECK(batch.empty());
}

TEST_CASE("BoundedQueue reset_stats", "[bounded_queue]") {
    BoundedQueue<int> q(3, DropPolicy::DropOldest);

    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    q.try_push(4);  // Drops oldest

    auto snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == 4);
    CHECK(snap.dropped == 1);

    q.reset_stats();
    snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == 0);
    CHECK(snap.dequeued == 0);
    CHECK(snap.dropped == 0);
    CHECK(snap.peak_size == 0);
    CHECK(snap.current_size == 0);
}

TEST_CASE("BoundedQueue clear then reuse", "[bounded_queue]") {
    BoundedQueue<int> q(4);

    q.try_push(1);
    q.try_push(2);
    q.clear();
    CHECK(q.empty());

    // Queue should work normally after clear
    q.try_push(10);
    q.try_push(20);
    CHECK(q.size() == 2);
    CHECK(*q.try_pop() == 10);
    CHECK(*q.try_pop() == 20);
}

TEST_CASE("BoundedQueue full() and empty() consistency", "[bounded_queue]") {
    BoundedQueue<int> q(2);

    CHECK(q.empty());
    CHECK_FALSE(q.full());

    q.try_push(1);
    CHECK_FALSE(q.empty());
    CHECK_FALSE(q.full());

    q.try_push(2);
    CHECK_FALSE(q.empty());
    CHECK(q.full());

    q.try_pop();
    CHECK_FALSE(q.empty());
    CHECK_FALSE(q.full());

    q.try_pop();
    CHECK(q.empty());
    CHECK_FALSE(q.full());
}

TEST_CASE("BoundedQueue DropOldest under contention", "[bounded_queue]") {
    BoundedQueue<int> q(8, DropPolicy::DropOldest);
    constexpr int count = 500;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            q.try_push(i);
        }
    });

    std::thread consumer([&] {
        int consumed = 0;
        for (int i = 0; i < count; ++i) {
            auto val = q.pop_for(std::chrono::milliseconds(100));
            if (val.has_value()) ++consumed;
        }
    });

    producer.join();
    consumer.join();

    auto snap = QueueStatsSnapshot::from(q.stats());
    CHECK(snap.enqueued == count);
    // Some items may have been dropped — that's expected with DropOldest
    CHECK(snap.enqueued == snap.dequeued + snap.dropped + snap.current_size);
}
