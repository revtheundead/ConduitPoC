// SPDX-License-Identifier: MIT
// Bgen - Python Code Generation Backend Implementation
//
// Generates a self-contained Python codec package from BMDL protocol
// definitions. The output is wire-compatible with C++-generated code.

#include "python_backend.hpp"
#include "cpp_structs_helpers.hpp"
#include "emit_context.hpp"
#include "name_utils.hpp"
#include "../logger.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <functional>
#include <set>
#include <sstream>

namespace bgen::codegen {

namespace {

// ============================================================================
// File writer
// ============================================================================

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        Logger::error("cannot write to " + path.string());
        return false;
    }
    out << content;
    out.flush();
    if (!out) {
        Logger::error("write failed for " + path.string());
        return false;
    }
    return true;
}

// ============================================================================
// Doc-string helpers
// ============================================================================

// Split a doc string into trimmed lines.
std::vector<std::string> py_doc_lines(const std::string& doc) {
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (start < doc.size()) {
        auto nl = doc.find('\n', start);
        auto segment = (nl == std::string::npos)
            ? doc.substr(start)
            : doc.substr(start, nl - start);
        // Trim trailing whitespace
        while (!segment.empty() && (segment.back() == ' ' || segment.back() == '\r' || segment.back() == '\t'))
            segment.pop_back();
        // Trim leading whitespace
        std::string::size_type first_non_ws = segment.find_first_not_of(" \t");
        if (first_non_ws != std::string::npos)
            segment = segment.substr(first_non_ws);
        else
            segment.clear();
        lines.push_back(segment);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// Emit a (possibly multi-line) doc string as Python # comment lines.
// Each line of `doc` becomes a separate "# ..." line at the current indent.
void py_emit_doc_comment(EmitContext& ctx, const std::string& doc) {
    if (doc.empty()) return;
    for (const auto& line : py_doc_lines(doc)) {
        if (line.empty())
            ctx.line("#");
        else
            ctx.line("# " + line);
    }
}

// Emit a (possibly multi-line) doc string as a Python triple-quoted docstring.
// Each line is properly indented at the current context level.
void py_emit_docstring(EmitContext& ctx, const std::string& doc) {
    if (doc.empty()) return;
    auto lines = py_doc_lines(doc);
    if (lines.size() == 1) {
        ctx.line("\"\"\"" + lines[0] + "\"\"\"");
    } else {
        ctx.line("\"\"\"" + lines[0]);
        for (size_t i = 1; i < lines.size(); i++)
            ctx.line(lines[i]);
        ctx.line("\"\"\"");
    }
}

// ============================================================================
// Python naming helpers
// ============================================================================

std::string py_snake(std::string_view name) {
    std::string r;
    r.reserve(name.size());
    for (char c : name) r += (c == '-') ? '_' : c;
    return r;
}

std::string py_class(std::string_view name) {
    return to_pascal_case(name);
}

std::string py_enum_val(std::string_view name) {
    std::string r;
    r.reserve(name.size());
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '-' || c == '_') {
            r += '_';
        } else if (std::isupper(static_cast<unsigned char>(c)) && i > 0 &&
                   !std::isupper(static_cast<unsigned char>(name[i-1])) &&
                   name[i-1] != '-' && name[i-1] != '_') {
            r += '_';
            r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else {
            r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return r;
}

bool is_py_keyword(std::string_view name) {
    static const std::set<std::string_view> kw = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else",
        "except", "finally", "for", "from", "global", "if", "import",
        "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
        "return", "try", "while", "with", "yield",
    };
    return kw.count(name) > 0;
}

std::string py_field(std::string_view name) {
    auto s = py_snake(name);
    if (is_py_keyword(s)) return s + "_";
    return s;
}

// Qualify bare constant names with Constants. prefix
// e.g. "MAGIC" -> "Constants.MAGIC", "0xBEEF" -> "0xBEEF", "42" -> "42"
std::string py_qualify_const(const std::string& val) {
    if (val.empty()) return val;
    // Check if it looks like a constant name (starts with upper-case letter)
    char c = val[0];
    if (std::isalpha(static_cast<unsigned char>(c)) && std::isupper(static_cast<unsigned char>(c))) {
        return "Constants." + val;
    }
    return val;
}

std::string py_hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string py_double(double v) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, ptr);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        s += ".0";
    return s;
}

// Helper: Python read expression for a type based on wire encoding and signedness
std::string py_type_read_expr(const model::TypeDef& t, bool is_signed) {
    std::string bits_s = std::to_string(t.bits);
    if (t.wire_encoding == model::WireEncoding::BCD)
        return "r.read_bcd(" + bits_s + ")";
    if (t.wire_encoding == model::WireEncoding::BCD_S)
        return "r.read_bcd_signed(" + bits_s + ")";
    if (t.wire_encoding == model::WireEncoding::BNR_S)
        return "r.read_sign_magnitude(" + bits_s + ")";
    return std::string("r.read_") + (is_signed ? "signed_bits" : "bits") +
           "(" + bits_s + ")";
}

// Helper: Python write statement for a type based on wire encoding and signedness
std::string py_type_write_stmt(const std::string& val, const model::TypeDef& t, bool is_signed) {
    std::string bits_s = std::to_string(t.bits);
    if (t.wire_encoding == model::WireEncoding::BCD)
        return "w.write_bcd(" + val + ", " + bits_s + ")";
    if (t.wire_encoding == model::WireEncoding::BCD_S)
        return "w.write_bcd_signed(" + val + ", " + bits_s + ")";
    if (t.wire_encoding == model::WireEncoding::BNR_S)
        return "w.write_sign_magnitude(" + val + ", " + bits_s + ")";
    return std::string("w.write_") + (is_signed ? "signed_bits" : "bits") +
           "(" + val + ", " + bits_s + ")";
}

// ============================================================================
// Expression codegen for Python
// ============================================================================

std::string py_expr(const model::Expr& e, const std::string& obj = "self") {
    switch (e.op) {
        case model::ExprOp::NumberLit:
            return std::to_string(e.number_value);
        case model::ExprOp::BoolLit:
            return e.bool_value ? "True" : "False";
        case model::ExprOp::FieldRef: {
            std::string path = e.name;
            std::string result = obj;
            size_t pos = 0;
            while (pos < path.size()) {
                size_t dot = path.find('.', pos);
                std::string seg;
                if (dot == std::string::npos) { seg = path.substr(pos); pos = path.size(); }
                else { seg = path.substr(pos, dot - pos); pos = dot + 1; }
                result += "." + py_field(seg);
            }
            return result;
        }
        case model::ExprOp::ConstantRef:
            return "Constants." + py_snake(e.name);
        case model::ExprOp::Remaining:
            return "(r.remaining_bits() // 8)";
        case model::ExprOp::Add:    return "(" + py_expr(*e.left, obj) + " + " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Sub:    return "(" + py_expr(*e.left, obj) + " - " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Mul:    return "(" + py_expr(*e.left, obj) + " * " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Div:    return "(" + py_expr(*e.left, obj) + " // " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Mod:    return "(" + py_expr(*e.left, obj) + " % " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Eq:     return "(" + py_expr(*e.left, obj) + " == " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Neq:    return "(" + py_expr(*e.left, obj) + " != " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Lt:     return "(" + py_expr(*e.left, obj) + " < " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Lte:    return "(" + py_expr(*e.left, obj) + " <= " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Gt:     return "(" + py_expr(*e.left, obj) + " > " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Gte:    return "(" + py_expr(*e.left, obj) + " >= " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::LogAnd: return "(" + py_expr(*e.left, obj) + " and " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::LogOr:  return "(" + py_expr(*e.left, obj) + " or " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::BitAnd: return "(" + py_expr(*e.left, obj) + " & " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::BitOr:  return "(" + py_expr(*e.left, obj) + " | " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::BitXor: return "(" + py_expr(*e.left, obj) + " ^ " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::ShiftLeft:  return "(" + py_expr(*e.left, obj) + " << " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::ShiftRight: return "(" + py_expr(*e.left, obj) + " >> " + py_expr(*e.right, obj) + ")";
        case model::ExprOp::Negate: return "(-" + py_expr(*e.left, obj) + ")";
        case model::ExprOp::BitNot: return "(~" + py_expr(*e.left, obj) + ")";
        case model::ExprOp::LogNot: return "(not " + py_expr(*e.left, obj) + ")";
    }
    return "0";
}

// ============================================================================
// Python field type resolution
// ============================================================================

struct PyFieldInfo {
    std::string py_type;
    bool is_struct = false;
    bool is_enum = false;
    bool is_string = false;
    bool is_string_struct = false; // struct type wrapping a string (e.g. AsciiStr)
    bool is_bytes = false;
    bool is_float = false;
    bool is_bool = false;
    int bits = 0;
    bool is_signed = false;
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian endian = model::Endian::Big;
    model::WireEncoding wire_enc = model::WireEncoding::Default;
};

PyFieldInfo py_resolve_field(const model::Field& f, const analyzer::TypeIndex& index) {
    auto cfi = resolve_field_type(f, index);
    PyFieldInfo pi;
    pi.is_struct = cfi.is_struct;
    pi.is_enum = cfi.is_enum;
    pi.is_string = cfi.is_string;
    pi.is_bytes = cfi.is_bytes;
    pi.is_float = cfi.is_float;
    pi.is_bool = cfi.is_bool;
    pi.bits = cfi.bits;
    pi.is_signed = cfi.is_signed;
    pi.has_scale = cfi.has_field_scale;
    pi.scale = cfi.field_scale;
    pi.offset = cfi.field_offset;
    pi.raw_bits = cfi.raw_bits;
    pi.raw_signed = cfi.raw_signed;
    pi.endian = f.endian;
    pi.wire_enc = cfi.wire_encoding;

    if (pi.is_string) pi.py_type = "str";
    else if (pi.is_bytes) pi.py_type = "bytes";
    else if (pi.is_bool) pi.py_type = "bool";
    else if (pi.is_float || pi.has_scale) pi.py_type = "float";
    else if (pi.is_enum || pi.is_struct) pi.py_type = py_class(f.type_ref);
    else pi.py_type = "int";

    // Detect string-based struct types (e.g. AsciiStr wrapping a string)
    if (pi.is_struct && !f.type_ref.empty()) {
        auto resolved = index.find(f.type_ref);
        if (resolved) {
            std::visit([&pi](const auto* def) {
                using T = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<T, model::TypeDef>) {
                    if (def->base == model::PrimitiveBase::String)
                        pi.is_string_struct = true;
                }
            }, *resolved);
        }
    }

    return pi;
}

// Helper: resolve element type info for an array definition
PyFieldInfo py_resolve_element_type(const model::ArrayDef& ad, const analyzer::TypeIndex& index) {
    model::Field tmp;
    tmp.type_ref = ad.type_ref;
    return py_resolve_field(tmp, index);
}

// Byte alignment tracker for code generation (mirrors C++ StructEmitter::bit_mod8_).
// Tracks cumulative bit offset mod 8 so byte-optimized reads/writes are only
// emitted when the reader/writer is known to be byte-aligned.
static constexpr int BITS_PER_BYTE = 8;

struct PyBitTracker {
    int bit_mod8 = 0; // -1 means unknown alignment

    bool is_byte_aligned() const { return bit_mod8 == 0; }

    void advance_bits(int bits) {
        if (bit_mod8 < 0) return; // already unknown
        bit_mod8 = (bit_mod8 + bits) % BITS_PER_BYTE;
    }

    void advance_bits_variable() {
        if (bit_mod8 != 0) bit_mod8 = -1;
        // else: stays 0 (byte-aligned -> still byte-aligned after whole-byte field)
    }

    void advance_field(const PyFieldInfo& fi) {
        if (fi.is_struct || fi.is_string || fi.is_bytes) {
            advance_bits_variable();
        } else {
            advance_bits(fi.bits);
        }
    }
};

// ============================================================================
// Outer-scope analysis for nested choices (Python)
// ============================================================================

struct PyOuterParam {
    std::string bmdl_name;
};

using PyOuterScopeMap = std::unordered_map<std::string, std::vector<PyOuterParam>>;
using PyOuterContext = std::unordered_map<std::string, std::string>;

// Map from BMDL inline type name → resolved Python class name (parent-prefixed or typeName-overridden)
using PyInlineNameMap = std::unordered_map<std::string, std::string>;

// Resolve the Python class name for an inline type, applying parent-prefix or typeName override.
std::string py_resolve_inline_name(const std::string& bmdl_name,
                                    const std::string& parent_name,
                                    const std::optional<std::string>& type_name_override) {
    if (type_name_override && !type_name_override->empty())
        return py_class(*type_name_override);
    std::string name = py_class(bmdl_name);
    if (!parent_name.empty())
        name = py_class(parent_name) + name;
    return name;
}

// Look up the resolved class name for an inline type, falling back to py_class(bmdl_name)
std::string py_inline_class(const std::string& bmdl_name, const PyInlineNameMap& name_map) {
    auto it = name_map.find(bmdl_name);
    if (it != name_map.end()) return it->second;
    return py_class(bmdl_name);
}

void py_collect_expr_refs(const model::Expr* expr, std::set<std::string>& refs) {
    if (!expr) return;
    if (expr->op == model::ExprOp::FieldRef) {
        auto dot = expr->name.find('.');
        refs.insert(dot != std::string::npos ? expr->name.substr(0, dot) : expr->name);
    }
    py_collect_expr_refs(expr->left.get(), refs);
    py_collect_expr_refs(expr->right.get(), refs);
}

void py_collect_scope_refs(const std::vector<model::StructChild>& children,
                            std::set<std::string>& refs) {
    for (const auto& child : children) {
        std::visit([&refs](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                py_collect_expr_refs(c.present_when.get(), refs);
                py_collect_expr_refs(c.length_from.get(), refs);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                py_collect_expr_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                py_collect_expr_refs(c.count_from.get(), refs);
                py_collect_expr_refs(c.length_from.get(), refs);
                py_collect_expr_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                py_collect_expr_refs(c.switch_expr.get(), refs);
                py_collect_expr_refs(c.present_when.get(), refs);
                py_collect_expr_refs(c.length_from.get(), refs);
                for (const auto& cs : c.cases) {
                    if (cs.type_ref.empty()) py_collect_scope_refs(cs.children, refs);
                }
                if (c.otherwise && c.otherwise->type_ref.empty()) {
                    py_collect_scope_refs(c.otherwise->children, refs);
                }
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                py_collect_scope_refs(c.children, refs);
            }
        }, child);
    }
}

void py_collect_local_names(const std::vector<model::StructChild>& children,
                             std::set<std::string>& names) {
    for (const auto& child : children) {
        std::visit([&names](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field> || std::is_same_v<T, model::StructDef> ||
                          std::is_same_v<T, model::ArrayDef> || std::is_same_v<T, model::ChoiceDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                py_collect_local_names(c.children, names);
            }
        }, child);
    }
}

