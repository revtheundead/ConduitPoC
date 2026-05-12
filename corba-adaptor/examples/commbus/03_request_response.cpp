// SPDX-License-Identifier: MIT
//
// commbus example 03 — request/response via a WaitRegistry
// ========================================================
//
// This is the canonical pattern for "send a request, wait for the matching
// response that comes back through a singleton receiver".  The actors are:
//
//   * The bus task        — generates a correlation id, registers a slot
//                            under it, issues the outbound request
//                            carrying the id, parks on ctx.wait(slot).
//
//   * The registry        — a per-result-type, app-owned dispatcher.
//                            Maps correlation ids to slots.
//
//   * The "receiver"      — a long-lived singleton (here simulated by a
//                            background thread) that takes each incoming
//                            response, reads its correlation id, and calls
//                            registry.deliver(cid, response).
//
// The same pattern fits CORBA reverse-callbacks, TCP request/response,
// or any transport where the response carries the request's correlation
// id.  The bus task doesn't care which transport is involved.

#include "commbus/commbus.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace commbus;

// ─────────────────────────────────────────────────────────────────────────────
// Pretend transport: a "wire" that carries request bytes one way, and a
// background "remote service" thread that produces responses.  Real code
// substitutes this with TcpPeer / CORBA / event channel / whatever.
// ─────────────────────────────────────────────────────────────────────────────

struct Request  { uint64_t correlation_id; std::string command; };
struct Response { uint64_t correlation_id; std::string result;  };

class FakeTransport {
public:
    FakeTransport()
        : running_(true),
          server_thread_(&FakeTransport::server_loop, this) {}

    ~FakeTransport() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            running_ = false;
        }
        cv_.notify_all();
        if (server_thread_.joinable()) server_thread_.join();
    }

    // Outbound: enqueue a request into the "wire".  Real code calls
    // peer.send(...) here.
    void send(const Request& req) {
        std::lock_guard<std::mutex> lock(mu_);
        wire_.push_back(req);
        cv_.notify_all();
    }

    // Inbound: the receiver registers a per-type listener.  In real code
    // this is wired via TcpPeer::on<T>(handler) or a CORBA servant.
    using ResponseSink = std::function<void(const Response&)>;
    void set_response_sink(ResponseSink sink) { sink_ = std::move(sink); }

private:
    void server_loop() {
        while (true) {
            Request req;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [&]{ return !running_ || !wire_.empty(); });
                if (!running_ && wire_.empty()) return;
                req = wire_.front();
                wire_.pop_front();
            }
            // Simulate work latency.
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            Response resp;
            resp.correlation_id = req.correlation_id;
            resp.result = "echo:" + req.command;
            if (sink_) sink_(resp);
        }
    }

    std::mutex                  mu_;
    std::condition_variable     cv_;
    std::deque<Request>         wire_;
    bool                        running_;
    std::thread                 server_thread_;
    ResponseSink                sink_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Application wiring
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    // App-owned long-lived objects.
    CommBus              bus(CommBus::Config(/*queue*/ 64, /*workers*/ 4));
    WaitRegistry<Response> registry;
    FakeTransport        transport;

    // The "singleton receiver" — wired ONCE at app startup.  Every
    // inbound Response is routed through the registry by correlation id.
    transport.set_response_sink([&registry](const Response& resp){
        bool delivered = registry.deliver(resp.correlation_id, resp);
        if (!delivered) {
            // Late response, no waiter — log and drop.  Common at shutdown.
            std::fprintf(stderr,
                "[receiver] orphan response cid=%llu, dropping\n",
                static_cast<unsigned long long>(resp.correlation_id));
        }
    });

    // ────── Issue several parallel requests through the bus ──────────────────
    //
    // Each task is identical except for its outbound payload.  They run
    // in parallel up to the worker count (4); a slow remote system
    // doesn't head-of-line block flows behind it.

    std::vector<std::future<Result<Response>>> futures;
    for (int i = 0; i < 6; ++i) {
        futures.push_back(bus.submit_with_result<Response>(
            [i, &registry, &transport](Context& ctx) -> Result<Response> {
                // (1) Mint a fresh correlation id and a slot for the reply.
                uint64_t cid  = registry.next_correlation_id();
                auto     slot = ctx.make_slot<Response>();

                // (2) RAII-register the slot in the registry.  WaitGuard
                //     guarantees unregister() on every exit path, including
                //     cancellation and exception, so the registry never
                //     leaks entries.
                WaitGuard<Response> guard(registry, cid, slot);
                if (!guard.ok()) {
                    return cpp11::make_unexpected(guard.last_error());
                }

                // (3) Ship the request carrying the correlation id.
                //     Real code: peer.send(MyRequest{..., cid}); or a
                //     CORBA invocation with cid as a parameter.
                Request req;
                req.correlation_id = cid;
                req.command = "do-thing-" + std::to_string(i);
                transport.send(req);

                // (4) Park the worker until the receiver delivers, the
                //     5s deadline expires, or the bus is shutting down.
                return ctx.wait(slot, std::chrono::seconds(5));
            }));
    }

    // ────── Collect results ───────────────────────────────────────────────────
    for (std::size_t i = 0; i < futures.size(); ++i) {
        auto r = futures[i].get();
        if (!r) {
            std::cerr << "task " << i << " failed: "
                      << r.error().format() << '\n';
        } else {
            std::printf("task %zu got: cid=%llu result='%s'\n",
                        i,
                        static_cast<unsigned long long>(r->correlation_id),
                        r->result.c_str());
        }
    }

    // ────── Shutdown contract demonstration ──────────────────────────────────
    //
    // On shutdown, fail every pending registration so any stranded
    // tasks wake from ctx.wait().  Then stop the bus.  Real apps wire
    // this into their shutdown sequence (e.g. Adaptor::stop()).

    registry.fail_all(Error(ErrorCode_BusStopped,
                            "shutting down: cancelling pending requests"));
    bus.stop();

    std::printf("registry pending after fail_all: %zu (expected 0)\n",
                registry.pending_count());

    return 0;
}
