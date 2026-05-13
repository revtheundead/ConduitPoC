// SPDX-License-Identifier: MIT
//
// commbus/broadcaster.hpp
// =======================
//
// `Broadcaster<T>` is a transport-agnostic publish/subscribe primitive
// that lets a single message source deliver a copy of each message to
// many independent consumer inboxes.  It is the answer to "I have
// multiple tasks each interested in different subsets of the same
// stream" — each consumer gets its OWN inbox, the broadcaster ensures
// every alive subscriber sees every published message, and consumers
// can drain (or filter / discard) at their own pace without affecting
// any other consumer.
//
// Zero-copy fan-out by default
// -----------------------------
// The broadcaster carries `std::shared_ptr<const T>` end-to-end.  A
// call to `publish(T msg)` allocates the shared message ONCE, then
// every subscriber gets a `shared_ptr` copy — i.e. an atomic refcount
// bump (~10 ns) regardless of message size.  For medium-to-large
// payloads (10 KB and up) this is a dramatic win over a value-copying
// design; for tiny payloads it's about the same cost.
//
// Subscribers consume `commbus::Inbox<Broadcaster<T>::Message>` where
// `Message` is `std::shared_ptr<const T>`.  Inside the consumer task,
// dereference twice to reach the value: `**ctx.recv(inbox, t)` or
// `(*ctx.recv(inbox, t))->member` for a single-step access.
//
// Lifetime + lazy unsubscribe
// ---------------------------
// The broadcaster holds subscribers via `std::weak_ptr`, so dropping
// your local `shared_ptr<Inbox<Message>>` automatically unsubscribes
// you — no manual cleanup, no risk of leaking subscribers across
// task lifetimes.  Dead weak_ptrs are pruned lazily on each publish().
//
// Threading
// ---------
// `publish()` and `subscribe()` are both safe to call concurrently
// from any thread.  The broadcaster's mutex is held only for the
// brief snapshot of live subscribers; the actual `inbox->push` calls
// happen outside the lock, so a slow inbox does not serialise other
// publishes.  Each Inbox has its own mutex; pushes to different
// inboxes are independent.

#ifndef COMMBUS_BROADCASTER_HPP
#define COMMBUS_BROADCASTER_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "inbox.hpp"

namespace commbus {

template <typename T>
class Broadcaster {
public:
    // The wire type subscribers receive.  shared_ptr<const T> means
    // every consumer reads from the same immutable backing allocation
    // — no copies even for very large T.
    using Message    = std::shared_ptr<const T>;

    // The handle subscribers hold (and the broadcaster holds weakly).
    using Subscriber = std::shared_ptr<Inbox<Message>>;

    Broadcaster() = default;
    Broadcaster(const Broadcaster&)            = delete;
    Broadcaster& operator=(const Broadcaster&) = delete;

    // ────── Subscribe ────────────────────────────────────────────────────────
    //
    // Register an inbox to receive every subsequent publish.  Existing
    // queued messages are NOT replayed; you start receiving at the next
    // publish.
    //
    // Drop the returned Subscriber (or the original shared_ptr) to
    // automatically unsubscribe — the broadcaster prunes dead
    // weak_ptrs on the next publish.
    //
    // Convenience: `make_subscriber()` mints a fresh Inbox and
    // subscribes it in one call.
    void subscribe(Subscriber inbox) {
        if (!inbox) return;
        std::lock_guard<std::mutex> lock(mu_);
        subs_.emplace_back(inbox);
    }

    // Convenience: allocate an Inbox<Message> with the given capacity
    // and overflow policy, subscribe it, and hand it back.  Most users
    // will want this form.
    Subscriber make_subscriber(
        std::size_t capacity = 64,
        typename Inbox<Message>::OverflowPolicy policy =
            Inbox<Message>::DropOldest) {
        auto inbox = std::make_shared<Inbox<Message>>(capacity, policy);
        subscribe(inbox);
        return inbox;
    }

    // ────── Publish ──────────────────────────────────────────────────────────
    //
    // Wrap the message in shared_ptr exactly once and hand a copy of
    // the pointer to every alive subscriber.  Safe to call from any
    // thread (the TCP recv thread, an ORB worker, a timer callback,
    // another bus task...).
    //
    // The argument is taken by value so callers may move-in to avoid
    // an upstream copy.  The internal allocation goes through
    // make_shared, so the control block and payload share a cache
    // line.
    void publish(T msg) {
        publish_shared(std::make_shared<const T>(std::move(msg)));
    }

    // Pre-wrapped variant.  Useful when the message is already on the
    // heap (forwarding from another broadcaster, re-publishing, etc.)
    // — saves a redundant allocation.
    void publish_shared(Message msg) {
        if (!msg) return;

        // Snapshot live subscribers under a brief lock, prune dead
        // weak_ptrs in the same pass.  Pushes happen outside the lock
        // so a slow subscriber doesn't serialise other publishes.
        std::vector<Subscriber> alive;
        {
            std::lock_guard<std::mutex> lock(mu_);
            alive.reserve(subs_.size());
            auto it = subs_.begin();
            while (it != subs_.end()) {
                if (auto s = it->lock()) {
                    alive.push_back(std::move(s));
                    ++it;
                } else {
                    it = subs_.erase(it);  // lazy cleanup
                }
            }
        }
        for (std::size_t i = 0; i < alive.size(); ++i) {
            alive[i]->push(msg);  // shared_ptr copy: atomic refcount bump
        }
    }

    // ────── Diagnostics ──────────────────────────────────────────────────────

    // Number of subscriptions currently held, including any that may
    // have been dropped but not yet pruned (publish() does the pruning).
    // For an accurate live count call compact() first.
    std::size_t subscriber_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return subs_.size();
    }

    // Eagerly prune dead weak_ptrs.  Lazy cleanup happens on publish(),
    // so this is normally unnecessary; useful if the broadcaster is
    // long-quiet and you want to free dropped Inbox memory promptly.
    void compact() {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = subs_.begin();
        while (it != subs_.end()) {
            if (it->expired()) it = subs_.erase(it);
            else               ++it;
        }
    }

private:
    mutable std::mutex                              mu_;
    std::vector<std::weak_ptr<Inbox<Message>>>      subs_;
};

} // namespace commbus

#endif
