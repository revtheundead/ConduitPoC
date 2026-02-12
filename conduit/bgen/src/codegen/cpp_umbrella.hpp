// SPDX-License-Identifier: MIT
// Bgen - Umbrella Header Generator

#pragma once

#include <string>

namespace bgen::codegen {

// Generate the umbrella include header
std::string generate_umbrella(const std::string& protocol_name);

} // namespace bgen::codegen
