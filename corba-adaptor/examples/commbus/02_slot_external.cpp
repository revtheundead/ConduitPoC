// SPDX-License-Identifier: MIT
//
// commbus example 02 — waiting on an external event with a Slot
// ==============================================================
//
// Demonstrates the load-bearing pattern: a task body that needs to wait
// for something arriving on a *different* thread.  Here the "external
// event" is just a `std::thread` that sleeps and then fulfils the slot;
// the same pattern works for CORBA reply handlers, timer callbacks,
// async DB drivers, signal handlers, etc.  The slot doesn't know what's
// on the other side, so any of those work with zero changes.
//
// Three points illustrated:
//   * make_slot + ctx.wait round-trips between the bus worker and an
//     external thread.
//   * Timeouts.  ctx.wait honors a deadline; the producer never arrives.
//   * Failure propagation.  An external producer can resolve the slot
//     with an error via slot->fail(...), and the task body sees it as
//     a normal Result<T> error.

#include "commbus/commbus.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace commbus;

// Simulates an external subsystem that, asynchronously, produces a
// result for whoever holds the slot.  Could be a CORBA reply handler,
// a timer, anything — the slot doesn't care.
void external_producer(std::shared_ptr<Slot<int>> slot,
                       std::chrono::milliseconds delay,
                       bool succeed) {
    std::thread([slot, delay, succeed]{
        std::this_thread::sleep_for(delay);
        if (succeed) {
            slot->fulfil(1234);
        } else {
            slot->fail(Error(ErrorCode_ExternalFailure,
                             "external subsystem rejected the request"));
        }
    }).detach();
}

int main() {
    CommBus bus(CommBus::Config(/*queue*/ 16, /*workers*/ 2));
    bus.set_error_sink([](const Error& e){
        std::cerr << "[bus] " << e.format() << '\n';
    });

    // ────── Case 1: successful external producer ──────────────────────────────
    {
        auto fut = bus.submit_with_result<int>(
            [](Context& ctx) -> Result<int> {
                auto slot = ctx.make_slot<int>();
                // Hand the slot to whatever external machinery fulfils
                // it.  Here it's a side thread; in real code it's a
                // CORBA reply servant that holds a shared_ptr to slot.
                external_producer(slot,
                                  std::chrono::milliseconds(50),
                                  /*succeed*/ true);
                // Park the worker on the slot.  The producer wakes us.
                return ctx.wait(slot, std::chrono::seconds(1));
            });
        Result<int> r = fut.get();
        if (!r) std::cerr << "case 1 failed: " << r.error().format() << '\n';
        else    std::printf("case 1 got: %d\n", *r);
    }

    // ────── Case 2: timeout (no producer arrives) ─────────────────────────────
    {
        auto fut = bus.submit_with_result<int>(
            [](Context& ctx) -> Result<int> {
                auto slot = ctx.make_slot<int>();
                // Deliberately don't fulfil anything.
                return ctx.wait(slot, std::chrono::milliseconds(50));
            });
        Result<int> r = fut.get();
        if (r) std::cerr << "case 2: expected timeout, got value\n";
        else   std::printf("case 2 timed out as expected: %s\n",
                           r.error().format().c_str());
    }

    // ────── Case 3: external failure ──────────────────────────────────────────
    {
        auto fut = bus.submit_with_result<int>(
            [](Context& ctx) -> Result<int> {
                auto slot = ctx.make_slot<int>();
                external_producer(slot,
                                  std::chrono::milliseconds(30),
                                  /*succeed*/ false);
                return ctx.wait(slot, std::chrono::seconds(1));
            });
        Result<int> r = fut.get();
        if (r) std::cerr << "case 3: expected failure, got value\n";
        else   std::printf("case 3 failed as expected: %s\n",
                           r.error().format().c_str());
    }

    // ────── Case 4: cancellation during a wait ────────────────────────────────
    //
    // A long-lived task that gets cancelled while parked on a slot.
    // bus.stop() flips every running task's stop_requested(), which
    // wakes ctx.wait() with Cancelled.

    auto cancel_fut = bus.submit_with_result<int>(
        [](Context& ctx) -> Result<int> {
            auto slot = ctx.make_slot<int>();
            // Wait essentially forever.
            return ctx.wait(slot, std::chrono::hours(1));
        });
    // Give it a moment to enter wait, then shut the bus down.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bus.stop();
    Result<int> r = cancel_fut.get();
    if (r) std::cerr << "case 4: expected cancel, got value\n";
    else   std::printf("case 4 cancelled as expected: %s\n",
                       r.error().format().c_str());

    return 0;
}
