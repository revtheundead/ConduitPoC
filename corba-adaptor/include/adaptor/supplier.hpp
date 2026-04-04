// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Supplier / Consumer Framework
//
// Design:
//   - DataSupplierServant:  CORBA servant that consumers subscribe to.
//     Maintains a thread-safe subscription registry.  When publish() is
//     called it fans out DataPackets to every matching consumer.  Dead
//     consumers (CORBA exceptions on push) are automatically reaped, and
//     a periodic health-check sweep catches silently-dead ones.
//
//   - ConsumerProxy:  Client-side helper that wraps a DataConsumer servant,
//     connects to a remote DataSupplier, and automatically reconnects with
//     exponential backoff if the supplier becomes unreachable.  Provides a
//     simple callback-based API so downstream code never touches CORBA.

#pragma once

#include <CorbaAdaptorS.h>
#include <adaptor/types.hpp>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace adaptor {

// ============================================================================
// Subscription entry (internal)
// ============================================================================

struct Subscription {
    CorbaAdaptor::SubscriptionId    id{};
    CorbaAdaptor::DataConsumer_var  consumer;
    CorbaAdaptor::SubscriptionFilter filter;
    std::chrono::steady_clock::time_point subscribed_at{};
    uint64_t                        packets_delivered{};
    uint64_t                        delivery_errors{};
};

// ============================================================================
// DataSupplierServant — server-side supplier
// ============================================================================

class DataSupplierServant : public POA_CorbaAdaptor::DataSupplier {
public:
    DataSupplierServant();
    ~DataSupplierServant() override;

    DataSupplierServant(const DataSupplierServant&) = delete;
    DataSupplierServant& operator=(const DataSupplierServant&) = delete;

    // --- CORBA interface (called by remote consumers) -----------------------

    CorbaAdaptor::SubscriptionId subscribe(
        CorbaAdaptor::DataConsumer_ptr consumer,
        const CorbaAdaptor::SubscriptionFilter& filter) override;

    void unsubscribe(CorbaAdaptor::SubscriptionId id) override;

    CORBA::ULong subscriber_count() override;

    // --- Local API (called by the adaptor) ----------------------------------

    /// Publish a single packet to all matching consumers.
    void publish(const CorbaAdaptor::DataPacket& packet);

    /// Publish a batch of packets.
    void publish_batch(const std::vector<CorbaAdaptor::DataPacket>& packets);

    /// Start the periodic health-check sweep thread.
    /// @param interval  How often to probe consumers via is_alive().
    void start_health_checks(std::chrono::seconds interval = std::chrono::seconds{30});

    /// Stop health checks and notify all consumers of shutdown.
    void shutdown(const std::string& reason = "supplier shutting down");

    /// Callback invoked when a consumer is reaped (for logging).
    using ReapCallback = std::function<void(CorbaAdaptor::SubscriptionId)>;
    void set_reap_callback(ReapCallback cb);

private:
    bool matches_filter(const CorbaAdaptor::DataPacket& packet,
                        const CorbaAdaptor::SubscriptionFilter& filter) const;

    void deliver_to(Subscription& sub, const CorbaAdaptor::DataPacket& packet);
    void reap_dead_consumers();
    void health_check_loop();

    mutable std::mutex                                          mu_;
    std::unordered_map<CorbaAdaptor::SubscriptionId, Subscription> subs_;
    CorbaAdaptor::SubscriptionId                                next_id_{1};

    std::atomic<bool>       health_running_{false};
    std::thread             health_thread_;
    std::mutex              health_mu_;
    std::condition_variable health_cv_;
    std::chrono::seconds    health_interval_{30};

    ReapCallback            reap_cb_;
};

// ============================================================================
// ConsumerProxy — client-side auto-reconnecting consumer
// ============================================================================

/// Configuration for the consumer proxy.
struct ConsumerProxyConfig {
    /// IOR or corbaname URI of the remote DataSupplier.
    std::string                         supplier_ior;

    /// Subscription filter.
    CorbaAdaptor::SubscriptionFilter    filter{};

    /// Reconnection parameters.
    bool                                auto_reconnect{true};
    std::chrono::milliseconds           initial_delay{1000};
    std::chrono::milliseconds           max_delay{30000};
    double                              backoff_multiplier{2.0};
    uint32_t                            max_attempts{0};  // 0 = unlimited

    /// Batch threshold: if >= this many packets queue up between polls,
    /// the on_data_batch callback is invoked instead of per-packet on_data.
    uint32_t                            batch_threshold{0};  // 0 = no batching
};

class ConsumerProxy {
public:
    /// Callback when a single packet arrives.
    using OnData       = std::function<void(const CorbaAdaptor::DataPacket&)>;
    /// Callback when a batch arrives.
    using OnBatch      = std::function<void(const CorbaAdaptor::DataPacketSeq&)>;
    /// Callback on connection state changes.
    using OnConnection = std::function<void(bool /*connected*/)>;

    ConsumerProxy(CORBA::ORB_ptr orb,
                  PortableServer::POA_ptr poa,
                  ConsumerProxyConfig config);
    ~ConsumerProxy();

    ConsumerProxy(const ConsumerProxy&) = delete;
    ConsumerProxy& operator=(const ConsumerProxy&) = delete;

    /// Set callbacks (call before connect()).
    void on_data(OnData cb);
    void on_batch(OnBatch cb);
    void on_connection(OnConnection cb);

    /// Begin connection (and reconnection loop if enabled).
    void connect();

    /// Disconnect and stop reconnection.
    void disconnect();

    /// Whether we are currently subscribed to the supplier.
    [[nodiscard]] bool is_connected() const noexcept;

private:
    class ConsumerServant;  // nested CORBA servant impl

    void reconnect_loop();
    bool try_connect();

    CORBA::ORB_var                      orb_;
    PortableServer::POA_var             poa_;
    ConsumerProxyConfig                 config_;

    OnData                              on_data_cb_;
    OnBatch                             on_batch_cb_;
    OnConnection                        on_conn_cb_;

    std::atomic<bool>                   connected_{false};
    std::atomic<bool>                   running_{false};
    std::thread                         reconnect_thread_;
    std::mutex                          mu_;
    std::condition_variable             cv_;

    CorbaAdaptor::DataSupplier_var      supplier_;
    CorbaAdaptor::SubscriptionId        sub_id_{0};
    PortableServer::ServantBase_var     consumer_servant_;
    CorbaAdaptor::DataConsumer_var      consumer_ref_;
};

} // namespace adaptor
