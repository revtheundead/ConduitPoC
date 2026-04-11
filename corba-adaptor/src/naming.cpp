// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Naming Service Helper Implementation

#include <adaptor/naming.hpp>

#include <ace/Log_Msg.h>

#include <sstream>
#include <vector>

namespace adaptor {

namespace {

// Split a "a/b/c" path into individual components.
std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) {
                out.push_back(std::move(cur));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// Build a CosNaming::Name from path components. The last component is
// flagged as a leaf (no kind) so it can hold either a context or an object.
CosNaming::Name make_name(const std::vector<std::string>& parts) {
    CosNaming::Name n;
    n.length(static_cast<CORBA::ULong>(parts.size()));
    for (CORBA::ULong i = 0; i < parts.size(); ++i) {
        n[i].id   = CORBA::string_dup(parts[i].c_str());
        n[i].kind = CORBA::string_dup("");
    }
    return n;
}

} // namespace

// ============================================================================
// NamingHelper
// ============================================================================

NamingHelper::NamingHelper(CORBA::ORB_ptr orb) {
    CORBA::Object_var obj = orb->resolve_initial_references("NameService");
    if (CORBA::is_nil(obj.in())) {
        ACE_DEBUG((LM_ERROR,
            "NamingHelper: NameService initial reference is nil — "
            "did you pass -ORBInitRef NameService=corbaloc:...?\n"));
        return;
    }
    root_ = CosNaming::NamingContextExt::_narrow(obj.in());
    if (CORBA::is_nil(root_.in())) {
        ACE_DEBUG((LM_ERROR,
            "NamingHelper: NameService reference is not a NamingContextExt\n"));
    }
}

bool NamingHelper::is_valid() const noexcept {
    return !CORBA::is_nil(root_.in());
}

void NamingHelper::bind(std::string_view path, CORBA::Object_ptr obj) {
    if (!is_valid()) {
        throw CORBA::BAD_INV_ORDER();
    }

    auto parts = split_path(path);
    if (parts.empty()) {
        throw CORBA::BAD_PARAM();
    }

    // Walk / create intermediate contexts for everything but the final leaf
    CosNaming::NamingContext_var ctx =
        CosNaming::NamingContext::_duplicate(root_.in());

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        CosNaming::Name sub;
        sub.length(1);
        sub[0].id   = CORBA::string_dup(parts[i].c_str());
        sub[0].kind = CORBA::string_dup("");

        CosNaming::NamingContext_var child;
        try {
            CORBA::Object_var found = ctx->resolve(sub);
            child = CosNaming::NamingContext::_narrow(found.in());
        } catch (const CosNaming::NamingContext::NotFound&) {
            child = ctx->bind_new_context(sub);
        }

        if (CORBA::is_nil(child.in())) {
            throw CORBA::INTERNAL();
        }
        ctx = child;
    }

    // Rebind the leaf so re-starting the adaptor doesn't fail with
    // AlreadyBound.
    CosNaming::Name leaf;
    leaf.length(1);
    leaf[0].id   = CORBA::string_dup(parts.back().c_str());
    leaf[0].kind = CORBA::string_dup("");
    ctx->rebind(leaf, obj);

    ACE_DEBUG((LM_INFO, "NamingHelper: bound %s\n",
               std::string(path).c_str()));
}

void NamingHelper::unbind(std::string_view path) noexcept {
    if (!is_valid()) return;

    try {
        auto parts = split_path(path);
        auto name  = make_name(parts);
        root_->unbind(name);
        ACE_DEBUG((LM_INFO, "NamingHelper: unbound %s\n",
                   std::string(path).c_str()));
    } catch (const CORBA::Exception&) {
        // Best-effort — nothing to clean up
    }
}

} // namespace adaptor
