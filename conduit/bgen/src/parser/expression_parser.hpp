// SPDX-License-Identifier: MIT
// Bgen - Expression Parser for BMDL expressions

#pragma once

#include "../model/ast.hpp"
#include <expected>
#include <string>
#include <string_view>

namespace bgen::parser {

struct ParseError {
    std::string message;
    model::SourceLoc loc;

    std::string format() const {
        return loc.to_string() + ": " + message;
    }
};

using ExprResult = std::expected<std::unique_ptr<model::Expr>, ParseError>;

// Parse a BMDL expression string.
// Used for present-when, length-from, count-from, switch attributes.
ExprResult parse_expression(std::string_view input, const model::SourceLoc& base_loc = {});

} // namespace bgen::parser
