// SPDX-License-Identifier: MIT
//
// commbus example 07 — Broadcaster fan-out to multiple consumers
// ===============================================================
//
// Demonstrates the zero-copy publish/subscribe pattern: one producer
// pushes messages into a Broadcaster<T>; multiple consumer tasks each
// own a private inbox subscribed to the broadcaster.  Every consumer
// sees every published message, in arrival order, without contention.
//
// Each consumer filters or processes its inbox INDEPENDENTLY.
// Discarding a message in one consumer has no effect on the others —
// every subscriber holds its own copy of the shared_ptr.
//
// Three consumers in this example:
//
//   * Consumer A — counts every reading from sensor 1.
//   * Consumer B — looks for sensor 3 with value > 80 and reports.
//   * Consumer C — bulk-counts everything.
//
// All three see the full stream.  None blocks any other.

#include "commbus/commbus.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <thread>

using namespace commbus;

// "Medium-sized" payload to show the cost story.  At 1 KB this is the
// kind of message the broadcaster saves real work on: every subscriber
// gets a shared_ptr copy, not a kilobyte memcpy.
struct Reading {
    int   sensor_id;
    int   value;
    char  blob[1024];   // simulate the bulk of a real radar/telemetry record
};

int main() {
    CommBus           bus(CommBus::Config(/*queue*/ 32, /*workers*/ 4));
    Broadcaster<Reading> bc;

    bus.set_error_sink([](const Error& e){
        std::cerr << "[bus] " << e.format() << '\n';
    });

    // ────── Spin up three concurrent consumer tasks ───────────────────────────
    //
    // Each one subscribes its own inbox.  The broadcaster delivers a
    // shared_ptr<const Reading> to each.  Even if Consumer A discards a
    // message, B and C still see it.

    std::atomic<int> a_count(0), b_hits(0), c_count(0);

    bus.submit([&bc, &a_count](Context& ctx) {
        auto inbox = bc.make_subscriber(/*capacity=*/ 128);
        while (!ctx.stop_requested()) {
            auto m = ctx.recv(inbox, std::chrono::milliseconds(500));
            if (!m) {
                if (m.error().code() == ErrorCode_Cancelled) break;
                continue;
            }
            const Reading& r = **m;     // shared_ptr<const Reading>
            if (r.sensor_id == 1) a_count.fetch_add(1);
        }
    });

    bus.submit([&bc, &b_hits](Context& ctx) {
        auto inbox = bc.make_subscriber(128);
        while (!ctx.stop_requested()) {
            auto m = ctx.recv(inbox, std::chrono::milliseconds(500));
            if (!m) {
                if (m.error().code() == ErrorCode_Cancelled) break;
                continue;
            }
            const Reading& r = **m;
            if (r.sensor_id == 3 && r.value > 80) b_hits.fetch_add(1);
        }
    });

    bus.submit([&bc, &c_count](Context& ctx) {
        auto inbox = bc.make_subscriber(128);
        while (!ctx.stop_requested()) {
            auto m = ctx.recv(inbox, std::chrono::milliseconds(500));
            if (!m) {
                if (m.error().code() == ErrorCode_Cancelled) break;
                continue;
            }
            c_count.fetch_add(1);
        }
    });

    // Give the consumer tasks a moment to subscribe before we start
    // publishing.  In real code the subscription order doesn't matter
    // — late subscribers just miss messages emitted before they
    // subscribed.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // ────── Producer: publish a burst of messages ─────────────────────────────
    std::mt19937 rng(0xC0DE);
    std::uniform_int_distribution<int> sensor(1, 5);
    std::uniform_int_distribution<int> value(0, 100);

    const int N = 500;
    for (int i = 0; i < N; ++i) {
        Reading r;
        r.sensor_id = sensor(rng);
        r.value     = value(rng);
        // Imagine populating r.blob with real payload data; left
        // uninitialized here just to keep the example terse.
        bc.publish(std::move(r));
    }

    // Let consumers drain.  In production, the consumers run for the
    // lifetime of the bus; we sleep here only because main() will
    // otherwise return immediately.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::printf("Broadcaster fan-out results after %d publishes:\n", N);
    std::printf("  Consumer A (sensor==1 count): %d\n", a_count.load());
    std::printf("  Consumer B (sensor==3 & v>80): %d\n", b_hits.load());
    std::printf("  Consumer C (total seen):       %d (expected %d)\n",
                c_count.load(), N);
    std::printf("  Live subscribers: %zu\n", bc.subscriber_count());

    // ────── Subscriber lifetime demo ──────────────────────────────────────────
    //
    // Drop one consumer's inbox by letting it go out of scope.  Its
    // weak_ptr is pruned lazily on the next publish.
    {
        auto transient = bc.make_subscriber(16);
        std::printf("  After ephemeral subscribe: %zu\n", bc.subscriber_count());
        (void)transient;
    }
    // transient is destroyed; publish triggers cleanup of its weak_ptr.
    bc.publish(Reading{0, 0, {}});
    std::printf("  After lazy cleanup publish: %zu\n", bc.subscriber_count());

    bus.stop();
    return 0;
}
