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
    return pi;
}

// ============================================================================
// Outer-scope analysis for nested choices (Python)
// ============================================================================

struct PyOuterParam {
    std::string bmdl_name;
};

using PyOuterScopeMap = std::unordered_map<std::string, std::vector<PyOuterParam>>;
using PyOuterContext = std::unordered_map<std::string, std::string>;

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
            if constexpr (std::is_same_v<T, model::Field>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
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
    if (e.op == model::ExprOp::FieldRef && !outer_ctx.empty()) {
        auto dot = e.name.find('.');
        std::string root = (dot != std::string::npos) ? e.name.substr(0, dot) : e.name;
        auto it = outer_ctx.find(root);
        if (it != outer_ctx.end()) {
            return it->second;
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


# EBCDIC <-> ASCII conversion tables (Code Page 037)
_EBCDIC_TO_ASCII = bytearray(256)
_ASCII_TO_EBCDIC = bytearray(256)
_ebcdic_pairs = [
    (0x40, 0x20), (0x4B, 0x2E), (0x4C, 0x3C), (0x4D, 0x28), (0x4E, 0x2B), (0x4F, 0x7C),
    (0x50, 0x26), (0x5A, 0x21), (0x5B, 0x24), (0x5C, 0x2A), (0x5D, 0x29), (0x5E, 0x3B),
    (0x60, 0x2D), (0x61, 0x2F), (0x6B, 0x2C), (0x6C, 0x25), (0x6D, 0x5F), (0x6E, 0x3E),
    (0x6F, 0x3F), (0x7A, 0x3A), (0x7B, 0x23), (0x7C, 0x40), (0x7D, 0x27), (0x7E, 0x3D), (0x7F, 0x22),
]
for _i in range(9): _ebcdic_pairs.append((0xC1 + _i, 0x41 + _i))
for _i in range(9): _ebcdic_pairs.append((0xD1 + _i, 0x4A + _i))
for _i in range(8): _ebcdic_pairs.append((0xE2 + _i, 0x53 + _i))
for _i in range(9): _ebcdic_pairs.append((0x81 + _i, 0x61 + _i))
for _i in range(9): _ebcdic_pairs.append((0x91 + _i, 0x6A + _i))
for _i in range(8): _ebcdic_pairs.append((0xA2 + _i, 0x73 + _i))
for _i in range(10): _ebcdic_pairs.append((0xF0 + _i, 0x30 + _i))
for _e, _a in _ebcdic_pairs:
    _EBCDIC_TO_ASCII[_e] = _a
    _ASCII_TO_EBCDIC[_a] = _e


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
            b = bytearray((c + 0x40 if 0 < c < 32 else c) for c in (v & 0x3F for v in b))
        return bytes(b).decode('latin-1')

    def read_packed_chars(self, count: int, char_bits: int) -> str:
        chars = []
        for _ in range(count):
            c = self.read_bits(char_bits)
            chars.append(chr(0 if c == 0 else (c + 0x40 if c < 32 else c)))
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
            b = bytearray((c - 0x40 if c >= 0x40 else c) for c in b)
        for i in range(length):
            self.write_u8(b[i] if i < len(b) else pad)

    def write_packed_chars(self, s: str, count: int, char_bits: int) -> None:
        for i in range(count):
            c = ord(s[i]) if i < len(s) else 0
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
            if (!t.doc.empty()) ctx.line("\"\"\"" + t.doc + "\"\"\"");
            for (const auto& ev : t.enum_values)
                ctx.line(py_enum_val(ev.name) + " = " + std::to_string(ev.id));
            ctx.line();
            ctx.line("@staticmethod");
            ctx.line("def decode(r: BitReader) -> '" + name + "':");
            ctx.indent();
            ctx.line("raw = r.read_bits(" + std::to_string(t.bits) + ")");
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
            ctx.line("w.write_bits(self.value, " + std::to_string(t.bits) + ")");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        } else if (is_flags) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            if (!t.doc.empty()) ctx.line("\"\"\"" + t.doc + "\"\"\"");
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
            ctx.line("return " + name + "(r.read_bits(" + std::to_string(t.bits) + "))");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line("w.write_bits(self._raw, " + std::to_string(t.bits) + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        } else if (has_scale) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            if (!t.doc.empty()) ctx.line("\"\"\"" + t.doc + "\"\"\"");
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
            ctx.line("raw = r.read_" + std::string(is_signed ? "signed_bits" : "bits") +
                     "(" + std::to_string(t.bits) + ")");
            if (t.constraint && t.constraint->max)
                ctx.line("if raw > " + *t.constraint->max + ": raise ConstraintError('" + name + " exceeds max')");
            if (t.constraint && t.constraint->min && (*t.constraint->min != "0" || is_signed))
                ctx.line("if raw < " + *t.constraint->min + ": raise ConstraintError('" + name + " below min')");
            ctx.line("return " + name + "(raw)");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line("w.write_" + std::string(is_signed ? "signed_bits" : "bits") +
                     "(self._raw, " + std::to_string(t.bits) + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        } else if (is_string && t.length) {
            std::string name = py_class(t.name);
            ctx.line();
            ctx.line("class " + name + ":");
            ctx.indent();
            ctx.line("__slots__ = ('_value',)");
            ctx.line("WIRE_SIZE = " + std::to_string(*t.length));
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
            ctx.line("s = r.read_string(" + std::to_string(*t.length) + ")");
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
            int pad = (t.padding == model::StringPadding::Space) ? 0x20 : 0;
            ctx.line("w.write_string(self._value, " + std::to_string(*t.length) + ", " + std::to_string(pad) + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._value == o._value");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        } else if (t.constraint) {
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
            ctx.line("raw = r.read_" + std::string(is_signed ? "signed_bits" : "bits") +
                     "(" + std::to_string(t.bits) + ")");
            if (t.constraint->max)
                ctx.line("if raw > " + *t.constraint->max + ": raise ConstraintError('" + name + " exceeds max')");
            if (t.constraint->min && (*t.constraint->min != "0" || is_signed))
                ctx.line("if raw < " + *t.constraint->min + ": raise ConstraintError('" + name + " below min')");
            ctx.line("return " + name + "(raw)");
            ctx.dedent();
            ctx.line();
            ctx.line("def encode(self, w: BitWriter) -> None:");
            ctx.indent();
            ctx.line("w.write_" + std::string(is_signed ? "signed_bits" : "bits") +
                     "(self._raw, " + std::to_string(t.bits) + ")");
            ctx.dedent();
            ctx.line();
            ctx.line("def __eq__(self, o: object) -> bool:");
            ctx.indent();
            ctx.line("return isinstance(o, " + name + ") and self._raw == o._raw");
            ctx.dedent();
            ctx.dedent();
            ctx.line();
        }
    }
    return ctx.str();
}

// ============================================================================
// Field read/write helpers
// ============================================================================

std::string py_read_expr(const PyFieldInfo& fi) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "r.read_bcd(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "r.read_bcd_signed(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "r.read_sign_magnitude(" + std::to_string(fi.bits) + ")";
    std::string be = (fi.endian == model::Endian::Big) ? "True" : "False";
    if (fi.is_float) return (fi.bits <= 32) ? "r.read_f32(" + be + ")" : "r.read_f64(" + be + ")";
    if (fi.bits == 8 && !fi.is_signed) return "r.read_u8()";
    if (fi.bits == 16 && !fi.is_signed) return "r.read_u16(" + be + ")";
    if (fi.bits == 32 && !fi.is_signed) return "r.read_u32(" + be + ")";
    if (fi.bits == 64 && !fi.is_signed) return "r.read_u64(" + be + ")";
    if (fi.is_signed) return "r.read_signed_bits(" + std::to_string(fi.bits) + ")";
    return "r.read_bits(" + std::to_string(fi.bits) + ")";
}

std::string py_write_stmt(const std::string& val, const PyFieldInfo& fi) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "w.write_bcd(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "w.write_bcd_signed(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "w.write_sign_magnitude(" + val + ", " + std::to_string(fi.bits) + ")";
    std::string be = (fi.endian == model::Endian::Big) ? "True" : "False";
    if (fi.is_float) return (fi.bits <= 32) ? "w.write_f32(" + val + ", " + be + ")" : "w.write_f64(" + val + ", " + be + ")";
    // bool must be checked before bit-width checks for correct encoding
    if (fi.is_bool) return "w.write_bits(1 if " + val + " else 0, " + std::to_string(fi.bits) + ")";
    if (fi.bits == 8 && !fi.is_signed) return "w.write_u8(" + val + ")";
    if (fi.bits == 16 && !fi.is_signed) return "w.write_u16(" + val + ", " + be + ")";
    if (fi.bits == 32 && !fi.is_signed) return "w.write_u32(" + val + ", " + be + ")";
    if (fi.bits == 64 && !fi.is_signed) return "w.write_u64(" + val + ", " + be + ")";
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
void emit_py_field_trim(EmitContext& ctx, const std::string& m, const model::Field& f) {
    auto eff_trim = f.trim.value_or(model::StringTrim::Right);
    std::string ch = (f.padding && *f.padding == model::StringPadding::Space) ? "' '" : "'\\x00'";
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

void emit_py_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.py_type + ".decode(r)"); return; }
    if (fi.is_string) {
        bool has_enc = py_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", " + py_encoding_const(f)) : "";
        if (f.char_bits && f.length) {
            // Packed character decode (e.g., ICAO 6-bit chars)
            ctx.line(m + " = r.read_packed_chars(" + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ")");
            emit_py_field_trim(ctx, m, f);
        } else if (f.terminated) {
            // Terminated string decode
            int max_len = f.max_length ? *f.max_length : 65535;
            if (*f.terminated == "crlf") {
                ctx.line(m + " = r.read_crlf_terminated_string(" + std::to_string(max_len) + ")");
            } else {
                std::string term = "0";
                if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line(m + " = r.read_terminated_string(" + term + ", " + std::to_string(max_len) + ")");
            }
            emit_py_field_trim(ctx, m, f);
        } else if (f.length) {
            ctx.line(m + " = r.read_string(" + std::to_string(*f.length) + enc_arg + ")");
            emit_py_field_trim(ctx, m, f);
        } else if (f.length_from) {
            ctx.line(m + " = r.read_string(int(" + py_expr(*f.length_from, pfx) + ")" + enc_arg + ")");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            std::string be = (pti.endian == model::Endian::Big) ? "True" : "False";
            if (pti.bits <= 8) ctx.line("_pl = r.read_u8()");
            else if (pti.bits <= 16) ctx.line("_pl = r.read_u16(" + be + ")");
            else ctx.line("_pl = r.read_u32(" + be + ")");
            if (f.length_includes_prefix)
                ctx.line("_pl -= " + std::to_string(get_prefix_bytes(pti)));
            ctx.line(m + " = r.read_string(_pl" + enc_arg + ")");
        } else if (f.length_star) {
            ctx.line(m + " = r.read_string(r.remaining_bytes()" + enc_arg + ")");
        } else {
            ctx.line(m + " = r.read_string(r.remaining_bytes()" + enc_arg + ")");
        }
        // max_length validation (matching C++ MaxLengthExceeded check)
        if (f.max_length) {
            ctx.line("if len(" + m + ") > " + std::to_string(*f.max_length) + ": raise ConstraintError('" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "')");
        }
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.read_bytes(" + std::to_string(len) + ")");
        else if (f.length_from) ctx.line(m + " = r.read_bytes(int(" + py_expr(*f.length_from, pfx) + "))");
        else ctx.line(m + " = r.read_bytes(r.remaining_bytes())");
        if (f.max_length) {
            ctx.line("if len(" + m + ") > " + std::to_string(*f.max_length) + ": raise ConstraintError('" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "')");
        }
        return;
    }
    if (fi.has_scale) {
        PyFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        std::string ve = "_raw";
        if (fi.scale != 1.0) ve += " * " + py_double(fi.scale);
        if (fi.offset != 0.0) ve += " + " + py_double(fi.offset);
        ctx.line("_raw = " + py_read_expr(raw_fi));
        ctx.line(m + " = " + ve);
        return;
    }
    if (fi.is_bool) { ctx.line(m + " = (" + py_read_expr(fi) + " != 0)"); return; }
    ctx.line(m + " = " + py_read_expr(fi));
    // Field-level constraint checks (matching C++ emit_constraint_check)
    // Skip deferred constraints (validated externally, not at decode time)
    if (f.constraint && f.constraint->validate != model::ValidateTiming::Deferred) {
        if (f.constraint->equals) {
            ctx.line("if " + m + " != " + *f.constraint->equals + ": raise ConstraintError('" + f.name + " constraint violation: expected " + *f.constraint->equals + "')");
        }
        if (f.constraint->max) {
            ctx.line("if " + m + " > " + *f.constraint->max + ": raise ConstraintError('" + f.name + " exceeds max " + *f.constraint->max + "')");
        }
        bool is_signed = fi.is_signed;
        if (f.constraint->min && (*f.constraint->min != "0" || is_signed)) {
            ctx.line("if " + m + " < " + *f.constraint->min + ": raise ConstraintError('" + f.name + " below min " + *f.constraint->min + "')");
        }
    }
}

