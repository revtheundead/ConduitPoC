// SPDX-License-Identifier: MIT
// Bgen - Auto expression parser for auto="..." attribute values

#pragma once

#include "../model/ast.hpp"
#include <expected>
#include <string>

namespace bgen::parser {

struct AutoExprError {
    std::string message;
};

using AutoExprResult = std::expected<model::AutoExpr, AutoExprError>;

// Parse an auto="..." attribute value into an AutoExpr.
// Supports: id, length, length(field), length {+-*/%} {N|field},
//           length(field) {+-*/%} {N|field},
//           count(field), increment, config(key), timestamp
AutoExprResult parse_auto_expr(const std::string& input);

} // namespace bgen::parser
