// SPDX-License-Identifier: MIT
//
// commbus example 06 — draining a stream with Inbox until satisfied
// ==================================================================
//
// Demonstrates the Inbox primitive: a per-task queue that a task drains
// at its own pace, deciding when it has seen enough.  This is the
// answer for protocols where requests and responses cannot be
// correlated by id — the task observes the stream of incoming
// messages and applies its own predicate / state machine / settle
// logic to decide when it's satisfied.
//
// What's shown:
//   * A background "transport" pushes periodic messages of the same
//     type into an Inbox.
//   * The task body drains the inbox via ctx.recv (bus-shutdown aware)
//     until a message satisfying the task's predicate arrives, or until
//     the overall deadline fires.
//   * The inbox keeps going across many tasks because it's owned by
//     the application, not the task.  But each task can ALSO mint a
//     fresh inbox for its own private observation window if it wants
//     "everything from now on".

#include "commbus/commbus.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <thread>

using namespace commbus;

// Simulated periodic sensor reading.  Real code: tcp_peer::Cat021Record
// or whatever the protocol streams continuously.
struct Reading {
    int sensor_id;
    int value;
};

int main() {
    CommBus bus(CommBus::Config(/*queue*/ 32, /*workers*/ 2));
    bus.set_error_sink([](const Error& e){
        std::cerr << "[bus] " << e.format() << '\n';
    });

    // App-owned inbox.  In a real adaptor a single TcpPeer::on<T>
    // handler would push every incoming Reading here.
    auto inbox = std::make_shared<Inbox<Reading>>(/*capacity*/ 64);

    // Stand-in "periodic transport" — a background thread that pushes a
    // Reading every 30 ms with a random sensor_id and a random value.
    std::atomic<bool> producer_running(true);
    std::thread producer([&]{
        std::mt19937 rng(0xBEEF);
        std::uniform_int_distribution<int> sensor(1, 5);
        std::uniform_int_distribution<int> value(0, 100);
        while (producer_running.load()) {
            inbox->push(Reading{sensor(rng), value(rng)});
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    });

    // ────── Task: drain until satisfied ───────────────────────────────────────
    //
    // Wait for a Reading from sensor 3 with value > 80, OR give up
    // after 2 seconds total.  This is the "no correlation, scan the
    // stream" pattern: the task does not know which message it wants
    // until it sees one matching its predicate.

    auto fut = bus.submit_with_result<Reading>(
        [inbox](Context& ctx) -> Result<Reading> {
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(2);

            while (std::chrono::steady_clock::now() < deadline) {
                auto remaining = deadline - std::chrono::steady_clock::now();
                auto remaining_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        remaining);
                if (remaining_ms.count() <= 0) break;

                auto m = ctx.recv(inbox, remaining_ms);
                if (!m) {
                    // Closed / cancelled / timeout — propagate.
                    return cpp11::make_unexpected(m.error());
                }
                std::printf("[task] saw sensor=%d value=%d\n",
                            m->sensor_id, m->value);

                if (m->sensor_id == 3 && m->value > 80) {
                    return *m;     // satisfied
                }
                // Not satisfied — keep draining.
            }
            return cpp11::make_unexpected(timeout_error());
        });

    auto outcome = fut.get();
    if (!outcome) {
        std::cerr << "drain failed: " << outcome.error().format() << '\n';
    } else {
        std::printf("[main] satisfied with sensor=%d value=%d\n",
                    outcome->sensor_id, outcome->value);
    }

    // Diagnostics: how many messages were dropped because the consumer
    // (the task body) was slower than the producer?  Inbox is bounded;
    // overflow is visible, not silent.
    std::printf("[main] inbox dropped_count=%zu remaining=%zu\n",
                inbox->dropped_count(), inbox->size());

    // ────── Shutdown ─────────────────────────────────────────────────────────
    //
    // Stop the producer, close the inbox (wakes any future recv), stop
    // the bus.  Order matters: close the inbox before stop() so any
    // task currently draining sees Cancelled promptly instead of waiting
    // for the bus cancel flag to flip and the next 50 ms poll cycle.

    producer_running.store(false);
    if (producer.joinable()) producer.join();
    inbox->close();
    bus.stop();
    return 0;
}
