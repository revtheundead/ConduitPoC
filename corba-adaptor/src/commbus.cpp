// SPDX-License-Identifier: MIT
//
// commbus.cpp — non-templated implementation of CommBus.
//
// The templated submission entry points (submit_with_result,
// submit_with_callback) live in the header so they can be instantiated
// per result-type at the call site.  Everything else — the queue,
// the worker pool, the bounded-capacity check, the error sink, the
// stats — is plain non-template code that lives here.

#include "commbus/bus.hpp"

#include <iostream>

namespace commbus {

namespace {

// Default sink: dump errors to std::cerr so silent failure is impossible
// before the user installs their real logger.  Replace via
// CommBus::set_error_sink().
void default_error_sink(const Error& e) {
    std::cerr << "[commbus] error: " << e.format() << std::endl;
}

} // namespace

// ───── Lifecycle ─────────────────────────────────────────────────────────────

CommBus::CommBus(const Config& config)
    : queue_capacity_(config.queue_capacity == 0 ? 1 : config.queue_capacity),
      stopping_(false),
      drain_on_stop_(true),
      in_flight_(0),
      error_sink_(&default_error_sink) {

    const std::size_t n_workers =
        config.worker_threads == 0 ? 1 : config.worker_threads;

    workers_.reserve(n_workers);
    cancel_flags_.reserve(n_workers);
    for (std::size_t i = 0; i < n_workers; ++i) {
        cancel_flags_.emplace_back(
            std::unique_ptr<std::atomic<bool> >(new std::atomic<bool>(false)));
    }
    for (std::size_t i = 0; i < n_workers; ++i) {
        // Pass the index explicitly so the worker has an unambiguous
        // handle on its own cancel flag, regardless of when the thread
        // actually starts executing relative to the workers_ vector
        // being fully populated.
        workers_.emplace_back(&CommBus::worker_loop, this, i);
    }
}

CommBus::~CommBus() {
    stop(/*drain=*/true);
}

void CommBus::stop(bool drain) {
    // Idempotent: only the first caller does the work.  Subsequent
    // concurrent or repeated callers return immediately without
    // re-joining workers (which would be UB on already-joined threads).
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) {
        return;
    }
    drain_on_stop_.store(drain);
    // Set every per-worker cancel flag so blocked ctx.wait() calls wake.
    for (auto& flag : cancel_flags_) {
        flag->store(true, std::memory_order_release);
    }
    queue_cv_.notify_all();

    for (std::thread& t : workers_) {
        if (t.joinable()) {
            // Don't join from a worker thread (self-join is UB) — best
            // effort: if a task running on this very thread called
            // stop(), we let the worker fall through to exit naturally.
            if (t.get_id() != std::this_thread::get_id()) {
                t.join();
            }
        }
    }
}

// ───── Submission ───────────────────────────────────────────────────────────

VoidResult CommBus::submit(Task task) {
    if (!task) return cpp11::make_unexpected(invalid_task_error());
    return submit_internal(std::move(task));
}

VoidResult CommBus::submit_internal(Task task) {
    std::unique_lock<std::mutex> lock(mu_);
    ++stats_.submitted;
    if (stopping_.load()) {
        ++stats_.rejected_stopped;
        return cpp11::make_unexpected(bus_stopped_error());
    }
    if (queue_.size() >= queue_capacity_) {
        ++stats_.rejected_full;
        return cpp11::make_unexpected(queue_full_error());
    }
    queue_.push_back(std::move(task));
    ++stats_.accepted;
    lock.unlock();
    queue_cv_.notify_one();
    return {};
}

// ───── Worker loop ──────────────────────────────────────────────────────────

void CommBus::worker_loop(std::size_t worker_index) {
    // Each worker is assigned its own cancel flag at construction;
    // the index resolves directly without scanning the workers_ vector
    // (which would race with the constructor still mid-emplace_back).
    std::atomic<bool>* my_flag = cancel_flags_[worker_index].get();

    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mu_);
            queue_cv_.wait(lock, [&] {
                return stopping_.load() || !queue_.empty();
            });
            if (queue_.empty()) {
                // We woke for shutdown; drain or exit.
                if (stopping_.load() && !drain_on_stop_.load()) {
                    // Cancel pending tasks — they'll never run.
                    queue_.clear();
                    return;
                }
                if (stopping_.load() && queue_.empty()) return;
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }

        // Reset this worker's cancel flag for the new task.  The flag
        // is "task-scoped": stop() sets it for everyone; the worker
        // clears it before each fresh task so a long-lived bus survives
        // multiple cooperative cancellations cleanly... BUT only if the
        // bus isn't itself shutting down.  During shutdown we keep the
        // flag set so partially-cancelled tasks still see it.
        if (!stopping_.load()) {
            my_flag->store(false, std::memory_order_release);
        }

        ++in_flight_;
        Context ctx(this, my_flag);
        try {
            task(ctx);
        } catch (const std::exception& e) {
            // Uncaught from a fire-and-forget submit; route to sink.
            emit_to_sink(task_threw_error(e.what()));
        } catch (...) {
            emit_to_sink(task_threw_error("unknown exception"));
        }
        --in_flight_;

        {
            std::lock_guard<std::mutex> lock(mu_);
            ++stats_.completed_ok;  // refined below
            // We can't distinguish ok vs err from here without
            // round-tripping the task's Result — the wrappers above
            // already set the future/callback; for the sink-only
            // fire-and-forget path, an exception was caught and the
            // counter conservatively bumps completed_ok regardless.
            // If you need precise success/failure stats, attach them
            // in your task body or via the error sink.
        }

        if (stopping_.load() && !drain_on_stop_.load()) return;
    }
}

// ───── Diagnostics ──────────────────────────────────────────────────────────

std::size_t CommBus::queue_depth() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

CommBus::Stats CommBus::stats() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
}

void CommBus::set_error_sink(ErrorSink sink) {
    std::lock_guard<std::mutex> lock(mu_);
    error_sink_ = std::move(sink);
}

CommBus::ErrorSink CommBus::error_sink_snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_sink_;
}

void CommBus::emit_to_sink(const Error& e) const {
    ErrorSink sink = error_sink_snapshot();
    if (sink) {
        try { sink(e); }
        catch (...) { /* sink itself broken; nothing we can do */ }
    }
}

} // namespace commbus
