// SPDX-License-Identifier: MIT
//
// commbus/bus.hpp
// ===============
//
// `CommBus` is the single class users instantiate.  It owns a bounded
// task queue and a small worker pool.  Tasks are arbitrary lambdas
// `void(Context&)`; the bus runs them on a worker thread and exposes
// three submission patterns:
//
//   1. submit(task)
//        Fire-and-forget.  Returns a VoidResult that succeeds if the task
//        was enqueued (false on QueueFull / BusStopped).  Mid-task errors
//        for fire-and-forget tasks land in the error sink.
//
//   2. submit_with_result<R>(task) -> std::future<Result<R>>
//        Blocking pattern.  The returned future is fulfilled with the
//        task's return value (success or error) or, for rejected
//        submissions, with a synchronously-set queue-full / bus-stopped
//        error.  `fut.get()` returns a Result<R>; it does NOT throw for
//        known errors.  It only throws if the task body itself escaped
//        a raw exception — the bus wraps task exceptions in
//        Result<R>(make_unexpected(TaskThrew)).
//
//   3. submit_with_callback<R>(task, on_complete)
//        Asynchronous callback pattern.  Returns a VoidResult that is OK
//        when the task is accepted.  The callback receives a Result<R>
//        and is invoked exactly once — even for rejected submissions
//        (synchronously, with QueueFull/BusStopped); even for task
//        exceptions (asynchronously, with TaskThrew).  Callbacks that
//        themselves throw are caught and reported via the error sink.
//
// Threading model: ONE bounded queue, N worker threads.  N = 1 is the
// default — strict serial execution, no concurrent flow side-effects to
// reason about.  Raise N only when you have flows that genuinely park
// on external waits (CORBA reverse-callbacks, DB queries) and you've
// measured queue backlog under load.
//
// Cooperative cancellation only: tasks must poll `ctx.stop_requested()`
// or be parked in `ctx.wait(...)` for cancellation to take effect.  The
// bus never kills a worker thread.

#ifndef COMMBUS_BUS_HPP
#define COMMBUS_BUS_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "error.hpp"
#include "context.hpp"

namespace commbus {

class CommBus {
public:
    // ────── Public configuration ──────────────────────────────────────────────

    using Task      = std::function<void(Context&)>;
    using ErrorSink = std::function<void(const Error&)>;

    struct Config {
        // Max number of tasks queued ahead of the workers.  submit() and
        // its siblings refuse new tasks once the queue holds this many
        // pending entries.
        std::size_t queue_capacity;

        // Number of worker threads draining the queue.  Each task runs
        // on exactly one worker; a task that ctx.wait()s on an external
        // event ties up that worker for the wait duration.
        std::size_t worker_threads;

        Config()
            : queue_capacity(256),
              worker_threads(1) {}

        Config(std::size_t cap, std::size_t workers)
            : queue_capacity(cap),
              worker_threads(workers) {}
    };

    // ────── Lifecycle ────────────────────────────────────────────────────────

    explicit CommBus(const Config& config = Config{});

    // Destructor calls stop(/*drain=*/true) and joins workers.
    ~CommBus();

    CommBus(const CommBus&)            = delete;
    CommBus& operator=(const CommBus&) = delete;

    // Initiate shutdown.  If `drain` is true (default), queued tasks
    // still run; if false, they are dropped (their result-future or
    // callback receives BusStopped).  Either way, no new tasks are
    // accepted, running tasks see stop_requested(), all ctx.wait() calls
    // wake with Cancelled.  Joins the worker threads before returning.
    //
    // stop() is idempotent.  The destructor calls it; explicit early
    // calls are useful when you want a graceful shutdown deadline of
    // your own.
    void stop(bool drain = true);

    bool is_stopping() const { return stopping_.load(); }

    // ────── Submission API ───────────────────────────────────────────────────

    // Fire-and-forget.  OK = accepted; error => QueueFull or BusStopped.
    // Mid-task errors surface via the error sink (see set_error_sink).
    VoidResult submit(Task task);

    // Blocking pattern.  Returns std::future<Result<R>>.  For rejected
    // submissions the future is already-ready with the rejection error.
    // The task body must itself return Result<R>; if it throws, the
    // future yields Result<R>(make_unexpected(TaskThrew)).
    template <typename R>
    std::future<Result<R>>
    submit_with_result(std::function<Result<R>(Context&)> task) {
        auto prom = std::make_shared<std::promise<Result<R>>>();
        std::future<Result<R>> fut = prom->get_future();

        if (!task) {
            prom->set_value(cpp11::make_unexpected(invalid_task_error()));
            return fut;
        }

        Task wrapper = make_result_task<R>(task, prom);
        VoidResult accepted = submit_internal(std::move(wrapper));
        if (!accepted) {
            prom->set_value(cpp11::make_unexpected(accepted.error()));
        }
        return fut;
    }

