// SPDX-License-Identifier: MIT
// Bgen - AST Dump (human-readable AST printer)

#pragma once

#include "ast.hpp"
#include <ostream>

namespace bgen::model {

/// Print the full Protocol AST in a human-readable indented format.
void dump_protocol(const Protocol& protocol, std::ostream& out);

} // namespace bgen::model
