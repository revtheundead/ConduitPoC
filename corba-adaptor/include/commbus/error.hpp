// SPDX-License-Identifier: MIT
//
// commbus/error.hpp
// =================
//
// CommBus reuses the codebase's existing `bgen11::Error` value type so that
// user code can keep a single error vocabulary across codec failures, peer
// failures, and bus-orchestration failures.  The codes below extend the
// numeric space defined in `bgen11/error.hpp`; they all live in the 1000+
// range to make them visually distinct in logs.
//
// Every commbus public API returns `commbus::Result<T>` (or the void
// variant) which is a typedef of `cpp11::expected<T, bgen11::Error>`.  The
// `BGEN11_TRY` family of macros from bgen11 work unchanged here.
//
// Design note: keeping the error TYPE unified (`bgen11::Error`) and only
// extending the error-CODE namespace means errors flow through the system
// without wrapping or rewrapping at every layer boundary.

#ifndef COMMBUS_ERROR_HPP
#define COMMBUS_ERROR_HPP

#include <bgen11/error.hpp>

namespace commbus {

// Use the project's existing Result + Error vocabulary.
using bgen11::Error;
template <typename T>
using Result = bgen11::Result<T>;
using VoidResult = bgen11::VoidResult;

// CommBus error codes occupy the 1000+ range to stay clear of bgen11's
// decoder (100..) / encoder (200..) / internal (900..) codes.
enum ErrorCode {
    // Submission failures (return synchronously from submit / future / callback)
    ErrorCode_QueueFull           = 1000,  // bounded queue at capacity
    ErrorCode_BusStopped          = 1001,  // bus.stop() already called
    ErrorCode_InvalidTask         = 1002,  // empty std::function passed in

    // Task-execution failures (returned from inside the task body)
    ErrorCode_Cancelled           = 1010,  // cooperative cancel / shutdown
    ErrorCode_Timeout             = 1011,  // ctx.wait deadline expired
    ErrorCode_TaskThrew           = 1012,  // task body escaped an exception
    ErrorCode_CallbackThrew       = 1013,  // completion callback threw

    // Slot / registry failures
    ErrorCode_SlotAlreadyResolved = 1020,  // double-fulfil attempt (no-op)
    ErrorCode_NoMatchingWaiter    = 1021,  // deliver() found nothing to wake
    ErrorCode_DuplicateWaiter     = 1022,  // register_wait with existing id

    // External-event-driven failures (caller supplies via Slot::fail)
    ErrorCode_ExternalFailure     = 1030   // generic carrier for "the thing
                                           //  on the other end said no"
};

// Convenience builders — keep call sites short and uniform.

inline Error make_error(int code, const std::string& msg) {
    return Error(code, msg);
}

inline Error queue_full_error() {
    return Error(ErrorCode_QueueFull, "CommBus queue at capacity");
}
inline Error bus_stopped_error() {
    return Error(ErrorCode_BusStopped, "CommBus already stopping/stopped");
}
inline Error invalid_task_error() {
    return Error(ErrorCode_InvalidTask, "task callable is empty");
}
inline Error cancelled_error() {
    return Error(ErrorCode_Cancelled, "operation cancelled");
}
inline Error timeout_error() {
    return Error(ErrorCode_Timeout, "deadline expired");
}
inline Error task_threw_error(const std::string& what) {
    return Error(ErrorCode_TaskThrew, "task threw: " + what);
}
inline Error callback_threw_error(const std::string& what) {
    return Error(ErrorCode_CallbackThrew, "callback threw: " + what);
}
inline Error duplicate_waiter_error() {
    return Error(ErrorCode_DuplicateWaiter,
                 "wait registry: duplicate correlation id");
}

} // namespace commbus

#endif
