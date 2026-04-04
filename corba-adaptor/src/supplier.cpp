// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Supplier / Consumer Implementation

#include <adaptor/supplier.hpp>

#include <ace/Log_Msg.h>

#include <algorithm>
#include <cstring>

namespace adaptor {

// ============================================================================
// Iterative glob-style label matching (supports '*' and '?' wildcards)
// ============================================================================

namespace {

bool glob_match(const char* pattern, const char* text) {
    const char* star_pattern = nullptr;
    const char* star_text    = nullptr;

    while (*text) {
        if (*pattern == '*') {
            // Record the '*' position and advance pattern
            star_pattern = ++pattern;
            star_text    = text;
        } else if (*pattern == '?' || *pattern == *text) {
            ++pattern;
            ++text;
        } else if (star_pattern) {
            // Backtrack: try matching '*' against one more character
            pattern = star_pattern;
            text    = ++star_text;
        } else {
            return false;
        }
    }
    // Consume trailing '*'s
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

} // anonymous namespace

// ============================================================================
// DataSupplierServant
// ============================================================================

DataSupplierServant::DataSupplierServant() = default;

DataSupplierServant::~DataSupplierServant() {
    shutdown();
}

// --- CORBA interface --------------------------------------------------------

CorbaAdaptor::SubscriptionId DataSupplierServant::subscribe(
    CorbaAdaptor::DataConsumer_ptr consumer,
    const CorbaAdaptor::SubscriptionFilter& filter)
{
    std::unique_lock lock(mu_);

    auto sub = std::make_shared<Subscription>();
    sub->id          = next_id_++;
    sub->consumer    = CorbaAdaptor::DataConsumer::_duplicate(consumer);
    sub->filter      = filter;
    sub->subscribed_at = std::chrono::steady_clock::now();

    auto id = sub->id;
    subs_.emplace(id, std::move(sub));

    ACE_DEBUG((LM_INFO, "DataSupplier: consumer subscribed (id=%u, total=%u)\n",
               id, static_cast<unsigned>(subs_.size())));
    return id;
}

void DataSupplierServant::unsubscribe(CorbaAdaptor::SubscriptionId id) {
    std::unique_lock lock(mu_);
    if (auto it = subs_.find(id); it != subs_.end()) {
        subs_.erase(it);
        ACE_DEBUG((LM_INFO, "DataSupplier: consumer unsubscribed (id=%u, total=%u)\n",
                   id, static_cast<unsigned>(subs_.size())));
    }
}

CORBA::ULong DataSupplierServant::subscriber_count() {
    std::shared_lock lock(mu_);
    return static_cast<CORBA::ULong>(subs_.size());
}

// --- Local publishing API ---------------------------------------------------

void DataSupplierServant::publish(const CorbaAdaptor::DataPacket& packet) {
    // Snapshot matching consumers under the read lock
    auto targets = snapshot_matching(packet);

    // Deliver WITHOUT holding the lock — CORBA calls may be slow
    for (auto& target : targets) {
        try {
            target.consumer->on_data(packet);
            // Update stats on the shared_ptr (still valid even if sub was removed)
            if (auto sub_lock = std::shared_lock(mu_); true) {
                if (auto it = subs_.find(target.id); it != subs_.end()) {
                    it->second->packets_delivered.fetch_add(1, std::memory_order_relaxed);
                    it->second->delivery_errors.store(0, std::memory_order_relaxed);
                }
            }
        } catch (const CORBA::Exception&) {
            record_delivery_error(target.id);
        }
    }
}

void DataSupplierServant::publish_batch(
    const std::vector<CorbaAdaptor::DataPacket>& packets)
{
    if (packets.empty()) return;

    // Snapshot all subscriptions once
    std::vector<std::pair<DeliveryTarget, CorbaAdaptor::SubscriptionFilter>> targets;
    {
        std::shared_lock lock(mu_);
        targets.reserve(subs_.size());
        for (const auto& [id, sub] : subs_) {
            DeliveryTarget dt;
            dt.id       = sub->id;
            dt.consumer = CorbaAdaptor::DataConsumer::_duplicate(sub->consumer.in());
            targets.emplace_back(std::move(dt), sub->filter);
        }
    }

    // Deliver without lock
    for (auto& [target, filter] : targets) {
        CorbaAdaptor::DataPacketSeq batch;
        batch.length(0);

        for (const auto& pkt : packets) {
            if (!matches_filter(pkt, filter)) continue;
            auto idx = batch.length();
            batch.length(idx + 1);
            batch[idx] = pkt;
        }

        if (batch.length() == 0) continue;

        try {
            target.consumer->on_data_batch(batch);
            if (auto sub_lock = std::shared_lock(mu_); true) {
                if (auto it = subs_.find(target.id); it != subs_.end()) {
                    it->second->packets_delivered.fetch_add(
                        batch.length(), std::memory_order_relaxed);
                    it->second->delivery_errors.store(0, std::memory_order_relaxed);
                }
            }
        } catch (const CORBA::Exception&) {
            record_delivery_error(target.id);
        }
    }
}

// --- Health checks ----------------------------------------------------------

void DataSupplierServant::start_health_checks(std::chrono::seconds interval) {
    health_interval_ = interval;
    health_running_ = true;
    health_thread_ = std::thread([this] { health_check_loop(); });
}

void DataSupplierServant::shutdown(const std::string& reason) {
    // Stop health checks
    if (health_running_.exchange(false)) {
        {
            std::lock_guard lock(health_mu_);
            health_cv_.notify_all();
        }
        if (health_thread_.joinable()) {
            health_thread_.join();
        }
    }

    // Snapshot consumers, then notify without holding lock
    std::vector<CorbaAdaptor::DataConsumer_var> consumers;
    {
        std::unique_lock lock(mu_);
        consumers.reserve(subs_.size());
        for (auto& [id, sub] : subs_) {
            consumers.push_back(
                CorbaAdaptor::DataConsumer::_duplicate(sub->consumer.in()));
        }
        subs_.clear();
    }

    for (auto& consumer : consumers) {
        try {
            consumer->on_supplier_disconnect(reason.c_str());
        } catch (const CORBA::Exception&) {
            // Consumer already gone — ignore
        }
    }
}

void DataSupplierServant::set_reap_callback(ReapCallback cb) {
    std::unique_lock lock(mu_);
    reap_cb_ = std::move(cb);
}

// --- Private helpers --------------------------------------------------------

bool DataSupplierServant::matches_filter(
    const CorbaAdaptor::DataPacket& packet,
    const CorbaAdaptor::SubscriptionFilter& filter) const
{
    if (packet.header.source == CorbaAdaptor::PEER_TCP && !filter.accept_tcp_peer)
        return false;
    if (packet.header.source == CorbaAdaptor::PEER_CORBA && !filter.accept_corba_peer)
        return false;

    const char* pattern = filter.label_pattern.in();
    if (pattern && pattern[0] != '\0') {
        if (!glob_match(pattern, packet.header.label.in())) {
            return false;
        }
    }

    return true;
}

std::vector<DataSupplierServant::DeliveryTarget>
DataSupplierServant::snapshot_matching(
    const CorbaAdaptor::DataPacket& packet) const
{
    std::shared_lock lock(mu_);
    std::vector<DeliveryTarget> targets;
    targets.reserve(subs_.size());

    for (const auto& [id, sub] : subs_) {
        if (!matches_filter(packet, sub->filter)) continue;
        DeliveryTarget dt;
        dt.id       = sub->id;
        dt.consumer = CorbaAdaptor::DataConsumer::_duplicate(sub->consumer.in());
        targets.push_back(std::move(dt));
    }

    return targets;
}

void DataSupplierServant::record_delivery_error(
    CorbaAdaptor::SubscriptionId id)
{
    std::unique_lock lock(mu_);
    if (auto it = subs_.find(id); it != subs_.end()) {
        auto errs = it->second->delivery_errors.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (errs > kMaxDeliveryErrors) {
            auto reap_id = it->first;
            subs_.erase(it);
            ACE_DEBUG((LM_WARNING,
                       "DataSupplier: reaped dead consumer (id=%u)\n", reap_id));
            if (reap_cb_) {
                reap_cb_(reap_id);
            }
        }
    }
}

void DataSupplierServant::reap_dead_consumers() {
    // Caller must hold exclusive mu_
    std::vector<CorbaAdaptor::SubscriptionId> dead;

    for (const auto& [id, sub] : subs_) {
        if (sub->delivery_errors.load(std::memory_order_relaxed) > kMaxDeliveryErrors) {
            dead.push_back(id);
        }
    }

    for (auto id : dead) {
        subs_.erase(id);
        ACE_DEBUG((LM_WARNING,
                   "DataSupplier: reaped dead consumer (id=%u)\n", id));
        if (reap_cb_) {
            reap_cb_(id);
        }
    }
}

void DataSupplierServant::health_check_loop() {
    while (health_running_) {
        {
            std::unique_lock lock(health_mu_);
            health_cv_.wait_for(lock, health_interval_,
                                [this] { return !health_running_.load(); });
        }

        if (!health_running_) break;

        // Snapshot consumers for health probing (don't hold mu_ during CORBA calls)
        std::vector<DeliveryTarget> targets;
        {
            std::shared_lock lock(mu_);
            targets.reserve(subs_.size());
            for (const auto& [id, sub] : subs_) {
                DeliveryTarget dt;
                dt.id       = sub->id;
                dt.consumer = CorbaAdaptor::DataConsumer::_duplicate(sub->consumer.in());
                targets.push_back(std::move(dt));
            }
        }

        // Probe without lock
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

        // Reap under exclusive lock
        if (!dead.empty()) {
            std::unique_lock lock(mu_);
            for (auto id : dead) {
                if (subs_.erase(id)) {
                    ACE_DEBUG((LM_WARNING,
                               "DataSupplier: health-check reaped consumer (id=%u)\n", id));
                    if (reap_cb_) {
                        reap_cb_(id);
                    }
                }
            }
        }
    }
}

// ============================================================================
// ConsumerProxy — nested ConsumerServant
// ============================================================================

class ConsumerProxy::ConsumerServant
    : public POA_CorbaAdaptor::DataConsumer
{
public:
    explicit ConsumerServant(ConsumerProxy& proxy) : proxy_(proxy) {}

    void on_data(const CorbaAdaptor::DataPacket& packet) override {
        if (proxy_.on_data_cb_) {
            proxy_.on_data_cb_(packet);
        }
    }

    void on_data_batch(const CorbaAdaptor::DataPacketSeq& packets) override {
        if (proxy_.on_batch_cb_) {
            proxy_.on_batch_cb_(packets);
        } else if (proxy_.on_data_cb_) {
            for (CORBA::ULong i = 0; i < packets.length(); ++i) {
                proxy_.on_data_cb_(packets[i]);
            }
        }
    }

    void on_supplier_disconnect(const char* reason) override {
        ACE_DEBUG((LM_INFO, "ConsumerProxy: supplier disconnected: %s\n", reason));
        proxy_.on_supplier_lost();
    }

    CORBA::Boolean is_alive() override {
        return true;
    }

private:
    ConsumerProxy& proxy_;
};

// ============================================================================
// ConsumerProxy
// ============================================================================

ConsumerProxy::ConsumerProxy(
    CORBA::ORB_ptr orb,
    PortableServer::POA_ptr poa,
    ConsumerProxyConfig config)
    : orb_(CORBA::ORB::_duplicate(orb))
    , poa_(PortableServer::POA::_duplicate(poa))
    , config_(std::move(config))
{
}

ConsumerProxy::~ConsumerProxy() {
    disconnect();
}

void ConsumerProxy::on_data(OnData cb) { on_data_cb_ = std::move(cb); }
void ConsumerProxy::on_batch(OnBatch cb) { on_batch_cb_ = std::move(cb); }
void ConsumerProxy::on_connection(OnConnection cb) { on_conn_cb_ = std::move(cb); }

void ConsumerProxy::connect() {
    running_ = true;

    // Activate our consumer servant
    auto* servant = new ConsumerServant(*this);
    consumer_servant_ = servant;

    PortableServer::ObjectId_var oid = poa_->activate_object(servant);
    CORBA::Object_var obj = poa_->id_to_reference(oid.in());
    consumer_ref_ = CorbaAdaptor::DataConsumer::_narrow(obj.in());

    if (try_connect()) {
        connected_ = true;
        if (on_conn_cb_) on_conn_cb_(true);
    } else if (config_.auto_reconnect) {
        reconnect_thread_ = std::thread([this] { reconnect_loop(); });
    }
}

void ConsumerProxy::disconnect() {
    running_ = false;
    {
        std::lock_guard lock(mu_);
        cv_.notify_all();
    }
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }

