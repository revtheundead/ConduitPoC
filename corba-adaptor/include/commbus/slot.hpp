// SPDX-License-Identifier: MIT
//
// commbus/slot.hpp
// ================
//
// `Slot<T>` is a one-shot, thread-safe rendezvous primitive.  It is the
// load-bearing piece of the CommBus design: anywhere a task body needs to
// wait for something that arrives on a different thread (a CORBA callback,
// a timer, a DB driver completion, an event-channel publish, another
// thread's signal), the task creates a Slot, hands it to whatever will
// fulfil it, and parks itself on `Context::wait(slot, timeout)`.
//
// Lifetime is always managed via `shared_ptr<Slot<T>>`.  This is essential:
// a late callback that arrives after the waiter has given up must not
// crash — its shared_ptr keeps the slot alive long enough for `fulfil` /
// `fail` to write into it; the result is simply never read.
//
// Slots are one-shot.  The first `fulfil` or `fail` wins; subsequent calls
// are no-ops.  This makes "racy producers" (e.g. a CORBA reply that arrives
// *just* as the timeout fires) safe by construction.
//
// The slot intentionally knows nothing about CORBA, TCP, or any other
// transport.  It is a generic synchronization point.

#ifndef COMMBUS_SLOT_HPP
#define COMMBUS_SLOT_HPP

#include <condition_variable>
#include <mutex>
#include <utility>

#include <compat11/optional.hpp>

#include "error.hpp"

namespace commbus {

template <typename T>
class Slot {
public:
    Slot() = default;

    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    Slot(Slot&&) = delete;
    Slot& operator=(Slot&&) = delete;

    // Resolve the slot with a value.  Callable from any thread, at most
    // once-effective: the first fulfil/fail wins, subsequent calls are
    // silently dropped (so racing "I have a result" / "the timeout fired
    // and I am giving up" producers are both safe).
    void fulfil(T value) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (resolved_) return;
            value_    = std::move(value);
            resolved_ = true;
        }
        cv_.notify_all();
    }

    // Resolve the slot with an error.  Same once-effective semantics as
    // fulfil().
    void fail(Error err) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (resolved_) return;
            error_    = std::move(err);
            resolved_ = true;
        }
        cv_.notify_all();
    }

    // Whether the slot has been resolved (in either direction).
    // Useful for diagnostics; the value/error itself is consumed via
    // Context::wait.
    bool is_resolved() const {
        std::lock_guard<std::mutex> lock(mu_);
        return resolved_;
    }

private:
    // Context::wait reaches in via friendship to grab the result without
    // duplicating the mutex.  Only Context is allowed to consume; everyone
    // else can only fulfil/fail.
    template <typename U> friend class WaitOps;
    friend class Context;

    mutable std::mutex                          mu_;
    std::condition_variable                     cv_;
    bool                                        resolved_ = false;
    cpp11::optional<T>                          value_;
    cpp11::optional<Error>                      error_;
};

} // namespace commbus

#endif
