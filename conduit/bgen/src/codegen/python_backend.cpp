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

    def read_string(self, length: int) -> str:
        return bytes(self.read_bits(8) for _ in range(length)).decode('latin-1')

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

    def write_string(self, s: str, length: int, pad: int = 0) -> None:
        encoded = s.encode('latin-1')
        for i in range(length):
            self.write_u8(encoded[i] if i < len(encoded) else pad)

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

// ============================================================================
// Decode/Encode children
// ============================================================================

void emit_py_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.py_type + ".decode(r)"); return; }
    if (fi.is_string) {
        if (f.length) {
            ctx.line(m + " = r.read_string(" + std::to_string(*f.length) + ")");
            std::string ch = "'\\x00'";
            if (f.padding && *f.padding == model::StringPadding::Space) ch = "' '";
            ctx.line(m + " = " + m + ".rstrip(" + ch + ")");
        } else if (f.length_from) {
            ctx.line(m + " = r.read_string(int(" + py_expr(*f.length_from, pfx) + "))");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            std::string be = (pti.endian == model::Endian::Big) ? "True" : "False";
            if (pti.bits <= 8) ctx.line("_pl = r.read_u8()");
            else if (pti.bits <= 16) ctx.line("_pl = r.read_u16(" + be + ")");
            else ctx.line("_pl = r.read_u32(" + be + ")");
            if (f.length_includes_prefix)
                ctx.line("_pl -= " + std::to_string(get_prefix_bytes(pti)));
            ctx.line(m + " = r.read_string(_pl)");
        } else {
            ctx.line(m + " = r.read_string(r.remaining_bytes())");
        }
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.read_bytes(" + std::to_string(len) + ")");
        else if (f.length_from) ctx.line(m + " = r.read_bytes(int(" + py_expr(*f.length_from, pfx) + "))");
        else ctx.line(m + " = r.read_bytes(r.remaining_bytes())");
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
}

