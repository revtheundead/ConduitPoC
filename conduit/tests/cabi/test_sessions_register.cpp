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

#ifndef CONDUIT_CABI_EXPORTS
// When the full CABI library is not linked (conduit_codec_cabi_test), try to
// resolve conduit_xcvr_register_session at runtime so sessions are registered
// in the xcvr registry when loaded alongside the full CABI library.
using xcvr_reg_fn = void(*)(const char*, void*(*)(void));

#ifdef _WIN32
#include <windows.h>
static xcvr_reg_fn resolve_xcvr_register() {
    HMODULE mod = GetModuleHandleA(nullptr);
    if (!mod) return nullptr;
    return reinterpret_cast<xcvr_reg_fn>(
        GetProcAddress(mod, "conduit_xcvr_register_session"));
}
#else
#include <dlfcn.h>
static xcvr_reg_fn resolve_xcvr_register() {
    return reinterpret_cast<xcvr_reg_fn>(
        dlsym(RTLD_DEFAULT, "conduit_xcvr_register_session"));
}
#endif
#endif // !CONDUIT_CABI_EXPORTS

static void register_xcvr_sessions() {
#ifdef CONDUIT_CABI_EXPORTS
    // Direct call — symbol is linked into this library.
    conduit_xcvr_register_session("session_protocol", create_session_protocol);
    conduit_xcvr_register_session("choice_protocol", create_choice_protocol);
    conduit_xcvr_register_session("sentry_link", create_sentry_link);
    conduit_xcvr_register_session("direction_qualified", create_direction_qualified);
#else
    // Runtime resolution — works when loaded into a process that also has
    // the full CABI library (e.g. Java loading both .so/.dll).
    auto xcvr_reg = resolve_xcvr_register();
    if (xcvr_reg) {
        xcvr_reg("session_protocol", create_session_protocol);
        xcvr_reg("choice_protocol", create_choice_protocol);
        xcvr_reg("sentry_link", create_sentry_link);
        xcvr_reg("direction_qualified", create_direction_qualified);
    }
#endif
}

// Auto-register on library load
struct SessionRegistrar {
    SessionRegistrar() {
        // Codec CABI registry (always available)
        conduit_register_session("session_protocol", create_session_protocol);
        conduit_register_session("choice_protocol", create_choice_protocol);
        conduit_register_session("sentry_link", create_sentry_link);
        conduit_register_session("direction_qualified", create_direction_qualified);

        // Transceiver CABI registry
        register_xcvr_sessions();
    }
};

static SessionRegistrar registrar_;

} // namespace