void py_analyze_outer_scope(const std::string& child_name,
                             const std::vector<model::StructChild>& child_children,
                             const std::vector<model::StructChild>& parent_children,
                             const std::string& current_type_name,
                             PyOuterScopeMap& map) {
    std::set<std::string> refs;
    py_collect_scope_refs(child_children, refs);

    std::set<std::string> local;
    py_collect_local_names(child_children, local);
    for (const auto& n : local) refs.erase(n);

    if (refs.empty()) return;

    std::vector<PyOuterParam> params;
    for (const auto& ref : refs) {
        bool found = false;
        for (const auto& pc : parent_children) {
            if (auto* f = std::get_if<model::Field>(&pc)) {
                if (f->name == ref) {
                    params.push_back({ref});
                    found = true;
                    break;
                }
            }
        }
        if (!found && !current_type_name.empty()) {
            auto it = map.find(current_type_name);
            if (it != map.end()) {
                for (const auto& p : it->second) {
                    if (p.bmdl_name == ref) {
                        params.push_back(p);
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    if (!params.empty()) {
        map[child_name] = std::move(params);
    }
}

std::string py_expr_ctx(const model::Expr& e, const std::string& obj,
                         const PyOuterContext& outer_ctx) {
    if (outer_ctx.empty()) return py_expr(e, obj);
    if (e.op == model::ExprOp::FieldRef) {
        auto dot = e.name.find('.');
        std::string root = (dot != std::string::npos) ? e.name.substr(0, dot) : e.name;
        auto it = outer_ctx.find(root);
        if (it != outer_ctx.end()) {
            if (dot != std::string::npos) {
                std::string rest = e.name.substr(dot);
                std::string result = it->second;
                size_t pos = 1;
                while (pos < rest.size()) {
                    size_t next_dot = rest.find('.', pos);
                    std::string seg;
                    if (next_dot == std::string::npos) { seg = rest.substr(pos); pos = rest.size(); }
                    else { seg = rest.substr(pos, next_dot - pos); pos = next_dot + 1; }
                    result += "." + py_field(seg);
                }
                return result;
            }
            return it->second;
        }
    }
    // For compound expressions, recurse so inner FieldRefs are resolved
    if (e.left && e.right) {
        std::string l = py_expr_ctx(*e.left, obj, outer_ctx);
        std::string r = py_expr_ctx(*e.right, obj, outer_ctx);
        switch (e.op) {
            case model::ExprOp::Add:    return "(" + l + " + " + r + ")";
            case model::ExprOp::Sub:    return "(" + l + " - " + r + ")";
            case model::ExprOp::Mul:    return "(" + l + " * " + r + ")";
            case model::ExprOp::Div:    return "(" + l + " // " + r + ")";
            case model::ExprOp::Mod:    return "(" + l + " % " + r + ")";
            case model::ExprOp::Eq:     return "(" + l + " == " + r + ")";
            case model::ExprOp::Neq:    return "(" + l + " != " + r + ")";
            case model::ExprOp::Lt:     return "(" + l + " < " + r + ")";
            case model::ExprOp::Lte:    return "(" + l + " <= " + r + ")";
            case model::ExprOp::Gt:     return "(" + l + " > " + r + ")";
            case model::ExprOp::Gte:    return "(" + l + " >= " + r + ")";
            case model::ExprOp::LogAnd: return "(" + l + " and " + r + ")";
            case model::ExprOp::LogOr:  return "(" + l + " or " + r + ")";
            case model::ExprOp::BitAnd: return "(" + l + " & " + r + ")";
            case model::ExprOp::BitOr:  return "(" + l + " | " + r + ")";
            default: break;
        }
    }
    return py_expr(e, obj);
}

std::string py_build_outer_args(const std::string& type_name,
                                 const PyOuterScopeMap& scope_map,
                                 const std::string& pfx,
                                 const PyOuterContext& outer_ctx) {
    auto it = scope_map.find(type_name);
    if (it == scope_map.end()) return {};
    std::string args;
    for (const auto& p : it->second) {
        auto ctx_it = outer_ctx.find(p.bmdl_name);
        if (ctx_it != outer_ctx.end()) {
            args += ", " + ctx_it->second;
        } else {
            args += ", " + pfx + "." + py_field(p.bmdl_name);
        }
    }
    return args;
}

// ============================================================================
// bit_io.py — Python BitReader/BitWriter
// ============================================================================

std::string generate_bit_io() {
    return R"PY("""Bit-level I/O for binary protocols. Wire-compatible with conduit C++ BitReader/BitWriter."""
from __future__ import annotations
import struct
from typing import Optional


class ConduitError(Exception):
    """Base error for conduit codec operations."""
    pass


class DecodeError(ConduitError):
    """Raised when decoding fails."""
    pass


class EncodeError(ConduitError):
    """Raised when encoding fails."""
    pass


class ConstraintError(ConduitError):
    """Raised on constraint violation."""
    pass


# EBCDIC Code Page 037 <-> ASCII full 256-entry conversion tables
_EBCDIC_TO_ASCII = bytearray([
    0x00,0x01,0x02,0x03,0x1A,0x09,0x1A,0x7F, 0x1A,0x1A,0x1A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x1A,0x0A,0x08,0x1A, 0x18,0x19,0x1A,0x1A,0x1C,0x1D,0x1E,0x1F,
    0x1A,0x1A,0x1A,0x1A,0x1A,0x0A,0x17,0x1B, 0x1A,0x1A,0x1A,0x1A,0x1A,0x05,0x06,0x07,
    0x1A,0x1A,0x16,0x1A,0x1A,0x1A,0x1A,0x04, 0x1A,0x1A,0x1A,0x1A,0x14,0x15,0x1A,0x1A,
    0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5, 0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
    0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF, 0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0xAC,
    0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5, 0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
    0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF, 0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
    0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67, 0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
    0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70, 0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
    0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78, 0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
    0x5E,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC, 0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
    0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47, 0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
    0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50, 0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
    0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58, 0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, 0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x9F,
])
_ASCII_TO_EBCDIC = bytearray([
    0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F, 0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26, 0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
    0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D, 0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7, 0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
    0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7, 0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
    0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6, 0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0xB0,0x6D,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6, 0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
    0x20,0x21,0x22,0x23,0x24,0x15,0x06,0x17, 0x28,0x29,0x2A,0x2B,0x2C,0x09,0x0A,0x1B,
    0x30,0x31,0x1A,0x33,0x34,0x35,0x36,0x08, 0x38,0x39,0x3A,0x3B,0x04,0x14,0x3E,0xFF,
    0x41,0xAA,0x4A,0xB1,0x9F,0xB2,0x6A,0xB5, 0xBB,0xB4,0x9A,0x8A,0xB0,0xCA,0xAF,0xBC,
    0x90,0x8F,0xEA,0xFA,0xBE,0xA0,0xB6,0xB3, 0x9D,0xDA,0x9B,0x8B,0xB7,0xB8,0xB9,0xAB,
    0x64,0x65,0x62,0x66,0x63,0x67,0x9E,0x68, 0x74,0x71,0x72,0x73,0x78,0x75,0x76,0x77,
    0xAC,0x69,0xED,0xEE,0xEB,0xEF,0xEC,0xBF, 0x80,0xFD,0xFE,0xFB,0xFC,0xBA,0xAE,0x59,
    0x44,0x45,0x42,0x46,0x43,0x47,0x9C,0x48, 0x54,0x51,0x52,0x53,0x58,0x55,0x56,0x57,
    0x8C,0x49,0xCD,0xCE,0xCB,0xCF,0xCC,0xE1, 0x70,0xDD,0xDE,0xDB,0xDC,0x8D,0x8E,0xDF,
])


class BitReader:
    """Read individual bits and multi-byte values from a bytes buffer."""

    __slots__ = ('_data', '_bit_pos', '_bit_len')

    def __init__(self, data: bytes | bytearray | memoryview) -> None:
        self._data = bytes(data)
        self._bit_pos = 0
        self._bit_len = len(self._data) * 8

    def remaining_bits(self) -> int:
        return max(0, self._bit_len - self._bit_pos)

    def remaining_bytes(self) -> int:
        return self.remaining_bits() // 8

    def _check(self, n: int) -> None:
        if self._bit_pos + n > self._bit_len:
            raise DecodeError(f"underflow: need {n} bits, have {self.remaining_bits()}")

    def read_bits(self, n: int) -> int:
        self._check(n)
        if (self._bit_pos & 7) == 0 and (n & 7) == 0:
            start = self._bit_pos >> 3
            nbytes = n >> 3
            val = int.from_bytes(self._data[start:start + nbytes], 'big')
            self._bit_pos += n
            return val
        val = 0
        remaining = n
        while remaining > 0:
            byte_idx = self._bit_pos // 8
            bit_off = self._bit_pos % 8
            avail = 8 - bit_off
            take = min(avail, remaining)
            mask = ((1 << take) - 1) << (avail - take)
            bits = (self._data[byte_idx] & mask) >> (avail - take)
            val = (val << take) | bits
            self._bit_pos += take
            remaining -= take
        return val

    def read_signed_bits(self, n: int) -> int:
        val = self.read_bits(n)
        if n > 0 and (val >> (n - 1)) & 1:
            val -= (1 << n)
        return val

    def read_u8(self) -> int:
        return self.read_bits(8)

    def read_u16(self, big_endian: bool = True) -> int:
        b0 = self.read_bits(8)
        b1 = self.read_bits(8)
        return struct.unpack('>H' if big_endian else '<H', bytes([b0, b1]))[0]

    def read_u32(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(4)])
        return struct.unpack('>I' if big_endian else '<I', b)[0]

    def read_u64(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(8)])
        return struct.unpack('>Q' if big_endian else '<Q', b)[0]

    def read_s16(self, big_endian: bool = True) -> int:
        b0 = self.read_bits(8)
        b1 = self.read_bits(8)
        return struct.unpack('>h' if big_endian else '<h', bytes([b0, b1]))[0]

    def read_s32(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(4)])
        return struct.unpack('>i' if big_endian else '<i', b)[0]

    def read_s64(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(8)])
        return struct.unpack('>q' if big_endian else '<q', b)[0]

    def read_f32(self, big_endian: bool = True) -> float:
        b = bytes([self.read_bits(8) for _ in range(4)])
        return struct.unpack('>f' if big_endian else '<f', b)[0]

    def read_f64(self, big_endian: bool = True) -> float:
        b = bytes([self.read_bits(8) for _ in range(8)])
        return struct.unpack('>d' if big_endian else '<d', b)[0]

    def read_string(self, length: int, encoding: int = 0) -> str:
        b = bytearray(self.read_bits(8) for _ in range(length))
        if encoding == 2:
            b = bytearray(_EBCDIC_TO_ASCII[c] for c in b)
        elif encoding == 1:
            b = bytearray(c & 0x7F for c in b)
        return bytes(b).decode('latin-1')

    def read_packed_chars(self, count: int, char_bits: int) -> str:
        chars = []
        for _ in range(count):
            c = self.read_bits(char_bits)
            if char_bits < 7:
                chars.append(chr(0 if c == 0 else (c + 0x40 if c < 32 else c)))
            else:
                chars.append(chr(c))
        return ''.join(chars)

    def read_terminated_string(self, terminator: int, max_len: int) -> str:
        chars = []
        for _ in range(max_len):
            c = self.read_bits(8)
            if c == terminator:
                break
            chars.append(chr(c))
        return ''.join(chars)

    def read_crlf_terminated_string(self, max_len: int) -> str:
        chars = []
        prev = 0
        for _ in range(max_len):
            c = self.read_bits(8)
            if prev == 0x0D and c == 0x0A:
                chars.pop()
                break
            chars.append(chr(c))
            prev = c
        return ''.join(chars)

    def read_bytes(self, length: int) -> bytes:
        return bytes(self.read_bits(8) for _ in range(length))

    def read_bcd(self, bits: int) -> int:
        raw = self.read_bits(bits)
        result = 0
        mult = 1
        for i in range(bits // 4):
            digit = (raw >> (i * 4)) & 0xF
            result += digit * mult
            mult *= 10
        return result

    def read_bcd_signed(self, bits: int) -> int:
        sign_bit = self.read_bits(1)
        val = self.read_bcd(bits - 1)
        return -val if sign_bit else val

    def read_sign_magnitude(self, bits: int) -> int:
        sign_bit = self.read_bits(1)
        magnitude = self.read_bits(bits - 1)
        return -magnitude if sign_bit else magnitude

    def skip_bits(self, n: int) -> None:
        self._check(n)
        self._bit_pos += n

    def sub_reader(self, byte_count: int) -> 'BitReader':
        return BitReader(self.read_bytes(byte_count))

    def align_to(self, boundary: int) -> None:
        if boundary <= 0:
            return
        rem = self._bit_pos % 8
        if rem != 0:
            self._bit_pos += (8 - rem)
        byte_pos = self._bit_pos // 8
        b_rem = byte_pos % boundary
        if b_rem != 0:
            self._bit_pos += (boundary - b_rem) * 8
        if self._bit_pos > self._bit_len:
            self._bit_pos = self._bit_len


class BitWriter:
    """Write individual bits and multi-byte values to a growable buffer."""

    __slots__ = ('_buf', '_bit_pos')

    def __init__(self) -> None:
        self._buf = bytearray()
        self._bit_pos = 0

    def _ensure(self, n: int) -> None:
        needed = (self._bit_pos + n + 7) // 8
        while len(self._buf) < needed:
            self._buf.append(0)

    def write_bits(self, value: int, n: int) -> None:
        self._ensure(n)
        if (self._bit_pos & 7) == 0 and (n & 7) == 0:
            start = self._bit_pos >> 3
            nbytes = n >> 3
            if nbytes > 0:
                self._buf[start:start + nbytes] = value.to_bytes(nbytes, 'big')
            self._bit_pos += n
            return
        remaining = n
        while remaining > 0:
            byte_idx = self._bit_pos // 8
            bit_off = self._bit_pos % 8
            avail = 8 - bit_off
            take = min(avail, remaining)
            shift = remaining - take
            bits = (value >> shift) & ((1 << take) - 1)
            self._buf[byte_idx] |= bits << (avail - take)
            self._bit_pos += take
            remaining -= take

    def write_signed_bits(self, value: int, n: int) -> None:
        if value < 0:
            value = value + (1 << n)
        self.write_bits(value & ((1 << n) - 1), n)

    def write_u8(self, value: int) -> None:
        self.write_bits(value & 0xFF, 8)

    def write_u16(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>H' if big_endian else '<H', value & 0xFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_u32(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>I' if big_endian else '<I', value & 0xFFFFFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_u64(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>Q' if big_endian else '<Q', value & 0xFFFFFFFFFFFFFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_f32(self, value: float, big_endian: bool = True) -> None:
        for byte in struct.pack('>f' if big_endian else '<f', value):
            self.write_bits(byte, 8)

    def write_f64(self, value: float, big_endian: bool = True) -> None:
        for byte in struct.pack('>d' if big_endian else '<d', value):
            self.write_bits(byte, 8)

    def write_string(self, s: str, length: int, pad: int = 0, encoding: int = 0) -> None:
        b = bytearray(s.encode('latin-1'))
        if encoding == 2:
            b = bytearray(_ASCII_TO_EBCDIC[c] for c in b)
        elif encoding == 1:
            b = bytearray(c & 0x7F for c in b)
        for i in range(length):
            self.write_u8(b[i] if i < len(b) else pad)

    def write_packed_chars(self, s: str, count: int, char_bits: int, pad: int = 0) -> None:
        for i in range(count):
            c = ord(s[i]) if i < len(s) else pad
            if char_bits < 7 and ord('a') <= c <= ord('z'):
                c -= 32
            self.write_bits(c - 0x40 if c >= 0x40 else c, char_bits)

    def write_terminated_string(self, s: str, terminator: int) -> None:
        for c in s:
            self.write_u8(ord(c))
        self.write_u8(terminator)

    def write_crlf_terminated_string(self, s: str) -> None:
        for c in s:
            self.write_u8(ord(c))
        self.write_u8(0x0D)
        self.write_u8(0x0A)

    def write_bytes(self, data: bytes | bytearray) -> None:
        for b in data:
            self.write_u8(b)

    def write_bcd(self, value: int, bits: int) -> None:
        max_val = 10 ** (bits // 4) - 1
        if abs(value) > max_val:
            raise EncodeError(f"BCD overflow: {value} exceeds {bits // 4}-digit max ({max_val})")
        raw = 0
        v = abs(value)
        for i in range(bits // 4):
            raw |= (v % 10) << (i * 4)
            v //= 10
        self.write_bits(raw, bits)

    def write_bcd_signed(self, value: int, bits: int) -> None:
        self.write_bits(1 if value < 0 else 0, 1)
        self.write_bcd(abs(value), bits - 1)

    def write_sign_magnitude(self, value: int, bits: int) -> None:
        self.write_bits(1 if value < 0 else 0, 1)
        self.write_bits(abs(value), bits - 1)

    def size_bytes(self) -> int:
        return (self._bit_pos + 7) // 8

    def to_bytes(self) -> bytes:
        return bytes(self._buf[:self.size_bytes()])

    def align_to(self, boundary: int) -> None:
        rem = self.size_bytes() % boundary
        if rem != 0:
            for _ in range(boundary - rem):
                self.write_u8(0)

    def patch_u8(self, offset: int, value: int) -> None:
        if offset < len(self._buf):
            self._buf[offset] = value & 0xFF

    def patch_u16(self, offset: int, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>H' if big_endian else '<H', value & 0xFFFF)
        for i, byte in enumerate(b):
            if offset + i < len(self._buf):
                self._buf[offset + i] = byte

    def patch_u32(self, offset: int, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>I' if big_endian else '<I', value & 0xFFFFFFFF)
        for i, byte in enumerate(b):
            if offset + i < len(self._buf):
                self._buf[offset + i] = byte
)PY";
}

// ============================================================================
// constants.py
// ============================================================================

std::string generate_py_constants(const model::Protocol& protocol) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line();
    ctx.line();
    ctx.line("class Constants:");
    ctx.indent();
    if (protocol.constants.empty()) {
        ctx.line("pass");
    } else {
        for (const auto& c : protocol.constants) {
            ctx.line(py_snake(c.name) + " = " + c.value);
        }
    }
    ctx.dedent();
    ctx.line();
    return ctx.str();
}

// ============================================================================
// types.py
// ============================================================================

std::string generate_py_types(const model::Protocol& protocol,
                               const analyzer::TypeIndex& /*index*/) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from enum import IntEnum");
    ctx.line("from .bit_io import BitReader, BitWriter, ConstraintError, DecodeError");
    ctx.line();

    for (const auto& t : protocol.types) {
        bool is_enum = !t.enum_values.empty();
        bool is_flags = !t.flags.empty();
        bool has_scale = t.scale.has_value() || t.offset.has_value();
        bool is_string = (t.base == model::PrimitiveBase::String);
        bool is_signed = (t.base == model::PrimitiveBase::Int);

        if (is_enum) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + "(IntEnum):");
            ctx.indent();
            py_emit_docstring(ctx, t.doc);
            for (const auto& ev : t.enum_values)
                ctx.line(py_enum_val(ev.name) + " = " + std::to_string(ev.id));
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            ctx.line("raw = " + py_type_read_expr(t, is_signed));
            ctx.line("try:");
            ctx.indent();
            ctx.line("return " + name + "(raw)");
            ctx.dedent();
            ctx.line("except ValueError:");
            ctx.indent();
            ctx.line("raise DecodeError(f'unknown " + name + " value: {raw}')");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line(py_type_write_stmt("self.value", t, is_signed));
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        } else if (is_flags) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            py_emit_docstring(ctx, t.doc);
            ctx.line("__slots__ = ('_raw',)");
            ctx.line();
            ctx.line("def __init__(self, raw: int = 0) -> None:");
            ctx.indent();
            ctx.line("self._raw = raw");
            ctx.dedent();
            for (const auto& f : t.flags) {
                std::string acc = py_field(f.name);
                std::string bit_s = std::to_string(f.bit);
                ctx.line();
                ctx.line("@property");
                ctx.line("def " + acc + "(self) -> bool:");
                ctx.indent();
                ctx.line("return bool((self._raw >> " + bit_s + ") & 1)");
                ctx.dedent();
                ctx.line();
                ctx.line("@" + acc + ".setter");
                ctx.line("def " + acc + "(self, v: bool) -> None:");
                ctx.indent();
                ctx.line("if v: self._raw |= (1 << " + bit_s + ")");
                ctx.line("else: self._raw &= ~(1 << " + bit_s + ")");
                ctx.dedent();
            }
            ctx.line();
            ctx.line("@property");
            ctx.line("def raw(self) -> int: return self._raw");
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            ctx.line("return " + name + "(" + py_type_read_expr(t, is_signed) + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line(py_type_write_stmt("self._raw", t, is_signed));
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.line();
            ctx.line("def __hash__(self) -> int: return hash(self._raw)");
            ctx.line();
            ctx.line("def __repr__(self) -> str: return f'{type(self).__name__}(0x{self._raw:x})'");
            ctx.dedent();
            ctx.line();
        } else if (has_scale) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            py_emit_docstring(ctx, t.doc);
            ctx.line("__slots__ = ('_raw',)");
            if (t.scale) ctx.line("SCALE = " + py_double(*t.scale));
            if (t.offset) ctx.line("OFFSET = " + py_double(*t.offset));
            ctx.line();
            ctx.line("def __init__(self, raw: int = 0) -> None: self._raw = raw");
            ctx.line();
            ctx.line("@property");
            ctx.line("def value(self) -> float:");
            ctx.indent();
            std::string ve = "self._raw";
            if (t.scale) ve += " * self.SCALE";
            if (t.offset) ve += " + self.OFFSET";
            ctx.line("return " + ve);
            ctx.dedent();
            ctx.line();
            ctx.line("@value.setter");
            ctx.line("def value(self, v: float) -> None:");
            ctx.indent();
            std::string inv = "v";
            if (t.offset) inv = "(" + inv + " - self.OFFSET)";
            if (t.scale) inv = "(" + inv + " / self.SCALE)";
            ctx.line("self._raw = int(" + inv + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("@property");
            ctx.line("def raw(self) -> int: return self._raw");
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            ctx.line("raw = " + py_type_read_expr(t, is_signed));
            if (t.constraint && t.constraint->equals)
                ctx.line("if raw != " + *t.constraint->equals + ": raise ConstraintError('" + name + " constraint: expected " + *t.constraint->equals + "')");
            if (t.constraint && t.constraint->max)
                ctx.line("if raw > " + *t.constraint->max + ": raise ConstraintError('" + name + " exceeds max')");
            if (t.constraint && t.constraint->min && (*t.constraint->min != "0" || is_signed))
                ctx.line("if raw < " + *t.constraint->min + ": raise ConstraintError('" + name + " below min')");
            ctx.line("return " + name + "(raw)");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line(py_type_write_stmt("self._raw", t, is_signed));
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.line();
            ctx.line("def __hash__(self) -> int: return hash(self._raw)");
            ctx.line();
            ctx.line("def __repr__(self) -> str: return f'{type(self).__name__}({self.value})'");
            ctx.dedent();
            ctx.line();
        } else if (is_string && t.length) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            ctx.line("__slots__ = ('_value',)");
            {
                int wire_bits = t.char_bits ? (*t.length * *t.char_bits) : (*t.length * 8);
                int wire_bytes = (wire_bits + 7) / 8;
                ctx.line("WIRE_SIZE = " + std::to_string(wire_bytes));
            }
            ctx.line();
            ctx.line("def __init__(self, value: str = '') -> None: self._value = value");
            ctx.line();
            ctx.line("@property");
            ctx.line("def value(self) -> str: return self._value");
            ctx.line();
            ctx.line("@value.setter");
            ctx.line("def value(self, v: str) -> None: self._value = v");
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            if (t.char_bits) {
                // Packed character decode (e.g., ICAO 6-bit chars)
                ctx.line("s = r.read_packed_chars(" + std::to_string(*t.length) + ", " + std::to_string(*t.char_bits) + ")");
            } else {
                std::string enc_suffix;
                if (t.encoding == model::StringEncoding::Ia5) enc_suffix = ", 1";
                else if (t.encoding == model::StringEncoding::Ebcdic) enc_suffix = ", 2";
                ctx.line("s = r.read_string(" + std::to_string(*t.length) + enc_suffix + ")");
            }
            std::string ch = (t.padding == model::StringPadding::Space) ? "' '" : "'\\x00'";
            if (t.trim == model::StringTrim::Right || t.trim == model::StringTrim::Both)
                ctx.line("s = s.rstrip(" + ch + ")");
            if (t.trim == model::StringTrim::Left || t.trim == model::StringTrim::Both)
                ctx.line("s = s.lstrip(" + ch + ")");
            ctx.line("return " + name + "(s)");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            if (t.char_bits) {
                // Packed character encode
                int pad = (t.padding == model::StringPadding::Space) ? 0x20 : 0;
                ctx.line("w.write_packed_chars(self._value, " + std::to_string(*t.length) + ", " + std::to_string(*t.char_bits) + ", " + std::to_string(pad) + ")");
            } else {
                int pad = (t.padding == model::StringPadding::Space) ? 0x20 : 0;
                std::string enc_suffix;
                if (t.encoding == model::StringEncoding::Ia5) enc_suffix = ", encoding=1";
                else if (t.encoding == model::StringEncoding::Ebcdic) enc_suffix = ", encoding=2";
                ctx.line("w.write_string(self._value, " + std::to_string(*t.length) + ", " + std::to_string(pad) + enc_suffix + ")");
            }
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("if isinstance(o, str): return self._value == o");
            ctx.line("return isinstance(o, " + name + ") and self._value == o._value");
            ctx.dedent();
            ctx.line();
            ctx.line("def __str__(self) -> str: return self._value");
            ctx.line("def __repr__(self) -> str: return self._value");
            ctx.line("def __len__(self) -> int: return len(self._value)");
            ctx.line("def __getitem__(self, key): return self._value[key]");
            ctx.line("def __hash__(self) -> int: return hash(self._value)");
            ctx.dedent();
            ctx.line();
        } else if (t.constraint || (!is_enum && !is_flags && !has_scale && !is_string)) {
            // Constrained type alias OR plain integer wrapper (e.g. uint16 -> Uint16)
            // Both need decode()/encode() so they can be used as array element types.
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            ctx.line("__slots__ = ('_raw',)");
            ctx.line();
            ctx.line("def __init__(self, raw: int = 0) -> None: self._raw = raw");
            ctx.line();
            ctx.line("@property");
            ctx.line("def value(self) -> int: return self._raw");
            ctx.line("@property");
            ctx.line("def raw(self) -> int: return self._raw");
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            ctx.line("raw = " + py_type_read_expr(t, is_signed));
            if (t.constraint && t.constraint->equals)
                ctx.line("if raw != " + py_qualify_const(*t.constraint->equals) + ": raise ConstraintError('" + name + " constraint: expected " + *t.constraint->equals + "')");
            if (t.constraint && t.constraint->max)
                ctx.line("if raw > " + py_qualify_const(*t.constraint->max) + ": raise ConstraintError('" + name + " exceeds max')");
            if (t.constraint && t.constraint->min && (*t.constraint->min != "0" || is_signed))
                ctx.line("if raw < " + py_qualify_const(*t.constraint->min) + ": raise ConstraintError('" + name + " below min')");
            ctx.line("return " + name + "(raw)");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line(py_type_write_stmt("self._raw", t, is_signed));
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.line();
            ctx.line("def __hash__(self) -> int: return hash(self._raw)");
            ctx.line();
            ctx.line("def __repr__(self) -> str: return f'{type(self).__name__}({self._raw})'");
            ctx.dedent();
            ctx.line();
        }
    }
    return ctx.str();
}

// ============================================================================
// Field read/write helpers
// ============================================================================

std::string py_read_expr(const PyFieldInfo& fi, bool byte_aligned = true) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "r.read_bcd(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "r.read_bcd_signed(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "r.read_sign_magnitude(" + std::to_string(fi.bits) + ")";
    std::string be = (fi.endian == model::Endian::Big) ? "True" : "False";
    if (fi.is_float) return (fi.bits <= 32) ? "r.read_f32(" + be + ")" : "r.read_f64(" + be + ")";
    // Byte-optimized reads (read_u8, read_u16, etc.) auto-align to byte
    // boundaries, which corrupts data when the reader is mid-byte.
    // Only use them when we know the position is byte-aligned.
    if (byte_aligned) {
        if (fi.bits == 8 && !fi.is_signed) return "r.read_u8()";
        if (fi.bits == 16 && !fi.is_signed) return "r.read_u16(" + be + ")";
        if (fi.bits == 32 && !fi.is_signed) return "r.read_u32(" + be + ")";
        if (fi.bits == 64 && !fi.is_signed) return "r.read_u64(" + be + ")";
        // Signed byte-aligned reads: use struct-based methods (matches C++ emit_read_expr)
        if (fi.bits == 16 && fi.is_signed) return "r.read_s16(" + be + ")";
        if (fi.bits == 32 && fi.is_signed) return "r.read_s32(" + be + ")";
        if (fi.bits == 64 && fi.is_signed) return "r.read_s64(" + be + ")";
    }
    if (fi.is_signed) return "r.read_signed_bits(" + std::to_string(fi.bits) + ")";
    return "r.read_bits(" + std::to_string(fi.bits) + ")";
}

std::string py_write_stmt(const std::string& val, const PyFieldInfo& fi, bool byte_aligned = true) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "w.write_bcd(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "w.write_bcd_signed(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "w.write_sign_magnitude(" + val + ", " + std::to_string(fi.bits) + ")";
    std::string be = (fi.endian == model::Endian::Big) ? "True" : "False";
    if (fi.is_float) return (fi.bits <= 32) ? "w.write_f32(" + val + ", " + be + ")" : "w.write_f64(" + val + ", " + be + ")";
    // bool must be checked before bit-width checks for correct encoding
    if (fi.is_bool) return "w.write_bits(1 if " + val + " else 0, " + std::to_string(fi.bits) + ")";
    if (byte_aligned) {
        if (fi.bits == 8 && !fi.is_signed) return "w.write_u8(" + val + ")";
        if (fi.bits == 16 && !fi.is_signed) return "w.write_u16(" + val + ", " + be + ")";
        if (fi.bits == 32 && !fi.is_signed) return "w.write_u32(" + val + ", " + be + ")";
        if (fi.bits == 64 && !fi.is_signed) return "w.write_u64(" + val + ", " + be + ")";
    }
    if (fi.is_signed) return "w.write_signed_bits(" + val + ", " + std::to_string(fi.bits) + ")";
    return "w.write_bits(" + val + ", " + std::to_string(fi.bits) + ")";
}

// Helper: return Python encoding constant for a field (0=ASCII, 1=IA5, 2=EBCDIC)
std::string py_encoding_const(const model::Field& f) {
    if (f.encoding) {
        switch (*f.encoding) {
            case model::StringEncoding::Ia5: return "1";
            case model::StringEncoding::Ebcdic: return "2";
            default: break;
        }
    }
    return "";
}

bool py_field_needs_encoding(const model::Field& f) {
    return f.encoding && (*f.encoding == model::StringEncoding::Ia5 || *f.encoding == model::StringEncoding::Ebcdic);
}

// Helper: emit Python string trim code based on field trim mode
model::StringPadding py_resolve_effective_padding(const model::Field& f,
                                                   const analyzer::TypeIndex& index) {
    if (f.padding) return *f.padding;
    if (!f.type_ref.empty()) {
        auto it = index.types.find(f.type_ref);
        if (it != index.types.end()) return it->second->padding;
    }
    return model::StringPadding::Null;
}

void emit_py_field_trim(EmitContext& ctx, const std::string& m, const model::Field& f,
                        const analyzer::TypeIndex& index) {
    if (!f.trim) return;
    auto eff_trim = *f.trim;
    auto padding = py_resolve_effective_padding(f, index);
    std::string ch = (padding == model::StringPadding::Space) ? "' '" : "'\\x00'";
    switch (eff_trim) {
        case model::StringTrim::Right:
            ctx.line(m + " = " + m + ".rstrip(" + ch + ")");
            break;
        case model::StringTrim::Left:
            ctx.line(m + " = " + m + ".lstrip(" + ch + ")");
            break;
        case model::StringTrim::Both:
            ctx.line(m + " = " + m + ".strip(" + ch + ")");
            break;
        case model::StringTrim::None:
            break;
    }
}

// ============================================================================
// Decode/Encode children
// ============================================================================

// Forward declarations for mutual recursion with inline struct handling
void emit_py_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             PyBitTracker& tracker,
                             const PyOuterScopeMap& scope_map = {},
                             const PyOuterContext& outer_ctx = {},
                             const PyInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {});
void emit_py_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             PyBitTracker& tracker,
                             const std::string& len_ref_target = {},
                             const model::Field* auto_len_ref_field = nullptr,
                             const PyInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {});

void emit_py_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          PyBitTracker& tracker,
                          const PyOuterContext& outer_ctx = {},
                          const std::string& parent_class_name = {}) {
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string enum_name = parent_class_name + py_class(f.name);
        std::string m = pfx + "." + py_field(f.name);
        ctx.line(m + " = " + enum_name + ".decode(r)");
        if (f.bits) tracker.advance_bits(*f.bits);
        else tracker.advance_bits_variable();
        return;
    }
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    // Inline struct: decode children directly into parent (flattened)
    if (f.is_inline && !f.type_ref.empty()) {
        auto sit = index.structs.find(f.type_ref);
        if (sit != index.structs.end()) {
            emit_py_decode_children(ctx, sit->second->children, index, pfx, tracker);
            return;
        }
        auto mit = index.messages.find(f.type_ref);
        if (mit != index.messages.end()) {
            emit_py_decode_children(ctx, mit->second->children, index, pfx, tracker);
            return;
        }
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.py_type + ".decode(r)"); tracker.advance_field(fi); return; }
    if (fi.is_string) {
        bool has_enc = py_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", " + py_encoding_const(f)) : "";
        if (f.char_bits && f.length) {
            // Packed character decode (e.g., ICAO 6-bit chars)
            ctx.line(m + " = r.read_packed_chars(" + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ")");
            emit_py_field_trim(ctx, m, f, index);
        } else if (f.terminated) {
            // Terminated string decode
            int max_len = f.max_length ? *f.max_length : 65535;
            if (*f.terminated == "crlf") {
                ctx.line(m + " = r.read_crlf_terminated_string(" + std::to_string(max_len) + ")");
            } else {
                std::string term = "0";
                if (*f.terminated == "newline") {
                    term = "0x0A";
                } else if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line(m + " = r.read_terminated_string(" + term + ", " + std::to_string(max_len) + ")");
            }
            emit_py_field_trim(ctx, m, f, index);
        } else if (f.length) {
            ctx.line(m + " = r.read_string(" + std::to_string(*f.length) + enc_arg + ")");
            emit_py_field_trim(ctx, m, f, index);
        } else if (f.length_from) {
            ctx.line(m + " = r.read_string(int(" + py_expr_ctx(*f.length_from, pfx, outer_ctx) + ")" + enc_arg + ")");
            emit_py_field_trim(ctx, m, f, index);
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            std::string be = (pti.endian == model::Endian::Big) ? "True" : "False";
            if (pti.bits <= 8) ctx.line("_pl = r.read_u8()");
            else if (pti.bits <= 16) ctx.line("_pl = r.read_u16(" + be + ")");
            else ctx.line("_pl = r.read_u32(" + be + ")");
            if (f.length_includes_prefix)
                ctx.line("_pl -= " + std::to_string(get_prefix_bytes(pti)));
            ctx.line(m + " = r.read_string(_pl" + enc_arg + ")");
            emit_py_field_trim(ctx, m, f, index);
        } else {
            ctx.line(m + " = r.read_string(r.remaining_bytes()" + enc_arg + ")");
            emit_py_field_trim(ctx, m, f, index);
        }
        // max_length validation (matching C++ MaxLengthExceeded check)
        if (f.max_length) {
            ctx.line("if len(" + m + ") > " + std::to_string(*f.max_length) + ": raise ConstraintError('" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "')");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.read_bytes(" + std::to_string(len) + ")");
        else if (f.length_from) ctx.line(m + " = r.read_bytes(int(" + py_expr_ctx(*f.length_from, pfx, outer_ctx) + "))");
        else ctx.line(m + " = r.read_bytes(r.remaining_bytes())");
        if (f.max_length) {
            ctx.line("if len(" + m + ") > " + std::to_string(*f.max_length) + ": raise ConstraintError('" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "')");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.has_scale) {
        PyFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        std::string ve = "_raw";
        if (fi.scale != 1.0) ve += " * " + py_double(fi.scale);
        if (fi.offset != 0.0) ve += " + " + py_double(fi.offset);
        ctx.line("_raw = " + py_read_expr(raw_fi, tracker.is_byte_aligned()));
        ctx.line(m + " = " + ve);
        tracker.advance_bits(raw_fi.bits);
        return;
    }
    if (fi.is_bool) { ctx.line(m + " = (" + py_read_expr(fi, tracker.is_byte_aligned()) + " != 0)"); tracker.advance_field(fi); return; }
    ctx.line(m + " = " + py_read_expr(fi, tracker.is_byte_aligned()));
    tracker.advance_field(fi);
    // Field-level constraint checks (matching C++ emit_constraint_check)
    // Skip deferred constraints (validated externally, not at decode time)
    if (f.constraint && f.constraint->validate != model::ValidateTiming::Deferred) {
        if (f.constraint->equals) {
            ctx.line("if " + m + " != " + py_qualify_const(*f.constraint->equals) + ": raise ConstraintError('" + f.name + " constraint violation: expected " + *f.constraint->equals + "')");
        }
        if (f.constraint->max) {
            ctx.line("if " + m + " > " + py_qualify_const(*f.constraint->max) + ": raise ConstraintError('" + f.name + " exceeds max " + *f.constraint->max + "')");
        }
        bool is_signed = fi.is_signed;
        if (f.constraint->min && (*f.constraint->min != "0" || is_signed)) {
            ctx.line("if " + m + " < " + py_qualify_const(*f.constraint->min) + ": raise ConstraintError('" + f.name + " below min " + *f.constraint->min + "')");
        }
    }
}

void emit_py_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          PyBitTracker& tracker,
                          const std::string& parent_class_name = {}) {
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string m = pfx + "." + py_field(f.name);
        ctx.line(m + ".encode(w)");
        if (f.bits) tracker.advance_bits(*f.bits);
        else tracker.advance_bits_variable();
        return;
    }
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    // Inline struct: encode children directly from parent (flattened)
    if (f.is_inline && !f.type_ref.empty()) {
        auto sit = index.structs.find(f.type_ref);
        if (sit != index.structs.end()) {
            PyBitTracker inline_tracker;
            emit_py_encode_children(ctx, sit->second->children, index, pfx, inline_tracker);
            return;
        }
        auto mit = index.messages.find(f.type_ref);
        if (mit != index.messages.end()) {
            PyBitTracker inline_tracker;
            emit_py_encode_children(ctx, mit->second->children, index, pfx, inline_tracker);
            return;
        }
    }
    if (f.auto_expr) {
        if (f.auto_expr->kind == model::AutoKind::Length) {
            ctx.line("_len_pos = w.size_bytes()");
            ctx.line(py_write_stmt("0", fi, tracker.is_byte_aligned()));
            tracker.advance_field(fi);
            return;
        }
        if (f.auto_expr->kind == model::AutoKind::Count) {
            // Auto-count: write the length of the referenced array
            std::string ref_name = f.auto_expr->field_ref.empty() ? "" : py_field(f.auto_expr->field_ref);
            if (!ref_name.empty()) {
                ctx.line(py_write_stmt("(len(" + pfx + "." + ref_name + ") if " + pfx + "." + ref_name + " is not None else 0)", fi, tracker.is_byte_aligned()));
            } else {
                ctx.line(py_write_stmt("0", fi, tracker.is_byte_aligned()));
            }
            tracker.advance_field(fi);
            return;
        }
        if (f.auto_expr->kind == model::AutoKind::Id) {
            // Auto-id: write the ID_VALUE constant
            ctx.line(py_write_stmt(pfx + ".ID_VALUE", fi, tracker.is_byte_aligned()));
            tracker.advance_field(fi);
            return;
        }
    }
    // Encode-time constraint checks (matching C++ emit_encode_constraint_check)
    // Skip deferred constraints (validated externally, not at encode time)
    if (f.constraint && !fi.is_struct && !fi.is_enum && !fi.is_bytes
        && f.constraint->validate != model::ValidateTiming::Deferred) {
        if (f.constraint->equals) {
            ctx.line("if " + m + " != " + py_qualify_const(*f.constraint->equals) + ": raise ConstraintError('" + f.name + " constraint: expected " + *f.constraint->equals + "')");
        }
        if (f.constraint->max) {
            ctx.line("if " + m + " > " + py_qualify_const(*f.constraint->max) + ": raise ConstraintError('" + f.name + " exceeds max " + *f.constraint->max + "')");
        }
        if (f.constraint->min && (*f.constraint->min != "0" || fi.is_signed)) {
            ctx.line("if " + m + " < " + py_qualify_const(*f.constraint->min) + ": raise ConstraintError('" + f.name + " below min " + *f.constraint->min + "')");
        }
    }
    if (fi.is_enum) {
        // Handle None default for non-optional enums: write zero bits
        if (fi.bits > 0) {
            ctx.line(m + ".encode(w) if " + m + " is not None else w.write_bits(0, " + std::to_string(fi.bits) + ")");
        } else {
            ctx.line(m + ".encode(w)");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_struct) {
        if (fi.is_string_struct) {
            // Handle both raw string and wrapper type assignment
            std::string type = py_class(f.type_ref);
            ctx.line("(" + type + "(" + m + ") if isinstance(" + m + ", str) else " + m + ").encode(w)");
        } else {
            ctx.line(m + ".encode(w)");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_string) {
        bool has_enc = py_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", encoding=" + py_encoding_const(f)) : "";
        if (f.char_bits && f.length) {
            // Packed character encode
            int pad = (f.padding == model::StringPadding::Space) ? 0x20 : 0;
            ctx.line("w.write_packed_chars(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ", " + std::to_string(pad) + ")");
        } else if (f.terminated) {
            // Terminated string encode
            if (*f.terminated == "crlf") {
                ctx.line("w.write_crlf_terminated_string(" + m + ")");
            } else {
                std::string term = "0";
                if (*f.terminated == "newline") {
                    term = "0x0A";
                } else if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line("w.write_terminated_string(" + m + ", " + term + ")");
            }
        } else if (f.length) {
            // Use EBCDIC space (0x40) for EBCDIC-encoded space-padded strings
            int pad = 0;
            if (f.padding && *f.padding == model::StringPadding::Space) {
                pad = (f.encoding && *f.encoding == model::StringEncoding::Ebcdic) ? 0x40 : 0x20;
            }
            ctx.line("w.write_string(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(pad) + enc_arg + ")");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            std::string be = (pti.endian == model::Endian::Big) ? "True" : "False";
            std::string le = "len(" + m + ")";
            if (f.length_includes_prefix) le += " + " + std::to_string(get_prefix_bytes(pti));
            if (pti.bits <= 8) ctx.line("w.write_u8(" + le + ")");
            else if (pti.bits <= 16) ctx.line("w.write_u16(" + le + ", " + be + ")");
            else ctx.line("w.write_u32(" + le + ", " + be + ")");
            ctx.line("w.write_string(" + m + ", len(" + m + ")" + enc_arg + ")");
        } else {
            ctx.line("w.write_string(" + m + ", len(" + m + ")" + enc_arg + ")");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_bytes) { ctx.line("w.write_bytes(" + m + ")"); tracker.advance_field(fi); return; }
    if (fi.has_scale) {
        std::string inv = m;
        if (fi.offset != 0.0) inv = "(" + inv + " - " + py_double(fi.offset) + ")";
        if (fi.scale != 1.0) inv = "(" + inv + " / " + py_double(fi.scale) + ")";
        PyFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        ctx.line(py_write_stmt("int(" + inv + ")", raw_fi, tracker.is_byte_aligned()));
        tracker.advance_bits(raw_fi.bits);
        return;
    }
    ctx.line(py_write_stmt(m, fi, tracker.is_byte_aligned()));
    tracker.advance_field(fi);
}

void emit_py_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             PyBitTracker& tracker,
                             const PyOuterScopeMap& scope_map,
                             const PyOuterContext& outer_ctx,
                             const PyInlineNameMap& name_map,
                             const std::string& parent_class_name) {
    // Pre-scan: find struct-level auto-length field (auto="length" with no field_ref).
    // When present, we create a bounded sub_reader after reading the length field
    // so that subsequent children cannot over-read past the struct boundary.
    int auto_length_idx = -1;
    model::ArithModifier auto_length_mod;
    std::string auto_length_field_name;
    for (size_t i = 0; i < children.size(); ++i) {
        if (auto* f = std::get_if<model::Field>(&children[i])) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length
                && f->auto_expr->field_ref.empty()) {
                auto_length_idx = static_cast<int>(i);
                auto_length_mod = f->auto_expr->modifier;
                auto_length_field_name = f->name;
                break;
            }
        }
    }

    // Emit start position marker if auto-length is present
    if (auto_length_idx >= 0 && auto_length_idx + 1 < static_cast<int>(children.size())) {
        ctx.line("_auto_len_start = r.remaining_bytes()");
    }

    for (size_t idx = 0; idx < children.size(); ++idx) {
        const auto& child = children[idx];
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto emit_field_decode_wrapped = [&]() {
                ctx.line("try:");
                ctx.indent();
                emit_py_field_decode(ctx, *f, index, pfx, tracker, outer_ctx, parent_class_name);
                ctx.dedent();
                ctx.line("except Exception as _e: raise type(_e)(\"field '" + f->name + "': \" + str(_e)) from _e");
            };
            if (f->present_when) {
                ctx.line("if " + py_expr_ctx(*f->present_when, pfx, outer_ctx) + ":");
                ctx.indent();
                emit_field_decode_wrapped();
                ctx.dedent();
                // present_when field makes alignment unknown at compile time
                tracker.advance_bits_variable();
            } else emit_field_decode_wrapped();
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + py_field(sd->name);
            std::string resolved = py_inline_class(sd->name, name_map);
            std::string args = py_build_outer_args(sd->name, scope_map, pfx, outer_ctx);
            if (sd->present_when) {
                ctx.line("if " + py_expr_ctx(*sd->present_when, pfx, outer_ctx) + ":");
                ctx.indent(); ctx.line(m + " = " + resolved + ".decode(r" + args + ")"); ctx.dedent();
                tracker.advance_bits_variable();
            } else {
                ctx.line(m + " = " + resolved + ".decode(r" + args + ")");
                tracker.advance_bits_variable();
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            std::string elem = ad->type_ref.empty() ? py_inline_class(ad->name, name_map) : py_class(ad->type_ref);
            auto emit_array_decode = [&]() {
                if (ad->fixed_count) {
                    ctx.line(m + " = [" + elem + ".decode(r) for _ in range(" + std::to_string(*ad->fixed_count) + ")]");
                } else if (ad->count_from) {
                    ctx.line(m + " = [" + elem + ".decode(r) for _ in range(int(" + py_expr_ctx(*ad->count_from, pfx, outer_ctx) + "))]");
                } else if (ad->length_from) {
                    // Bounded array: create sub-reader limited to length_from bytes
                    ctx.line("_ar = r.sub_reader(int(" + py_expr_ctx(*ad->length_from, pfx, outer_ctx) + "))");
                    ctx.line(m + " = []");
                    ctx.line("while _ar.remaining_bytes() > 0:");
                    ctx.indent(); ctx.line(m + ".append(" + elem + ".decode(_ar))"); ctx.dedent();
                } else {
                    ctx.line(m + " = []");
                    ctx.line("while r.remaining_bytes() > 0:");
                    ctx.indent(); ctx.line(m + ".append(" + elem + ".decode(r))"); ctx.dedent();
                }
            };
            if (ad->present_when) {
                ctx.line("if " + py_expr_ctx(*ad->present_when, pfx, outer_ctx) + ":");
                ctx.indent(); emit_array_decode(); ctx.dedent();
            } else {
                emit_array_decode();
            }
            tracker.advance_bits_variable();
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = py_expr_ctx(*cd->switch_expr, pfx, outer_ctx);
            std::string m = pfx + "." + py_field(cd->name);

            // Create bounded sub-reader if choice has length/length_from
            bool bounded = cd->length_from != nullptr || cd->length.has_value();
            std::string reader_var = "r";
            if (bounded) {
                if (cd->length_from) {
                    ctx.line("_cr = r.sub_reader(int(" + py_expr_ctx(*cd->length_from, pfx, outer_ctx) + "))");
                } else if (cd->length) {
                    ctx.line("_cr = r.sub_reader(" + std::to_string(*cd->length) + ")");
                }
                reader_var = "_cr";
            }

            auto emit_choice_decode = [&]() {
                // Collect receive/both values and ranges for send-only dedup
                std::set<std::string> recv_values;
                std::set<std::string> recv_ranges;
                for (const auto& cs : cd->cases) {
                    if (cs.direction == model::Direction::Send) continue;
                    if (cs.value) recv_values.insert(*cs.value);
                    if (cs.range) recv_ranges.insert(*cs.range);
                }

                bool first = true;
                for (const auto& cs : cd->cases) {
                    // Skip send-only cases when a receive/both case exists for same value/range
                    if (cs.direction == model::Direction::Send && cs.value) {
                        if (recv_values.count(*cs.value)) continue;
                    }
                    if (cs.direction == model::Direction::Send && cs.range) {
                        if (recv_ranges.count(*cs.range)) continue;
                    }

                    std::string cond;
                    if (cs.value) {
                        std::string val = *cs.value;
                        if (index.constants.count(*cs.value)) {
                            val = "Constants." + py_snake(*cs.value);
                        }
                        cond = sv + " == " + val;
                    } else if (cs.range) {
                        auto dot_pos = cs.range->find("..");
                        if (dot_pos != std::string::npos) {
                            std::string min_s = cs.range->substr(0, dot_pos);
                            std::string max_s = cs.range->substr(dot_pos + 2);
                            if (min_s == "0") {
                                cond = sv + " <= " + max_s;
                            } else {
                                cond = min_s + " <= " + sv + " <= " + max_s;
                            }
                        } else {
                            cond = sv + " == " + *cs.range;
                        }
                    } else {
                        continue;
                    }
                    ctx.line(std::string(first ? "if " : "elif ") + cond + ":");
                    ctx.indent();
                    std::string et = cs.type_ref.empty() ? py_inline_class(cs.name, name_map) : py_class(cs.type_ref);
                    std::string case_name = cs.type_ref.empty() ? cs.name : cs.type_ref;
                    std::string args = py_build_outer_args(case_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(" + reader_var + args + ")");
                    ctx.dedent();
                    first = false;
                }
                if (cd->otherwise) {
                    ctx.line("else:");
                    ctx.indent();
                    std::string et = cd->otherwise->type_ref.empty() ? py_inline_class(cd->otherwise->name, name_map) : py_class(cd->otherwise->type_ref);
                    std::string ow_name = cd->otherwise->type_ref.empty() ? cd->otherwise->name : cd->otherwise->type_ref;
                    std::string args = py_build_outer_args(ow_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(" + reader_var + args + ")");
                    ctx.dedent();
                } else if (!first) {
                    ctx.line("else:");
                    ctx.indent();
                    ctx.line("raise DecodeError(\"choice '" + cd->name + "': no case matched switch value\")");
                    ctx.dedent();
                }
            };
            if (cd->present_when) {
                ctx.line("if " + py_expr_ctx(*cd->present_when, pfx, outer_ctx) + ":");
                ctx.indent(); emit_choice_decode(); ctx.dedent();
            } else {
                emit_choice_decode();
            }
            tracker.advance_bits_variable();
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skip_bits(" + std::to_string(res->bits) + ")");
            tracker.advance_bits(res->bits);
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("r.align_to(" + std::to_string(al->to) + ")");
            tracker.bit_mod8 = 0; // alignment resets to byte-aligned
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // FX extension: read continuation bit, conditionally decode children
            ctx.line("if r.read_bits(1) != 0:");
            tracker.advance_bits(1); // FX bit
            ctx.indent();
            emit_py_decode_children(ctx, fx->children, index, pfx, tracker, scope_map, outer_ctx, name_map, parent_class_name);
            // Read terminal FX=0 bit if this FX extent has no nested FxBlock
            {
                bool has_nested_fx = false;
                for (const auto& fc : fx->children) {
                    if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
                }
                if (!has_nested_fx) {
                    ctx.line("r.skip_bits(1)  # Terminal FX=0");
                }
            }
            ctx.dedent();
        }

        // After auto-length field, create bounded sub_reader for remaining children
        if (auto_length_idx >= 0 && static_cast<int>(idx) == auto_length_idx
            && idx + 1 < children.size()) {
            std::string len_member = pfx + "." + py_field(auto_length_field_name);
            // Compute remaining: total struct length minus bytes already consumed
            // auto="length" measures from struct start, so track consumed bytes at runtime
            std::string raw_len = len_member;
            if (auto_length_mod.has_modifier()) {
                std::string inv_op;
                switch (auto_length_mod.op) {
                    case model::ArithOp::Mul: inv_op = " // "; break;
                    case model::ArithOp::Div: inv_op = " * "; break;
                    case model::ArithOp::Add: inv_op = " - "; break;
                    case model::ArithOp::Sub: inv_op = " + "; break;
                    default: break;
                }
                if (!inv_op.empty()) {
                    raw_len = "(" + len_member + inv_op
                              + std::to_string(auto_length_mod.literal) + ")";
                }
            }
            std::string remaining = "int(" + raw_len + " - (_auto_len_start - r.remaining_bytes()))";
            ctx.line("r = r.sub_reader(" + remaining + ")");
        }
    }
}

// Helper: emit code to check if any FX child fields have non-None values
void py_fx_has_fields_check(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const std::string& pfx, const std::string& flag_var) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            ctx.line("if " + pfx + "." + py_field(f->name) + " is not None: " + flag_var + " = True");
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            if (!sd->name.empty()) {
                ctx.line("if " + pfx + "." + py_field(sd->name) + " is not None: " + flag_var + " = True");
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            ctx.line("if " + pfx + "." + py_field(ad->name) + " is not None: " + flag_var + " = True");
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            ctx.line("if " + pfx + "." + py_field(cd->name) + " is not None: " + flag_var + " = True");
        } else if (auto* nested_fx = std::get_if<model::FxBlock>(&child)) {
            py_fx_has_fields_check(ctx, nested_fx->children, pfx, flag_var);
        }
    }
}

// Encode children within an FX block: optional fields must write zero-fill when absent
void emit_py_encode_fx_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                                const analyzer::TypeIndex& index, const std::string& pfx,
                                const PyInlineNameMap& name_map,
                                const std::string& parent_class_name) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = py_resolve_field(*f, index);
            std::string m = pfx + "." + py_field(f->name);
            if (fi.is_enum || (!f->enum_values.empty() && f->type_ref.empty())) {
                // Enum: encode if present, else write zero bits
                ctx.line("if " + m + " is not None:");
                ctx.indent(); ctx.line(m + ".encode(w)"); ctx.dedent();
                ctx.line("else:");
                ctx.indent(); ctx.line(py_write_stmt("0", fi)); ctx.dedent();
            } else if (fi.is_struct && !fi.is_string && !fi.is_bytes) {
                // Struct: encode if present, else default-construct and encode
                std::string stype = f->type_ref.empty() ? py_inline_class(f->name, name_map) : py_class(f->type_ref);
                if (fi.is_string_struct) {
                    ctx.line("if " + m + " is not None:");
                    ctx.indent(); ctx.line("(" + stype + "(" + m + ") if isinstance(" + m + ", str) else " + m + ").encode(w)"); ctx.dedent();
                    ctx.line("else:");
                    ctx.indent(); ctx.line(stype + "().encode(w)"); ctx.dedent();
                } else {
                    ctx.line("if " + m + " is not None:");
                    ctx.indent(); ctx.line(m + ".encode(w)"); ctx.dedent();
                    ctx.line("else:");
                    ctx.indent(); ctx.line(stype + "().encode(w)"); ctx.dedent();
                }
            } else if (fi.is_string) {
                // String: write empty string with proper padding when absent
                if (f->char_bits && f->length) {
                    // Packed character encode (e.g., ICAO 6-bit chars)
                    int pad = (f->padding && *f->padding == model::StringPadding::Space) ? 0x20 : 0;
                    ctx.line("w.write_packed_chars(" + m + " if " + m + " is not None else '', " +
                             std::to_string(*f->length) + ", " + std::to_string(*f->char_bits) + ", " + std::to_string(pad) + ")");
                } else if (f->length) {
                    int pad = (f->padding && *f->padding == model::StringPadding::Space) ? 0x20 : 0;
                    bool has_enc = py_field_needs_encoding(*f);
                    std::string enc_suffix = has_enc ? (", encoding=" + py_encoding_const(*f)) : "";
                    ctx.line("w.write_string(" + m + " if " + m + " is not None else '', " +
                             std::to_string(*f->length) + ", " + std::to_string(pad) + enc_suffix + ")");
                } else {
                    ctx.line("if " + m + " is not None:");
                    ctx.indent(); ctx.line("w.write_string(" + m + ", len(" + m + "))"); ctx.dedent();
                }
            } else if (fi.is_bytes) {
                if (f->length) {
                    ctx.line("if " + m + " is not None:");
                    ctx.indent(); ctx.line("w.write_bytes(" + m + ")"); ctx.dedent();
                    ctx.line("else:");
                    ctx.indent(); ctx.line("w.write_bits(0, " + std::to_string(*f->length * 8) + ")"); ctx.dedent();
                } else {
                    ctx.line("if " + m + " is not None:");
                    ctx.indent(); ctx.line("w.write_bytes(" + m + ")"); ctx.dedent();
                }
            } else if (fi.has_scale) {
                // Scaled: use 0.0 when absent (parenthesize the ternary to avoid
                // operator precedence issues with offset subtraction)
                std::string val = "(" + m + " if " + m + " is not None else 0.0)";
                std::string inv = val;
                if (fi.offset != 0.0) inv = "(" + inv + " - " + py_double(fi.offset) + ")";
                if (fi.scale != 1.0) inv = "(" + inv + " / " + py_double(fi.scale) + ")";
                PyFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
                raw_fi.is_float = false; raw_fi.has_scale = false;
                ctx.line(py_write_stmt("int(" + inv + ")", raw_fi));
            } else {
                // Primitive: value_or(0)
                ctx.line(py_write_stmt("(" + m + " if " + m + " is not None else 0)", fi));
            }
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            // Nested struct in FX: encode if present, else default-construct
            std::string m = pfx + "." + py_field(sd->name);
            std::string stype = py_inline_class(sd->name, name_map);
            ctx.line("if " + m + " is not None:");
            ctx.indent(); ctx.line(m + ".encode(w)"); ctx.dedent();
            ctx.line("else:");
            ctx.indent(); ctx.line(stype + "().encode(w)"); ctx.dedent();
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            ctx.line("if " + m + " is not None:");
            ctx.indent(); ctx.line("for _item in " + m + ": _item.encode(w)"); ctx.dedent();
            // For fixed-count arrays in FX blocks, write zero-fill when absent
            if (ad->fixed_count) {
                auto elem_fi = py_resolve_element_type(*ad, index);
                if (elem_fi.bits > 0) {
                    int total_bits = *ad->fixed_count * elem_fi.bits;
                    ctx.line("else:");
                    ctx.indent(); ctx.line("w.write_bits(0, " + std::to_string(total_bits) + ")"); ctx.dedent();
                }
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + py_field(cd->name);
            ctx.line("if " + m + " is not None: " + m + ".encode(w)");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.write_bits(0, " + std::to_string(res->bits) + ")");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.align_to(" + std::to_string(al->to) + ")");
        } else if (auto* nested_fx = std::get_if<model::FxBlock>(&child)) {
            // Nested FX block
            ctx.line("_fx_continue = False");
            py_fx_has_fields_check(ctx, nested_fx->children, pfx, "_fx_continue");
            ctx.line("w.write_bits(1 if _fx_continue else 0, 1)");
            ctx.line("if _fx_continue:");
            ctx.indent();
            emit_py_encode_fx_children(ctx, nested_fx->children, index, pfx, name_map, parent_class_name);
            bool has_nested_fx = false;
            for (const auto& fc : nested_fx->children) {
                if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
            }
            if (!has_nested_fx) {
                ctx.line("w.write_bits(0, 1)  # Terminal FX=0");
            }
            ctx.dedent();
        }
    }
}

void emit_py_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             PyBitTracker& tracker,
                             const std::string& len_ref_target,
                             const model::Field* auto_len_ref_field,
                             const PyInlineNameMap& name_map,
                             const std::string& parent_class_name) {
    // Helper lambda: get BMDL name from a StructChild
    auto get_child_name = [](const model::StructChild& c) -> std::string {
        if (auto* f = std::get_if<model::Field>(&c)) return f->name;
        if (auto* sd = std::get_if<model::StructDef>(&c)) return sd->name;
        if (auto* ad = std::get_if<model::ArrayDef>(&c)) return ad->name;
        if (auto* cd = std::get_if<model::ChoiceDef>(&c)) return cd->name;
        return {};
    };

    // Helper lambda: emit the auto-length(field) backpatch after the target field
    auto emit_length_ref_patch = [&]() {
        if (!auto_len_ref_field || !auto_len_ref_field->auto_expr) return;
        auto al_fi = py_resolve_field(*auto_len_ref_field, index);
        bool be = (al_fi.endian == model::Endian::Big);
        std::string target_py = py_field(auto_len_ref_field->auto_expr->field_ref);
        std::string size_expr = "w.size_bytes() - _" + target_py + "_start";
        if (auto_len_ref_field->auto_expr->modifier.has_modifier()) {
            auto& mod = auto_len_ref_field->auto_expr->modifier;
            switch (mod.op) {
                case model::ArithOp::Add:
                    size_expr = "(" + size_expr + " + " + std::to_string(mod.literal) + ")";
                    break;
                case model::ArithOp::Sub:
                    size_expr = "(" + size_expr + " - " + std::to_string(mod.literal) + ")";
                    break;
                case model::ArithOp::Mul:
                    size_expr = "(" + size_expr + " * " + std::to_string(mod.literal) + ")";
                    break;
                case model::ArithOp::Div:
                    size_expr = "(" + size_expr + " // " + std::to_string(mod.literal) + ")";
                    break;
                default: break;
            }
        }
        if (al_fi.bits <= 8) ctx.line("w.patch_u8(_len_pos, " + size_expr + ")");
        else if (al_fi.bits <= 16) ctx.line("w.patch_u16(_len_pos, " + size_expr + ", " +
            std::string(be ? "True" : "False") + ")");
        else ctx.line("w.patch_u32(_len_pos, " + size_expr + ", " +
            std::string(be ? "True" : "False") + ")");
    };

    bool is_target_active = false;
    for (const auto& child : children) {
        std::string child_name = get_child_name(child);

        // auto-length(field) start marker: record position before the target child
        if (!len_ref_target.empty() && child_name == len_ref_target) {
            ctx.line("_" + py_field(len_ref_target) + "_start = w.size_bytes()");
            is_target_active = true;
        } else if (is_target_active) {
            // The previous child was the target field; emit the backpatch now
            emit_length_ref_patch();
            is_target_active = false;
        }

        if (auto* f = std::get_if<model::Field>(&child)) {
            auto emit_field_encode_wrapped = [&]() {
                ctx.line("try:");
                ctx.indent();
                emit_py_field_encode(ctx, *f, index, pfx, tracker, parent_class_name);
                ctx.dedent();
                ctx.line("except Exception as _e: raise type(_e)(\"field '" + f->name + "': \" + str(_e)) from _e");
            };
            if (f->present_when) {
                ctx.line("if " + py_expr(*f->present_when, pfx) + ":");
                ctx.indent(); emit_field_encode_wrapped(); ctx.dedent();
                tracker.advance_bits_variable();
            } else emit_field_encode_wrapped();
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + py_field(sd->name);
            if (sd->present_when) {
                ctx.line("if " + m + " is not None: " + m + ".encode(w)");
            } else ctx.line(m + ".encode(w)");
            tracker.advance_bits_variable();
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            if (ad->present_when) {
                ctx.line("if " + m + " is not None:");
                ctx.indent(); ctx.line("for _item in " + m + ": _item.encode(w)"); ctx.dedent();
            } else {
                ctx.line("for _item in " + m + ": _item.encode(w)");
            }
            tracker.advance_bits_variable();
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + py_field(cd->name);
            ctx.line("if " + m + " is not None: " + m + ".encode(w)");
            tracker.advance_bits_variable();
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.write_bits(0, " + std::to_string(res->bits) + ")");
            tracker.advance_bits(res->bits);
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.align_to(" + std::to_string(al->to) + ")");
            tracker.bit_mod8 = 0;
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // FX extension: check if any children are set, write FX bit, conditionally encode
            ctx.line("_fx_continue = False");
            py_fx_has_fields_check(ctx, fx->children, pfx, "_fx_continue");
            ctx.line("w.write_bits(1 if _fx_continue else 0, 1)");
            ctx.line("if _fx_continue:");
            ctx.indent();
            // Use FX-aware encoder that writes zero-fill for absent optional fields
            emit_py_encode_fx_children(ctx, fx->children, index, pfx, name_map, parent_class_name);
            // Write terminal FX=0 bit if this FX extent has no nested FxBlock
            {
                bool has_nested_fx = false;
                for (const auto& fc : fx->children) {
                    if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
                }
                if (!has_nested_fx) {
                    ctx.line("w.write_bits(0, 1)  # Terminal FX=0");
                }
            }
            ctx.dedent();
        }
    }
    // If the target field was the last child, emit the backpatch now
    if (is_target_active) {
        emit_length_ref_patch();
    }
}

// ============================================================================
// Field collection for __init__
// ============================================================================

struct PyFieldDef {
    std::string name;
    std::string py_type;
    std::string default_val;
    model::DisplayFormat format = model::DisplayFormat::Decimal;
    bool is_numeric = false;  // true for simple int fields (not struct/enum/string/bytes)
    // Constraint & accessor info (matching C++ emit_plain_accessors / emit_setter_constraint_checks)
    const model::Constraint* constraint = nullptr;
    bool is_signed = false;
    bool is_optional = false;        // FX / bitmap / present-when (nullable)
    bool is_enum = false;
    bool is_struct = false;
    bool is_string = false;
    bool is_string_struct = false;   // struct wrapping a string type
    bool is_bytes = false;
    bool is_bool = false;
    std::optional<int> max_length;
    // Scaled field raw accessors
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    // Source BMDL name for accessor naming
    std::string bmdl_name;
    // Documentation string from <doc> tag
    std::string doc;
};

void collect_py_fields(const std::vector<model::StructChild>& children,
                       const analyzer::TypeIndex& index,
                       std::vector<PyFieldDef>& fields,
                       const PyInlineNameMap& name_map = {},
                       const std::string& parent_class_name = {},
                       bool in_fx = false) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            // Inline enum field: enum_values populated, type_ref empty
            if (!f->enum_values.empty() && f->type_ref.empty() && !parent_class_name.empty()) {
                std::string enum_name = parent_class_name + py_class(f->name);
                PyFieldDef ef;
                ef.name = py_field(f->name);
                ef.py_type = enum_name;
                ef.default_val = "None";
                ef.bmdl_name = f->name;
                ef.is_enum = true;
                ef.doc = f->doc;
                fields.push_back(ef);
                continue;
            }
            auto fi = py_resolve_field(*f, index);
            PyFieldDef pf;
            pf.name = py_field(f->name);
            pf.py_type = fi.py_type;
            pf.format = f->format;
            pf.is_numeric = !fi.is_struct && !fi.is_enum && !fi.is_string && !fi.is_bytes && !fi.is_bool &&
                            !fi.is_float && !fi.has_scale && (fi.bits > 0);
            bool optional = in_fx || (f->present_when != nullptr || f->bit.has_value());
            if (optional) pf.default_val = "None";
            else if (fi.is_enum) {
                // Non-optional enum: default to first enum value (not None)
                pf.default_val = "None";  // fallback
                if (!f->type_ref.empty()) {
                    auto resolved = index.find(f->type_ref);
                    if (resolved) {
                        std::visit([&pf, &f](const auto* def) {
                            using T = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<T, model::TypeDef>) {
                                if (!def->enum_values.empty()) {
                                    pf.default_val = py_class(f->type_ref) + "." + py_enum_val(def->enum_values[0].name);
                                }
                            }
                        }, *resolved);
                    }
                }
            }
            else if (fi.is_struct) pf.default_val = pf.py_type + "()";
            else if (fi.is_string) pf.default_val = "''";
            else if (fi.is_bytes) pf.default_val = "b''";
            else if (fi.is_float || fi.has_scale) pf.default_val = "0.0";
            else if (fi.is_bool) pf.default_val = "False";
            else pf.default_val = "0";
            if (f->default_value) {
                pf.default_val = *f->default_value;
                // Enum fields: qualify bare value name with type prefix
                if (fi.is_enum && !f->type_ref.empty()) {
                    pf.default_val = py_class(f->type_ref) + "." + py_enum_val(pf.default_val);
                } else {
                    pf.default_val = py_qualify_const(pf.default_val);
                }
            }
            // constraint equals="X" implies default="X" (matching C++ behavior)
            if (!f->default_value && f->constraint && f->constraint->equals) {
                pf.default_val = py_qualify_const(*f->constraint->equals);
            }
            // Populate constraint & accessor metadata (matching C++ collect_fields)
            pf.bmdl_name = f->name;
            pf.is_optional = optional;
            pf.is_signed = fi.is_signed;
            pf.is_enum = fi.is_enum;
            pf.is_struct = fi.is_struct;
            pf.is_string = fi.is_string;
            pf.is_string_struct = fi.is_string_struct;
            pf.is_bytes = fi.is_bytes;
            pf.is_bool = fi.is_bool;
            pf.max_length = f->max_length;
            if (f->constraint) pf.constraint = &*f->constraint;
            pf.has_scale = fi.has_scale;
            pf.scale = fi.scale;
            pf.offset = fi.offset;
            pf.raw_bits = fi.raw_bits;
            pf.raw_signed = fi.raw_signed;
            pf.doc = f->doc;
            fields.push_back(pf);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            PyFieldDef sdf;
            sdf.name = py_field(sd->name);
            sdf.py_type = py_inline_class(sd->name, name_map);
            sdf.bmdl_name = sd->name;
            sdf.is_struct = true;
            sdf.is_optional = in_fx || sd->present_when != nullptr || sd->bit.has_value();
            sdf.default_val = sdf.is_optional ? "None" : sdf.py_type + "()";
            sdf.doc = sd->doc;
            fields.push_back(sdf);
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            PyFieldDef adf;
            adf.name = py_field(ad->name);
            adf.py_type = "list";
            adf.bmdl_name = ad->name;
            adf.is_optional = in_fx || ad->present_when != nullptr || ad->bit.has_value();
            // FX arrays default to None (absence indicator); present_when arrays default to []
            adf.default_val = in_fx ? "None" : "[]";
            fields.push_back(adf);
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            PyFieldDef cdf;
            cdf.name = py_field(cd->name);
            cdf.py_type = "object";
            cdf.default_val = "None";
            cdf.bmdl_name = cd->name;
            cdf.is_optional = true;
            fields.push_back(cdf);
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_py_fields(fx->children, index, fields, name_map, parent_class_name, true);
        }
    }
}

// ============================================================================
// Python Bitmap/FSPEC class generation
// ============================================================================

static const int PY_BITS_PER_BYTE = 8;

struct PyBitmapField {
    std::string name;
    std::string py_type;
    int bit = 0;
    bool is_struct = false;
    bool is_enum = false;
    bool is_string = false;
    bool is_bytes = false;
    bool is_float = false;
    bool is_bool = false;
    bool is_signed = false;
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int bits = 0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian endian = model::Endian::Big;
    model::Endian raw_endian = model::Endian::Big;
    model::WireEncoding wire_enc = model::WireEncoding::Default;
    std::optional<int> length;
    std::optional<int> bytes_attr;
    bool is_choice = false;
    const model::ChoiceDef* choice_def = nullptr;
    const model::Field* source_field = nullptr;
    std::string doc;
};

void emit_py_bitmap_class(EmitContext& ctx, const model::StructDef& sd,
                           const analyzer::TypeIndex& index,
                           const std::unordered_map<std::string, uint64_t>& /*tid_map*/,
                           const PyOuterScopeMap& scope_map = {},
                           const PyInlineNameMap& name_map = {},
                           const std::string& class_name_override = {},
                           const analyzer::WireSizeInfo* sizes = nullptr) {
    std::string cn = class_name_override.empty() ? py_class(sd.name) : class_name_override;

    // Collect bitmap-controlled fields
    std::vector<PyBitmapField> bfields;
    for (const auto& child : sd.children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->bit) {
                PyBitmapField bf;
                bf.name = f->name;
                auto fi = py_resolve_field(*f, index);
                bf.py_type = fi.py_type;
                bf.bit = *f->bit;
                bf.is_struct = fi.is_struct;
                bf.is_enum = fi.is_enum;
                bf.is_string = fi.is_string;
                bf.is_bytes = fi.is_bytes;
                bf.is_float = fi.is_float;
                bf.is_bool = fi.is_bool;
                bf.is_signed = fi.is_signed;
                bf.has_scale = fi.has_scale;
                bf.scale = fi.scale;
                bf.offset = fi.offset;
                bf.bits = fi.bits;
                bf.raw_bits = fi.raw_bits;
                bf.raw_signed = fi.raw_signed;
                bf.endian = fi.endian;
                bf.raw_endian = fi.endian;
                bf.wire_enc = fi.wire_enc;
                bf.length = f->length;
                bf.bytes_attr = f->bytes_attr;
                bf.source_field = f;
                bf.doc = f->doc;
                bfields.push_back(bf);
            }
        } else if (auto* child_sd = std::get_if<model::StructDef>(&child)) {
            if (child_sd->bit) {
                PyBitmapField bf;
                bf.name = child_sd->name;
                bf.py_type = py_inline_class(child_sd->name, name_map);
                bf.bit = *child_sd->bit;
                bf.is_struct = true;
                bf.doc = child_sd->doc;
                bfields.push_back(bf);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (cd->bit) {
                PyBitmapField bf;
                bf.name = cd->name;
                bf.py_type = py_inline_class(cd->name, name_map);
                bf.bit = *cd->bit;
                bf.is_struct = true;
                bf.is_choice = true;
                bf.choice_def = cd;
                bfields.push_back(bf);
            }
        }
    }

    int max_bit = 0;
    for (const auto& bf : bfields) max_bit = std::max(max_bit, bf.bit);
    bool has_ext = sd.bitmap_ext.has_value();
    int max_octet = max_bit / PY_BITS_PER_BYTE;
    int num_octets = max_octet + 1;
    bool fspec_le = !bfields.empty() && bfields[0].endian == model::Endian::Little;

    auto sorted_fields = bfields;
    std::sort(sorted_fields.begin(), sorted_fields.end(), [](const auto& a, const auto& b) {
        int a_oct = a.bit / 8;
        int b_oct = b.bit / 8;
        if (a_oct != b_oct) return a_oct < b_oct;
        return a.bit > b.bit;
    });

    ctx.line();
    ctx.line("class " + cn + ":");
    ctx.indent();
    py_emit_docstring(ctx, sd.doc);
    if (sizes) {
        auto ws = sizes->get(sd.name);
        if (ws) {
            ctx.line("WIRE_SIZE = " + std::to_string(*ws));
        }
    }

    // __slots__
    if (!bfields.empty()) {
        std::string slots = "__slots__ = (";
        for (size_t i = 0; i < bfields.size(); i++) {
            if (i > 0) slots += ", ";
            slots += "'" + py_field(bfields[i].name) + "'";
        }
        if (bfields.size() == 1) slots += ",";
        ctx.line(slots + ")");
    }
    ctx.line();

    // __init__
    ctx.line("def __init__(self) -> None:");
    ctx.indent();
    if (bfields.empty()) ctx.line("pass");
    else for (const auto& bf : bfields) {
        py_emit_doc_comment(ctx, bf.doc);
        ctx.line("self." + py_field(bf.name) + " = None");
    }
    ctx.dedent();
    ctx.line();

    // Accessors for bitmap fields (all nullable/optional)
    for (const auto& bf : bfields) {
        std::string m = py_field(bf.name);

        // has / clear
        ctx.line("def has_" + m + "(self) -> bool: return self." + m + " is not None");
        ctx.line("def clear_" + m + "(self) -> None: self." + m + " = None");

        // Validated setter
        bool has_constraint = bf.source_field && bf.source_field->constraint
            && bf.source_field->constraint->validate != model::ValidateTiming::Deferred
            && (bf.source_field->constraint->equals || bf.source_field->constraint->min ||
                bf.source_field->constraint->max);
        bool has_ml = bf.source_field && bf.source_field->max_length.has_value();

        if (has_constraint || has_ml) {
            ctx.line("def set_" + m + "(self, v):");
            ctx.indent();
            if (has_constraint) {
                const auto& con = *bf.source_field->constraint;
                if (con.equals)
                    ctx.line("if v != " + py_qualify_const(*con.equals) +
                             ": raise ConstraintError('" + bf.name +
                             " constraint: expected " + *con.equals + "')");
                if (con.max)
                    ctx.line("if v > " + py_qualify_const(*con.max) +
                             ": raise ConstraintError('" + bf.name +
                             " exceeds max " + *con.max + "')");
                if (con.min && (*con.min != "0" || bf.is_signed))
                    ctx.line("if v < " + py_qualify_const(*con.min) +
                             ": raise ConstraintError('" + bf.name +
                             " below min " + *con.min + "')");
            }
            if (has_ml) {
                int ml = *bf.source_field->max_length;
                ctx.line("if len(v) > " + std::to_string(ml) +
                         ": raise ConstraintError('" + bf.name +
                         " exceeds max length " + std::to_string(ml) + "')");
            }
            ctx.line("self." + m + " = v");
            ctx.dedent();
            ctx.line();
        }

        // Raw accessors for scaled bitmap fields
        if (bf.has_scale) {
            std::string scale_s = double_literal(bf.scale);
            std::string offset_s = double_literal(bf.offset);
            if (bf.offset != 0.0) {
                ctx.line("def get_" + m + "_raw(self) -> int: return int((self." +
                         m + " - " + offset_s + ") / " + scale_s +
                         ") if self." + m + " is not None else 0");
            } else {
                ctx.line("def get_" + m + "_raw(self) -> int: return int(self." +
                         m + " / " + scale_s +
                         ") if self." + m + " is not None else 0");
            }
            ctx.line("def set_" + m + "_raw(self, v: int) -> None: self." +
                     m + " = float(v) * " + scale_s + " + " + offset_s);
            ctx.line();
        }
    }

    // Build outer-scope decode parameters
    auto osp_it = scope_map.find(sd.name);
    std::string decode_params;
    PyOuterContext outer_ctx;
    if (osp_it != scope_map.end()) {
        for (const auto& p : osp_it->second) {
            decode_params += ", " + py_field(p.bmdl_name);
            outer_ctx[p.bmdl_name] = py_field(p.bmdl_name);
        }
    }

    // decode
    ctx.line("@staticmethod");
    ctx.line("def decode(r: 'BitReader'" + decode_params + ") -> '" + cn + "':");
    ctx.indent();
    ctx.line("result = " + cn + "()");
    ctx.line();
    ctx.line("# Read FSPEC bitmap");
    ctx.line("fspec = bytearray(" + std::to_string(num_octets) + ")");
    ctx.line("fspec_len = 0");
    if (has_ext) {
        ctx.line("while True:");
        ctx.indent();
        ctx.line("b = r.read_u8()");
        ctx.line("if fspec_len < " + std::to_string(num_octets) + ": fspec[fspec_len] = b");
        ctx.line("fspec_len += 1");
        ctx.line("if not (b & (1 << " + std::to_string(*sd.bitmap_ext) + ")): break");
        ctx.dedent();
    } else {
        ctx.line("for i in range(" + std::to_string(num_octets) + "):");
        ctx.indent();
        ctx.line("fspec[i] = r.read_u8()");
        ctx.dedent();
        ctx.line("fspec_len = " + std::to_string(num_octets));
    }
    if (fspec_le) {
        ctx.line("fspec[:fspec_len] = fspec[:fspec_len][::-1]");
    }
    ctx.line();

    // Decode fields based on FSPEC bits
    for (const auto& bf : sorted_fields) {
        int byte_idx = bf.bit / PY_BITS_PER_BYTE;
        int bit_in_byte = bf.bit % PY_BITS_PER_BYTE;
        std::string m = "result." + py_field(bf.name);
        ctx.line("if fspec_len > " + std::to_string(byte_idx) +
                 " and (fspec[" + std::to_string(byte_idx) +
                 "] & (1 << " + std::to_string(bit_in_byte) + ")):");
        ctx.indent();
        if (bf.is_choice && bf.choice_def && bf.choice_def->switch_expr) {
            // Choice decode based on switch expression
            std::string sv = "result." + py_field(bf.choice_def->switch_expr->name);
            bool first_case = true;
            for (const auto& cs : bf.choice_def->cases) {
                std::string cond;
                if (cs.value) {
                    std::string val = *cs.value;
                    if (index.constants.count(*cs.value)) {
                        val = "Constants." + py_snake(*cs.value);
                    }
                    cond = sv + " == " + val;
                } else if (cs.range) {
                    auto dot_pos = cs.range->find("..");
                    if (dot_pos != std::string::npos) {
                        std::string min_s = cs.range->substr(0, dot_pos);
                        std::string max_s = cs.range->substr(dot_pos + 2);
                        if (min_s == "0") {
                            cond = sv + " <= " + max_s;
                        } else {
                            cond = min_s + " <= " + sv + " <= " + max_s;
                        }
                    } else {
                        cond = sv + " == " + *cs.range;
                    }
                } else {
                    continue;
                }
                std::string prefix = first_case ? "if" : "elif";
                first_case = false;
                ctx.line(prefix + " " + cond + ":");
                ctx.indent();
                std::string et = cs.type_ref.empty() ? py_inline_class(cs.name, name_map) : py_class(cs.type_ref);
                std::string case_args;
                auto child_osp = scope_map.find(cs.type_ref.empty() ? cs.name : cs.type_ref);
                if (child_osp != scope_map.end()) {
                    for (const auto& p : child_osp->second)
                        case_args += ", result." + py_field(p.bmdl_name);
                }
                ctx.line(m + " = " + et + ".decode(r" + case_args + ")");
                ctx.dedent();
            }
            if (bf.choice_def->otherwise) {
                ctx.line("else:");
                ctx.indent();
                std::string et = bf.choice_def->otherwise->type_ref.empty()
                    ? py_class(bf.choice_def->otherwise->name)
                    : py_class(bf.choice_def->otherwise->type_ref);
                ctx.line(m + " = " + et + ".decode(r)");
                ctx.dedent();
            }
        } else if (bf.is_struct && !bf.is_string && !bf.is_bytes) {
            std::string args;
            auto child_osp = scope_map.find(bf.name);
            if (child_osp != scope_map.end()) {
                for (const auto& p : child_osp->second)
                    args += ", result." + py_field(p.bmdl_name);
            }
            ctx.line(m + " = " + bf.py_type + ".decode(r" + args + ")");
        } else if (bf.is_enum) {
            ctx.line(m + " = " + bf.py_type + ".decode(r)");
        } else if (bf.has_scale) {
            std::string read;
            if (bf.raw_signed) {
                read = "r.read_signed_bits(" + std::to_string(bf.raw_bits) + ")";
            } else {
                if (bf.raw_bits == 16) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    read = "r.read_u16(" + be + ")";
                } else if (bf.raw_bits == 32) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    read = "r.read_u32(" + be + ")";
                } else if (bf.raw_bits == 64) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    read = "r.read_u64(" + be + ")";
                } else {
                    read = "r.read_bits(" + std::to_string(bf.raw_bits) + ")";
                }
            }
            ctx.line(m + " = " + read + " * " + py_double(bf.scale) +
                     (bf.offset != 0.0 ? " + " + py_double(bf.offset) : ""));
        } else if (bf.is_string) {
            std::string enc_arg;
            if (bf.source_field) enc_arg = py_encoding_const(*bf.source_field);
            std::string enc_suffix = enc_arg.empty() ? "" : ", " + enc_arg;
            if (bf.length)
                ctx.line(m + " = r.read_string(" + std::to_string(*bf.length) + enc_suffix + ")");
            else
                ctx.line(m + " = r.read_string(r.remaining_bytes()" + enc_suffix + ")");
        } else if (bf.is_bytes) {
            if (bf.length)
                ctx.line(m + " = r.read_bytes(" + std::to_string(*bf.length) + ")");
            else if (bf.bytes_attr)
                ctx.line(m + " = r.read_bytes(" + std::to_string(*bf.bytes_attr) + ")");
            else
                ctx.line(m + " = r.read_bytes(r.remaining_bytes())");
        } else if (bf.is_bool) {
            ctx.line(m + " = r.read_bits(1) != 0");
        } else {
            // Primitive integer
            std::string be = (bf.endian == model::Endian::Big) ? "True" : "False";
            std::string read;
            if (bf.wire_enc == model::WireEncoding::BCD) {
                read = "r.read_bcd(" + std::to_string(bf.bits) + ")";
            } else if (bf.wire_enc == model::WireEncoding::BCD_S) {
                read = "r.read_bcd_signed(" + std::to_string(bf.bits) + ")";
            } else if (bf.wire_enc == model::WireEncoding::BNR_S) {
                read = "r.read_sign_magnitude(" + std::to_string(bf.bits) + ")";
            } else if (bf.is_signed || bf.wire_enc == model::WireEncoding::CB2) {
                read = "r.read_signed_bits(" + std::to_string(bf.bits) + ")";
            } else {
                if (bf.bits == 16) read = "r.read_u16(" + be + ")";
                else if (bf.bits == 32) read = "r.read_u32(" + be + ")";
                else if (bf.bits == 64) read = "r.read_u64(" + be + ")";
                else read = "r.read_bits(" + std::to_string(bf.bits) + ")";
            }
            if (bf.is_float) {
                if (bf.bits == 32) read = "r.read_f32(" + be + ")";
                else read = "r.read_f64(" + be + ")";
            }
            ctx.line(m + " = " + read);
        }
        ctx.dedent();
    }

    ctx.line("return result");
    ctx.dedent();
    ctx.line();

    // decode_bytes
    ctx.line("@staticmethod");
    ctx.line("def decode_bytes(data: bytes) -> '" + cn + "':");
    ctx.indent();
    ctx.line("return " + cn + ".decode(BitReader(data))");
    ctx.dedent();
    ctx.line();

    // encode
    ctx.line("def encode(self, w: 'BitWriter') -> None:");
    ctx.indent();
    ctx.line("fspec = bytearray(" + std::to_string(num_octets) + ")");
    if (has_ext) {
        ctx.line("last_octet = 0");
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / PY_BITS_PER_BYTE;
            int bit_in_byte = bf.bit % PY_BITS_PER_BYTE;
            ctx.line("if self." + py_field(bf.name) + " is not None: fspec[" +
                     std::to_string(byte_idx) + "] |= (1 << " +
                     std::to_string(bit_in_byte) + "); last_octet = max(last_octet, " +
                     std::to_string(byte_idx) + ")");
        }
        ctx.line("for i in range(last_octet): fspec[i] |= (1 << " +
                 std::to_string(*sd.bitmap_ext) + ")");
        if (fspec_le) {
            ctx.line("fspec[:last_octet + 1] = fspec[:last_octet + 1][::-1]");
        }
        ctx.line("w.write_bytes(bytes(fspec[:last_octet + 1]))");
    } else {
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / PY_BITS_PER_BYTE;
            int bit_in_byte = bf.bit % PY_BITS_PER_BYTE;
            ctx.line("if self." + py_field(bf.name) + " is not None: fspec[" +
                     std::to_string(byte_idx) + "] |= (1 << " +
                     std::to_string(bit_in_byte) + ")");
        }
        if (fspec_le) {
            ctx.line("fspec[:] = fspec[::-1]");
        }
        ctx.line("w.write_bytes(bytes(fspec))");
    }

    // Encode present fields
    for (const auto& bf : sorted_fields) {
        std::string m = "self." + py_field(bf.name);
        ctx.line("if " + m + " is not None:");
        ctx.indent();
        if (bf.is_choice || (bf.is_struct && !bf.is_string && !bf.is_bytes) || bf.is_enum) {
            ctx.line(m + ".encode(w)");
        } else if (bf.has_scale) {
            std::string reverse_scale = "int((" + m + " - " + py_double(bf.offset) + ") / " + py_double(bf.scale) + ")";
            if (bf.raw_signed) {
                ctx.line("w.write_signed_bits(" + reverse_scale + ", " + std::to_string(bf.raw_bits) + ")");
            } else {
                if (bf.raw_bits == 16) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    ctx.line("w.write_u16(" + reverse_scale + ", " + be + ")");
                } else if (bf.raw_bits == 32) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    ctx.line("w.write_u32(" + reverse_scale + ", " + be + ")");
                } else if (bf.raw_bits == 64) {
                    std::string be = (bf.raw_endian == model::Endian::Big) ? "True" : "False";
                    ctx.line("w.write_u64(" + reverse_scale + ", " + be + ")");
                } else {
                    ctx.line("w.write_bits(" + reverse_scale + ", " + std::to_string(bf.raw_bits) + ")");
                }
            }
        } else if (bf.is_string) {
            int pad = 0;
            if (bf.source_field && bf.source_field->padding && *bf.source_field->padding == model::StringPadding::Space)
                pad = 0x20;
            std::string enc_arg;
            if (bf.source_field) enc_arg = py_encoding_const(*bf.source_field);
            std::string enc_suffix = enc_arg.empty() ? "" : ", encoding=" + enc_arg;
            if (bf.length)
                ctx.line("w.write_string(" + m + ", " + std::to_string(*bf.length) + ", " + std::to_string(pad) + enc_suffix + ")");
            else
                ctx.line("w.write_string(" + m + ", len(" + m + "), " + std::to_string(pad) + enc_suffix + ")");
        } else if (bf.is_bytes) {
            ctx.line("w.write_bytes(" + m + ")");
        } else if (bf.is_bool) {
            ctx.line("w.write_bits(1 if " + m + " else 0, 1)");
        } else {
            std::string be = (bf.endian == model::Endian::Big) ? "True" : "False";
            if (bf.is_float) {
                if (bf.bits == 32) ctx.line("w.write_f32(" + m + ", " + be + ")");
                else ctx.line("w.write_f64(" + m + ", " + be + ")");
            } else if (bf.wire_enc == model::WireEncoding::BCD) {
                ctx.line("w.write_bcd(" + m + ", " + std::to_string(bf.bits) + ")");
            } else if (bf.wire_enc == model::WireEncoding::BCD_S) {
                ctx.line("w.write_bcd_signed(" + m + ", " + std::to_string(bf.bits) + ")");
            } else if (bf.wire_enc == model::WireEncoding::BNR_S) {
                ctx.line("w.write_sign_magnitude(" + m + ", " + std::to_string(bf.bits) + ")");
            } else if (bf.is_signed || bf.wire_enc == model::WireEncoding::CB2) {
                ctx.line("w.write_signed_bits(" + m + ", " + std::to_string(bf.bits) + ")");
            } else {
                if (bf.bits == 16) ctx.line("w.write_u16(" + m + ", " + be + ")");
                else if (bf.bits == 32) ctx.line("w.write_u32(" + m + ", " + be + ")");
                else if (bf.bits == 64) ctx.line("w.write_u64(" + m + ", " + be + ")");
                else ctx.line("w.write_bits(" + m + ", " + std::to_string(bf.bits) + ")");
            }
        }
        ctx.dedent();
    }

    ctx.dedent();
    ctx.line();

    // encode_bytes
    ctx.line("def encode_bytes(self) -> bytes:");
    ctx.indent();
    ctx.line("w = BitWriter()");
    ctx.line("self.encode(w)");
    ctx.line("return w.to_bytes()");
    ctx.dedent();
    ctx.line();

    // to_dict
    ctx.line("def to_dict(self) -> dict:");
    ctx.indent();
    ctx.line("d = {}");
    for (const auto& bf : bfields) {
        std::string key = bf.name;
        std::string val = "self." + py_field(bf.name);
        if (bf.is_enum) {
            ctx.line("d['" + key + "'] = " + val + ".value if " + val + " is not None else None");
        } else if (bf.has_scale) {
            ctx.line("d['" + key + "'] = " + val + ".value if " + val + " is not None else None");
        } else if (bf.is_struct && !bf.is_string && !bf.is_bytes) {
            ctx.line("d['" + key + "'] = " + val + ".to_dict() if " + val + " is not None else None");
        } else if (bf.is_bytes) {
            ctx.line("d['" + key + "'] = list(" + val + ") if " + val + " is not None else None");
        } else {
            ctx.line("d['" + key + "'] = " + val);
        }
    }
    ctx.line("return d");
    ctx.dedent();
    ctx.line();

    // from_dict
    ctx.line("@classmethod");
    ctx.line("def from_dict(cls, d: dict) -> '" + cn + "':");
    ctx.indent();
    ctx.line("obj = cls()");
    for (const auto& bf : bfields) {
        std::string key = bf.name;
        std::string target = "obj." + py_field(bf.name);
        ctx.line("if '" + key + "' in d: " + target + " = d['" + key + "']");
    }
    ctx.line("return obj");
    ctx.dedent();
    ctx.line();

    // __repr__
    ctx.line("def __repr__(self) -> str:");
    ctx.indent();
    if (bfields.empty()) {
        ctx.line("return '" + cn + "()'");
    } else {
        std::string fmt = "return f'" + cn + "(";
        for (size_t i = 0; i < bfields.size(); i++) {
            if (i > 0) fmt += ", ";
            fmt += py_field(bfields[i].name) + "={self." + py_field(bfields[i].name) + "}";
        }
        ctx.line(fmt + ")'");
    }
    ctx.dedent();

    // validate() for bitmap class
    {
        bool has_any = false;
        for (const auto& child : sd.children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->constraint &&
                    (f->constraint->equals || f->constraint->min || f->constraint->max)) {
                    has_any = true; break;
                }
            }
        }
        if (has_any) {
            ctx.line();
            ctx.line("def validate(self) -> None:");
            ctx.indent();
            for (const auto& child : sd.children) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    if (!f->constraint) continue;
                    const auto& con = *f->constraint;
                    if (!con.equals && !con.min && !con.max) continue;
                    auto fi = py_resolve_field(*f, index);
                    if (fi.is_struct || fi.is_enum || fi.is_string || fi.is_bytes) continue;
                    std::string m = "self." + py_field(f->name);
                    ctx.line("if " + m + " is not None:");
                    ctx.indent();
                    if (con.equals) {
                        ctx.line("if " + m + " != " + py_qualify_const(*con.equals) + ":");
                        ctx.indent();
                        ctx.line("raise ConstraintError('" + f->name + ": expected " + *con.equals + "')");
                        ctx.dedent();
                    }
                    if (con.max) {
                        ctx.line("if " + m + " > " + py_qualify_const(*con.max) + ":");
                        ctx.indent();
                        ctx.line("raise ConstraintError('" + f->name + " exceeds max " + *con.max + "')");
                        ctx.dedent();
                    }
                    if (con.min && (*con.min != "0" || fi.is_signed)) {
                        ctx.line("if " + m + " < " + py_qualify_const(*con.min) + ":");
                        ctx.indent();
                        ctx.line("raise ConstraintError('" + f->name + " below min " + *con.min + "')");
                        ctx.dedent();
                    }
                    ctx.dedent();
                }
            }
            ctx.dedent();
        }
    }

    ctx.dedent();
}

// ============================================================================
// structs.py + messages.py (normal non-bitmap classes)
// ============================================================================

void emit_py_class(EmitContext& ctx, const std::string& name,
                   const std::vector<model::StructChild>& children,
                   const analyzer::TypeIndex& index,
                   const std::unordered_map<std::string, uint64_t>& tid_map,
                   const std::string& msg_id = "",
                   const PyOuterScopeMap& scope_map = {},
                   const PyInlineNameMap& name_map = {},
                   const std::string& class_name_override = {},
                   const std::vector<PyFieldDef>& extra_fields = {},
                   const std::string& doc = {},
                   const analyzer::WireSizeInfo* sizes = nullptr) {
    std::string cn = class_name_override.empty() ? py_class(name) : class_name_override;
    std::vector<PyFieldDef> fields;
    // Add frame header/footer fields if this is a message used in a frame
    for (const auto& ef : extra_fields) fields.push_back(ef);
    collect_py_fields(children, index, fields, name_map, cn);

    ctx.line();
    ctx.line("class " + cn + ":");
    ctx.indent();
    py_emit_docstring(ctx, doc);

    auto tid_it = tid_map.find(name);
    if (tid_it != tid_map.end()) {
        ctx.line("TYPE_ID = " + py_hex64(tid_it->second));
        ctx.line("TYPE_NAME = '" + name + "'");
    }
    if (!msg_id.empty()) ctx.line("ID_VALUE = " + msg_id);
    if (sizes) {
        auto ws = sizes->get(name);
        if (ws) {
            ctx.line("WIRE_SIZE = " + std::to_string(*ws));
        }
    }

    if (!fields.empty()) {
        std::string slots = "__slots__ = (";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) slots += ", ";
            slots += "'" + fields[i].name + "'";
        }
        if (fields.size() == 1) slots += ",";
        ctx.line(slots + ")");
    }
    ctx.line();

    // __init__
    ctx.line("def __init__(self) -> None:");
    ctx.indent();
    if (fields.empty()) ctx.line("pass");
    else for (const auto& f : fields) {
        py_emit_doc_comment(ctx, f.doc);
        if (f.default_val == "None" && f.py_type == "list" && !f.is_optional)
            ctx.line("self." + f.name + ": list = []");
        else
            ctx.line("self." + f.name + " = " + f.default_val);
    }
    ctx.dedent();
    ctx.line();

    // Accessors (matching C++ emit_plain_accessors / emit_setter_constraint_checks)
    for (const auto& f : fields) {
        if (f.bmdl_name.empty()) continue; // extra fields without bmdl_name

        // Determine if setter needs constraint validation
        bool has_immediate_constraint = f.constraint
            && f.constraint->validate != model::ValidateTiming::Deferred
            && (f.constraint->equals || f.constraint->min || f.constraint->max);
        bool needs_validation = has_immediate_constraint || f.max_length.has_value();

        if (needs_validation) {
            ctx.line("def set_" + f.name + "(self, v):");
            ctx.indent();
            if (has_immediate_constraint && f.is_bytes) {
                bool need_numeric = f.constraint->equals || f.constraint->max ||
                    (f.constraint->min && (*f.constraint->min != "0" || f.is_signed));
                if (need_numeric) {
                    ctx.line("_raw = int.from_bytes(v, 'big')");
                    if (f.constraint->equals)
                        ctx.line("if _raw != " + py_qualify_const(*f.constraint->equals) +
                                 ": raise ConstraintError('" + f.bmdl_name +
                                 " constraint: expected " + *f.constraint->equals + "')");
                    if (f.constraint->max)
                        ctx.line("if _raw > " + py_qualify_const(*f.constraint->max) +
                                 ": raise ConstraintError('" + f.bmdl_name +
                                 " exceeds max " + *f.constraint->max + "')");
                    if (f.constraint->min && (*f.constraint->min != "0" || f.is_signed))
                        ctx.line("if _raw < " + py_qualify_const(*f.constraint->min) +
                                 ": raise ConstraintError('" + f.bmdl_name +
                                 " below min " + *f.constraint->min + "')");
                }
            } else if (has_immediate_constraint) {
                if (f.constraint->equals)
                    ctx.line("if v != " + py_qualify_const(*f.constraint->equals) +
                             ": raise ConstraintError('" + f.bmdl_name +
                             " constraint: expected " + *f.constraint->equals + "')");
                if (f.constraint->max)
                    ctx.line("if v > " + py_qualify_const(*f.constraint->max) +
                             ": raise ConstraintError('" + f.bmdl_name +
                             " exceeds max " + *f.constraint->max + "')");
                if (f.constraint->min && (*f.constraint->min != "0" || f.is_signed))
                    ctx.line("if v < " + py_qualify_const(*f.constraint->min) +
                             ": raise ConstraintError('" + f.bmdl_name +
                             " below min " + *f.constraint->min + "')");
            }
            if (f.max_length) {
                ctx.line("if len(v) > " + std::to_string(*f.max_length) +
                         ": raise ConstraintError('" + f.bmdl_name +
                         " exceeds max length " + std::to_string(*f.max_length) + "')");
            }
            ctx.line("self." + f.name + " = v");
            ctx.dedent();
            ctx.line();
        }

        // Raw accessors for scaled fields
        if (f.has_scale) {
            std::string scale_s = double_literal(f.scale);
            std::string offset_s = double_literal(f.offset);
            if (f.offset != 0.0) {
                ctx.line("def get_" + f.name + "_raw(self) -> int: return int((self." +
                         f.name + " - " + offset_s + ") / " + scale_s + ")");
            } else {
                ctx.line("def get_" + f.name + "_raw(self) -> int: return int(self." +
                         f.name + " / " + scale_s + ")");
            }
            ctx.line("def set_" + f.name + "_raw(self, v: int) -> None: self." +
                     f.name + " = float(v) * " + scale_s + " + " + offset_s);
            ctx.line();
        }

        // Optional field helpers
        if (f.is_optional) {
            ctx.line("def has_" + f.name + "(self) -> bool: return self." +
                     f.name + " is not None");
            ctx.line("def clear_" + f.name + "(self) -> None: self." +
                     f.name + " = None");
            ctx.line();
        }
    }

    // Build outer-scope decode parameters and context for this class
    auto osp_it = scope_map.find(name);
    std::string decode_params;
    PyOuterContext outer_ctx;
    if (osp_it != scope_map.end()) {
        for (const auto& p : osp_it->second) {
            decode_params += ", " + py_field(p.bmdl_name);
            outer_ctx[p.bmdl_name] = py_field(p.bmdl_name);
        }
    }

    // decode
    ctx.line("@staticmethod");
    ctx.line("def decode(r: 'BitReader'" + decode_params + ") -> '" + cn + "':");
    ctx.indent();
    ctx.line("result = " + cn + "()");
    { PyBitTracker decode_tracker;
    emit_py_decode_children(ctx, children, index, "result", decode_tracker, scope_map, outer_ctx, name_map, cn); }
    ctx.line("return result");
    ctx.dedent();
    ctx.line();

    // decode_bytes
    ctx.line("@staticmethod");
    ctx.line("def decode_bytes(data: bytes) -> '" + cn + "':");
    ctx.indent();
    ctx.line("return " + cn + ".decode(BitReader(data))");
    ctx.dedent();
    ctx.line();

    // encode
    ctx.line("def encode(self, w: 'BitWriter') -> None:");
    ctx.indent();
    if (children.empty()) ctx.line("pass");
    else {
        // Check for auto-length fields; if present, record struct start position
        bool has_auto_length = false;
        const model::Field* auto_len_ref_field = nullptr;
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                    if (f->auto_expr->field_ref.empty()) {
                        has_auto_length = true;
                    } else {
                        auto_len_ref_field = f;
                    }
                }
            }
        }
        if (has_auto_length) {
            ctx.line("_struct_start = w.size_bytes()");
        }
        std::string len_ref_target = (auto_len_ref_field && auto_len_ref_field->auto_expr) ? auto_len_ref_field->auto_expr->field_ref : "";
        PyBitTracker encode_tracker;
        emit_py_encode_children(ctx, children, index, "self", encode_tracker, len_ref_target, auto_len_ref_field, name_map, cn);
        // Auto-length backpatching (struct-level only: auto="length" with no field_ref)
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length
                    && f->auto_expr->field_ref.empty()) {
                    auto lfi = py_resolve_field(*f, index);
                    std::string length_expr = "w.size_bytes() - _struct_start";
                    if (f->auto_expr->modifier.has_modifier()) {
                        auto& mod = f->auto_expr->modifier;
                        // Apply forward modifier: auto="length + N" means wire_val = actual + N
                        switch (mod.op) {
                            case model::ArithOp::Add:
                                length_expr = "(" + length_expr + " + " + std::to_string(mod.literal) + ")";
                                break;
                            case model::ArithOp::Sub:
                                length_expr = "(" + length_expr + " - " + std::to_string(mod.literal) + ")";
                                break;
                            case model::ArithOp::Mul:
                                length_expr = "(" + length_expr + " * " + std::to_string(mod.literal) + ")";
                                break;
                            case model::ArithOp::Div:
                                length_expr = "(" + length_expr + " // " + std::to_string(mod.literal) + ")";
                                break;
                            default: break;
                        }
                    }
                    if (lfi.bits <= 8) ctx.line("w.patch_u8(_len_pos, " + length_expr + ")");
                    else if (lfi.bits <= 16) ctx.line("w.patch_u16(_len_pos, " + length_expr + ", " +
                        std::string((lfi.endian == model::Endian::Big) ? "True" : "False") + ")");
                    else ctx.line("w.patch_u32(_len_pos, " + length_expr + ", " +
                        std::string((lfi.endian == model::Endian::Big) ? "True" : "False") + ")");
                    break; // Only one length field per struct
                }
            }
        }
        // Note: auto-length(field) backpatching is emitted inline within
        // emit_py_encode_children, right after the target field is written.
    }
    ctx.dedent();
    ctx.line();

    // encode_bytes
    ctx.line("def encode_bytes(self) -> bytes:");
    ctx.indent();
    ctx.line("w = BitWriter()");
    ctx.line("self.encode(w)");
    ctx.line("return w.to_bytes()");
    ctx.dedent();
    ctx.line();

    // to_dict
    ctx.line("def to_dict(self) -> dict:");
    ctx.indent();
    ctx.line("d = {}");
    for (const auto& f : fields) {
        std::string key = f.bmdl_name.empty() ? f.name : f.bmdl_name;
        std::string val = "self." + f.name;
        bool nullable = (f.default_val == "None" || f.is_optional);
        if (f.py_type == "list") {
            if (nullable)
                ctx.line("d['" + key + "'] = [x.to_dict() if hasattr(x, 'to_dict') else x for x in " + val + "] if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = [x.to_dict() if hasattr(x, 'to_dict') else x for x in " + val + "]");
        } else if (f.py_type == "object") {
            ctx.line("d['" + key + "'] = " + val + ".to_dict() if " + val + " is not None and hasattr(" + val + ", 'to_dict') else " + val);
        } else if (f.is_enum) {
            if (nullable)
                ctx.line("d['" + key + "'] = " + val + ".value if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = " + val + ".value");
        } else if (f.has_scale) {
            if (nullable)
                ctx.line("d['" + key + "'] = " + val + ".value if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = " + val + ".value");
        } else if (f.is_struct && f.is_string_struct) {
            if (nullable)
                ctx.line("d['" + key + "'] = str(" + val + ") if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = str(" + val + ")");
        } else if (f.is_struct) {
            if (nullable)
                ctx.line("d['" + key + "'] = " + val + ".to_dict() if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = " + val + ".to_dict()");
        } else if (f.is_bytes) {
            if (nullable)
                ctx.line("d['" + key + "'] = list(" + val + ") if " + val + " is not None else None");
            else
                ctx.line("d['" + key + "'] = list(" + val + ")");
        } else {
            ctx.line("d['" + key + "'] = " + val);
        }
    }
    ctx.line("return d");
    ctx.dedent();
    ctx.line();

    // from_dict
    ctx.line("@classmethod");
    ctx.line("def from_dict(cls, d: dict) -> '" + cn + "':");
    ctx.indent();
    ctx.line("obj = cls()");
    for (const auto& f : fields) {
        std::string key = f.bmdl_name.empty() ? f.name : f.bmdl_name;
        std::string target = "obj." + f.name;
        ctx.line("if '" + key + "' in d:");
        ctx.indent();
        if (f.py_type == "list") {
            ctx.line(target + " = d['" + key + "'] if d['" + key + "'] is None else list(d['" + key + "'])");
        } else if (f.py_type == "object") {
            ctx.line(target + " = d['" + key + "']");
        } else if (f.is_enum) {
            ctx.line("_ev = d['" + key + "']");
            ctx.line("if isinstance(_ev, int): " + target + " = next((e for e in " + f.py_type + " if e.value == _ev), _ev)");
            ctx.line("else: " + target + " = _ev");
        } else if (f.has_scale) {
            ctx.line("_sv = " + f.py_type + "()");
            ctx.line("_sv.value = d['" + key + "']");
            ctx.line(target + " = _sv");
        } else if (f.is_struct && f.is_string_struct) {
            ctx.line(target + " = " + f.py_type + "(d['" + key + "']) if d['" + key + "'] is not None else None");
        } else if (f.is_struct) {
            ctx.line(target + " = " + f.py_type + ".from_dict(d['" + key + "']) if isinstance(d['" + key + "'], dict) else d['" + key + "']");
        } else if (f.is_bytes) {
            ctx.line(target + " = bytes(d['" + key + "']) if d['" + key + "'] is not None else None");
        } else {
            ctx.line(target + " = d['" + key + "']");
        }
        ctx.dedent();
    }
    ctx.line("return obj");
    ctx.dedent();
    ctx.line();

    // __repr__
    ctx.line("def __repr__(self) -> str:");
    ctx.indent();
    if (fields.empty()) ctx.line("return '" + cn + "()'");
    else {
        std::string fmt = "return f'" + cn + "(";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) fmt += ", ";
            bool nullable = (fields[i].default_val == "None");
            if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Hex) {
                if (nullable)
                    fmt += fields[i].name + "={hex(self." + fields[i].name + ") if self." + fields[i].name + " is not None else None}";
                else
                    fmt += fields[i].name + "={hex(self." + fields[i].name + ")}";
            } else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Octal) {
                if (nullable)
                    fmt += fields[i].name + "={oct(self." + fields[i].name + ") if self." + fields[i].name + " is not None else None}";
                else
                    fmt += fields[i].name + "={oct(self." + fields[i].name + ")}";
            } else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Binary) {
                if (nullable)
                    fmt += fields[i].name + "={bin(self." + fields[i].name + ") if self." + fields[i].name + " is not None else None}";
                else
                    fmt += fields[i].name + "={bin(self." + fields[i].name + ")}";
            } else
                fmt += fields[i].name + "={self." + fields[i].name + "}";
        }
        ctx.line(fmt + ")'");
    }
    ctx.dedent();

    // validate()
    {
        bool has_any = false;
        std::function<bool(const std::vector<model::StructChild>&)> has_constraints =
            [&](const std::vector<model::StructChild>& cs) -> bool {
            for (const auto& c : cs) {
                if (auto* f = std::get_if<model::Field>(&c)) {
                    if (f->constraint &&
                        (f->constraint->equals || f->constraint->min || f->constraint->max))
                        return true;
                } else if (auto* fx = std::get_if<model::FxBlock>(&c)) {
                    if (has_constraints(fx->children)) return true;
                }
            }
            return false;
        };
        has_any = has_constraints(children);
        if (has_any) {
            ctx.line();
            ctx.line("def validate(self) -> None:");
            ctx.indent();
            std::function<void(const std::vector<model::StructChild>&, bool)> emit_checks =
                [&](const std::vector<model::StructChild>& cs, bool nullable) {
                for (const auto& child : cs) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        if (!f->constraint) continue;
                        const auto& con = *f->constraint;
                        if (!con.equals && !con.min && !con.max) continue;
                        auto fi = py_resolve_field(*f, index);
                        if (fi.is_struct || fi.is_enum || fi.is_string || fi.is_bytes) continue;
                        std::string m = "self." + py_field(f->name);
                        if (nullable) {
                            ctx.line("if " + m + " is not None:");
                            ctx.indent();
                        }
                        if (con.equals) {
                            ctx.line("if " + m + " != " + py_qualify_const(*con.equals) + ":");
                            ctx.indent();
                            ctx.line("raise ConstraintError('" + f->name + ": expected " + *con.equals + "')");
                            ctx.dedent();
                        }
                        if (con.max) {
                            ctx.line("if " + m + " > " + py_qualify_const(*con.max) + ":");
                            ctx.indent();
                            ctx.line("raise ConstraintError('" + f->name + " exceeds max " + *con.max + "')");
                            ctx.dedent();
                        }
                        if (con.min && (*con.min != "0" || fi.is_signed)) {
                            ctx.line("if " + m + " < " + py_qualify_const(*con.min) + ":");
                            ctx.indent();
                            ctx.line("raise ConstraintError('" + f->name + " below min " + *con.min + "')");
                            ctx.dedent();
                        }
                        if (nullable) {
                            ctx.dedent();
                        }
                    } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
                        emit_checks(fx->children, true);
                    }
                }
            };
            emit_checks(children, false);
            ctx.dedent();
        }
    }

    ctx.dedent();
}

// Recursively emit Python classes for inline struct/array/choice types.
// current_bmdl_name: the BMDL name of the current parent (for outer-scope analysis)
// current_resolved_name: the resolved class name of the current parent (for child prefixing)
void emit_py_inline_types(EmitContext& ctx, const std::vector<model::StructChild>& children,
                           const analyzer::TypeIndex& index,
                           const std::unordered_map<std::string, uint64_t>& tid_map,
                           PyOuterScopeMap& scope_map,
                           const std::string& current_bmdl_name,
                           PyInlineNameMap& name_map,
                           const std::string& current_resolved_name = {}) {
    // Use resolved name for child prefixing; fall back to PascalCase of BMDL name
    const std::string& prefix = current_resolved_name.empty()
        ? current_bmdl_name : current_resolved_name;
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            // Generate Python IntEnum class for inline enum fields
            if (!f->enum_values.empty() && f->type_ref.empty()) {
                std::string enum_name = py_class(prefix) + py_class(f->name);
                int bits = f->bits.value_or(8);
                ctx.line();
                ctx.line("class " + enum_name + "(IntEnum):");
                ctx.indent();
                for (const auto& ev : f->enum_values)
                    ctx.line(py_enum_val(ev.name) + " = " + std::to_string(ev.id));
                ctx.line();
                ctx.line("@staticmethod");
                ctx.line("def decode(r: BitReader) -> '" + enum_name + "':");
                ctx.indent();
                ctx.line("raw = r.read_bits(" + std::to_string(bits) + ")");
                ctx.line("try:");
                ctx.indent();
                ctx.line("return " + enum_name + "(raw)");
                ctx.dedent();
                ctx.line("except ValueError:");
                ctx.indent();
                ctx.line("raise DecodeError(f'unknown " + enum_name + " value: {raw}')");
                ctx.dedent();
                ctx.dedent();
                ctx.line();
                ctx.line("def encode(self, w: BitWriter) -> None:");
                ctx.indent();
                ctx.line("w.write_bits(self.value, " + std::to_string(bits) + ")");
                ctx.dedent();
                ctx.dedent();
                ctx.line();
            }
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string resolved = py_resolve_inline_name(sd->name, prefix, sd->type_name);
            name_map[sd->name] = resolved;
            py_analyze_outer_scope(sd->name, sd->children, children, current_bmdl_name, scope_map);
            emit_py_inline_types(ctx, sd->children, index, tid_map, scope_map, sd->name, name_map, resolved);
            name_map[sd->name] = resolved;
            if (sd->is_bitmap) {
                emit_py_bitmap_class(ctx, *sd, index, tid_map, scope_map, name_map, resolved);
            } else {
                emit_py_class(ctx, sd->name, sd->children, index, tid_map, {}, scope_map, name_map, resolved, {}, sd->doc);
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                std::string resolved = py_resolve_inline_name(ad->name, prefix, ad->type_name);
                name_map[ad->name] = resolved;
                emit_py_inline_types(ctx, ad->children, index, tid_map, scope_map, ad->name, name_map, resolved);
                name_map[ad->name] = resolved;
                emit_py_class(ctx, ad->name, ad->children, index, tid_map, {}, scope_map, name_map, resolved);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    std::string resolved = py_resolve_inline_name(cs.name, prefix, cs.type_name);
                    name_map[cs.name] = resolved;
                    py_analyze_outer_scope(cs.name, cs.children, children, current_bmdl_name, scope_map);
                    emit_py_inline_types(ctx, cs.children, index, tid_map, scope_map, cs.name, name_map, resolved);
                    name_map[cs.name] = resolved;
                    emit_py_class(ctx, cs.name, cs.children, index, tid_map, {}, scope_map, name_map, resolved);
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                std::string resolved = py_resolve_inline_name(cd->otherwise->name, prefix, cd->otherwise->type_name);
                name_map[cd->otherwise->name] = resolved;
                py_analyze_outer_scope(cd->otherwise->name, cd->otherwise->children, children, current_bmdl_name, scope_map);
                emit_py_inline_types(ctx, cd->otherwise->children, index, tid_map, scope_map, cd->otherwise->name, name_map, resolved);
                name_map[cd->otherwise->name] = resolved;
                emit_py_class(ctx, cd->otherwise->name, cd->otherwise->children, index, tid_map, {}, scope_map, name_map, resolved);
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_inline_types(ctx, fx->children, index, tid_map, scope_map, current_bmdl_name, name_map, prefix);
        }
    }
}

std::string generate_py_structs(const model::Protocol& protocol,
                                 const analyzer::TypeIndex& index,
                                 const analyzer::WireSizeInfo& sizes) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from .bit_io import BitReader, BitWriter, DecodeError, EncodeError, ConstraintError");
    ctx.line("from .types import *");
    ctx.line("from .constants import Constants");

    std::unordered_map<std::string, uint64_t> empty;
    for (const auto& sd : protocol.structs) {
        PyOuterScopeMap scope_map;
        PyInlineNameMap name_map;
        emit_py_inline_types(ctx, sd.children, index, empty, scope_map, sd.name, name_map);
        if (sd.is_bitmap) {
            emit_py_bitmap_class(ctx, sd, index, empty, scope_map, name_map, {}, &sizes);
        } else {
            emit_py_class(ctx, sd.name, sd.children, index, empty, {}, scope_map, name_map, {}, {}, sd.doc, &sizes);
        }
    }

    ctx.line();
    return ctx.str();
}

// ============================================================================
// Frame class generation (Packet, Frame, etc.)
// ============================================================================

void emit_py_frame_class(EmitContext& ctx, const analyzer::SessionInfo& si,
                         const analyzer::TypeIndex& index) {
    if (!si.frame) return;
    const model::FrameDef& frame = *si.frame;
    std::string cn = py_class(frame.name);

    // Collect header fields
    struct FrameFieldInfo {
        std::string py_name;
        PyFieldInfo fi;
        const model::Field* field;
    };
    std::vector<FrameFieldInfo> header_fields;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            header_fields.push_back({py_field(f->name), py_resolve_field(*f, index), f});
        }
    }
    std::vector<FrameFieldInfo> footer_fields;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            footer_fields.push_back({py_field(f->name), py_resolve_field(*f, index), f});
        }
    }

    ctx.line();
    ctx.line("class " + cn + ":");
    ctx.indent();

    // __slots__
    {
        std::vector<std::string> slot_names;
        for (const auto& hf : header_fields) slot_names.push_back("'" + hf.py_name + "'");
        slot_names.push_back("'payload'");
        for (const auto& ff : footer_fields) slot_names.push_back("'" + ff.py_name + "'");
        std::string slots;
        for (size_t i = 0; i < slot_names.size(); i++) {
            if (i > 0) slots += ", ";
            slots += slot_names[i];
        }
        if (slot_names.size() == 1) slots += ",";
        ctx.line("__slots__ = (" + slots + ")");
    }
    ctx.line();

    // __init__
    ctx.line("def __init__(self) -> None:");
    ctx.indent();
    for (const auto& hf : header_fields) {
        std::string def_val;
        if (hf.fi.is_string) def_val = "''";
        else if (hf.fi.is_bytes) def_val = "b''";
        else if (hf.fi.is_bool) def_val = "False";
        else if (hf.fi.is_float) def_val = "0.0";
        else def_val = "0";
        ctx.line("self." + hf.py_name + " = " + def_val);
    }
    if (si.payload_is_array) {
        ctx.line("self.payload = []");
    } else {
        ctx.line("self.payload = None");
    }
    for (const auto& ff : footer_fields) {
        std::string def_val;
        if (ff.fi.is_string) def_val = "''";
        else if (ff.fi.is_bytes) def_val = "b''";
        else if (ff.fi.is_bool) def_val = "False";
        else if (ff.fi.is_float) def_val = "0.0";
        else def_val = "0";
        ctx.line("self." + ff.py_name + " = " + def_val);
    }
    ctx.dedent();
    ctx.line();

    // wrap() static method — single method that works for any leaf type
    // (Python doesn't support overloading, so one method handles all types)
    ctx.line("@staticmethod");
    ctx.line("def wrap(msg) -> '" + cn + "':");
    ctx.indent();
    ctx.line("frame = " + cn + "()");
    // Set constraint-equals header fields (e.g., sync = Constants.SYNC)
    for (const auto& hf : header_fields) {
        if (hf.field->constraint && hf.field->constraint->equals) {
            std::string const_ref = *hf.field->constraint->equals;
            // Qualify bare constant names with Constants. prefix
            if (!const_ref.empty() && std::isupper(static_cast<unsigned char>(const_ref[0]))) {
                const_ref = "Constants." + const_ref;
            }
            ctx.line("frame." + hf.py_name + " = " + const_ref);
        }
    }
    // Set id field from msg.ID_VALUE if the message class has it
    if (!si.id_field_name.empty()) {
        ctx.line("if hasattr(msg, 'ID_VALUE'):");
        ctx.indent();
        ctx.line("frame." + py_field(si.id_field_name) + " = msg.ID_VALUE");
        ctx.dedent();
    }
    if (si.payload_is_array) {
        ctx.line("frame.payload.append(msg)");
    } else {
        ctx.line("frame.payload = msg");
    }
    ctx.line("return frame");
    ctx.dedent();
    ctx.line();

    // Find length field for backpatching
    const model::Field* length_field = nullptr;
    bool length_is_total_frame = false;
    bool length_is_payload = false;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                length_field = f;
                length_is_total_frame = f->auto_expr->field_ref.empty();
                length_is_payload = (!f->auto_expr->field_ref.empty() &&
                                     f->auto_expr->field_ref == "payload");
            }
        }
    }

    // encode() method
    ctx.line("def encode(self, w: 'BitWriter') -> None:");
    ctx.indent();
    if (length_is_total_frame) {
        ctx.line("_frame_start = w.size_bytes()");
    }
    for (const auto& hf : header_fields) {
        if (hf.field->auto_expr && hf.field->auto_expr->kind == model::AutoKind::Length) {
            ctx.line("_len_pos = w.size_bytes()");
            ctx.line(py_write_stmt("0", hf.fi));
            if (length_is_payload) {
                ctx.line("_payload_start = w.size_bytes()");
            }
        } else if (hf.field->auto_expr && hf.field->auto_expr->kind == model::AutoKind::Count) {
            if (hf.field->auto_expr->field_ref == "payload" && si.payload_is_array) {
                ctx.line(py_write_stmt("len(self.payload)", hf.fi));
            } else {
                ctx.line(py_write_stmt("self." + hf.py_name, hf.fi));
            }
        } else if (hf.field->constraint && hf.field->constraint->equals) {
            std::string const_ref = *hf.field->constraint->equals;
            if (!const_ref.empty() && std::isupper(static_cast<unsigned char>(const_ref[0]))) {
                const_ref = "Constants." + const_ref;
            }
            ctx.line(py_write_stmt(const_ref, hf.fi));
        } else {
            std::string val = "self." + hf.py_name;
            if (hf.fi.is_enum) val = val + ".value";
            ctx.line(py_write_stmt(val, hf.fi));
        }
    }

    // Write payload
    if (si.payload_is_array) {
        ctx.line("for _item in self.payload:");
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = py_class(lt.name);
            ctx.line(std::string(first ? "if " : "elif ") + "isinstance(_item, " + leaf_class + "):");
            ctx.indent();
            ctx.line("_item.encode(w)");
            ctx.dedent();
            first = false;
        }
        ctx.dedent();
    } else {
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = py_class(lt.name);
            ctx.line(std::string(first ? "if " : "elif ") + "isinstance(self.payload, " + leaf_class + "):");
            ctx.indent();
            ctx.line("self.payload.encode(w)");
            ctx.dedent();
            first = false;
        }
    }

    // Write footer fields
    for (const auto& ff : footer_fields) {
        std::string val = "self." + ff.py_name;
        if (ff.fi.is_enum) val = val + ".value";
        ctx.line(py_write_stmt(val, ff.fi));
    }

    // Backpatch length
    if (length_field) {
        auto al_fi = py_resolve_field(*length_field, index);
        bool be = (al_fi.endian == model::Endian::Big);
        std::string raw_length;
        if (length_is_payload) {
            raw_length = "w.size_bytes() - _payload_start";
        } else {
            raw_length = "w.size_bytes() - _frame_start";
        }
        // Apply arithmetic modifier
        if (length_field->auto_expr->modifier.has_modifier()) {
            std::string op_str;
            switch (length_field->auto_expr->modifier.op) {
                case model::ArithOp::Add: op_str = " + "; break;
                case model::ArithOp::Sub: op_str = " - "; break;
                case model::ArithOp::Mul: op_str = " * "; break;
                case model::ArithOp::Div: op_str = " // "; break;
                default: break;
            }
            if (!op_str.empty()) {
                raw_length = "((" + raw_length + ")" + op_str +
                             std::to_string(length_field->auto_expr->modifier.literal) + ")";
            }
        }
        if (al_fi.bits <= 8) {
            ctx.line("w.patch_u8(_len_pos, int(" + raw_length + "))");
        } else if (al_fi.bits <= 16) {
            ctx.line("w.patch_u16(_len_pos, int(" + raw_length + "), " + (be ? "True" : "False") + ")");
        } else {
            ctx.line("w.patch_u32(_len_pos, int(" + raw_length + "), " + (be ? "True" : "False") + ")");
        }
    }

    ctx.dedent();
    ctx.line();

    // encode_bytes()
    ctx.line("def encode_bytes(self) -> bytes:");
    ctx.indent();
    ctx.line("w = BitWriter()");
    ctx.line("self.encode(w)");
    ctx.line("return w.to_bytes()");
    ctx.dedent();
    ctx.line();

    // decode() static method
    ctx.line("@staticmethod");
    ctx.line("def decode(r: 'BitReader') -> '" + cn + "':");
    ctx.indent();
    ctx.line("result = " + cn + "()");

    // Read header fields
    for (const auto& hf : header_fields) {
        std::string m = "result." + hf.py_name;
        if (hf.fi.is_enum) {
            ctx.line(m + " = " + hf.fi.py_type + ".decode(r)");
        } else if (hf.fi.is_bool) {
            ctx.line(m + " = (" + py_read_expr(hf.fi) + " != 0)");
        } else {
            ctx.line(m + " = " + py_read_expr(hf.fi));
        }
    }

    // Compute header/footer sizes for payload bounding
    int header_bits = 0;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = py_resolve_field(*f, index);
            header_bits += fi.bits;
        } else if (auto* r = std::get_if<model::Reserved>(&child)) {
            header_bits += r->bits;
        }
    }
    int footer_bits = 0;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = py_resolve_field(*f, index);
            footer_bits += fi.bits;
        } else if (auto* r = std::get_if<model::Reserved>(&child)) {
            footer_bits += r->bits;
        }
    }
    int header_bytes = (header_bits + 7) / 8;
    int footer_bytes = (footer_bits + 7) / 8;

    // Build payload sub-reader when bounded by length field
    bool use_sub_reader = false;
    if (!si.length_field_name.empty() && si.count_field_name.empty()) {
        std::string len_member = "result." + py_field(si.length_field_name);
        std::string raw_val = "int(" + len_member + ")";
        std::string total_expr = raw_val;
        if (si.frame_length_modifier.has_modifier()) {
            switch (si.frame_length_modifier.op) {
                case model::ArithOp::Add:
                    total_expr = "(" + raw_val + " - " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Sub:
                    total_expr = "(" + raw_val + " + " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Mul:
                    total_expr = "(" + raw_val + " // " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Div:
                    total_expr = "(" + raw_val + " * " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                default: break;
            }
        }
        std::string size_expr;
        if (si.frame_length_field_ref.empty()) {
            int overhead = header_bytes + footer_bytes;
            size_expr = total_expr + " - " + std::to_string(overhead);
        } else {
            if (footer_bytes > 0) {
                size_expr = total_expr + " - " + std::to_string(footer_bytes);
            } else {
                size_expr = total_expr;
            }
        }
        ctx.line("_payload_r = r.sub_reader(" + size_expr + ")");
        use_sub_reader = true;
    } else if (footer_bits > 0 && si.length_field_name.empty() && si.count_field_name.empty()) {
        ctx.line("_payload_r = r.sub_reader(r.remaining_bytes() - " + std::to_string(footer_bytes) + ")");
        use_sub_reader = true;
    } else if (si.payload_length_from) {
        ctx.line("_payload_r = r.sub_reader(int(" + py_expr(*si.payload_length_from, "result") + "))");
        use_sub_reader = true;
    }

    std::string reader_name = use_sub_reader ? "_payload_r" : "r";
    std::string id_member = "result." + py_field(si.id_field_name);

    if (si.payload_is_array) {
        if (!si.count_field_name.empty()) {
            ctx.line("for _i in range(int(result." + py_field(si.count_field_name) + ")):");
        } else {
            ctx.line("while " + reader_name + ".remaining_bytes() > 0:");
        }
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            if (lt.send_only) continue;
            std::string leaf_class = py_class(lt.name);
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            ctx.line(std::string(first ? "if " : "elif ") + id_member + " == " + id_val + ":");
            ctx.indent();
            ctx.line("_msg = " + leaf_class + ".decode(" + reader_name + ")");
            // Copy header fields into decoded message (matching C++/Java)
            for (const auto& child : frame.header_fields) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    ctx.line("_msg." + py_field(f->name) + " = result." + py_field(f->name));
                }
            }
            ctx.line("result.payload.append(_msg)");
            ctx.dedent();
            first = false;
        }
        ctx.dedent();
    } else {
        // Single payload dispatch
        std::map<std::string, std::vector<const analyzer::LeafTypeInfo*>> id_groups;
        for (const auto& lt : si.leaf_types) {
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            id_groups[id_val].push_back(&lt);
        }
        bool first = true;
        for (const auto& [id_val, leaves] : id_groups) {
            const analyzer::LeafTypeInfo* decode_leaf = leaves[0];
            for (const auto* lt : leaves) {
                if (!lt->send_only) { decode_leaf = lt; break; }
            }
            std::string leaf_class = py_class(decode_leaf->name);
            ctx.line(std::string(first ? "if " : "elif ") + id_member + " == " + id_val + ":");
            ctx.indent();
            ctx.line("result.payload = " + leaf_class + ".decode(" + reader_name + ")");
            // Copy header fields into decoded message (matching C++/Java)
            for (const auto& child : frame.header_fields) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    ctx.line("result.payload." + py_field(f->name) + " = result." + py_field(f->name));
                }
            }
            ctx.dedent();
            first = false;
        }
        if (!first) {
            ctx.line("else:");
            ctx.indent();
            ctx.line("raise DecodeError(f'unknown message id: {" + id_member + "}')");
            ctx.dedent();
        }
    }

    // Read footer fields
    for (const auto& ff : footer_fields) {
        std::string m = "result." + ff.py_name;
        if (ff.fi.is_enum) {
            ctx.line(m + " = " + ff.fi.py_type + ".decode(r)");
        } else if (ff.fi.is_bool) {
            ctx.line(m + " = (" + py_read_expr(ff.fi) + " != 0)");
        } else {
            ctx.line(m + " = " + py_read_expr(ff.fi));
        }
    }

    // Copy footer fields into decoded payload messages (matching C++)
    if (!footer_fields.empty()) {
        if (si.payload_is_array) {
            ctx.line("for _item in result.payload:");
            ctx.indent();
            for (const auto& ff : footer_fields) {
                ctx.line("_item." + ff.py_name + " = result." + ff.py_name);
            }
            ctx.dedent();
        } else {
            ctx.line("if result.payload is not None:");
            ctx.indent();
            for (const auto& ff : footer_fields) {
                ctx.line("result.payload." + ff.py_name + " = result." + ff.py_name);
            }
            ctx.dedent();
        }
    }

    ctx.line("return result");
    ctx.dedent();
    ctx.line();

    // decode_bytes()
    ctx.line("@staticmethod");
    ctx.line("def decode_bytes(data: bytes) -> '" + cn + "':");
    ctx.indent();
    ctx.line("return " + cn + ".decode(BitReader(data))");
    ctx.dedent();
    ctx.line();

    // __repr__
    ctx.line("def __repr__(self) -> str:");
    ctx.indent();
    std::string repr_parts;
    for (const auto& hf : header_fields) {
        if (!repr_parts.empty()) repr_parts += ", ";
        repr_parts += hf.py_name + "={self." + hf.py_name + "}";
    }
    if (!repr_parts.empty()) repr_parts += ", ";
    repr_parts += "payload={self.payload}";
    for (const auto& ff : footer_fields) {
        repr_parts += ", " + ff.py_name + "={self." + ff.py_name + "}";
    }
    ctx.line("return f'" + cn + "(" + repr_parts + ")'");
    ctx.dedent();

    ctx.dedent(); // end class
    ctx.line();
}

std::string generate_py_messages(const model::Protocol& protocol,
                                  const analyzer::TypeIndex& index,
                                  const std::vector<analyzer::SessionInfo>& sessions,
                                  const analyzer::WireSizeInfo& sizes) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from .bit_io import BitReader, BitWriter, DecodeError, EncodeError, ConstraintError");
    ctx.line("from .types import *");
    ctx.line("from .structs import *");
    ctx.line("from .constants import Constants");

    std::unordered_map<std::string, uint64_t> tid_map;
    for (const auto& si : sessions)
        for (const auto& lt : si.leaf_types)
            tid_map[lt.name] = lt.type_id;

    // Collect frame header/footer fields that need to be injected into message classes
    std::unordered_map<std::string, std::vector<PyFieldDef>> msg_frame_fields;
    for (const auto& si : sessions) {
        if (!si.is_frame_based || !si.frame) continue;
        std::vector<PyFieldDef> frame_fields;
        for (const auto& child : si.frame->header_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                auto fi = py_resolve_field(*f, index);
                PyFieldDef fd;
                fd.name = py_field(f->name);
                fd.py_type = fi.py_type;
                if (fi.is_string) fd.default_val = "''";
                else if (fi.is_bytes) fd.default_val = "b''";
                else if (fi.is_bool) fd.default_val = "False";
                else if (fi.is_float) fd.default_val = "0.0";
                else fd.default_val = "0";
                frame_fields.push_back(fd);
            }
        }
        for (const auto& child : si.frame->footer_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                auto fi = py_resolve_field(*f, index);
                PyFieldDef fd;
                fd.name = py_field(f->name);
                fd.py_type = fi.py_type;
                if (fi.is_string) fd.default_val = "''";
                else if (fi.is_bytes) fd.default_val = "b''";
                else if (fi.is_bool) fd.default_val = "False";
                else if (fi.is_float) fd.default_val = "0.0";
                else fd.default_val = "0";
                frame_fields.push_back(fd);
            }
        }
        if (!frame_fields.empty()) {
            for (const auto& lt : si.leaf_types) {
                msg_frame_fields[lt.name] = frame_fields;
            }
        }
    }

    for (const auto& md : protocol.messages) {
        PyOuterScopeMap scope_map;
        PyInlineNameMap name_map;
        emit_py_inline_types(ctx, md.children, index, tid_map, scope_map, md.name, name_map);
        auto mff_it = msg_frame_fields.find(md.name);
        std::vector<PyFieldDef> extra_fields = (mff_it != msg_frame_fields.end()) ? mff_it->second : std::vector<PyFieldDef>{};
        emit_py_class(ctx, md.name, md.children, index, tid_map, md.id, scope_map, name_map, {}, extra_fields, md.doc, &sizes);
    }

    // Generate frame classes (Packet, Frame, etc.)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            emit_py_frame_class(ctx, si, index);
        }
    }

    ctx.line();
    return ctx.str();
}

// ============================================================================
// sessions.py + protocol.py + __init__.py
// ============================================================================

std::string generate_py_sessions(const model::Protocol& protocol,
                                  const analyzer::TypeIndex& index,
                                  const std::vector<analyzer::SessionInfo>& sessions) {
    (void)index;
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("import time");
    ctx.line("from typing import Any, Optional");
    ctx.line("from .bit_io import BitReader, BitWriter, DecodeError");
    ctx.line("from .messages import *");
    ctx.line("from .structs import *");
    ctx.line("from .constants import Constants");
    ctx.line();

    for (const auto& si : sessions) {
        if (!si.is_frame_based || !si.frame) continue;
        std::string frame_class = py_class(si.frame->name);
        std::string sc = frame_class + "Session";

        // Merge config fields (frame-level + message-level, deduplicated)
        std::map<std::string, analyzer::ConfigField> all_config;
        for (const auto& cf : si.config_fields)
            all_config.try_emplace(cf.key, cf);
        for (const auto& lt : si.leaf_types)
            for (const auto& cf : lt.config_fields)
                all_config.try_emplace(cf.key, cf);
        bool has_config = !all_config.empty();

        // Check if any leaf has auto-increment fields (reserved for future use)
        for (const auto& lt : si.leaf_types)
            if (!lt.auto_fields.empty()) { break; }

        ctx.line();
        ctx.line("class " + sc + ":");
        ctx.indent();

        // LEAF_TYPES dict
        ctx.line("LEAF_TYPES = {");
        ctx.indent();
        for (const auto& lt : si.leaf_types)
            ctx.line(py_hex64(lt.type_id) + ": '" + lt.name + "',");
        ctx.dedent();
        ctx.line("}");
        ctx.line();

        // RECEIVE_ONLY set
        {
            bool has_recv = false;
            for (const auto& lt : si.leaf_types)
                if (lt.receive_only) { has_recv = true; break; }
            if (has_recv) {
                ctx.line("RECEIVE_ONLY = {");
                ctx.indent();
                for (const auto& lt : si.leaf_types)
                    if (lt.receive_only)
                        ctx.line(py_hex64(lt.type_id) + ",  # " + lt.name);
                ctx.dedent();
                ctx.line("}");
                ctx.line();
            }
        }

        // __init__
        ctx.line("def __init__(self" + std::string(has_config ? ", config: dict | None = None" : "") + ") -> None:");
        ctx.indent();
        ctx.line("self._seq = 0");
        if (has_config) {
            ctx.line("self._config = config or {}");
        }
        ctx.dedent();
        ctx.line();

        // type_name
        ctx.line("def type_name(self, tid: int) -> str:");
        ctx.indent();
        ctx.line("return self.LEAF_TYPES.get(tid, 'unknown')");
        ctx.dedent();
        ctx.line();

        // leaf_type_ids
        ctx.line("def leaf_type_ids(self) -> list[int]:");
        ctx.indent();
        ctx.line("return list(self.LEAF_TYPES.keys())");
        ctx.dedent();
        ctx.line();

        // protocol_name
        ctx.line("def protocol_name(self) -> str:");
        ctx.indent();
        ctx.line("return '" + protocol.name + "'");
        ctx.dedent();
        ctx.line();

        // is_receive_only
        {
            bool has_recv = false;
            for (const auto& lt : si.leaf_types)
                if (lt.receive_only) { has_recv = true; break; }
            ctx.line("def is_receive_only(self, tid: int) -> bool:");
            ctx.indent();
            if (has_recv) {
                ctx.line("return tid in self.RECEIVE_ONLY");
            } else {
                ctx.line("return False");
            }
            ctx.dedent();
            ctx.line();
        }

        // sync_pattern
        ctx.line("def sync_pattern(self) -> bytes:");
        ctx.indent();
        if (!si.sync_pattern.empty()) {
            std::string bytes_lit = "b'";
            for (uint8_t b : si.sync_pattern) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02x", b);
                bytes_lit += buf;
            }
            bytes_lit += "'";
            ctx.line("return " + bytes_lit);
        } else {
            ctx.line("return b''");
        }
        ctx.dedent();
        ctx.line();

        // min_frame_header_size
        ctx.line("def min_frame_header_size(self) -> int:");
        ctx.indent();
        ctx.line("return " + std::to_string(si.min_frame_header_size));
        ctx.dedent();
        ctx.line();

        // extract_frame_length
        ctx.line("def extract_frame_length(self, header: bytes) -> int:");
        ctx.indent();
        if (si.frame_length_bits > 0) {
            ctx.line("if len(header) < " + std::to_string(si.min_frame_header_size) + ": return 0");
            ctx.line("r = BitReader(header)");
            if (si.frame_length_bit_offset > 0) {
                ctx.line("r.skip_bits(" + std::to_string(si.frame_length_bit_offset) + ")");
            }
            bool big = (si.frame_length_endian == model::Endian::Big);
            std::string endian_arg = big ? "" : "False";
            std::string read_call;
            if (si.frame_length_bits <= 8) {
                read_call = "r.read_u8()";
            } else if (si.frame_length_bits <= 16) {
                read_call = "r.read_u16(" + endian_arg + ")";
            } else if (si.frame_length_bits <= 32) {
                read_call = "r.read_u32(" + endian_arg + ")";
            } else {
                read_call = "r.read_u64(" + endian_arg + ")";
            }
            ctx.line("val = " + read_call);
            if (!si.frame_length_field_ref.empty() && si.frame_length_field_ref == "payload") {
                size_t overhead = si.min_frame_header_size + si.frame_footer_size;
                // Reverse arithmetic modifier
                if (si.frame_length_modifier.has_modifier()) {
                    std::string val_expr = "val";
                    // Reverse: Add↔Sub, Mul↔Div
                    switch (si.frame_length_modifier.op) {
                        case model::ArithOp::Add:
                            val_expr = "(val - " + std::to_string(si.frame_length_modifier.literal) + ")";
                            break;
                        case model::ArithOp::Sub:
                            val_expr = "(val + " + std::to_string(si.frame_length_modifier.literal) + ")";
                            break;
                        case model::ArithOp::Mul:
                            val_expr = "(val // " + std::to_string(si.frame_length_modifier.literal) + ")";
                            break;
                        case model::ArithOp::Div:
                            val_expr = "(val * " + std::to_string(si.frame_length_modifier.literal) + ")";
                            break;
                        default: break;
                    }
                    ctx.line("return " + val_expr + " + " + std::to_string(overhead));
                } else {
                    ctx.line("return val + " + std::to_string(overhead));
                }
            } else if (si.frame_length_modifier.has_modifier()) {
                std::string val_expr = "val";
                switch (si.frame_length_modifier.op) {
                    case model::ArithOp::Add:
                        val_expr = "(val - " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Sub:
                        val_expr = "(val + " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Mul:
                        val_expr = "(val // " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Div:
                        val_expr = "(val * " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    default: break;
                }
                ctx.line("return " + val_expr);
            } else {
                ctx.line("return val");
            }
        } else {
            ctx.line("return len(header)");
        }
        ctx.dedent();
        ctx.line();

        // decode_frame
        ctx.line("def decode_frame(self, data: bytes) -> list[dict]:");
        ctx.indent();
        ctx.line("try:");
        ctx.indent();
        ctx.line("frame = " + frame_class + ".decode_bytes(data)");
        ctx.dedent();
        ctx.line("except (DecodeError, ConstraintError):");
        ctx.indent();
        ctx.line("return []");
        ctx.dedent();
        if (si.payload_is_array) {
            ctx.line("messages = []");
            ctx.line("for item in frame.payload:");
            ctx.indent();
            ctx.line("dm = {'type_id': item.TYPE_ID, 'type_name': item.TYPE_NAME, 'payload': item, 'raw': data}");
            ctx.line("messages.append(dm)");
            ctx.dedent();
        } else {
            ctx.line("msg = frame.payload");
            ctx.line("messages = [{'type_id': msg.TYPE_ID, 'type_name': msg.TYPE_NAME, 'payload': msg, 'raw': data}]");
        }
        // Warn when receiving send-only message types
        {
            bool has_send_only = false;
            for (const auto& lt : si.leaf_types)
                if (lt.send_only) { has_send_only = true; break; }
            if (has_send_only) {
                ctx.line("import sys");
                ctx.line("for dm in messages:");
                ctx.indent();
                ctx.line("tid = dm['type_id']");
                for (const auto& lt : si.leaf_types) {
                    if (lt.send_only) {
                        ctx.line("if tid == " + py_hex64(lt.type_id) + ":");
                        ctx.indent();
                        ctx.line("print(f\"WARNING: Received send-only message type '" + lt.name + "'\", file=sys.stderr)");
                        ctx.dedent();
                    }
                }
                ctx.dedent();
            }
        }
        ctx.line("return messages");
        ctx.dedent();
        ctx.line();

        // encode_wrap
        ctx.line("def encode_wrap(self, type_id: int, payload) -> Optional[dict]:");
        ctx.indent();
        {
            bool first_branch = true;
            for (const auto& lt : si.leaf_types) {
                std::string leaf_class = py_class(lt.name);
                std::string prefix = first_branch ? "if" : "elif";
                first_branch = false;
                ctx.line(prefix + " type_id == " + py_hex64(lt.type_id) + ":");
                ctx.indent();

                ctx.line("_auto_fields = []");
                // Set message-level config fields on a copy before wrapping
                if (!lt.config_fields.empty()) {
                    for (const auto& cf : lt.config_fields) {
                        std::string cfg_key = py_snake(cf.key);
                        std::string field = py_field(cf.field_name);
                        ctx.line("payload." + field + " = self._config.get('" + cfg_key + "', 0)");
                        ctx.line("_auto_fields.append(('" + cf.field_name + "', str(payload." + field + ")))");
                    }
                }

                ctx.line("frame = " + frame_class + ".wrap(payload)");

                // Set frame-level config fields
                for (const auto& cf : si.config_fields) {
                    std::string cfg_key = py_snake(cf.key);
                    std::string field = py_field(cf.field_name);
                    ctx.line("frame." + field + " = self._config.get('" + cfg_key + "', 0)");
                    ctx.line("_auto_fields.append(('" + cf.field_name + "', str(frame." + field + ")))");
                }

                // Set auto-increment fields
                for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                    int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = py_field(lt.auto_fields[ai]);
                    ctx.line("_seq_val = self._seq & " + std::to_string(mask_val));
                    ctx.line("frame." + field + " = _seq_val");
                    ctx.line("self._seq += 1");
                    ctx.line("_auto_fields.append(('" + lt.auto_fields[ai] + "', str(_seq_val)))");
                }

                // Set auto-timestamp fields
                for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                    int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = py_field(lt.timestamp_fields[ti]);
                    ctx.line("_ts_val = int(time.time() * 1000) & " + std::to_string(mask_val));
                    ctx.line("frame." + field + " = _ts_val");
                    ctx.line("_auto_fields.append(('" + lt.timestamp_fields[ti] + "', str(_ts_val)))");
                }

                // Record auto-id field
                if (!si.id_field_name.empty()) {
                    ctx.line("_auto_fields.append(('" + si.id_field_name + "', str(" + leaf_class + ".ID_VALUE)))");
                }

                ctx.line("data = frame.encode_bytes()");
                ctx.line("return {'bytes': data, 'type_id': type_id, 'auto_fields': _auto_fields}");
                ctx.dedent();
            }
        }
        ctx.line("else:");
        ctx.indent();
        ctx.line("return None");
        ctx.dedent();
        ctx.dedent();
        ctx.line();

        // encode_batch — only for array-payload sessions
        if (si.payload_is_array) {
            ctx.line("def encode_batch(self, type_id: int, payloads: list) -> Optional[dict]:");
            ctx.indent();
            {
                bool first = true;
                for (const auto& lt : si.leaf_types) {
                    std::string leaf_class = py_class(lt.name);
                    std::string prefix = first ? "if" : "elif";
                    first = false;
                    ctx.line(prefix + " type_id == " + py_hex64(lt.type_id) + ":");
                    ctx.indent();
                    ctx.line("frame = " + frame_class + "()");

                    // Set constraint-equals header fields (e.g., sync words)
                    if (si.frame) {
                        for (const auto& hc : si.frame->header_fields) {
                            if (auto* f = std::get_if<model::Field>(&hc)) {
                                if (f->constraint && f->constraint->equals) {
                                    ctx.line("frame." + py_field(f->name) + " = " + py_qualify_const(*f->constraint->equals));
                                }
                            }
                        }
                    }

                    // Set id field from message's ID_VALUE
                    if (!si.id_field_name.empty()) {
                        ctx.line("frame." + py_field(si.id_field_name) + " = " + leaf_class + ".ID_VALUE");
                    }

                    // Set message-level config fields on each payload
                    if (!lt.config_fields.empty()) {
                        ctx.line("for _item in payloads:");
                        ctx.indent();
                        for (const auto& cf : lt.config_fields) {
                            std::string cfg_key = py_snake(cf.key);
                            std::string field = py_field(cf.field_name);
                            ctx.line("_item." + field + " = self._config.get('" + cfg_key + "', 0)");
                        }
                        ctx.dedent();
                    }

                    ctx.line("_auto_fields = []");

                    // Set auto-increment fields
                    for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                        int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                        uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                        std::string field = py_field(lt.auto_fields[ai]);
                        ctx.line("_seq_val = self._seq.get(type_id, 0) & " + std::to_string(mask_val));
                        ctx.line("frame." + field + " = _seq_val");
                        ctx.line("self._seq[type_id] = self._seq.get(type_id, 0) + 1");
                        ctx.line("_auto_fields.append(('" + lt.auto_fields[ai] + "', str(_seq_val)))");
                    }

                    // Set auto-timestamp fields
                    for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                        int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                        uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                        std::string field = py_field(lt.timestamp_fields[ti]);
                        ctx.line("_ts_val = int(time.time() * 1000) & " + std::to_string(mask_val));
                        ctx.line("frame." + field + " = _ts_val");
                        ctx.line("_auto_fields.append(('" + lt.timestamp_fields[ti] + "', str(_ts_val)))");
                    }

                    // Set frame-level config fields
                    for (const auto& cf : si.config_fields) {
                        std::string cfg_key = py_snake(cf.key);
                        std::string field = py_field(cf.field_name);
                        ctx.line("frame." + field + " = self._config.get('" + cfg_key + "', 0)");
                        ctx.line("_auto_fields.append(('" + cf.field_name + "', str(frame." + field + ")))");
                    }

                    // Record auto-id field
                    if (!si.id_field_name.empty()) {
                        ctx.line("_auto_fields.append(('" + si.id_field_name + "', str(" + leaf_class + ".ID_VALUE)))");
                    }

                    ctx.line("frame.payload = payloads");
                    ctx.line("data = frame.encode_bytes()");
                    ctx.line("return {'bytes': data, 'type_id': type_id, 'auto_fields': _auto_fields}");
                    ctx.dedent();
                }
            }
            ctx.line("else:");
            ctx.indent();
            ctx.line("return None");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        }

        // format_message
        ctx.line("def format_message(self, type_id: int, payload) -> str:");
        ctx.indent();
        {
            bool first = true;
            for (const auto& lt : si.leaf_types) {
                std::string prefix = first ? "if" : "elif";
                first = false;
                ctx.line(prefix + " type_id == " + py_hex64(lt.type_id) + ":");
                ctx.indent();
                ctx.line("return repr(payload)");
                ctx.dedent();
            }
        }
        ctx.line("return ''");
        ctx.dedent();
        ctx.line();

        // format_outbound
        ctx.line("def format_outbound(self, type_id: int, payload, auto_fields: list = None) -> str:");
        ctx.indent();
        {
            bool first = true;
            for (const auto& lt : si.leaf_types) {
                std::string prefix = first ? "if" : "elif";
                first = false;
                ctx.line(prefix + " type_id == " + py_hex64(lt.type_id) + ":");
                ctx.indent();
                ctx.line("return repr(payload)");
                ctx.dedent();
            }
        }
        ctx.line("return ''");
        ctx.dedent();
        ctx.line();

        // reset
        ctx.line("def reset(self) -> None:");
        ctx.indent();
        ctx.line("self._seq = 0");
        ctx.dedent();

        ctx.dedent();
    }
    ctx.line();
    return ctx.str();
}

std::string generate_py_protocol(const model::Protocol& protocol,
                                  const std::vector<analyzer::SessionInfo>& sessions) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from typing import Optional, NamedTuple");
    ctx.line();
    ctx.line();
    ctx.line("class TypeInfo(NamedTuple):");
    ctx.indent();
    ctx.line("type_id: int");
    ctx.line("type_name: str");
    ctx.dedent();
    ctx.line();
    ctx.line();
    ctx.line("class ProtocolDescriptor:");
    ctx.indent();
    ctx.line("NAME = '" + protocol.name + "'");
    ctx.line("VERSION = '" + protocol.version + "'");
    ctx.line();
    ctx.line("TYPES = [");
    ctx.indent();
    std::set<uint64_t> seen;
    for (const auto& si : sessions)
        for (const auto& lt : si.leaf_types)
            if (!seen.count(lt.type_id)) {
                seen.insert(lt.type_id);
                ctx.line("TypeInfo(" + py_hex64(lt.type_id) + ", '" + lt.name + "'),");
            }
    ctx.dedent();
    ctx.line("]");
    ctx.line();
    ctx.line("@staticmethod");
    ctx.line("def find_by_id(tid: int) -> Optional[TypeInfo]:");
    ctx.indent();
    ctx.line("return next((t for t in ProtocolDescriptor.TYPES if t.type_id == tid), None)");
    ctx.dedent();
    ctx.line();
    ctx.line("@staticmethod");
    ctx.line("def find_by_name(name: str) -> Optional[TypeInfo]:");
    ctx.indent();
    ctx.line("return next((t for t in ProtocolDescriptor.TYPES if t.type_name == name), None)");
    ctx.dedent();
    ctx.dedent();
    ctx.line();
    return ctx.str();
}

std::string generate_py_init(const model::Protocol& protocol) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT");
    ctx.line("Protocol: " + protocol.name + " v" + protocol.version + "\"\"\"");
    ctx.line("from .bit_io import BitReader, BitWriter, ConduitError, DecodeError, EncodeError, ConstraintError");
    ctx.line("from .constants import Constants");
    ctx.line("from .types import *");
    ctx.line("from .structs import *");
    ctx.line("from .messages import *");
    ctx.line("from .sessions import *");
    ctx.line("from .protocol import ProtocolDescriptor, TypeInfo");
    ctx.line();
    return ctx.str();
}

} // anonymous namespace

// ============================================================================
// PythonBackend::generate
// ============================================================================

bool PythonBackend::generate(
    const model::Protocol& protocol,
    const analyzer::TypeIndex& index,
    const analyzer::WireSizeInfo& sizes,
    const std::vector<analyzer::SessionInfo>& sessions,
    const std::string& ns,
    const std::filesystem::path& output_dir) {

    (void)ns;

    bool ok = true;
    ok &= write_file(output_dir / "bit_io.py", generate_bit_io());
    ok &= write_file(output_dir / "constants.py", generate_py_constants(protocol));
    ok &= write_file(output_dir / "types.py", generate_py_types(protocol, index));
    ok &= write_file(output_dir / "structs.py", generate_py_structs(protocol, index, sizes));
    ok &= write_file(output_dir / "messages.py", generate_py_messages(protocol, index, sessions, sizes));
    ok &= write_file(output_dir / "sessions.py", generate_py_sessions(protocol, index, sessions));
    ok &= write_file(output_dir / "protocol.py", generate_py_protocol(protocol, sessions));
    ok &= write_file(output_dir / "__init__.py", generate_py_init(protocol));

    if (ok) Logger::info("generated 8 Python files in " + output_dir.string());
    return ok;
}

} // namespace bgen::codegen
