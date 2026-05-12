// SPDX-License-Identifier: MIT
//
// commbus example 01 — the three submission patterns
// ===================================================
//
// Demonstrates the three ways a caller can hand work to the bus:
//
//   1. submit                — fire-and-forget; result via error sink only
//   2. submit_with_result    — blocking; caller does fut.get()
//   3. submit_with_callback  — async; caller supplies a completion callback
//
// All three accept the same task signature: `[](Context&)`.  The bus is
// transport-agnostic — tasks here are pure computation.  Later examples
// add slots, registries, and real external dependencies.

#include "commbus/commbus.hpp"

#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>

using namespace commbus;

int main() {
    CommBus bus(CommBus::Config(/*queue*/ 16, /*workers*/ 2));

    // Optional: route stray errors (fire-and-forget task failures,
    // callback exceptions) to your logger of choice.
    bus.set_error_sink([](const Error& e){
        std::cerr << "[bus error sink] " << e.format() << '\n';
    });

    // ────── Pattern 1: fire-and-forget ────────────────────────────────────────
    //
    // Use when the task produces side effects only (publish, log, etc.)
    // and the caller doesn't need a return value.  The submit call
    // returns immediately after enqueueing.

    auto ack = bus.submit([](Context& ctx){
        (void)ctx;
        std::printf("[task A] running, fire-and-forget\n");
    });
    if (!ack) {
        std::cerr << "submit refused: " << ack.error().format() << '\n';
    }

    // ────── Pattern 2: blocking via future ────────────────────────────────────
    //
    // Use when the caller's current thread has nothing else to do until
    // the result is ready.  Typical for CORBA servant methods that
    // need to synthesize a reply value.

    std::future<Result<int>> fut = bus.submit_with_result<int>(
        [](Context& ctx) -> Result<int> {
            (void)ctx;
            std::printf("[task B] computing answer\n");
            return 42;
        });

    Result<int> r = fut.get();    // blocks this thread
    if (!r) {
        std::cerr << "task B failed: " << r.error().format() << '\n';
    } else {
        std::printf("[main] task B returned: %d\n", *r);
    }

    // ────── Pattern 3: callback ────────────────────────────────────────────────
    //
    // Use when the caller should not block — typical for CORBA reverse-
    // callback flows where the original CORBA method must return quickly.
    // The completion callback runs on a bus worker thread once the task
    // has produced its result (or thrown).
    //
    // The callback is invoked EXACTLY ONCE:
    //   - synchronously if submit refuses (with QueueFull / BusStopped),
    //   - asynchronously if accepted (with the task's Result or TaskThrew).

    bus.submit_with_callback<std::string>(
        [](Context& ctx) -> Result<std::string> {
            (void)ctx;
            std::printf("[task C] producing a string\n");
            return std::string("hello, async");
        },
        [](Result<std::string> r) {
            if (!r) {
                std::cerr << "task C failed: " << r.error().format() << '\n';
            } else {
                std::printf("[callback] task C returned: %s\n", r->c_str());
            }
        });

    // Wait for everything to drain before tearing down the bus.
    // In a real app the bus lives for the process lifetime — this is
    // only needed because main() will return immediately otherwise.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bus.stop();
    return 0;
}