void emit_py_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    if (f.auto_expr) {
        if (f.auto_expr->kind == model::AutoKind::Length) {
            ctx.line("_len_pos = w.size_bytes()");
            ctx.line(py_write_stmt("0", fi));
            return;
        }
        if (f.auto_expr->kind == model::AutoKind::Count) {
            // Auto-count: write the length of the referenced array
            std::string ref_name = f.auto_expr->field_ref.empty() ? "" : py_field(f.auto_expr->field_ref);
            if (!ref_name.empty()) {
                ctx.line(py_write_stmt("len(" + pfx + "." + ref_name + ")", fi));
            } else {
                ctx.line(py_write_stmt("0", fi));
            }
            return;
        }
        if (f.auto_expr->kind == model::AutoKind::Id) {
            // Auto-id: write the ID_VALUE constant
            ctx.line(py_write_stmt(pfx + ".ID_VALUE", fi));
            return;
        }
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + ".encode(w)"); return; }
    if (fi.is_string) {
        bool has_enc = py_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", encoding=" + py_encoding_const(f)) : "";
        if (f.char_bits && f.length) {
            // Packed character encode
            ctx.line("w.write_packed_chars(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ")");
        } else if (f.terminated) {
            // Terminated string encode
            if (*f.terminated == "crlf") {
                ctx.line("w.write_crlf_terminated_string(" + m + ")");
            } else {
                std::string term = "0";
                if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line("w.write_terminated_string(" + m + ", " + term + ")");
            }
        } else if (f.length) {
            int pad = (f.padding && *f.padding == model::StringPadding::Space) ? 0x20 : 0;
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
        return;
    }
    if (fi.is_bytes) { ctx.line("w.write_bytes(" + m + ")"); return; }
    if (fi.has_scale) {
        std::string inv = m;
        if (fi.offset != 0.0) inv = "(" + inv + " - " + py_double(fi.offset) + ")";
        if (fi.scale != 1.0) inv = "(" + inv + " / " + py_double(fi.scale) + ")";
        PyFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        ctx.line(py_write_stmt("int(" + inv + ")", raw_fi));
        return;
    }
    ctx.line(py_write_stmt(m, fi));
}

