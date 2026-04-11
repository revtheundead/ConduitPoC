// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Named Supplier Template Base
//
// NamedSupplierServant<Skeleton, Consumer, Message> is a reusable template
// base that implements the boilerplate subscription registry and
// error-based reaping for a typed CORBA supplier.
//
// Concrete servants derive from this template (which in turn derives from
// their POA_CorbaAdaptor::<X>Supplier skeleton) and only need to implement
// two things:
//
//   1. The typed `subscribe(<X>Consumer_ptr)` method declared on their IDL
//      supplier interface — typically by delegating to `add_subscription()`.
//
//   2. The protected `deliver_one(consumer, msg)` hook, which knows the
//      typed `on_<x>(msg)` method name to invoke on each consumer.
//
// Health checks are *not* run from a per-supplier thread any more. The
// owning Adaptor runs a single periodic sweep over its supplier registry
// and invokes `sweep_dead_consumers()` on each entry. This collapses N
// sleeping threads (one per message type) into one.

#pragma once

#include <CorbaAdaptorS.h>

#include <ace/Log_Msg.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace adaptor {

// ============================================================================
// NamedSupplierServant
// ============================================================================

template <class Skeleton, class Consumer, class Message>
class NamedSupplierServant : public Skeleton {
public:
    using ConsumerVar = typename Consumer::_var_type;
    using ConsumerPtr = typename Consumer::_ptr_type;

    NamedSupplierServant() = default;

    ~NamedSupplierServant() override {
        // Double-shutdown is safe: the second call will find an empty map.
        shutdown("servant destroyed");
    }

    NamedSupplierServant(const NamedSupplierServant&) = delete;
    NamedSupplierServant& operator=(const NamedSupplierServant&) = delete;

    // ------------------------------------------------------------------------
    // BaseSupplier overrides (CORBA interface)
    // ------------------------------------------------------------------------

    void unsubscribe(CorbaAdaptor::SubscriptionId id) override {
        std::unique_lock lock(mu_);
        if (subs_.erase(id) > 0) {
            ACE_DEBUG((LM_INFO,
                "NamedSupplier(%s): consumer unsubscribed (id=%u, total=%u)\n",
                type_name(), id, static_cast<unsigned>(subs_.size())));
        }
    }

    CORBA::ULong subscriber_count() override {
        std::shared_lock lock(mu_);
        return static_cast<CORBA::ULong>(subs_.size());
    }

    // ------------------------------------------------------------------------
    // Local API
    // ------------------------------------------------------------------------

    /// Publish a typed message to every currently subscribed consumer.
    /// Takes a snapshot under the read lock, then delivers without holding
    /// it so slow CORBA calls don't block new subscriptions.
    void publish(const Message& msg) {
        auto targets = snapshot();

        for (auto& target : targets) {
            try {
                deliver_one(target.consumer.in(), msg);
                reset_error_count(target.id);
            } catch (const CORBA::Exception&) {
                record_delivery_error(target.id);
            }
        }
    }

    /// Perform one health-check sweep: call `is_alive()` on every currently
    /// subscribed consumer and reap any that return false or raise. Safe to
    /// call from any thread. The Adaptor calls this on a periodic timer.
    void sweep_dead_consumers() {
        auto targets = snapshot();

        std::vector<CorbaAdaptor::SubscriptionId> dead;
        for (auto& target : targets) {
            try {
                if (!target.consumer->is_alive()) {
                    dead.push_back(target.id);
                }
            } catch (const CORBA::Exception&) {
                dead.push_back(target.id);
            }
        }

        if (dead.empty()) return;

        std::unique_lock lock(mu_);
        for (auto id : dead) {
            if (subs_.erase(id)) {
                ACE_DEBUG((LM_WARNING,
                    "NamedSupplier(%s): health-check reaped consumer "
                    "(id=%u)\n", type_name(), id));
            }
        }
    }

