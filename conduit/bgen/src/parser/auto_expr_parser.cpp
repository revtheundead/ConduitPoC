// SPDX-License-Identifier: MIT
// Bgen - Auto expression parser implementation

#include "auto_expr_parser.hpp"
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

// Try to parse an offset: "+/- N" from the remaining string
bool try_parse_offset(std::string_view remaining, int& offset) {
    remaining = trim(remaining);
    if (remaining.empty()) { offset = 0; return true; }

    char sign = remaining[0];
    if (sign != '+' && sign != '-') return false;

    auto num_str = trim(remaining.substr(1));
    if (num_str.empty()) return false;

    int val = 0;
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
    if (ec != std::errc{} || ptr != num_str.data() + num_str.size()) return false;

    offset = (sign == '-') ? -val : val;
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

        // Check for offset
        auto rest = trim(sv.substr(pos));
        if (!rest.empty()) {
            int off = 0;
            if (!try_parse_offset(rest, off)) {
                return std::unexpected(AutoExprError{"invalid offset in length expression: '" + std::string(rest) + "'"});
            }
            result.offset = off;
        }

        return result;
    }

    return std::unexpected(AutoExprError{"unknown auto expression: '" + std::string(sv) + "'"});
}

} // namespace bgen::parser
