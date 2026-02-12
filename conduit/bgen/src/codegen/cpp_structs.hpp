// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include "../analyzer/wire_sizer.hpp"
#include "../analyzer/session_analyzer.hpp"
#include <string>
#include <vector>

namespace bgen::codegen {

// Generate structs.hpp content (struct classes from <types>)
std::string generate_structs(const model::Protocol& protocol,
                             const analyzer::TypeIndex& index,
                             const analyzer::WireSizeInfo& sizes,
                             const std::vector<analyzer::SessionInfo>& sessions,
                             const std::string& ns);

// Generate messages.hpp content (message classes with wrap() overloads for entry-points)
std::string generate_messages(const model::Protocol& protocol,
                              const analyzer::TypeIndex& index,
                              const analyzer::WireSizeInfo& sizes,
                              const std::vector<analyzer::SessionInfo>& sessions,
                              const std::string& ns);

} // namespace bgen::codegen
