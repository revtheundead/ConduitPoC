// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Subscription Manager Implementation

#include <adaptor/subscription_manager.hpp>

#include <ace/Log_Msg.h>

namespace adaptor {

SubscriptionManager::SubscriptionManager()
    : next_id_(1)
{
}

// ============================================================================
// Subscription lifecycle
// ============================================================================

CORBA::Long SubscriptionManager::add(CorbaAdaptor::BaseConsumer_ptr consumer) {
    std::lock_guard<std::mutex> lock(mu_);

    Entry entry;
    entry.id       = next_id_++;
    entry.consumer = CorbaAdaptor::BaseConsumer::_duplicate(consumer);

    CORBA::Long id = entry.id;
    subscribers_[id] = entry;

    ACE_DEBUG((LM_INFO,
        "SubscriptionManager: consumer subscribed (id=%d, total=%d)\n",
        id, static_cast<int>(subscribers_.size())));
    return id;
}

bool SubscriptionManager::remove(CORBA::Long id) {
    std::lock_guard<std::mutex> lock(mu_);

    if (subscribers_.erase(id) > 0) {
        error_counts_.erase(id);
        ACE_DEBUG((LM_INFO,
            "SubscriptionManager: consumer unsubscribed (id=%d, total=%d)\n",
            id, static_cast<int>(subscribers_.size())));
        return true;
    }
    return false;
}

CORBA::Long SubscriptionManager::count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<CORBA::Long>(subscribers_.size());
}

// ============================================================================
// Snapshot
// ============================================================================

std::vector<SubscriptionManager::Entry>
SubscriptionManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);

    std::vector<Entry> result;
    result.reserve(subscribers_.size());
    for (std::map<CORBA::Long, Entry>::const_iterator it = subscribers_.begin();
         it != subscribers_.end(); ++it) {
        Entry copy;
        copy.id       = it->second.id;
        copy.consumer = CorbaAdaptor::BaseConsumer::_duplicate(
            it->second.consumer.in());
        result.push_back(copy);
    }
    return result;
}

// ============================================================================
// Lifecycle
// ============================================================================

void SubscriptionManager::shutdown(const std::string& reason) {
    std::vector<CorbaAdaptor::BaseConsumer_var> to_notify;
    {
        std::lock_guard<std::mutex> lock(mu_);
        to_notify.reserve(subscribers_.size());
        for (std::map<CORBA::Long, Entry>::iterator it = subscribers_.begin();
             it != subscribers_.end(); ++it) {
            to_notify.push_back(
                CorbaAdaptor::BaseConsumer::_duplicate(
                    it->second.consumer.in()));
        }
        subscribers_.clear();
        error_counts_.clear();
    }

    for (std::size_t i = 0; i < to_notify.size(); ++i) {
        try {
            to_notify[i]->on_supplier_disconnect(reason.c_str());
        } catch (const CORBA::Exception&) {
            // Consumer already gone — ignore
        }
    }
}

// ============================================================================
// Health sweep
// ============================================================================

void SubscriptionManager::sweep_dead() {
    std::vector<Entry> entries = snapshot();

    std::vector<CORBA::Long> dead;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        try {
            if (!entries[i].consumer->is_alive()) {
                dead.push_back(entries[i].id);
            }
        } catch (const CORBA::Exception&) {
            dead.push_back(entries[i].id);
        }
    }

    if (dead.empty()) return;

    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t i = 0; i < dead.size(); ++i) {
        if (subscribers_.erase(dead[i]) > 0) {
            error_counts_.erase(dead[i]);
            ACE_DEBUG((LM_WARNING,
                "SubscriptionManager: health-check reaped consumer (id=%d)\n",
                dead[i]));
        }
    }
}

// ============================================================================
// Error tracking
// ============================================================================

void SubscriptionManager::record_error(CORBA::Long id) {
    std::lock_guard<std::mutex> lock(mu_);

    if (subscribers_.find(id) == subscribers_.end()) return;

    int& errors = error_counts_[id];
    ++errors;
    if (errors > MAX_DELIVERY_ERRORS) {
        subscribers_.erase(id);
        error_counts_.erase(id);
        ACE_DEBUG((LM_WARNING,
            "SubscriptionManager: reaped dead consumer after %d errors (id=%d)\n",
            errors, id));
    }
}

void SubscriptionManager::clear_errors(CORBA::Long id) {
    std::lock_guard<std::mutex> lock(mu_);
    error_counts_.erase(id);
}

} // namespace adaptor
