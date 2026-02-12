// SPDX-License-Identifier: MIT
// Bgen - Type Resolver

#pragma once

#include "../model/ast.hpp"
#include <expected>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace bgen::analyzer {

struct ResolveError {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": error: " + message;
    }
};

// Resolved type reference: what a type_ref string points to
using ResolvedDef = std::variant<
    const model::TypeDef*,
    const model::StructDef*,
    const model::MessageDef*
>;

// Type lookup tables built by resolver
struct TypeIndex {
    std::unordered_map<std::string, const model::TypeDef*> types;
    std::unordered_map<std::string, const model::StructDef*> structs;
    std::unordered_map<std::string, const model::MessageDef*> messages;
    std::unordered_map<std::string, const model::ConstDef*> constants;
    std::unordered_map<std::string, const model::FrameDef*> frames;

    // Look up any type by name
    std::optional<ResolvedDef> find(const std::string& name) const {
        auto it = types.find(name);
        if (it != types.end()) return ResolvedDef{it->second};
        auto it2 = structs.find(name);
        if (it2 != structs.end()) return ResolvedDef{it2->second};
        auto it3 = messages.find(name);
        if (it3 != messages.end()) return ResolvedDef{it3->second};
        return std::nullopt;
    }
};

using ResolveResult = std::expected<TypeIndex, std::vector<ResolveError>>;

// Build the type index from a protocol.
// Resolves all type references and applies inheritance rules.
ResolveResult resolve_types(model::Protocol& protocol);

} // namespace bgen::analyzer
