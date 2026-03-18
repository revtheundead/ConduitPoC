// SPDX-License-Identifier: MIT
// Register benchmark sessions with the CABI registry.
//
// This file is compiled into conduit_codec_cabi_bench so that the
// stress_protocol and sentry_link sessions are available via
// conduit_session_create() for the Python and Java benchmarks.

#include <conduit/cabi/conduit_codec_cabi.h>
#include <conduit/traits/session_traits.hpp>

// Generated sessions (from bgen)
#include <stress/sessions.hpp>
#include <sentry_link/sessions.hpp>

namespace {

void* create_stress_frame() {
    return static_cast<void*>(
        stress::create_stress_frame_session().release());
}

void* create_sentry_link() {
    return static_cast<void*>(
        sentry_link::create_frame_session().release());
}

// Auto-register on library load
struct BenchSessionRegistrar {
    BenchSessionRegistrar() {
        conduit_register_session("stress_frame", create_stress_frame);
        conduit_register_session("sentry_link", create_sentry_link);
    }
};

static BenchSessionRegistrar registrar_;

} // namespace
