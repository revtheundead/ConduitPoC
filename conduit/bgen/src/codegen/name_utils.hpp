// SPDX-License-Identifier: MIT
// Bgen - Name Conversion Utilities

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>

namespace bgen::codegen {

// Convert BMDL kebab-case to C++ snake_case: "my-field-name" -> "my_field_name"
inline std::string to_snake_case(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '-') ? '_' : c;
    }
    return result;
}

// Convert BMDL name to PascalCase: "my-field" -> "MyField", "heartbeat" -> "Heartbeat"
inline std::string to_pascal_case(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    bool cap_next = true;
    for (char c : name) {
        if (c == '-' || c == '_') {
            cap_next = true;
        } else if (cap_next) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap_next = false;
        } else {
            result += c;
        }
    }
    return result;
}

// Convert BMDL name to C++ field name with trailing underscore: "my-field" -> "my_field_"
inline std::string to_member_name(std::string_view name) {
    return to_snake_case(name) + "_";
}

// Convert BMDL name to C++ accessor name: "my-field" -> "my_field"
inline std::string to_accessor_name(std::string_view name) {
    return to_snake_case(name);
}

// Convert PascalCase or mixed-case to snake_case for identifiers:
// "Frame" -> "frame", "MyField" -> "my_field", "HTTPParser" -> "httpparser"
// Also handles hyphens like to_snake_case.
inline std::string to_lower_snake_case(std::string_view name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '-' || c == '_') {
            result += '_';
        } else if (std::isupper(static_cast<unsigned char>(c))) {
            if (i > 0 && name[i-1] != '-' && name[i-1] != '_' &&
                !std::isupper(static_cast<unsigned char>(name[i-1]))) {
                result += '_';
            }
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            result += c;
        }
    }
    return result;
}

// Get C++ type name for a BMDL type name
// Names are used as-is from the spec, with hyphens replaced by underscores.
// Developers are expected to use C++-compatible names in the BMDL spec.
inline std::string to_cpp_type_name(std::string_view name) {
    return to_snake_case(name);
}

// Get C++ enum value name — used as-is from the spec, hyphens replaced
inline std::string to_enum_value_name(std::string_view name) {
    return to_snake_case(name);
}

// Map BMDL primitive to C++ storage type
inline std::string primitive_cpp_type(std::string_view base, int bits, bool is_signed = false) {
    if (base == "float") {
        if (bits <= 32) return "float";
        return "double";
    }
    if (base == "bool") return "bool";
    if (base == "string" || base == "bytes") return ""; // special handling

    std::string prefix = is_signed ? "int" : "uint";
    if (bits <= 8) return prefix + "8_t";
    if (bits <= 16) return prefix + "16_t";
    if (bits <= 32) return prefix + "32_t";
    return prefix + "64_t";
}

// Get the C++ storage type for a given bit width
inline std::string storage_type_for_bits(int bits, bool is_signed) {
    std::string prefix = is_signed ? "int" : "uint";
    if (bits <= 8) return prefix + "8_t";
    if (bits <= 16) return prefix + "16_t";
    if (bits <= 32) return prefix + "32_t";
    return prefix + "64_t";
}

// Check if a C++ identifier (after name conversion) is a C++ keyword
inline bool is_cpp_keyword(std::string_view name) {
    static const std::unordered_set<std::string_view> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
        "bitor", "bool", "break", "case", "catch", "char", "char8_t",
        "char16_t", "char32_t", "class", "compl", "concept", "const",
        "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default",
        "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for",
        "friend", "goto", "if", "inline", "int", "long", "mutable",
        "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
        "operator", "or", "or_eq", "private", "protected", "public",
        "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union",
        "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "xor", "xor_eq",
    };
    return keywords.count(name) > 0;
}

// Check if name forms a valid C++ identifier after conversion
inline bool is_valid_cpp_identifier(std::string_view name) {
    if (name.empty()) return false;
    // Must not start with a digit
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    // Must not start with double underscore (reserved)
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') return false;
    // All characters must be alphanumeric or underscore
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

// Format uint32 as lowercase hex string (no "0x" prefix)
inline std::string to_hex(uint32_t val) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", val);
    return buf;
}

// Format uint64 type_id as "0x<016hex>ULL" literal
inline std::string type_id_literal(uint64_t val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llxULL", static_cast<unsigned long long>(val));
    return buf;
}

// Format double as a precise literal string (no trailing zeros)
inline std::string double_literal(double value) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, ptr);
}

} // namespace bgen::codegen