    // Asynchronous callback pattern.  The callback is invoked exactly
    // once — either synchronously (for rejected submissions) or
    // asynchronously on a bus worker (for accepted submissions, after
    // the task completes or throws).  VoidResult return tells the
    // caller whether the task was queued; the callback ALSO receives a
    // matching error for rejected submissions so error-handling code
    // can stay in one place.
    template <typename R>
    VoidResult
    submit_with_callback(std::function<Result<R>(Context&)> task,
                         std::function<void(Result<R>)>     on_complete) {
        if (!task) {
            Result<R> err = cpp11::make_unexpected(invalid_task_error());
            if (on_complete) safe_invoke(on_complete, std::move(err));
            return cpp11::make_unexpected(invalid_task_error());
        }

        Task wrapper = make_callback_task<R>(task, on_complete);
        VoidResult accepted = submit_internal(std::move(wrapper));
        if (!accepted) {
            if (on_complete) {
                Result<R> rejected =
                    cpp11::make_unexpected(accepted.error());
                safe_invoke(on_complete, std::move(rejected));
            }
        }
        return accepted;
    }

    // ────── Diagnostics ─────────────────────────────────────────────────────

    std::size_t queue_depth() const;
    std::size_t in_flight() const   { return in_flight_.load(); }
    std::size_t worker_count() const { return workers_.size(); }

    // Counters that survive across diagnostics calls.
    struct Stats {
        std::uint64_t submitted        = 0;
        std::uint64_t accepted         = 0;
        std::uint64_t rejected_full    = 0;
        std::uint64_t rejected_stopped = 0;
        std::uint64_t completed_ok     = 0;
        std::uint64_t completed_err    = 0;
    };
    Stats stats() const;

    // Set a sink for errors that have nowhere else to land:
    //   - fire-and-forget task threw or returned error
    //   - completion callback threw
    //   - rejection error for submit() (no future/callback to carry it)
    // Defaults to writing to std::cerr; pass an empty function to
    // silence, or your own logger to integrate.
    void set_error_sink(ErrorSink sink);

private:
    // Internal: enqueue a wrapped task.
    VoidResult submit_internal(Task task);

    // Internal: worker thread body.  The index uniquely identifies
    // this worker among the pool; used to look up the corresponding
    // per-worker cancel flag without a workers_ self-scan (which would
    // race against the constructor still filling the workers_ vector).
    void worker_loop(std::size_t worker_index);

    // Build the wrapper closure that fulfils the promise for
    // submit_with_result.  Implemented inline because R is templated.
    template <typename R>
    Task make_result_task(std::function<Result<R>(Context&)> task,
                          std::shared_ptr<std::promise<Result<R>>> prom) {
        return [task, prom](Context& ctx) {
            Result<R> r;
            try {
                r = task(ctx);
            } catch (const std::exception& e) {
                r = cpp11::make_unexpected(task_threw_error(e.what()));
            } catch (...) {
                r = cpp11::make_unexpected(
                    task_threw_error("unknown exception"));
            }
            try {
                prom->set_value(std::move(r));
            } catch (...) {
                // promise broken (e.g. set twice); nothing we can do.
            }
        };
    }

    // Wrapper for submit_with_callback.
    template <typename R>
    Task make_callback_task(std::function<Result<R>(Context&)> task,
                            std::function<void(Result<R>)>     cb) {
        ErrorSink sink_copy = error_sink_snapshot();
        return [task, cb, sink_copy](Context& ctx) {
            Result<R> r;
            try {
                r = task(ctx);
            } catch (const std::exception& e) {
                r = cpp11::make_unexpected(task_threw_error(e.what()));
            } catch (...) {
                r = cpp11::make_unexpected(
                    task_threw_error("unknown exception"));
            }
            if (cb) {
                try {
                    cb(std::move(r));
                } catch (const std::exception& e) {
                    if (sink_copy) sink_copy(callback_threw_error(e.what()));
                } catch (...) {
                    if (sink_copy)
                        sink_copy(callback_threw_error("unknown exception"));
                }
            }
        };
    }

    // Snapshot the current error sink atomically so wrappers don't have
    // to take the bus mutex on every invocation.
    ErrorSink error_sink_snapshot() const;

    template <typename Fn, typename Arg>
    void safe_invoke(Fn& fn, Arg&& arg) const {
        try { fn(std::forward<Arg>(arg)); }
        catch (const std::exception& e) {
            ErrorSink s = error_sink_snapshot();
            if (s) s(callback_threw_error(e.what()));
        } catch (...) {
            ErrorSink s = error_sink_snapshot();
            if (s) s(callback_threw_error("unknown exception"));
        }
    }

    void emit_to_sink(const Error& e) const;

    // ────── State ───────────────────────────────────────────────────────────

    mutable std::mutex                  mu_;             // guards queue_, stats_, error_sink_
    std::condition_variable             queue_cv_;
    std::deque<Task>                    queue_;
    std::size_t                         queue_capacity_;
    std::atomic<bool>                   stopping_;
    std::atomic<bool>                   drain_on_stop_;
    std::atomic<std::size_t>            in_flight_;

    std::vector<std::thread>            workers_;
    std::vector<std::unique_ptr<std::atomic<bool> > > cancel_flags_; // one per worker
    ErrorSink                           error_sink_;
    mutable Stats                       stats_;
};

} // namespace commbus

#endif
