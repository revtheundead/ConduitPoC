// SPDX-License-Identifier: MIT
// Bgen - Session Code Generator

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include "../analyzer/session_analyzer.hpp"
#include <string>
#include <vector>

namespace bgen::codegen {

// Generate sessions.hpp content
std::string generate_sessions(const model::Protocol& protocol,
                              const analyzer::TypeIndex& index,
                              const std::vector<analyzer::SessionInfo>& sessions,
                              const std::string& ns);

} // namespace bgen::codegen
