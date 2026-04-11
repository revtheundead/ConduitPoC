// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Typed Supplier Servants
//
// One concrete servant per typed supplier interface declared in the IDL.
// All share the same subscription management / reaping / health-check
// machinery via `NamedSupplierServant<Skeleton, Consumer, Message>`.
//
// Each concrete class only has to:
//   - Implement the typed `subscribe(<X>Consumer_ptr)` method by delegating
//     to `add_subscription()` on the base.
//   - Override the protected `deliver_one()` hook so the base's `publish()`
//     knows which typed `on_<x>(msg)` method to invoke.

#pragma once

#include <CorbaAdaptorS.h>
#include <adaptor/named_supplier.hpp>

namespace adaptor {

// ============================================================================
// Macro: declare a typed supplier servant
// ============================================================================
//
// The IDL shape is uniform — each `<Name>Supplier` has exactly one typed
// `subscribe()` returning a `SubscriptionId`, and its matching
// `<Name>Consumer` has exactly one typed `on_<name>()` callback — so the
// per-message boilerplate collapses into a macro.
//
// Usage:
//     ADAPTOR_DECLARE_TYPED_SUPPLIER(
//         Heartbeat,           // interface prefix in IDL
//         HeartbeatMsg,        // CORBA struct type
//         on_heartbeat,        // typed consumer method name
//         "Heartbeat");        // short type name (for logging)
// ============================================================================

#define ADAPTOR_DECLARE_TYPED_SUPPLIER(Iface, Struct, DeliverMethod, TypeName) \
class Iface##SupplierServant                                                   \
    : public NamedSupplierServant<                                             \
          POA_CorbaAdaptor::Iface##Supplier,                                   \
          CorbaAdaptor::Iface##Consumer,                                       \
          CorbaAdaptor::Struct>                                                \
{                                                                              \
public:                                                                        \
    CorbaAdaptor::SubscriptionId subscribe(                                    \
        CorbaAdaptor::Iface##Consumer_ptr consumer) override {                 \
        return add_subscription(consumer);                                     \
    }                                                                          \
                                                                               \
protected:                                                                     \
    void deliver_one(                                                          \
        CorbaAdaptor::Iface##Consumer_ptr consumer,                            \
        const CorbaAdaptor::Struct& msg) override {                            \
        consumer->DeliverMethod(msg);                                          \
    }                                                                          \
                                                                               \
    const char* type_name() const noexcept override { return TypeName; }       \
};

// ============================================================================
// TCP peer-side suppliers (decoded from the Conduit TCP Transceiver)
// ============================================================================

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    Heartbeat, HeartbeatMsg, on_heartbeat, "Heartbeat")

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    StatusReport, StatusReportMsg, on_status_report, "StatusReport")

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    DataPayload, DataPayloadMsg, on_data_payload, "DataPayload")

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    CommandResponse, CommandResponseMsg, on_command_response, "CommandResponse")

// ============================================================================
// CORBA peer-side suppliers (decoded from the CORBA raw byte stream)
// ============================================================================

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    Telemetry, TelemetryMsg, on_telemetry, "Telemetry")

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    Event, EventMsg, on_event, "Event")

ADAPTOR_DECLARE_TYPED_SUPPLIER(
    Alarm, AlarmMsg, on_alarm, "Alarm")

#undef ADAPTOR_DECLARE_TYPED_SUPPLIER

} // namespace adaptor