    // Unsubscribe from supplier
    if (connected_ && !CORBA::is_nil(supplier_.in()) && sub_id_ != 0) {
        try {
            supplier_->unsubscribe(sub_id_);
        } catch (const CORBA::Exception&) {
            // Supplier might already be gone
        }
    }

    connected_ = false;
    sub_id_ = 0;
}

bool ConsumerProxy::is_connected() const noexcept {
    return connected_;
}

void ConsumerProxy::on_supplier_lost() {
    bool was_connected = connected_.exchange(false);
    if (was_connected && on_conn_cb_) {
        on_conn_cb_(false);
    }

    // Re-enter reconnect loop if enabled and still running
    if (running_ && config_.auto_reconnect) {
        // Join any prior reconnect thread before starting a new one
        {
            std::lock_guard lock(mu_);
            // Wake the old thread so it exits its wait
            cv_.notify_all();
        }
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
        reconnect_thread_ = std::thread([this] { reconnect_loop(); });
    }
}

bool ConsumerProxy::try_connect() {
    try {
        CORBA::Object_var obj = orb_->string_to_object(config_.supplier_ior.c_str());
        if (CORBA::is_nil(obj.in())) return false;

        supplier_ = CorbaAdaptor::DataSupplier::_narrow(obj.in());
        if (CORBA::is_nil(supplier_.in())) return false;

        sub_id_ = supplier_->subscribe(consumer_ref_.in(), config_.filter);

        ACE_DEBUG((LM_INFO,
                   "ConsumerProxy: connected to supplier (sub_id=%u)\n", sub_id_));
        return true;
    } catch (const CORBA::Exception& ex) {
        ACE_DEBUG((LM_WARNING,
                   "ConsumerProxy: connection failed: %s\n", ex._info().c_str()));
        return false;
    }
}

void ConsumerProxy::reconnect_loop() {
    auto delay = config_.initial_delay;
    uint32_t attempts = 0;

    while (running_ && !connected_) {
        {
            std::unique_lock lock(mu_);
            cv_.wait_for(lock, delay, [this] { return !running_.load(); });
        }

        if (!running_) break;

        ++attempts;
        ACE_DEBUG((LM_INFO,
                   "ConsumerProxy: reconnect attempt %u (delay=%lldms)\n",
                   attempts, static_cast<long long>(delay.count())));

        if (try_connect()) {
            connected_ = true;
            if (on_conn_cb_) on_conn_cb_(true);
            break;
        }

        if (config_.max_attempts > 0 && attempts >= config_.max_attempts) {
            ACE_DEBUG((LM_ERROR,
                       "ConsumerProxy: max reconnect attempts (%u) reached\n",
                       config_.max_attempts));
            break;
        }

        // Exponential backoff
        delay = std::chrono::milliseconds(
            static_cast<int64_t>(delay.count() * config_.backoff_multiplier));
        if (delay > config_.max_delay) {
            delay = config_.max_delay;
        }
    }
}

} // namespace adaptor
