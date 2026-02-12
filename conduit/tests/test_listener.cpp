// SPDX-License-Identifier: MIT
// Conduit - Test progress listener
//
// Prints a single banner so conduit_tests doesn't appear to hang
// during long-running test suites.

#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdio>

namespace {

class ProgressListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const& /*info*/) override {
        std::fprintf(stdout, "[conduit] Running tests...\n");
        std::fflush(stdout);
    }
};

} // namespace

CATCH_REGISTER_LISTENER(ProgressListener)
