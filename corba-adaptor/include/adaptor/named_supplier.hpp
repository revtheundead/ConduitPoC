// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Named Supplier Template Base
//
// NamedSupplierServant<Skeleton, Consumer, Message> is a reusable template
// base that implements the boilerplate subscription registry, health-check
// sweep, and error-based reaping for a typed CORBA supplier.
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
// Common lifecycle methods inherited from BaseSupplier/BaseConsumer
// (`unsubscribe`, `subscriber_count`, `is_alive`, `on_supplier_disconnect`)
// are implemented once here.

#pragma once

#include <CorbaAdaptorS.h>

#include <ace/Log_Msg.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
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

    using ReapCallback =
        std::function<void(CorbaAdaptor::SubscriptionId)>;

    NamedSupplierServant() = default;

    ~NamedSupplierServant() override {
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

    /// Start the periodic health-check sweep thread.
    void start_health_checks(std::chrono::seconds interval) {
        if (health_running_.exchange(true)) return;
        health_interval_ = interval;
        health_thread_ = std::thread([this] { health_check_loop(); });
    }

    /// Stop health checks and notify all consumers of shutdown.
    void shutdown(const std::string& reason) {
        if (health_running_.exchange(false)) {
            {
                std::lock_guard lock(health_mu_);
                health_cv_.notify_all();
            }
            if (health_thread_.joinable()) {
                health_thread_.join();
            }
        }

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

    void set_reap_callback(ReapCallback cb) {
        std::unique_lock lock(mu_);
        reap_cb_ = std::move(cb);
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
        std::atomic<uint64_t>                   packets_delivered{0};
        std::atomic<uint64_t>                   delivery_errors{0};
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

    void reset_error_count(CorbaAdaptor::SubscriptionId id) {
        std::shared_lock lock(mu_);
        if (auto it = subs_.find(id); it != subs_.end()) {
            it->second->packets_delivered.fetch_add(1, std::memory_order_relaxed);
            it->second->delivery_errors.store(0, std::memory_order_relaxed);
        }
    }

    void record_delivery_error(CorbaAdaptor::SubscriptionId id) {
        ReapCallback cb_to_fire;
        CorbaAdaptor::SubscriptionId reap_id{0};
        {
            std::unique_lock lock(mu_);
            if (auto it = subs_.find(id); it != subs_.end()) {
                auto errs = it->second->delivery_errors.fetch_add(
                    1, std::memory_order_relaxed) + 1;
                if (errs > kMaxDeliveryErrors) {
                    reap_id = it->first;
                    subs_.erase(it);
                    cb_to_fire = reap_cb_;
                    ACE_DEBUG((LM_WARNING,
                        "NamedSupplier(%s): reaped dead consumer (id=%u)\n",
                        type_name(), reap_id));
                }
            }
        }
        if (cb_to_fire) {
            cb_to_fire(reap_id);
        }
    }

    void health_check_loop() {
        while (health_running_) {
            {
                std::unique_lock lock(health_mu_);
                health_cv_.wait_for(lock, health_interval_,
                    [this] { return !health_running_.load(); });
            }
            if (!health_running_) break;

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

            if (!dead.empty()) {
                ReapCallback cb_to_fire;
                std::vector<CorbaAdaptor::SubscriptionId> reaped;
                {
                    std::unique_lock lock(mu_);
                    for (auto id : dead) {
                        if (subs_.erase(id)) {
                            ACE_DEBUG((LM_WARNING,
                                "NamedSupplier(%s): health-check reaped consumer "
                                "(id=%u)\n", type_name(), id));
                            reaped.push_back(id);
                        }
                    }
                    cb_to_fire = reap_cb_;
                }
                if (cb_to_fire) {
                    for (auto id : reaped) cb_to_fire(id);
                }
            }
        }
    }

    mutable std::shared_mutex                                               mu_;
    std::unordered_map<CorbaAdaptor::SubscriptionId,
                       std::shared_ptr<Subscription>>                       subs_;
    CorbaAdaptor::SubscriptionId                                            next_id_{1};
    ReapCallback                                                            reap_cb_;

    std::atomic<bool>       health_running_{false};
    std::thread             health_thread_;
    std::mutex              health_mu_;
    std::condition_variable health_cv_;
    std::chrono::seconds    health_interval_{30};

    static constexpr uint64_t kMaxDeliveryErrors = 3;
};

} // namespace adaptor
