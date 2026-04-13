// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Subscription Manager
//
// Simple, reusable subscription lifecycle manager that any BaseSupplier
// servant can compose.  Manages a map of BaseConsumer references, tracks
// delivery errors, reaps dead consumers, and sends shutdown notifications.
//
// Design constraints:
//   - Uses only basic C++ (std::mutex, std::map, std::vector) so that it
//     is easy to reproduce in projects targeting older C++ standards.
//   - Thread-safe: all public methods acquire the internal mutex.
//   - Does not inherit from any CORBA skeleton — it is a helper, not a
//     servant.  The owning servant delegates subscribe/unsubscribe to it.

#pragma once

#include <CorbaAdaptorC.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace adaptor {

class SubscriptionManager {
public:
    struct Entry {
        CORBA::Long                         id;
        CorbaAdaptor::BaseConsumer_var       consumer;
    };

    SubscriptionManager();

    // -- Subscription lifecycle ----------------------------------------------

    /// Add a new subscriber.  Returns its positive subscription id.
    CORBA::Long add(CorbaAdaptor::BaseConsumer_ptr consumer);

    /// Remove a subscriber by id.  Returns true if it was found.
    bool remove(CORBA::Long id);

    /// Number of active subscribers.
    CORBA::Long count() const;

    // -- Snapshot for safe iteration -----------------------------------------

    /// Return a copy of all current subscribers.  The caller can iterate
    /// and make CORBA calls outside the lock.
    std::vector<Entry> snapshot() const;

    // -- Lifecycle -----------------------------------------------------------

    /// Notify every subscriber of shutdown and clear the map.
    void shutdown(const std::string& reason);

    // -- Health sweep --------------------------------------------------------

    /// Call is_alive() on each subscriber.  Reap those that return false
    /// or throw.
    void sweep_dead();

    // -- Error tracking for delivery failures --------------------------------

    /// Record a delivery error for a subscription.  After
    /// MAX_DELIVERY_ERRORS consecutive errors the subscriber is reaped.
    void record_error(CORBA::Long id);

    /// Clear the error counter for a subscription (call after a
    /// successful delivery).
    void clear_errors(CORBA::Long id);

private:
    static const int MAX_DELIVERY_ERRORS = 3;

    mutable std::mutex                      mu_;
    CORBA::Long                             next_id_;
    std::map<CORBA::Long, Entry>            subscribers_;
    std::map<CORBA::Long, int>              error_counts_;
};

} // namespace adaptor
