// SPDX-License-Identifier: MIT
// Bgen - Wire Size Computation

#pragma once

#include "../model/ast.hpp"
#include "type_resolver.hpp"
#include <cstddef>
#include <optional>
#include <string>

namespace bgen::analyzer {

// Wire size: either a fixed constant or dynamic (nullopt)
using WireSize = std::optional<size_t>;

struct WireSizeInfo {
    // Map of type/struct/message name -> wire size in bytes (nullopt = dynamic)
    std::unordered_map<std::string, WireSize> sizes;

    WireSize get(const std::string& name) const {
        auto it = sizes.find(name);
        if (it != sizes.end()) return it->second;
        return std::nullopt;
    }
};

// Compute wire sizes for all types, structs, and messages.
WireSizeInfo compute_wire_sizes(const model::Protocol& protocol, const TypeIndex& index);

} // namespace bgen::analyzer
