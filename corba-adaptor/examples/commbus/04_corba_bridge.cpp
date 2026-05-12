// SPDX-License-Identifier: MIT
//
// commbus example 04 — bridging a CORBA servant method to the bus
// ===============================================================
//
// This file does not link against TAO (so it builds in any environment),
// but it lays out the exact shape you'd write inside a real CORBA
// servant method.  The "CORBA call" is simulated by a function that:
//   * takes a callback object reference,
//   * runs work on a background thread,
//   * later calls the callback with the result.
//
// Two distinct bridge patterns are shown:
//
//   A) Blocking servant method: the ORB thread blocks on a future
//      from submit_with_result while a bus worker drives the call.
//
//   B) Non-blocking servant method: the ORB thread returns immediately
//      after submit_with_callback; the completion callback is what
//      eventually invokes the CORBA reply handler.
//
// The bus is created once at app start and shared by every servant.

#include "commbus/commbus.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace commbus;

// ─────────────────────────────────────────────────────────────────────────────
// Stand-ins for the CORBA pieces.  In real code:
//   ThingResult  -> CorbaAdaptor::ThingResult IDL struct
//   ThingReplyCb -> CorbaAdaptor::ThingResultHandler IDL interface (servant)
//   remote_obj   -> CorbaAdaptor::ThingService_var
// ─────────────────────────────────────────────────────────────────────────────

struct ThingResult { std::string detail; };

using ThingReplyCb = std::function<void(const ThingResult&)>;

// Pretend remote service: takes a callback, completes asynchronously.
void invoke_remote_thing(const std::string& params, ThingReplyCb cb) {
    std::thread([params, cb]{
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        ThingResult r;
        r.detail = "remote answered: " + params;
        cb(r);  // CORBA reverse-callback would call into our reply servant
    }).detach();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: encapsulate the slot/await dance for an outbound CORBA call.
// Real version uses a singleton reply-handler servant + WaitRegistry;
// here we capture the slot directly because there's no concurrent
// correlation problem in this single-call example.
// ─────────────────────────────────────────────────────────────────────────────

Result<ThingResult>
await_remote_thing(Context& ctx,
                   const std::string& params,
                   std::chrono::milliseconds timeout) {
    auto slot = ctx.make_slot<ThingResult>();

    // Hand a fulfilment closure to the remote service.  In CORBA-land
    // this is a small servant whose only method calls slot->fulfil().
    // Capture slot by VALUE (shared_ptr) so a late callback after the
    // task has given up is harmless — the slot stays alive long enough
    // to absorb the write, and the result is simply never read.
    invoke_remote_thing(params,
        [slot](const ThingResult& r) {
            slot->fulfil(r);
        });

    return ctx.wait(slot, timeout);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servant simulation
// ─────────────────────────────────────────────────────────────────────────────

// Real signature:
//   CorbaAdaptor::CommandResult MyServant::do_thing(const ParamsT& p);
//
// We pretend an ORB thread is running this.

struct CorbaCommandResult { bool ok; std::string detail; };

CorbaCommandResult servant_do_thing_blocking(CommBus& bus,
                                             const std::string& params) {
    // The ORB thread submits a bus task and waits on the returned future.
    // bus workers run on different threads than ORB workers, so this is
    // race-free.  fut.get() ONLY blocks the ORB thread — it never
    // occupies a bus worker, so other CORBA calls can still come in.

    auto fut = bus.submit_with_result<ThingResult>(
        [params](Context& ctx) -> Result<ThingResult> {
            return await_remote_thing(ctx, params,
                                      std::chrono::seconds(2));
        });

    // Outer CORBA SLA — independent of the bus task's own timeout.
    if (fut.wait_for(std::chrono::seconds(5))
        != std::future_status::ready) {
        return CorbaCommandResult{ false, "CORBA SLA exceeded" };
    }

    Result<ThingResult> outcome = fut.get();
    if (!outcome) {
        return CorbaCommandResult{ false,
            "bus task failed: " + outcome.error().format() };
    }
    return CorbaCommandResult{ true, outcome->detail };
}

// Real signature:
//   void MyServant::do_thing_async(const ParamsT& p,
//                                  CorbaAdaptor::ReplyHandler_ptr reply);

using OrbReplyCallback = std::function<void(CorbaCommandResult)>;

void servant_do_thing_async(CommBus& bus,
                            const std::string& params,
                            OrbReplyCallback orb_reply) {
    // The ORB thread submits a bus task with a completion callback,
    // then returns immediately.  When the task completes (later), the
    // callback fires on a bus worker and invokes the CORBA reply
    // handler — which is itself a (one-way or otherwise) CORBA call
    // back to the original caller.

    bus.submit_with_callback<ThingResult>(
        [params](Context& ctx) -> Result<ThingResult> {
            return await_remote_thing(ctx, params,
                                      std::chrono::seconds(2));
        },
        [orb_reply](Result<ThingResult> r) {
            // The completion callback runs on a bus worker thread,
            // so any work here is OK; calling back into CORBA is fine
            // as long as you wrap it for CORBA::Exception safety.
            try {
                if (!r) {
                    orb_reply(CorbaCommandResult{ false,
                        "task failed: " + r.error().format() });
                } else {
                    orb_reply(CorbaCommandResult{ true, r->detail });
                }
            } catch (const std::exception& e) {
                // E.g. caller's CORBA reference died; log and drop.
                std::fprintf(stderr,
                    "[callback] CORBA reply failed: %s\n", e.what());
            }
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Driver
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    CommBus bus(CommBus::Config(/*queue*/ 64, /*workers*/ 4));
    bus.set_error_sink([](const Error& e){
        std::cerr << "[bus] " << e.format() << '\n';
    });

    // ────── Blocking pattern ──────────────────────────────────────────────────
    {
        auto result = servant_do_thing_blocking(bus, "test-blocking");
        std::printf("blocking servant returned ok=%d detail='%s'\n",
                    result.ok ? 1 : 0, result.detail.c_str());
    }

    // ────── Async pattern ─────────────────────────────────────────────────────
    {
        std::atomic<bool> done(false);
        servant_do_thing_async(bus, "test-async",
            [&done](CorbaCommandResult r){
                std::printf("async reply ok=%d detail='%s'\n",
                            r.ok ? 1 : 0, r.detail.c_str());
                done.store(true);
            });
        // Wait for the reply to land (the ORB itself would just return
        // to the client; the client's reply handler servant is what
        // would receive the callback).
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    bus.stop();
    return 0;
}