void emit_py_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = py_resolve_field(f, index);
    std::string m = pfx + "." + py_field(f.name);
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length) {
        ctx.line("_len_pos = w.size_bytes()");
        ctx.line(py_write_stmt("0", fi));
        return;
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + ".encode(w)"); return; }
    if (fi.is_string) {
        if (f.length) {
            int pad = (f.padding && *f.padding == model::StringPadding::Space) ? 0x20 : 0;
            ctx.line("w.write_string(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(pad) + ")");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            std::string be = (pti.endian == model::Endian::Big) ? "True" : "False";
            std::string le = "len(" + m + ")";
            if (f.length_includes_prefix) le += " + " + std::to_string(get_prefix_bytes(pti));
            if (pti.bits <= 8) ctx.line("w.write_u8(" + le + ")");
            else if (pti.bits <= 16) ctx.line("w.write_u16(" + le + ", " + be + ")");
            else ctx.line("w.write_u32(" + le + ", " + be + ")");
            ctx.line("w.write_string(" + m + ", len(" + m + "))");
        } else ctx.line("w.write_string(" + m + ", len(" + m + "))");
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
                             const analyzer::TypeIndex& index, const std::string& pfx) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if " + py_expr(*f->present_when, pfx) + ":");
                ctx.indent();
                emit_py_field_decode(ctx, *f, index, pfx);
                ctx.dedent();
            } else emit_py_field_decode(ctx, *f, index, pfx);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + py_field(sd->name);
            if (sd->present_when) {
                ctx.line("if " + py_expr(*sd->present_when, pfx) + ":");
                ctx.indent(); ctx.line(m + " = " + py_class(sd->name) + ".decode(r)"); ctx.dedent();
            } else ctx.line(m + " = " + py_class(sd->name) + ".decode(r)");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + py_field(ad->name);
            std::string elem = ad->type_ref.empty() ? py_class(ad->name) : py_class(ad->type_ref);
            if (ad->fixed_count) {
                ctx.line(m + " = [" + elem + ".decode(r) for _ in range(" + std::to_string(*ad->fixed_count) + ")]");
            } else if (ad->count_from) {
                ctx.line(m + " = [" + elem + ".decode(r) for _ in range(int(" + py_expr(*ad->count_from, pfx) + "))]");
            } else {
                ctx.line(m + " = []");
                ctx.line("while r.remaining_bytes() > 0:");
                ctx.indent(); ctx.line(m + ".append(" + elem + ".decode(r))"); ctx.dedent();
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = py_expr(*cd->switch_expr, pfx);
            std::string m = pfx + "." + py_field(cd->name);
            bool first = true;
            for (const auto& cs : cd->cases) {
                std::string cond = sv + " == " + (cs.value ? *cs.value : "0");
                ctx.line(std::string(first ? "if " : "elif ") + cond + ":");
                ctx.indent();
                std::string et = cs.type_ref.empty() ? py_class(cs.name) : py_class(cs.type_ref);
                ctx.line(m + " = " + et + ".decode(r)");
                ctx.dedent();
                first = false;
            }
            if (cd->otherwise) {
                ctx.line("else:");
                ctx.indent();
                std::string et = cd->otherwise->type_ref.empty() ? py_class(cd->otherwise->name) : py_class(cd->otherwise->type_ref);
                ctx.line(m + " = " + et + ".decode(r)");
                ctx.dedent();
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skip_bits(" + std::to_string(res->bits) + ")");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_decode_children(ctx, fx->children, index, pfx);
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
            ctx.line("for _item in " + pfx + "." + py_field(ad->name) + ": _item.encode(w)");
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + py_field(cd->name);
            ctx.line("if " + m + " is not None: " + m + ".encode(w)");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.write_bits(0, " + std::to_string(res->bits) + ")");
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
                   const std::string& msg_id = "") {
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

    // decode
    ctx.line("@staticmethod");
    ctx.line("def decode(r: 'BitReader') -> '" + cn + "':");
    ctx.indent();
    ctx.line("result = " + cn + "()");
    emit_py_decode_children(ctx, children, index, "result");
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
    else emit_py_encode_children(ctx, children, index, "self");
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
                           const std::unordered_map<std::string, uint64_t>& tid_map) {
    for (const auto& child : children) {
        if (auto* sd = std::get_if<model::StructDef>(&child)) {
            emit_py_inline_types(ctx, sd->children, index, tid_map);
            emit_py_class(ctx, sd->name, sd->children, index, tid_map);
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                emit_py_inline_types(ctx, ad->children, index, tid_map);
                emit_py_class(ctx, ad->name, ad->children, index, tid_map);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    emit_py_inline_types(ctx, cs.children, index, tid_map);
                    emit_py_class(ctx, cs.name, cs.children, index, tid_map);
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                emit_py_inline_types(ctx, cd->otherwise->children, index, tid_map);
                emit_py_class(ctx, cd->otherwise->name, cd->otherwise->children, index, tid_map);
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_py_inline_types(ctx, fx->children, index, tid_map);
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
        emit_py_inline_types(ctx, sd.children, index, empty);
        emit_py_class(ctx, sd.name, sd.children, index, empty);
    }

    ctx.line();
    return ctx.str();
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
        emit_py_inline_types(ctx, md.children, index, tid_map);
        emit_py_class(ctx, md.name, md.children, index, tid_map, md.id);
    }

    ctx.line();
    return ctx.str();
}

// ============================================================================
// sessions.py + protocol.py + __init__.py
// ============================================================================

std::string generate_py_sessions(const model::Protocol& protocol,
                                  const std::vector<analyzer::SessionInfo>& sessions) {
    EmitContext ctx;
    ctx.line("\"\"\"Generated by bgen - DO NOT EDIT\"\"\"");
    ctx.line("from __future__ import annotations");
    ctx.line("from typing import Any");
    ctx.line("from .bit_io import BitReader, BitWriter, DecodeError");
    ctx.line("from .messages import *");
    ctx.line();

    for (const auto& si : sessions) {
        if (!si.is_frame_based || !si.frame) continue;
        std::string sc = py_class(si.frame->name) + "Session";
        ctx.line();
        ctx.line("class " + sc + ":");
        ctx.indent();
        ctx.line("LEAF_TYPES = {");
        ctx.indent();
        for (const auto& lt : si.leaf_types)
            ctx.line(py_hex64(lt.type_id) + ": '" + lt.name + "',");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
        ctx.line("def __init__(self) -> None: self._seq = 0");
        ctx.line();
        ctx.line("def type_name(self, tid: int) -> str: return self.LEAF_TYPES.get(tid, 'unknown')");
        ctx.line("def leaf_type_ids(self) -> list[int]: return list(self.LEAF_TYPES.keys())");
        ctx.line("def protocol_name(self) -> str: return '" + protocol.name + "'");
        ctx.line("def reset(self) -> None: self._seq = 0");
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
    ok &= write_file(output_dir / "sessions.py", generate_py_sessions(protocol, sessions));
    ok &= write_file(output_dir / "protocol.py", generate_py_protocol(protocol, sessions));
    ok &= write_file(output_dir / "__init__.py", generate_py_init(protocol));

    if (ok) Logger::info("generated 8 Python files in " + output_dir.string());
    return ok;
}

} // namespace bgen::codegen
