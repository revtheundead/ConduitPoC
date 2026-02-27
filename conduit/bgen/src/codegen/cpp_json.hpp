// SPDX-License-Identifier: MIT
// Bgen - JSON Serialization Code Generator

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include "../analyzer/wire_sizer.hpp"
#include "../analyzer/session_analyzer.hpp"
#include <string>
#include <vector>

namespace bgen::codegen {

// Generate json.hpp content (nlohmann to_json/from_json for all structs and messages)
std::string generate_json(const model::Protocol& protocol,
                          const analyzer::TypeIndex& index,
                          const analyzer::WireSizeInfo& sizes,
                          const std::vector<analyzer::SessionInfo>& sessions,
                          const std::string& ns);

} // namespace bgen::codegen
