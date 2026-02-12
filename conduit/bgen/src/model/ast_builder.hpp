// SPDX-License-Identifier: MIT
// Bgen - AST Builder

#pragma once

#include "ast.hpp"
#include "../parser/import_resolver.hpp"
#include <expected>
#include <string>
#include <vector>

namespace bgen::model {

struct BuildError {
    std::string message;
    SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": error: " + message;
    }
};

using BuildResult = std::expected<Protocol, std::vector<BuildError>>;

// Build a Protocol from a root BMDL file path.
// Resolves imports, merges definitions, applies defaults.
BuildResult build_protocol(const std::string& root_path);

} // namespace bgen::model