void emit_py_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             const PyOuterScopeMap& scope_map = {},
                             const PyOuterContext& outer_ctx = {}) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if " + py_expr_ctx(*f->present_when, pfx, outer_ctx) + ":");
                ctx.indent();
                emit_py_field_decode(ctx, *f, index, pfx);
                ctx.dedent();
            } else emit_py_field_decode(ctx, *f, index, pfx);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + py_field(sd->name);
            std::string args = py_build_outer_args(sd->name, scope_map, pfx, outer_ctx);
            if (sd->present_when) {
                ctx.line("if " + py_expr_ctx(*sd->present_when, pfx, outer_ctx) + ":");
                ctx.indent(); ctx.line(m + " = " + py_class(sd->name) + ".decode(r" + args + ")"); ctx.dedent();
            } else ctx.line(m + " = " + py_class(sd->name) + ".decode(r" + args + ")");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            std::string elem = ad->type_ref.empty() ? py_class(ad->name) : py_class(ad->type_ref);
            auto emit_array_decode = [&]() {
                if (ad->fixed_count) {
                    ctx.line(m + " = [" + elem + ".decode(r) for _ in range(" + std::to_string(*ad->fixed_count) + ")]");
                } else if (ad->count_from) {
                    ctx.line(m + " = [" + elem + ".decode(r) for _ in range(int(" + py_expr_ctx(*ad->count_from, pfx, outer_ctx) + "))]");
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
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = py_expr_ctx(*cd->switch_expr, pfx, outer_ctx);
            std::string m = pfx + "." + py_field(cd->name);
            auto emit_choice_decode = [&]() {
                bool first = true;
                for (const auto& cs : cd->cases) {
                    std::string val = cs.value ? *cs.value : "0";
                    if (cs.value && index.constants.count(*cs.value)) {
                        val = "Constants." + py_snake(*cs.value);
                    }
                    std::string cond = sv + " == " + val;
                    ctx.line(std::string(first ? "if " : "elif ") + cond + ":");
                    ctx.indent();
                    std::string et = cs.type_ref.empty() ? py_class(cs.name) : py_class(cs.type_ref);
                    std::string case_name = cs.type_ref.empty() ? cs.name : cs.type_ref;
                    std::string args = py_build_outer_args(case_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(r" + args + ")");
                    ctx.dedent();
                    first = false;
                }
                if (cd->otherwise) {
                    ctx.line("else:");
                    ctx.indent();
                    std::string et = cd->otherwise->type_ref.empty() ? py_class(cd->otherwise->name) : py_class(cd->otherwise->type_ref);
                    std::string ow_name = cd->otherwise->type_ref.empty() ? cd->otherwise->name : cd->otherwise->type_ref;
                    std::string args = py_build_outer_args(ow_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(r" + args + ")");
                    ctx.dedent();
                }
            };
            if (cd->present_when) {
                ctx.line("if " + py_expr_ctx(*cd->present_when, pfx, outer_ctx) + ":");
                ctx.indent(); emit_choice_decode(); ctx.dedent();
            } else {
                emit_choice_decode();
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skip_bits(" + std::to_string(res->bits) + ")");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("r.align_to(" + std::to_string(al->to) + ")");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_decode_children(ctx, fx->children, index, pfx, scope_map, outer_ctx);
        }
    }
}

void emit_py_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if " + py_expr(*f->present_when, pfx) + ":");
                ctx.indent(); emit_py_field_encode(ctx, *f, index, pfx); ctx.dedent();
            } else emit_py_field_encode(ctx, *f, index, pfx);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + py_field(sd->name);
            if (sd->present_when) {
                ctx.line("if " + m + " is not None: " + m + ".encode(w)");
            } else ctx.line(m + ".encode(w)");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            if (ad->present_when) {
                ctx.line("if " + m + " is not None:");
                ctx.indent(); ctx.line("for _item in " + m + ": _item.encode(w)"); ctx.dedent();
            } else {
                ctx.line("for _item in " + m + ": _item.encode(w)");
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + py_field(cd->name);
            ctx.line("if " + m + " is not None: " + m + ".encode(w)");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.write_bits(0, " + std::to_string(res->bits) + ")");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.align_to(" + std::to_string(al->to) + ")");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_encode_children(ctx, fx->children, index, pfx);
        }
    }
}

