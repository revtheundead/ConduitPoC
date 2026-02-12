// SPDX-License-Identifier: MIT
// Bgen - Shared parsing utilities for the analyzer layer

#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>

namespace bgen::analyzer {

// Parse a numeric literal string (decimal or 0x hex) to int64_t.
// Returns nullopt on parse failure or empty input.
inline std::optional<int64_t> parse_literal(const std::string& s) {
    if (s.empty()) return std::nullopt;
    int64_t val = 0;
    size_t start = 0;
    bool negate = false;
    if (s[0] == '-') { negate = true; start = 1; }
    if (s.size() > start + 2 && s[start] == '0' && (s[start+1] == 'x' || s[start+1] == 'X')) {
        auto [ptr, ec] = std::from_chars(s.data() + start + 2, s.data() + s.size(), val, 16);
        if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    } else {
        auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + s.size(), val);
        if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    }
    return negate ? -val : val;
}

} // namespace bgen::analyzer
