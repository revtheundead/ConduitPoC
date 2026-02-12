// SPDX-License-Identifier: MIT
// Bgen - Constants Code Generator

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include <string>

namespace bgen::codegen {

// Generate constants.hpp content
std::string generate_constants(const model::Protocol& protocol,
                               const analyzer::TypeIndex& index,
                               const std::string& ns);

} // namespace bgen::codegen
