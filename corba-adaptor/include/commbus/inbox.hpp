// SPDX-License-Identifier: MIT
//
// commbus/inbox.hpp
// =================
//
// `Inbox<T>` is a bounded, thread-safe queue + condition variable that a
// task can drain at its own pace.  Unlike `Slot<T>` (one-shot, single
// value), an inbox accepts an unbounded *stream* of values pushed from
// any thread; the owning task pulls them one at a time and decides when
// to stop.
//
// Use this primitive when:
//
//   * The flow needs to read several messages of the same type in order
//     ("drain until I find one that satisfies my predicate").
//   * There's no correlation field tying request to response — the task
//     receives every relevant message and uses content / timing / state
//     diff to identify the one it cares about.
//   * The flow needs to observe a continuous periodic stream.
//
// Use `Slot<T>` instead when there's exactly one expected reply.
//
// Transport-agnostic by construction.  The Inbox doesn't know where its
// inputs come from.  Producers (TcpPeer's typed handler, a CORBA event
// subscriber, a timer, another bus task) all just call `push(value)`.
//
// Held via `std::shared_ptr` so producers and consumers may have
// independent lifetimes — a late producer that pushes after the
// consumer has dropped its reference simply pushes into an inbox no
// one will read; the value is discarded when the last ref drops.
//
// For bus-shutdown-aware consumption, use `Context::recv(inbox,
// timeout)` from inside a task body.  The bare `Inbox::recv(timeout)`
// is also available for non-task code (initialization helpers,
// stand-alone utilities) but it only wakes on push / close / timeout —
// not on bus cancellation.

#ifndef COMMBUS_INBOX_HPP
#define COMMBUS_INBOX_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

#include <compat11/optional.hpp>

#include "error.hpp"

namespace commbus {

template <typename T>
class Inbox {
public:
    // Overflow policy when push() hits capacity.  DropOldest is the
    // default — it preserves recent state at the cost of losing the
    // earliest queued messages (and bumping dropped_count_).  DropNewest
    // refuses the incoming push instead, leaving the queue intact.
    enum OverflowPolicy { DropOldest = 0, DropNewest = 1 };

    explicit Inbox(std::size_t capacity = 64,
                   OverflowPolicy policy = DropOldest)
        : capacity_(capacity == 0 ? 1 : capacity),
          policy_(policy),
          closed_(false),
          dropped_count_(0) {}

    Inbox(const Inbox&)            = delete;
    Inbox& operator=(const Inbox&) = delete;

    // ────── Producer side ───────────────────────────────────────────────────
    //
    // Safe to call from any thread (TCP recv thread, CORBA ORB worker,
    // timer thread, another bus task...).  Always non-blocking.

    void push(T value) {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (closed_) return;            // late producer after close
            if (queue_.size() >= capacity_) {
                if (policy_ == DropOldest) {
                    queue_.pop_front();
                    ++dropped_count_;
                } else {
                    ++dropped_count_;
                    return;
                }
            }
            queue_.push_back(std::move(value));
            notify = true;
        }
        if (notify) cv_.notify_one();
    }

    // Variant that returns false instead of dropping on overflow.
    // Useful when the producer prefers back-pressure to drops.
    bool try_push(T value) {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (closed_) return false;
            if (queue_.size() >= capacity_) return false;
            queue_.push_back(std::move(value));
            notify = true;
        }
        if (notify) cv_.notify_one();
        return true;
    }

    // ────── Consumer side ───────────────────────────────────────────────────
    //
    // Intended to be called from a single owning consumer thread.
    // Multiple concurrent recv()s are technically safe (the cv handles
    // it) but messages are then distributed arbitrarily between them.

    // Block until a value is available, the inbox is closed, or the
    // deadline elapses.  Returns:
    //   * the value on success
    //   * Closed error if close() was called and the queue is drained
    //   * Timeout error if the deadline elapsed
    //
    // Note: a closed inbox still hands out any queued values before
    // signalling Closed.  This lets you call close() during shutdown
    // without losing already-buffered work.
    Result<T> recv(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!cv_.wait_for(lock, timeout,
                          [&]{ return !queue_.empty() || closed_; })) {
            return cpp11::make_unexpected(timeout_error());
        }
        if (!queue_.empty()) {
            T value = std::move(queue_.front());
            queue_.pop_front();
            return value;
        }
        // closed_ && queue empty
        return cpp11::make_unexpected(
            Error(ErrorCode_Cancelled, "inbox closed"));
    }

    // Non-blocking pull.  Returns the next value if one is queued,
    // otherwise an empty optional.  Does not distinguish empty-and-open
    // from empty-and-closed; use is_closed() for that.
    cpp11::optional<T> try_recv() {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) return cpp11::optional<T>();
        T value = std::move(queue_.front());
        queue_.pop_front();
        return cpp11::optional<T>(std::move(value));
    }

    // ────── Lifecycle / diagnostics ─────────────────────────────────────────

    // Wake any blocked recv() and prevent further pushes.  Subsequent
    // recv() calls drain any remaining queued values before yielding
    // Cancelled.  Idempotent.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mu_);
        return closed_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }

    // Total count of values dropped by overflow.  Resets on construction;
    // monotonically increases otherwise.  Useful as a health metric:
    // a non-zero value means the consumer is falling behind producers.
    std::size_t dropped_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return dropped_count_;
    }

    std::size_t capacity() const { return capacity_; }

private:
    // Context::recv reaches in for direct mutex/cv access so that the
    // bus-aware drain can wait on the inbox's cv and the task's cancel
    // flag in a single sleep cycle.  The plain Inbox::recv above uses
    // the same internals via its own member methods.
    friend class Context;

    mutable std::mutex          mu_;
    std::condition_variable     cv_;
    std::deque<T>               queue_;
    std::size_t                 capacity_;
    OverflowPolicy              policy_;
    bool                        closed_;
    std::size_t                 dropped_count_;
};

} // namespace commbus

#endif