// ============================================================================
// Field collection for __init__
// ============================================================================

struct PyFieldDef {
    std::string name;
    std::string py_type;
    std::string default_val;
};

void collect_py_fields(const std::vector<model::StructChild>& children,
                       const analyzer::TypeIndex& index,
                       std::vector<PyFieldDef>& fields) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = py_resolve_field(*f, index);
            PyFieldDef pf;
            pf.name = py_field(f->name);
            pf.py_type = fi.py_type;
            bool optional = (f->present_when != nullptr || f->bit.has_value());
            if (optional || fi.is_struct || fi.is_enum) pf.default_val = "None";
            else if (fi.is_string) pf.default_val = "''";
            else if (fi.is_bytes) pf.default_val = "b''";
            else if (fi.is_float || fi.has_scale) pf.default_val = "0.0";
            else if (fi.is_bool) pf.default_val = "False";
            else pf.default_val = "0";
            if (f->default_value) pf.default_val = *f->default_value;
            fields.push_back(pf);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            fields.push_back({py_field(sd->name), py_class(sd->name), "None"});
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            fields.push_back({py_field(ad->name), "list", "None"});
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            fields.push_back({py_field(cd->name), "object", "None"});
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_py_fields(fx->children, index, fields);
        }
    }
}

// ============================================================================
// structs.py + messages.py
// ============================================================================

