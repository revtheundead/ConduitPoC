// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Naming Service Helper
//
// Thin wrapper around CosNaming::NamingContextExt that binds and unbinds
// object references using human-readable "/" separated paths — e.g.
// "CorbaAdaptor/Heartbeat". Intermediate naming contexts are created on
// demand during bind() and looked up (not destroyed) during unbind().
//
// The helper resolves the NameService via
// ORB::resolve_initial_references("NameService"), so callers need to pass
// it on the command line as `-ORBInitRef NameService=corbaloc:...`.

#pragma once

#include <orbsvcs/CosNamingC.h>
#include <tao/ORB.h>

#include <string>
#include <string_view>

namespace adaptor {

class NamingHelper {
public:
    /// Construct from an ORB.  Resolves the `NameService` initial reference
    /// immediately; throws `CORBA::ORB::InvalidName` / `CORBA::NO_RESOURCES`
    /// (via the TAO exceptions) if the reference cannot be resolved or is
    /// not a `NamingContextExt`.
    explicit NamingHelper(CORBA::ORB_ptr orb);

    NamingHelper(const NamingHelper&) = delete;
    NamingHelper& operator=(const NamingHelper&) = delete;

    /// Bind `obj` to the given "/" separated path, creating any missing
    /// intermediate contexts along the way.  If a binding already exists
    /// at that path it is rebound (so re-starting the adaptor is safe).
    void bind(std::string_view path, CORBA::Object_ptr obj);

    /// Remove the binding at the given path, if any.  Swallows
    /// `NotFound` exceptions so unbinding a non-existent name is a no-op.
    void unbind(std::string_view path) noexcept;

    /// Whether the helper successfully resolved the NameService.
    [[nodiscard]] bool is_valid() const noexcept;

private:
    CosNaming::NamingContextExt_var root_;
};

} // namespace adaptor
