// SPDX-License-Identifier: MIT
// Bgen - Auto expression parser implementation

#include "auto_expr_parser.hpp"
#include <cctype>
#include <charconv>

namespace bgen::parser {

namespace {

// Trim leading/trailing whitespace
std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return sv;
}

// Try to parse a parenthesized argument: "keyword(arg)" -> returns arg
// If no parens, returns empty string_view and advances nothing.
// pos should point right after the keyword.
bool try_parse_paren_arg(std::string_view input, size_t keyword_end,
                         std::string_view& arg, size_t& after_paren) {
    size_t i = keyword_end;
    // Skip whitespace
    while (i < input.size() && (input[i] == ' ' || input[i] == '\t')) ++i;
    if (i >= input.size() || input[i] != '(') return false;
    ++i; // skip '('
    size_t start = i;
    while (i < input.size() && input[i] != ')') ++i;
    if (i >= input.size()) return false; // no closing paren
    arg = trim(input.substr(start, i - start));
    after_paren = i + 1;
    return true;
}

// Try to parse an arithmetic modifier: "op operand" from the remaining string.
// Supported operators: + - * / %
// Operand: integer literal or BMDL field name (starts with letter, may contain dashes)
bool try_parse_modifier(std::string_view remaining, model::ArithModifier& mod) {
    remaining = trim(remaining);
    if (remaining.empty()) { mod = {}; return true; }

    char op_ch = remaining[0];
    model::ArithOp op;
    switch (op_ch) {
        case '+': op = model::ArithOp::Add; break;
        case '-': op = model::ArithOp::Sub; break;
        case '*': op = model::ArithOp::Mul; break;
        case '/': op = model::ArithOp::Div; break;
        case '%': op = model::ArithOp::Mod; break;
        default: return false;
    }

    auto operand_str = trim(remaining.substr(1));
    if (operand_str.empty()) return false;

    // Determine if operand is a number or a field name
    char first = operand_str[0];
    if (std::isdigit(static_cast<unsigned char>(first)) || first == '-') {
        // Numeric literal
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(operand_str.data(), operand_str.data() + operand_str.size(), val);
        if (ec != std::errc{} || ptr != operand_str.data() + operand_str.size()) return false;
        mod.op = op;
        mod.literal = val;
        mod.field_ref.clear();
    } else if (std::isalpha(static_cast<unsigned char>(first)) || first == '_') {
        // Field name operand (e.g., "header-size", "prefix_len")
        // Validate all chars are alphanumeric, dash, or underscore
        for (char c : operand_str) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
        }
        mod.op = op;
        mod.literal = 0;
        mod.field_ref = std::string(operand_str);
    } else {
        return false;
    }

    return true;
}

} // anonymous namespace

AutoExprResult parse_auto_expr(const std::string& input) {
    auto sv = trim(std::string_view(input));
    if (sv.empty()) {
        return std::unexpected(AutoExprError{"empty auto expression"});
    }

    model::AutoExpr result;

    // Simple keywords (no arguments, no offset)
    if (sv == "id") {
        result.kind = model::AutoKind::Id;
        return result;
    }
    if (sv == "increment") {
        result.kind = model::AutoKind::Increment;
        return result;
    }
    if (sv == "timestamp") {
        result.kind = model::AutoKind::Timestamp;
        return result;
    }

    // Keywords that take a parenthesized argument
    if (sv.starts_with("config")) {
        std::string_view arg;
        size_t after;
        if (!try_parse_paren_arg(sv, 6, arg, after)) {
            return std::unexpected(AutoExprError{"config requires a key argument: config(key)"});
        }
        if (arg.empty()) {
            return std::unexpected(AutoExprError{"config key cannot be empty"});
        }
        auto rest = trim(sv.substr(after));
        if (!rest.empty()) {
            return std::unexpected(AutoExprError{"unexpected text after config(...): '" + std::string(rest) + "'"});
        }
        result.kind = model::AutoKind::Config;
        result.key = std::string(arg);
        return result;
    }

    // count(field)
    if (sv.starts_with("count")) {
        std::string_view arg;
        size_t after;
        if (!try_parse_paren_arg(sv, 5, arg, after)) {
            return std::unexpected(AutoExprError{"count requires a field argument: count(field)"});
        }
        if (arg.empty()) {
            return std::unexpected(AutoExprError{"count field reference cannot be empty"});
        }
        auto rest = trim(sv.substr(after));
        if (!rest.empty()) {
            return std::unexpected(AutoExprError{"unexpected text after count(...): '" + std::string(rest) + "'"});
        }
        result.kind = model::AutoKind::Count;
        result.field_ref = std::string(arg);
        return result;
    }

    // length, length(field), length +/- N, length(field) +/- N
    if (sv.starts_with("length")) {
        result.kind = model::AutoKind::Length;
        size_t pos = 6; // after "length"

        // Check for parenthesized field ref
        std::string_view arg;
        size_t after;
        if (try_parse_paren_arg(sv, pos, arg, after)) {
            if (arg.empty()) {
                return std::unexpected(AutoExprError{"length field reference cannot be empty"});
            }
            result.field_ref = std::string(arg);
            pos = after;
        }

        // Check for arithmetic modifier
        auto rest = trim(sv.substr(pos));
        if (!rest.empty()) {
            model::ArithModifier mod;
            if (!try_parse_modifier(rest, mod)) {
                return std::unexpected(AutoExprError{"invalid modifier in length expression: '" + std::string(rest) + "'"});
            }
            result.modifier = std::move(mod);
        }

        return result;
    }

    return std::unexpected(AutoExprError{"unknown auto expression: '" + std::string(sv) + "'"});
}

} // namespace bgen::parser
