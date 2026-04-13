// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Adaptor Supplier Servant Implementation

#include <adaptor/supplier.hpp>

#include <ace/Log_Msg.h>

namespace adaptor {

// ============================================================================
// BaseSupplier CORBA methods
// ============================================================================

CORBA::Long AdaptorSupplierServant::subscribe(
    CorbaAdaptor::BaseConsumer_ptr consumer) {
    return mgr_.add(consumer);
}

void AdaptorSupplierServant::unsubscribe(CORBA::Long subscription_id) {
    mgr_.remove(subscription_id);
}

CORBA::Long AdaptorSupplierServant::subscriber_count() {
    return mgr_.count();
}

// ============================================================================
// Typed publish methods
// ============================================================================
//
// Pattern: snapshot → narrow to AdaptorConsumer → call typed callback.
// Consumers that don't implement AdaptorConsumer are silently skipped.
// On CORBA exceptions the error counter is bumped; after enough
// consecutive failures the SubscriptionManager reaps that consumer.

#define ADAPTOR_PUBLISH(method_name, StructType, callback_fn)                 \
void AdaptorSupplierServant::method_name(                                     \
    const CorbaAdaptor::StructType& msg) {                                    \
    auto entries = mgr_.snapshot();                                           \
    for (std::size_t i = 0; i < entries.size(); ++i) {                        \
        CorbaAdaptor::AdaptorConsumer_var ac =                                \
            CorbaAdaptor::AdaptorConsumer::_narrow(                           \
                entries[i].consumer.in());                                    \
        if (CORBA::is_nil(ac.in())) continue;                                \
        try {                                                                 \
            ac->callback_fn(msg);                                             \
            mgr_.clear_errors(entries[i].id);                                 \
        } catch (const CORBA::Exception&) {                                   \
            mgr_.record_error(entries[i].id);                                 \
        }                                                                     \
    }                                                                         \
}

ADAPTOR_PUBLISH(publish_heartbeat,         HeartbeatMsg,         on_heartbeat)
ADAPTOR_PUBLISH(publish_status_report,     StatusReportMsg,      on_status_report)
ADAPTOR_PUBLISH(publish_data_payload,      DataPayloadMsg,       on_data_payload)
ADAPTOR_PUBLISH(publish_command_response,  CommandResponseMsg,   on_command_response)
ADAPTOR_PUBLISH(publish_telemetry,         TelemetryMsg,         on_telemetry)
ADAPTOR_PUBLISH(publish_event,             EventMsg,             on_event)
ADAPTOR_PUBLISH(publish_alarm,             AlarmMsg,             on_alarm)

#undef ADAPTOR_PUBLISH

// ============================================================================
// Lifecycle / health
// ============================================================================

void AdaptorSupplierServant::shutdown(const std::string& reason) {
    mgr_.shutdown(reason);
}

void AdaptorSupplierServant::sweep_dead_consumers() {
    mgr_.sweep_dead();
}

} // namespace adaptor