void emit_py_class(EmitContext& ctx, const std::string& name,
                   const std::vector<model::StructChild>& children,
                   const analyzer::TypeIndex& index,
                   const std::unordered_map<std::string, uint64_t>& tid_map,
                   const std::string& msg_id = "",
                   const PyOuterScopeMap& scope_map = {}) {
    std::string cn = py_class(name);
    std::vector<PyFieldDef> fields;
    collect_py_fields(children, index, fields);

    ctx.line();
    ctx.line("class " + cn + ":");
    ctx.indent();

    auto tid_it = tid_map.find(name);
    if (tid_it != tid_map.end()) {
        ctx.line("TYPE_ID = " + py_hex64(tid_it->second));
        ctx.line("TYPE_NAME = '" + name + "'");
    }
    if (!msg_id.empty()) ctx.line("ID_VALUE = " + msg_id);

    if (!fields.empty()) {
        std::string slots = "__slots__ = (";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) slots += ", ";
            slots += "'" + fields[i].name + "'";
        }
        ctx.line(slots + ")");
    }
    ctx.line();

    // __init__
    ctx.line("def __init__(self) -> None:");
    ctx.indent();
    if (fields.empty()) ctx.line("pass");
    else for (const auto& f : fields) {
        if (f.default_val == "None" && f.py_type == "list")
            ctx.line("self." + f.name + ": list = []");
        else
            ctx.line("self." + f.name + " = " + f.default_val);
    }
    ctx.dedent();
    ctx.line();

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
    emit_py_decode_children(ctx, children, index, "result", scope_map, outer_ctx);
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
        emit_py_encode_children(ctx, children, index, "self");
        // Auto-length backpatching: find any field with auto="length" and patch the written placeholder
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                    auto lfi = py_resolve_field(*f, index);
                    std::string length_expr = "w.size_bytes() - _len_pos";
                    if (f->auto_expr->modifier.has_modifier()) {
                        auto& mod = f->auto_expr->modifier;
                        // Reverse the modifier: if wire = actual + offset, then patch = actual - offset
                        if (mod.op == model::ArithOp::Add)
                            length_expr += " - " + std::to_string(mod.literal);
                        else if (mod.op == model::ArithOp::Sub)
                            length_expr += " + " + std::to_string(mod.literal);
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

    // __repr__
    ctx.line("def __repr__(self) -> str:");
    ctx.indent();
    if (fields.empty()) ctx.line("return '" + cn + "()'");
    else {
        std::string fmt = "return f'" + cn + "(";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) fmt += ", ";
            fmt += fields[i].name + "={self." + fields[i].name + "}";
        }
        ctx.line(fmt + ")'");
    }
    ctx.dedent();
    ctx.dedent();
}