    /// Notify all currently subscribed consumers of shutdown and clear the
    /// subscription map. Idempotent — a second call is a no-op.
    void shutdown(const std::string& reason) {
        std::vector<ConsumerVar> to_notify;
        {
            std::unique_lock lock(mu_);
            to_notify.reserve(subs_.size());
            for (auto& [id, sub] : subs_) {
                to_notify.push_back(
                    Consumer::_duplicate(sub->consumer.in()));
            }
            subs_.clear();
        }

        for (auto& c : to_notify) {
            try {
                c->on_supplier_disconnect(reason.c_str());
            } catch (const CORBA::Exception&) {
                // Consumer already gone — ignore
            }
        }
    }

protected:
    // ------------------------------------------------------------------------
    // Customization points (implemented by concrete servants)
    // ------------------------------------------------------------------------

    /// Deliver one typed message to one consumer via its typed callback.
    /// Concrete subclasses know the specific `on_<xxx>(msg)` method to call.
    virtual void deliver_one(ConsumerPtr consumer, const Message& msg) = 0;

    /// Short human-readable name for logging (e.g. "Heartbeat").
    virtual const char* type_name() const noexcept = 0;

    // ------------------------------------------------------------------------
    // Helper for concrete `subscribe()` implementations
    // ------------------------------------------------------------------------

    /// Store a new subscription and return its id. The consumer_ptr is
    /// duplicated internally.
    CorbaAdaptor::SubscriptionId add_subscription(ConsumerPtr consumer) {
        std::unique_lock lock(mu_);

        auto sub = std::make_shared<Subscription>();
        sub->id            = next_id_++;
        sub->consumer      = Consumer::_duplicate(consumer);
        sub->subscribed_at = std::chrono::steady_clock::now();

        auto id = sub->id;
        subs_.emplace(id, std::move(sub));

        ACE_DEBUG((LM_INFO,
            "NamedSupplier(%s): consumer subscribed (id=%u, total=%u)\n",
            type_name(), id, static_cast<unsigned>(subs_.size())));
        return id;
    }

private:
    // ------------------------------------------------------------------------
    // Internal state
    // ------------------------------------------------------------------------

    struct Subscription {
        CorbaAdaptor::SubscriptionId            id{};
        ConsumerVar                             consumer;
        std::chrono::steady_clock::time_point   subscribed_at{};
        std::atomic<std::uint64_t>              packets_delivered{0};
        std::atomic<std::uint64_t>              delivery_errors{0};
    };

    struct DeliveryTarget {
        CorbaAdaptor::SubscriptionId    id{};
        ConsumerVar                     consumer;
    };

    std::vector<DeliveryTarget> snapshot() const {
        std::shared_lock lock(mu_);
        std::vector<DeliveryTarget> targets;
        targets.reserve(subs_.size());
        for (const auto& [id, sub] : subs_) {
            DeliveryTarget dt;
            dt.id       = sub->id;
            dt.consumer = Consumer::_duplicate(sub->consumer.in());
            targets.push_back(std::move(dt));
        }
        return targets;
    }

    /// Bump the delivery counter and zero the error counter for a successful
    /// send. Only acquires a shared lock because the fields being mutated
    /// are `std::atomic`.
    void reset_error_count(CorbaAdaptor::SubscriptionId id) {
        std::shared_lock lock(mu_);
        if (auto it = subs_.find(id); it != subs_.end()) {
            it->second->packets_delivered.fetch_add(1, std::memory_order_relaxed);
            it->second->delivery_errors.store(0, std::memory_order_relaxed);
        }
    }

    void record_delivery_error(CorbaAdaptor::SubscriptionId id) {
        std::unique_lock lock(mu_);
        auto it = subs_.find(id);
        if (it == subs_.end()) return;

        auto errs = it->second->delivery_errors.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (errs > kMaxDeliveryErrors) {
            ACE_DEBUG((LM_WARNING,
                "NamedSupplier(%s): reaped dead consumer (id=%u)\n",
                type_name(), id));
            subs_.erase(it);
        }
    }

    mutable std::shared_mutex                                       mu_;
    std::unordered_map<CorbaAdaptor::SubscriptionId,
                       std::shared_ptr<Subscription>>               subs_;
    CorbaAdaptor::SubscriptionId                                    next_id_{1};

    static constexpr std::uint64_t kMaxDeliveryErrors = 3;
};

} // namespace adaptor
