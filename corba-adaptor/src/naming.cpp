// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Naming Service Helper Implementation (C++11)

#include "adaptor/naming.hpp"

#include <ace/Log_Msg.h>

#include <vector>

namespace adaptor {

namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '/') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

CosNaming::Name make_name(const std::vector<std::string>& parts) {
    CosNaming::Name n;
    n.length(static_cast<CORBA::ULong>(parts.size()));
    for (CORBA::ULong i = 0; i < parts.size(); ++i) {
        n[i].id   = CORBA::string_dup(parts[i].c_str());
        n[i].kind = CORBA::string_dup("");
    }
    return n;
}

}

NamingHelper::NamingHelper(CORBA::ORB_ptr orb) {
    CORBA::Object_var obj = orb->resolve_initial_references("NameService");
    if (CORBA::is_nil(obj.in())) {
        ACE_DEBUG((LM_ERROR,
            "NamingHelper: NameService initial reference is nil\n"));
        return;
    }
    root_ = CosNaming::NamingContextExt::_narrow(obj.in());
    if (CORBA::is_nil(root_.in())) {
        ACE_DEBUG((LM_ERROR,
            "NamingHelper: NameService reference is not a NamingContextExt\n"));
    }
}

bool NamingHelper::is_valid() const {
    return !CORBA::is_nil(root_.in());
}

void NamingHelper::bind(const std::string& path, CORBA::Object_ptr obj) {
    if (!is_valid()) throw CORBA::BAD_INV_ORDER();
    std::vector<std::string> parts = split_path(path);
    if (parts.empty()) throw CORBA::BAD_PARAM();

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
        if (CORBA::is_nil(child.in())) throw CORBA::INTERNAL();
        ctx = child;
    }

    CosNaming::Name leaf;
    leaf.length(1);
    leaf[0].id   = CORBA::string_dup(parts.back().c_str());
    leaf[0].kind = CORBA::string_dup("");
    ctx->rebind(leaf, obj);

    ACE_DEBUG((LM_INFO, "NamingHelper: bound %s\n", path.c_str()));
}

void NamingHelper::unbind(const std::string& path) {
    if (!is_valid()) return;
    try {
        std::vector<std::string> parts = split_path(path);
        CosNaming::Name name = make_name(parts);
        root_->unbind(name);
    } catch (const CORBA::Exception&) {}
}

} // namespace adaptor
