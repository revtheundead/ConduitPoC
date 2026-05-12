// SPDX-License-Identifier: MIT
//
// commbus/context.hpp
// ===================
//
// `Context` is the small per-task handle that a CommBus worker hands to
// each running task lambda.  It exposes exactly three operations:
//
//   * make_slot<T>()     — mint a fresh one-shot Slot<T>.
//   * wait(slot, t)      — block the worker on a slot, with timeout and
//                          full cancellation/shutdown integration.
//   * stop_requested()   — cooperative cancellation flag the bus toggles
//                          on shutdown or explicit task cancellation.
//
// Notably absent: anything transport-specific.  The context does NOT own a
// TcpPeer, a CORBA ORB reference, or any other interface.  A task that
// needs to send TCP bytes uses whatever TcpPeer reference the caller
// captured in the lambda; a task that needs to make a CORBA call uses the
// reference its caller captured.  The bus is a pure task executor —
// general over every interface.
//
// Thread safety: Context is created on a bus worker thread immediately
// before invoking the task, lives for the duration of that task only, and
// is destroyed when the task returns.  It is intentionally not copyable or
// movable.  References to it must not escape the task body.

#ifndef COMMBUS_CONTEXT_HPP
#define COMMBUS_CONTEXT_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

#include "error.hpp"
#include "slot.hpp"
#include "inbox.hpp"

namespace commbus {

class CommBus;  // fwd

class Context {
public:
    // Mint a fresh slot.  The caller and whatever-fulfils-it both hold
    // shared_ptrs; the slot lives until the last ref drops.
    template <typename T>
    std::shared_ptr<Slot<T>> make_slot() {
        return std::make_shared<Slot<T>>();
    }

    // Block the calling task on the slot until one of:
    //   * someone calls slot->fulfil(v)  -> Result<T> wraps the value
    //   * someone calls slot->fail(e)    -> Result<T> wraps the error
    //   * `timeout` elapses              -> Error{Timeout}
    //   * stop_requested() flips true    -> Error{Cancelled}
    //
    // `timeout` is a hard deadline; pass std::chrono::milliseconds::max()
    // for "wait forever".  Cancellation/shutdown wakes regardless of the
    // deadline, so even a "wait forever" task is teardown-safe.
    //
    // wait() MUST be called from the worker thread that owns this context.
    // Calling it from anywhere else is undefined behaviour.
    template <typename T>
    Result<T> wait(std::shared_ptr<Slot<T>> slot,
                   std::chrono::milliseconds timeout) {
        if (!slot) {
            return cpp11::make_unexpected(
                Error(ErrorCode_InvalidTask, "wait: null slot"));
        }

        // The cancel flag belongs to the worker, not to the slot, so a
        // cv waiting only on slot->cv_ cannot be woken by setting it.
        // We poll the flag every `poll_interval`; the slot's cv is
        // still the primary wake source for normal fulfilment, so a
        // resolved slot returns immediately without waiting the poll.
        // The poll interval bounds shutdown latency.
        const auto poll_interval = std::chrono::milliseconds(50);
        auto deadline = std::chrono::steady_clock::now() + timeout;

        std::unique_lock<std::mutex> lock(slot->mu_);
        while (true) {
            if (slot->resolved_) break;
            if (stop_requested()) {
                return cpp11::make_unexpected(cancelled_error());
            }
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return cpp11::make_unexpected(timeout_error());
            }
            auto next_wake = (now + poll_interval < deadline)
                                ? (now + poll_interval)
                                : deadline;
            slot->cv_.wait_until(lock, next_wake);
        }
        if (slot->error_) {
            return cpp11::make_unexpected(*slot->error_);
        }
        // Move the value out so a re-entered wait sees nothing — slots
        // are one-shot by contract.
        return std::move(*slot->value_);
    }

    // Bus-aware drain from an Inbox<T>.  Behaves like Inbox::recv but
    // ALSO honours the task's cancellation flag — the bus shutting down
    // while a task is mid-drain wakes recv() with Cancelled instead of
    // letting it sit for the full timeout.  Tasks consuming a stream
    // should use this rather than calling inbox->recv directly.
    //
    // Returns:
    //   * the next message on success,
    //   * Cancelled if the bus is shutting down or the inbox is closed,
    //   * Timeout if the deadline elapses with no message.
    //
    // Inbox is held by shared_ptr; this method does not take ownership.
    template <typename T>
    Result<T> recv(std::shared_ptr<Inbox<T>> inbox,
                   std::chrono::milliseconds timeout) {
        if (!inbox) {
            return cpp11::make_unexpected(
                Error(ErrorCode_InvalidTask, "recv: null inbox"));
        }

        // Poll-the-cancel-flag pattern, same as wait() for Slot.  We
        // can't natively wait on (inbox->cv_ OR cancel_flag_), so we
        // wake every poll_interval to re-check the flag.  Latency is
        // bounded at poll_interval; the inbox's cv itself wakes us
        // immediately on push() or close(), which is the common path.
        const auto poll_interval = std::chrono::milliseconds(50);
        auto deadline = std::chrono::steady_clock::now() + timeout;

        std::unique_lock<std::mutex> lock(inbox->mu_);
        while (true) {
            if (!inbox->queue_.empty()) {
                T value = std::move(inbox->queue_.front());
                inbox->queue_.pop_front();
                return value;
            }
            if (inbox->closed_) {
                return cpp11::make_unexpected(
                    Error(ErrorCode_Cancelled, "inbox closed"));
            }
            if (stop_requested()) {
                return cpp11::make_unexpected(cancelled_error());
            }
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return cpp11::make_unexpected(timeout_error());
            }
            auto next_wake = (now + poll_interval < deadline)
                                ? (now + poll_interval)
                                : deadline;
            inbox->cv_.wait_until(lock, next_wake);
        }
    }

    // Cooperative cancellation flag.  Long-running tasks (CPU loops,
    // tight non-recv work) should poll this at loop boundaries and bail
    // out by returning Result<...>(make_unexpected(cancelled_error())).
    // The bus sets it when stop() is called or when a per-task cancel
    // token fires.
    bool stop_requested() const {
        return cancel_flag_->load(std::memory_order_acquire);
    }

    // CommBus accessor — useful for tasks that want to submit follow-up
    // tasks.  Returns the bus that owns this context.  Beware: calling
    // submit_with_result(...).get() from a task body deadlocks at
    // worker_threads=1 and ties up two workers at worker_threads>1.  Use
    // fire-and-forget submit() or submit_with_callback() instead.
    CommBus& bus() { return *bus_; }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

private:
    friend class CommBus;

    Context(CommBus* bus, std::atomic<bool>* cancel_flag)
        : bus_(bus), cancel_flag_(cancel_flag) {}

    CommBus*           bus_;
    std::atomic<bool>* cancel_flag_;
};

} // namespace commbus

#endif