// Recursively emit Python classes for inline struct/array/choice types
void emit_py_inline_types(EmitContext& ctx, const std::vector<model::StructChild>& children,
                           const analyzer::TypeIndex& index,
                           const std::unordered_map<std::string, uint64_t>& tid_map,
                           PyOuterScopeMap& scope_map,
                           const std::string& current_type_name) {
    for (const auto& child : children) {
        if (auto* sd = std::get_if<model::StructDef>(&child)) {
            py_analyze_outer_scope(sd->name, sd->children, children, current_type_name, scope_map);
            emit_py_inline_types(ctx, sd->children, index, tid_map, scope_map, sd->name);
            emit_py_class(ctx, sd->name, sd->children, index, tid_map, {}, scope_map);
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                emit_py_inline_types(ctx, ad->children, index, tid_map, scope_map, ad->name);
                emit_py_class(ctx, ad->name, ad->children, index, tid_map, {}, scope_map);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    py_analyze_outer_scope(cs.name, cs.children, children, current_type_name, scope_map);
                    emit_py_inline_types(ctx, cs.children, index, tid_map, scope_map, cs.name);
                    emit_py_class(ctx, cs.name, cs.children, index, tid_map, {}, scope_map);
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                py_analyze_outer_scope(cd->otherwise->name, cd->otherwise->children, children, current_type_name, scope_map);
                emit_py_inline_types(ctx, cd->otherwise->children, index, tid_map, scope_map, cd->otherwise->name);
                emit_py_class(ctx, cd->otherwise->name, cd->otherwise->children, index, tid_map, {}, scope_map);
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_inline_types(ctx, fx->children, index, tid_map, scope_map, current_type_name);
        }
    }
}

