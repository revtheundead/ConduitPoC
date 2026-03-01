// Register asterix session factories with the Conduit CABI registries
// so that Java/Python apps can use them via conduit_add_peer("asterix"/"asterix_alt").

#include <conduit/cabi/conduit_cabi.h>

#include "asterix_sessions.hpp"
#include "asterix_alt_sessions.hpp"

static void* create_asterix_session() {
    return static_cast<void*>(
        asterix::create_asterix_data_block_session().release());
}

static void* create_asterix_alt_session() {
    return static_cast<void*>(
        asterix_alt::create_asterix_data_block_session().release());
}

// Auto-register on library load
namespace {
struct AsterixSessionRegistrar {
    AsterixSessionRegistrar() {
        conduit_xcvr_register_session("asterix", create_asterix_session);
        conduit_xcvr_register_session("asterix_alt", create_asterix_alt_session);
    }
};
static AsterixSessionRegistrar registrar_;
} // namespace

// Explicit registration function callable from FFI (Java/Python)
extern "C" {
CONDUIT_CABI_API void conduit_register_asterix_sessions(void) {
    conduit_xcvr_register_session("asterix", create_asterix_session);
    conduit_xcvr_register_session("asterix_alt", create_asterix_alt_session);
}
}
