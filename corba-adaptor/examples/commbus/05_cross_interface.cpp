// SPDX-License-Identifier: MIT
//
// commbus example 05 — request via one interface, response via another
// ====================================================================
//
// The bus task initiates a request through "interface A" (here a TCP-style
// outbound) and waits for a response that arrives through "interface B"
// (here a CORBA-style event channel publisher).  Both interfaces feed the
// SAME WaitRegistry<Response>; the bus task doesn't know which interface
// will deliver — it just waits.
//
// This is the pattern for systems where a command is issued one way and
// the eventual result is announced through a separate notification path:
// e.g. send a CORBA command, receive completion via an event channel; or
// send a TCP request, receive the result via a CORBA reverse callback.

#include "commbus/commbus.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>

using namespace commbus;

struct Response { uint64_t correlation_id; std::string body; };

// ── "Interface A" — TCP-style command channel.  Outbound only. ──────────────
class FakeTcp {
public:
    FakeTcp() : running_(true), thr_(&FakeTcp::loop, this) {}
    ~FakeTcp() {
        { std::lock_guard<std::mutex> g(mu_); running_ = false; }
        cv_.notify_all();
        if (thr_.joinable()) thr_.join();
    }

    void send(uint64_t correlation_id, const std::string& cmd) {
        std::lock_guard<std::mutex> g(mu_);
        outbox_.push_back(Outgoing{correlation_id, cmd});
        cv_.notify_all();
    }

    using Sink = std::function<void(uint64_t, const std::string&)>;
    void set_completion_sink(Sink s) { sink_ = std::move(s); }

private:
    struct Outgoing { uint64_t cid; std::string cmd; };

    void loop() {
        std::mt19937 rng(0xC0FFEE);
        std::uniform_int_distribution<int> dist(20, 80);
        while (true) {
            Outgoing pkt;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [&]{ return !running_ || !outbox_.empty(); });
                if (!running_ && outbox_.empty()) return;
                pkt = outbox_.front();
                outbox_.pop_front();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
            // Pretend the remote system internally publishes via the
            // OTHER interface; this thread merely forwards the
            // "completion signal" to whoever is the publisher.
            if (sink_) sink_(pkt.cid, "tcp-acked:" + pkt.cmd);
        }
    }

    std::mutex                  mu_;
    std::condition_variable     cv_;
    std::deque<Outgoing>        outbox_;
    bool                        running_;
    std::thread                 thr_;
    Sink                        sink_;
};

// ── "Interface B" — CORBA-style notification publisher.  Inbound only. ──────
//
// The publisher is just a function the TCP transport hands its completion
// signals to.  In real code this is a CORBA servant on which the remote
// side invokes a `notify(cid, result)` method.
class FakeNotifier {
public:
    using Sink = std::function<void(const Response&)>;
    void set_sink(Sink s) { sink_ = std::move(s); }
    void notify(uint64_t cid, const std::string& body) {
        if (sink_) sink_(Response{cid, body});
    }
private:
    Sink sink_;
};

int main() {
    CommBus              bus(CommBus::Config(/*queue*/ 64, /*workers*/ 4));
    WaitRegistry<Response> registry;
    FakeTcp              tcp;
    FakeNotifier         notifier;

    // ────── Topology wiring (done once at startup) ───────────────────────────
    //
    // TCP completes a request internally and publishes the completion
    // to the CORBA-style notifier.  The notifier feeds the registry.
    // The bus task only knows about the registry.

    tcp.set_completion_sink([&notifier](uint64_t cid, const std::string& body){
        notifier.notify(cid, body);
    });
    notifier.set_sink([&registry](const Response& resp){
        bool delivered = registry.deliver(resp.correlation_id, resp);
        if (!delivered) {
            std::fprintf(stderr,
                "[notifier] orphan cid=%llu dropped\n",
                static_cast<unsigned long long>(resp.correlation_id));
        }
    });

    // ────── Concurrent flows ─────────────────────────────────────────────────
    //
    // Several flows in parallel, each sending via TCP and awaiting a
    // notification.  None of them knows or cares that the result comes
    // back through a different path.

    std::vector<std::future<Result<Response>>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(bus.submit_with_result<Response>(
            [i, &registry, &tcp](Context& ctx) -> Result<Response> {
                uint64_t cid  = registry.next_correlation_id();
                auto     slot = ctx.make_slot<Response>();

                WaitGuard<Response> guard(registry, cid, slot);
                if (!guard.ok()) {
                    return cpp11::make_unexpected(guard.last_error());
                }

                // Send via interface A.
                tcp.send(cid, "request-" + std::to_string(i));

                // Wait for interface B to deliver.
                return ctx.wait(slot, std::chrono::seconds(5));
            }));
    }

    for (std::size_t i = 0; i < futures.size(); ++i) {
        auto r = futures[i].get();
        if (!r) {
            std::cerr << "flow " << i << " failed: "
                      << r.error().format() << '\n';
        } else {
            std::printf("flow %zu got cid=%llu body='%s'\n",
                        i,
                        static_cast<unsigned long long>(r->correlation_id),
                        r->body.c_str());
        }
    }

    registry.fail_all(Error(ErrorCode_BusStopped, "shutdown"));
    bus.stop();
    return 0;
}
