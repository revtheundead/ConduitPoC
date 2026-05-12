// SPDX-License-Identifier: MIT
//
// commbus/wait_registry.hpp
// =========================
//
// Per-type correlation registry.  Bridges between
//
//   * application-owned "singleton receiver" actors (a CORBA reply-handler
//     servant, a TcpPeer's typed handler, an event-channel subscriber,
//     anything that produces results), and
//   * `commbus::Slot<T>` instances owned by individual bus tasks.
//
// Each registry handles one result type T.  An application typically owns
// one WaitRegistry per distinct response type it can receive.  Per-type
// registries are simpler than a single type-erased registry: each receiver
// already knows which type it's delivering, and the compiler enforces the
// match.
//
// Usage shape:
//
//   WaitRegistry<MyReply> registry;          // app-owned, long-lived
//
//   // From a singleton receiver (any thread):
//   registry.deliver(correlation_id, reply);
//
//   // From a task:
//   uint64_t cid  = registry.next_correlation_id();
//   auto     slot = ctx.make_slot<MyReply>();
//   WaitGuard<MyReply> guard(registry, cid, slot);  // RAII register
//   send_request_carrying(cid);
//   auto r = ctx.wait(slot, 5s);     // blocks until deliver / timeout
//
// Thread safety: every method is safe to call from any thread.  Internally
// a single mutex guards the map; locks are held briefly (lookup + erase +
// pointer copy out) and never while invoking `slot->fulfil` (the slot has
// its own mutex; we follow strict no-nested-locks discipline to avoid
// deadlock).

#ifndef COMMBUS_WAIT_REGISTRY_HPP
#define COMMBUS_WAIT_REGISTRY_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "error.hpp"
#include "slot.hpp"

namespace commbus {

template <typename T>
class WaitRegistry {
public:
    WaitRegistry() : counter_(1) {}

    WaitRegistry(const WaitRegistry&) = delete;
    WaitRegistry& operator=(const WaitRegistry&) = delete;

    // Atomic monotonic counter — convenient source of unique correlation
    // ids for tasks that don't bring their own.  Starts at 1 so '0' can
    // be used as a "not yet allocated" sentinel by callers.
    uint64_t next_correlation_id() {
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }

    // Register a slot under a correlation id.  Returns DuplicateWaiter
    // if the id is already in use — callers should treat this as a bug
    // (always allocate fresh ids via next_correlation_id()).
    VoidResult register_wait(uint64_t correlation_id,
                             std::shared_ptr<Slot<T>> slot) {
        if (!slot) {
            return cpp11::make_unexpected(
                Error(ErrorCode_InvalidTask, "register_wait: null slot"));
        }
        std::lock_guard<std::mutex> lock(mu_);
        auto inserted = entries_.insert(
            std::make_pair(correlation_id, std::move(slot)));
        if (!inserted.second) {
            return cpp11::make_unexpected(duplicate_waiter_error());
        }
        return {};
    }

    // Remove a registration.  Returns true if an entry existed; false if
    // the id was unknown (which is fine — deliver() may have already
    // popped it).  Use this from the cleanup path of a task that has
    // either received its result or given up.
    bool unregister(uint64_t correlation_id) {
        std::lock_guard<std::mutex> lock(mu_);
        return entries_.erase(correlation_id) > 0;
    }

    // Deliver a value to whoever is waiting on this correlation id.
    // Returns true if a waiter was found and woken; false if no
    // registration matched (late delivery / unknown id / waiter already
    // gave up).  Safe to call from any thread.
    bool deliver(uint64_t correlation_id, T value) {
        std::shared_ptr<Slot<T>> target;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = entries_.find(correlation_id);
            if (it == entries_.end()) return false;
            target = it->second;
            entries_.erase(it);
        }
        // Fulfil OUTSIDE the registry mutex — the slot's own mutex must
        // be acquired alone to satisfy our no-nested-locks rule.
        target->fulfil(std::move(value));
        return true;
    }

    // Deliver a typed error.  Same semantics as deliver() for the
    // routing side; the slot ends up resolved with the error.
    bool deliver_error(uint64_t correlation_id, Error err) {
        std::shared_ptr<Slot<T>> target;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = entries_.find(correlation_id);
            if (it == entries_.end()) return false;
            target = it->second;
            entries_.erase(it);
        }
        target->fail(std::move(err));
        return true;
    }

    // Fail every outstanding registration with `err`.  Call this on
    // shutdown, on catastrophic peer loss, or when an entire category
    // of responses can no longer arrive.  Each affected task wakes from
    // its ctx.wait with the supplied error.
    void fail_all(Error err) {
        std::unordered_map<uint64_t, std::shared_ptr<Slot<T>>> drained;
        {
            std::lock_guard<std::mutex> lock(mu_);
            drained.swap(entries_);
        }
        // Iterate outside the lock — see no-nested-locks rule above.
        for (auto& kv : drained) {
            kv.second->fail(err);
        }
    }

    // Diagnostic: how many slots are currently registered.
    std::size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return entries_.size();
    }

private:
    mutable std::mutex                                                  mu_;
    std::unordered_map<uint64_t, std::shared_ptr<Slot<T>>>              entries_;
    std::atomic<uint64_t>                                               counter_;
};

// -----------------------------------------------------------------------------
// WaitGuard — RAII helper that guarantees unregister() is called on every
// task exit path (normal return, error return, exception, cancellation).
// Construct one inside the task body; let it destruct at scope exit.
// -----------------------------------------------------------------------------

template <typename T>
class WaitGuard {
public:
    WaitGuard(WaitRegistry<T>& registry,
              uint64_t correlation_id,
              std::shared_ptr<Slot<T>> slot)
        : registry_(&registry),
          correlation_id_(correlation_id),
          slot_(std::move(slot)),
          registered_(false) {
        auto r = registry_->register_wait(correlation_id_, slot_);
        registered_ = static_cast<bool>(r);
        last_error_ = r ? Error() : r.error();
    }

    ~WaitGuard() {
        if (registered_) registry_->unregister(correlation_id_);
    }

    WaitGuard(const WaitGuard&) = delete;
    WaitGuard& operator=(const WaitGuard&) = delete;

    // True iff register_wait succeeded.  If false, the task should NOT
    // proceed to wait — call last_error() to see why (usually
    // DuplicateWaiter, a programmer mistake).
    bool ok() const { return registered_; }
    const Error& last_error() const { return last_error_; }

    uint64_t                       correlation_id() const { return correlation_id_; }
    const std::shared_ptr<Slot<T>>& slot() const          { return slot_; }

private:
    WaitRegistry<T>*               registry_;
    uint64_t                       correlation_id_;
    std::shared_ptr<Slot<T>>       slot_;
    bool                           registered_;
    Error                          last_error_;
};

} // namespace commbus

#endif
