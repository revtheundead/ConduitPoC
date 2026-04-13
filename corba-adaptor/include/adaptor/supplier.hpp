// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Adaptor Supplier Servant
//
// Single supplier servant that publishes all adaptor message types to
// subscribed consumers.  Inherits BaseSupplier (subscribe/unsubscribe/
// subscriber_count) from the IDL and delegates all subscription lifecycle
// to a SubscriptionManager.
//
// Consumers subscribe as BaseConsumer references.  On each publish call
// the servant narrows to AdaptorConsumer and invokes the typed callback.
// Consumers that do not implement AdaptorConsumer are silently skipped
// (they still receive lifecycle events like on_supplier_disconnect).

#pragma once

#include <CorbaAdaptorS.h>
#include <adaptor/subscription_manager.hpp>

#include <string>

namespace adaptor {

class AdaptorSupplierServant : public POA_CorbaAdaptor::AdaptorSupplier {
public:
    AdaptorSupplierServant() = default;
    ~AdaptorSupplierServant() override = default;

    AdaptorSupplierServant(const AdaptorSupplierServant&) = delete;
    AdaptorSupplierServant& operator=(const AdaptorSupplierServant&) = delete;

    // -- BaseSupplier CORBA methods (delegated to SubscriptionManager) --------

    CORBA::Long subscribe(
        CorbaAdaptor::BaseConsumer_ptr consumer) override;

    void unsubscribe(CORBA::Long subscription_id) override;

    CORBA::Long subscriber_count() override;

    // -- Typed publish methods ------------------------------------------------
    //
    // Each method takes a snapshot of subscribers, narrows each
    // BaseConsumer to AdaptorConsumer, and invokes the typed callback.

    void publish_heartbeat(const CorbaAdaptor::HeartbeatMsg& msg);
    void publish_status_report(const CorbaAdaptor::StatusReportMsg& msg);
    void publish_data_payload(const CorbaAdaptor::DataPayloadMsg& msg);
    void publish_command_response(const CorbaAdaptor::CommandResponseMsg& msg);
    void publish_telemetry(const CorbaAdaptor::TelemetryMsg& msg);
    void publish_event(const CorbaAdaptor::EventMsg& msg);
    void publish_alarm(const CorbaAdaptor::AlarmMsg& msg);

    // -- Lifecycle / health ---------------------------------------------------

    void shutdown(const std::string& reason);
    void sweep_dead_consumers();

private:
    SubscriptionManager mgr_;
};

} // namespace adaptor
