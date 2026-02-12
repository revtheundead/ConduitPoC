// SPDX-License-Identifier: MIT
// Bgen - Types Code Generator

#pragma once

#include "../model/ast.hpp"
#include <string>

namespace bgen::codegen {

// Generate types.hpp content (aliases, enums, flags, scaled wrappers)
std::string generate_types(const model::Protocol& protocol,
                           const std::string& ns);

} // namespace bgen::codegen
