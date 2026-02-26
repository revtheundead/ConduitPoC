// SPDX-License-Identifier: MIT
// Register test sessions with the CABI registries for Python/Java test use.
//
// This file is compiled into the CABI shared libraries so that the test
// protocols are available via conduit_session_create("session_protocol"), etc.

#include <conduit/cabi/conduit_codec_cabi.h>
#ifdef CONDUIT_CABI_EXPORTS
#include <conduit/cabi/conduit_cabi.h>
#endif
#include <conduit/traits/session_traits.hpp>

// Generated sessions
#include "session_protocol/sessions.hpp"
#include "choice_protocol/sessions.hpp"
#include "sentry_link/sessions.hpp"
#include "direction_qualified/sessions.hpp"

namespace {

// Factory functions returning void* (new ISession on the heap)
void* create_session_protocol() {
    return static_cast<void*>(
        session_test::create_packet_session().release());
}

void* create_choice_protocol() {
    return static_cast<void*>(
        choice_test::create_frame_session().release());
}

void* create_sentry_link() {
    return static_cast<void*>(
        sentry_link::create_frame_session().release());
}

void* create_direction_qualified() {
    return static_cast<void*>(
        direction_qualified::create_frame_session().release());
}

// Auto-register on library load
struct SessionRegistrar {
    SessionRegistrar() {
        // Codec CABI registry
        conduit_register_session("session_protocol", create_session_protocol);
        conduit_register_session("choice_protocol", create_choice_protocol);
        conduit_register_session("sentry_link", create_sentry_link);
        conduit_register_session("direction_qualified", create_direction_qualified);
#ifdef CONDUIT_CABI_EXPORTS
        // Transceiver CABI registry (only when building xcvr lib)
        conduit_xcvr_register_session("session_protocol", create_session_protocol);
        conduit_xcvr_register_session("choice_protocol", create_choice_protocol);
        conduit_xcvr_register_session("sentry_link", create_sentry_link);
        conduit_xcvr_register_session("direction_qualified", create_direction_qualified);
#endif
    }
};

static SessionRegistrar registrar_;

} // namespace
