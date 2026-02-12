// SPDX-License-Identifier: MIT
// Bgen - Protocol Descriptor Generator

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/session_analyzer.hpp"
#include <string>
#include <vector>

namespace bgen::codegen {

// Generate protocol.hpp content with ProtocolDescriptor
std::string generate_protocol(const model::Protocol& protocol,
                              const std::vector<analyzer::SessionInfo>& sessions,
                              const std::string& ns);

} // namespace bgen::codegen