std::string generate_py_structs(const model::Protocol& protocol,
                                 const analyzer::TypeIndex& index) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from .bit_io import BitReader, BitWriter, DecodeError, EncodeError, ConstraintError");
    ctx.line("from .types import *");
    ctx.line("from .constants import Constants");

    std::unordered_map<std::string, uint64_t> empty;
    for (const auto& sd : protocol.structs) {
        PyOuterScopeMap scope_map;
        emit_py_inline_types(ctx, sd.children, index, empty, scope_map, sd.name);
        emit_py_class(ctx, sd.name, sd.children, index, empty, {}, scope_map);
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
        std::string slots;
        for (const auto& hf : header_fields) {
            if (!slots.empty()) slots += ", ";
            slots += "'" + hf.py_name + "'";
        }
        if (!slots.empty()) slots += ", ";
        slots += "'payload'";
        for (const auto& ff : footer_fields) {
            if (!slots.empty()) slots += ", ";
            slots += "'" + ff.py_name + "'";
        }
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
                case model::ArithOp::Div: op_str = " / "; break;
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
                    total_expr = "(" + raw_val + " / " + std::to_string(si.frame_length_modifier.literal) + ")";
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
            ctx.dedent();
            first = false;
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
                                  const std::vector<analyzer::SessionInfo>& sessions) {
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

    for (const auto& md : protocol.messages) {
        PyOuterScopeMap scope_map;
        emit_py_inline_types(ctx, md.children, index, tid_map, scope_map, md.name);
        emit_py_class(ctx, md.name, md.children, index, tid_map, md.id, scope_map);
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

        // Check if any leaf has auto-increment fields
        bool has_auto_fields = false;
        for (const auto& lt : si.leaf_types)
            if (!lt.auto_fields.empty()) { has_auto_fields = true; break; }

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
        ctx.line("except Exception:");
        ctx.indent();
        ctx.line("return None");
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

                // Set message-level config fields on a copy before wrapping
                if (!lt.config_fields.empty()) {
                    for (const auto& cf : lt.config_fields) {
                        std::string cfg_key = py_snake(cf.key);
                        std::string field = py_field(cf.field_name);
                        ctx.line("payload." + field + " = self._config.get('" + cfg_key + "', 0)");
                    }
                }

                ctx.line("frame = " + frame_class + ".wrap(payload)");

                // Set frame-level config fields
                for (const auto& cf : si.config_fields) {
                    std::string cfg_key = py_snake(cf.key);
                    std::string field = py_field(cf.field_name);
                    ctx.line("frame." + field + " = self._config.get('" + cfg_key + "', 0)");
                }

                // Set auto-increment fields
                for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                    int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = py_field(lt.auto_fields[ai]);
                    ctx.line("frame." + field + " = self._seq & " + std::to_string(mask_val));
                    ctx.line("self._seq += 1");
                }

                // Set auto-timestamp fields
                for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                    int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = py_field(lt.timestamp_fields[ti]);
                    ctx.line("frame." + field + " = int(time.time() * 1000) & " + std::to_string(mask_val));
                }

                ctx.line("data = frame.encode_bytes()");
                ctx.line("return {'bytes': data, 'type_id': type_id}");
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
                                    ctx.line("frame." + py_field(f->name) + " = " + *f->constraint->equals);
                                }
                            }
                        }
                    }

                    // Set auto-increment fields
                    for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                        int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                        uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                        std::string field = py_field(lt.auto_fields[ai]);
                        ctx.line("frame." + field + " = self._seq & " + std::to_string(mask_val));
                        ctx.line("self._seq += 1");
                    }

                    // Set frame-level config fields
                    for (const auto& cf : si.config_fields) {
                        std::string cfg_key = py_snake(cf.key);
                        std::string field = py_field(cf.field_name);
                        ctx.line("frame." + field + " = self._config.get('" + cfg_key + "', 0)");
                    }

                    ctx.line("frame.payload = payloads");
                    ctx.line("data = frame.encode_bytes()");
                    ctx.line("return {'bytes': data, 'type_id': type_id}");
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

    (void)sizes;
    (void)ns;

    bool ok = true;
    ok &= write_file(output_dir / "bit_io.py", generate_bit_io());
    ok &= write_file(output_dir / "constants.py", generate_py_constants(protocol));
    ok &= write_file(output_dir / "types.py", generate_py_types(protocol, index));
    ok &= write_file(output_dir / "structs.py", generate_py_structs(protocol, index));
    ok &= write_file(output_dir / "messages.py", generate_py_messages(protocol, index, sessions));
    ok &= write_file(output_dir / "sessions.py", generate_py_sessions(protocol, index, sessions));
    ok &= write_file(output_dir / "protocol.py", generate_py_protocol(protocol, sessions));
    ok &= write_file(output_dir / "__init__.py", generate_py_init(protocol));

    if (ok) Logger::info("generated 8 Python files in " + output_dir.string());
    return ok;
}

} // namespace bgen::codegen
