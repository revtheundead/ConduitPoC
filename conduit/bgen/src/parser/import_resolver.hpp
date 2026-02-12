// SPDX-License-Identifier: MIT
// Bgen - Import Resolver

#pragma once

#include "../model/ast.hpp"
#include "xml_parser.hpp"
#include <expected>
#include <string>
#include <vector>

namespace bgen::parser {

struct ResolveError {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": error: " + message;
    }
};

using ResolveResult = std::expected<std::vector<model::BmdlFile>, std::vector<ResolveError>>;

// Resolve all imports starting from a root BMDL file.
// Returns all BmdlFiles in dependency order (imports first, root last), deduplicated by absolute path.
ResolveResult resolve_imports(const std::string& root_path);

} // namespace bgen::parser
