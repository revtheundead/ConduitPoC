// SPDX-License-Identifier: MIT
// Bgen - Reusable Enum Code Emitter

#pragma once

#include "emit_context.hpp"
#include "name_utils.hpp"
#include "../model/ast.hpp"
#include <string>
#include <vector>

namespace bgen::codegen {

// Emit a standalone enum class with to_string, decode_, and encode_ functions.
// Used for both type-level enums (in cpp_types.cpp) and inline field-level enums.
inline void emit_enum_class(EmitContext& ctx,
                            const std::string& enum_name,
                            const std::string& underlying_type,
                            int bits,
                            model::Endian endian,
                            const std::vector<model::EnumValue>& values) {
    // enum class definition
    ctx.line("enum class " + enum_name + " : " + underlying_type + " {");
    ctx.indent();
    for (const auto& ev : values) {
        ctx.line(to_enum_value_name(ev.name) + " = " + std::to_string(ev.id) + ",");
    }
    ctx.dedent();
    ctx.line("};");
    ctx.line();

    // to_string
    ctx.line("inline std::string_view to_string(" + enum_name + " v) {");
    ctx.indent();
    ctx.line("switch (v) {");
    ctx.indent();
    for (const auto& ev : values) {
        ctx.line("case " + enum_name + "::" + to_enum_value_name(ev.name) +
                 ": return \"" + ev.name + "\";");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line("return \"unknown\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decode function
    std::string endian_str_val = (endian == model::Endian::Little)
        ? "conduit::io::Endian::Little" : "conduit::io::Endian::Big";
    ctx.line("inline conduit::Result<" + enum_name + "> decode_" + enum_name +
             "(conduit::io::BitReader& r) {");
    ctx.indent();
    // Read raw bits — use read_bits for single-byte or sub-byte widths (alignment-safe),
    // byte-oriented reads for exact 16/32-bit widths (endianness-aware).
    if (bits <= 8) {
        ctx.line("auto raw = r.read_bits(" + std::to_string(bits) + ");");
    } else if (bits == 16) {
        ctx.line("auto raw = r.read_u16(" + endian_str_val + ");");
    } else if (bits == 32) {
        ctx.line("auto raw = r.read_u32(" + endian_str_val + ");");
    } else {
        ctx.line("auto raw = r.read_bits(" + std::to_string(bits) + ");");
    }
    ctx.line("if (!raw) return std::unexpected(raw.error());");
    ctx.line("auto val = static_cast<" + enum_name + ">(*raw);");
    ctx.line("switch (val) {");
    ctx.indent();
    for (const auto& ev : values) {
        ctx.line("case " + enum_name + "::" + to_enum_value_name(ev.name) + ":");
    }
    ctx.indent();
    ctx.line("return val;");
    ctx.dedent();
    ctx.dedent();
    ctx.line("}");
    ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownEnumValue,");
    ctx.line("    \"unknown " + enum_name + " value: \" + std::to_string(static_cast<" +
             underlying_type + ">(val))));");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encode function
    ctx.line("inline conduit::VoidResult encode_" + enum_name + "(" + enum_name +
             " v, conduit::io::BitWriter& w) {");
    ctx.indent();
    ctx.line("switch (v) {");
    ctx.indent();
    for (const auto& ev : values) {
        ctx.line("case " + enum_name + "::" + to_enum_value_name(ev.name) + ":");
    }
    ctx.indent();
    ctx.line("break;");
    ctx.dedent();
    ctx.line("default:");
    ctx.indent();
    ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
    ctx.line("    \"invalid " + enum_name + " enum value: \" + std::to_string(static_cast<" +
             underlying_type + ">(v))));");
    ctx.dedent();
    ctx.dedent();
    ctx.line("}");
    // Write bits — use write_bits for single-byte or sub-byte widths (alignment-safe),
    // byte-oriented writes for exact 16/32-bit widths (endianness-aware).
    if (bits <= 8) {
        ctx.line("w.write_bits(static_cast<" + underlying_type + ">(v), " + std::to_string(bits) + ");");
    } else if (bits == 16) {
        ctx.line("w.write_u16(static_cast<" + underlying_type + ">(v), " + endian_str_val + ");");
    } else if (bits == 32) {
        ctx.line("w.write_u32(static_cast<" + underlying_type + ">(v), " + endian_str_val + ");");
    } else {
        ctx.line("w.write_bits(static_cast<" + underlying_type + ">(v), " + std::to_string(bits) + ");");
    }
    ctx.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

} // namespace bgen::codegen
