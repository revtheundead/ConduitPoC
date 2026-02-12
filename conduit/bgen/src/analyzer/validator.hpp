// SPDX-License-Identifier: MIT
// Bgen - BMDL Validator

#pragma once

#include "../model/ast.hpp"
#include "type_resolver.hpp"
#include <expected>
#include <string>
#include <vector>

namespace bgen::analyzer {

struct ValidationError {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": error: " + message;
    }
};

using ValidationResult = std::expected<void, std::vector<ValidationError>>;

// Validate a protocol against all BMDL spec rules.
// Requires a resolved TypeIndex for reference checking.
ValidationResult validate(const model::Protocol& protocol, const TypeIndex& index);

} // namespace bgen::analyzer
