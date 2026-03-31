// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Free Helper Functions

#include "cpp_structs_helpers.hpp"
#include "../logger.hpp"
#include <stdexcept>

namespace bgen::codegen {

// ============================================================================
// Helper: determine C++ type for a field
// ============================================================================

FieldTypeInfo resolve_field_type(const model::Field& f, const analyzer::TypeIndex& index) {
    FieldTypeInfo info;

    if (f.type_is_inline || f.type_ref.empty()) {
        // Check explicit base first for inline fields
        if (f.base) {
            switch (*f.base) {
                case model::PrimitiveBase::Float:
                    info.bits = f.bits.value_or(32);
                    info.is_float = true;
                    info.cpp_type = (info.bits <= 32) ? "float" : "double";
                    if (f.wire_encoding) info.wire_encoding = *f.wire_encoding;
                    return info;
                case model::PrimitiveBase::String:
                    info.is_string = true;
                    info.cpp_type = "std::string";
                    if (f.wire_encoding) info.wire_encoding = *f.wire_encoding;
                    return info;
                case model::PrimitiveBase::Bytes:
                    info.is_bytes = true;
                    if (f.length) {
                        info.cpp_type = "std::array<uint8_t, " + std::to_string(*f.length) + ">";
                    } else if (f.bytes_attr) {
                        info.cpp_type = "std::array<uint8_t, " + std::to_string(*f.bytes_attr) + ">";
                    } else {
                        info.cpp_type = "std::vector<uint8_t>";
                    }
                    if (f.wire_encoding) info.wire_encoding = *f.wire_encoding;
                    return info;
                case model::PrimitiveBase::Bool:
                    info.bits = f.bits.value_or(1);
                    info.is_signed = false;
                    info.cpp_type = "bool";
                    info.is_bool = true;
                    if (f.wire_encoding) info.wire_encoding = *f.wire_encoding;
                    return info;
                case model::PrimitiveBase::Int:
                case model::PrimitiveBase::Uint:
                    // Fall through to existing bits/bytes_attr handling below
                    break;
            }
        }
        // Inline bits/bytes
        if (f.bits) {
            info.bits = *f.bits;
            info.is_signed = f.is_signed;
            info.cpp_type = storage_type_for_bits(*f.bits, f.is_signed);
            // Field-level scale/offset: promote to double with inline scale arithmetic
            if (f.scale || f.offset) {
                info.has_field_scale = true;
                info.raw_bits = info.bits;
                info.raw_signed = info.is_signed;
                info.raw_endian = f.endian;
                info.field_scale = f.scale.value_or(1.0);
                info.field_offset = f.offset.value_or(0.0);
                info.cpp_type = "double";
                info.bits = 0;
                info.is_signed = false;
            }
        } else if (f.bytes_attr) {
            if (*f.bytes_attr <= 8) {
                // Small enough for native integer — treat as numeric
                info.bits = *f.bytes_attr * 8;
                info.is_signed = f.is_signed;
                info.cpp_type = storage_type_for_bits(info.bits, f.is_signed);
                // Support scale/offset like regular bits fields
                if (f.scale || f.offset) {
                    info.has_field_scale = true;
                    info.raw_bits = info.bits;
                    info.raw_signed = info.is_signed;
                    info.raw_endian = f.endian;
                    info.field_scale = f.scale.value_or(1.0);
                    info.field_offset = f.offset.value_or(0.0);
                    info.cpp_type = "double";
                    info.bits = 0;
                    info.is_signed = false;
                }
            } else {
                // Too large for native integer — keep as byte array, pass through
                info.is_bytes = true;
                info.cpp_type = "std::array<uint8_t, " + std::to_string(*f.bytes_attr) + ">";
            }
        }
        // Field-level wire_encoding for inline fields
        if (f.wire_encoding) info.wire_encoding = *f.wire_encoding;
        return info;
    }

    if (f.type_ref == "bytes") {
        info.is_bytes = true;
        if (f.length) {
            info.cpp_type = "std::array<uint8_t, " + std::to_string(*f.length) + ">";
        } else {
            info.cpp_type = "std::vector<uint8_t>";
        }
        return info;
    }

    if (f.type_ref == "string") {
        info.is_string = true;
        info.cpp_type = "std::string";
        return info;
    }

    auto resolved = index.find(f.type_ref);
    if (!resolved) {
        info.cpp_type = to_cpp_type_name(f.type_ref);
        return info;
    }

    std::visit([&info, &f](const auto* def) {
        using T = std::decay_t<decltype(*def)>;
        if constexpr (std::is_same_v<T, model::TypeDef>) {
            bool has_enum = !def->enum_values.empty();
            bool has_flags = !def->flags.empty();
            bool has_scale = def->scale.has_value() || def->offset.has_value();
            bool is_string_type = (def->base == model::PrimitiveBase::String);
            bool has_constraint = def->constraint.has_value();

            if (has_enum || has_flags || has_scale || is_string_type || has_constraint) {
                info.cpp_type = to_cpp_type_name(def->name);
                if (has_enum) {
                    info.is_enum = true;
                    info.bits = def->bits;  // Track enum bit width for alignment
                    info.is_signed = (def->base == model::PrimitiveBase::Int);
                } else {
                    info.is_struct = true; // Wrapped types behave like structs for encode/decode
                }
            } else if (def->base == model::PrimitiveBase::Bool) {
                // B1: Bool with bits==0 => 1 bit
                info.cpp_type = to_cpp_type_name(def->name);
                info.bits = (def->bits > 0) ? def->bits : 1;
                info.is_signed = false;
                info.is_bool = true;
            } else if (def->base == model::PrimitiveBase::Float) {
                // A4: Float type
                info.cpp_type = to_cpp_type_name(def->name);
                info.bits = def->bits;
                info.is_float = true;
            } else {
                // Simple alias
                info.cpp_type = to_cpp_type_name(def->name);
                info.bits = def->bits;
                info.is_signed = (def->base == model::PrimitiveBase::Int);
                // Field-level scale/offset: promote to double with inline scale arithmetic
                if (f.scale || f.offset) {
                    info.has_field_scale = true;
                    info.raw_bits = info.bits;
                    info.raw_signed = info.is_signed;
                    info.raw_endian = def->endian;
                    info.field_scale = f.scale.value_or(1.0);
                    info.field_offset = f.offset.value_or(0.0);
                    info.cpp_type = "double";
                    info.bits = 0;
                    info.is_signed = false;
                }
            }
        } else if constexpr (std::is_same_v<T, model::StructDef>) {
            info.cpp_type = to_cpp_type_name(def->name);
            info.is_struct = true;
        } else if constexpr (std::is_same_v<T, model::MessageDef>) {
            info.cpp_type = to_cpp_type_name(def->name);
            info.is_struct = true;
        }
    }, *resolved);

    // Resolve wire_encoding: field-level overrides type-level
    if (f.wire_encoding) {
        info.wire_encoding = *f.wire_encoding;
    } else {
        // Fall back to type-level wire_encoding
        auto it = index.types.find(f.type_ref);
        if (it != index.types.end()) {
            info.wire_encoding = it->second->wire_encoding;
        }
    }

    return info;
}

// ============================================================================
// Helper: get endian enum string
// ============================================================================

std::string endian_str(model::Endian e) {
    return e == model::Endian::Big ? "conduit::io::Endian::Big" : "conduit::io::Endian::Little";
}

// G6: Check if a field-level encoding needs conversion
bool field_needs_encoding(const model::Field& f) {
    return f.encoding && (*f.encoding == model::StringEncoding::Ia5 || *f.encoding == model::StringEncoding::Ebcdic);
}

std::string field_encoding_enum(const model::Field& f) {
    if (!f.encoding) return "";
    switch (*f.encoding) {
        case model::StringEncoding::Ia5: return "conduit::string::Encoding::IA5";
        case model::StringEncoding::Ebcdic: return "conduit::string::Encoding::EBCDIC";
        case model::StringEncoding::Utf8: return "conduit::string::Encoding::UTF8";
        default: return "conduit::string::Encoding::ASCII";
    }
}

// Resolve the effective padding for a field, checking field-level then type-level.
static model::StringPadding resolve_effective_padding(const model::Field& f,
                                                      const analyzer::TypeIndex& index) {
    if (f.padding) return *f.padding;
    if (!f.type_ref.empty()) {
        auto it = index.types.find(f.type_ref);
        if (it != index.types.end()) return it->second->padding;
    }
    return model::StringPadding::Null;
}

// Return the C++ back-char comparison and find_first_not_of argument for a padding type.
static std::pair<std::string, std::string> trim_chars_for_padding(model::StringPadding padding) {
    switch (padding) {
        case model::StringPadding::Space:
            return {"' '", "\" \""};
        case model::StringPadding::Null:
        default:
            return {"'\\0'", "std::string_view(\"\\0\", 1)"};
    }
}

// Emit trim code for a field-level string variable.
// Only emits if the field has a trim setting (set by defaults propagation).
// Strips only the character matching the field's effective padding type.
void emit_field_trim(EmitContext& ctx, const std::string& var, const model::Field& f,
                     const analyzer::TypeIndex& index) {
    if (!f.trim) return;
    auto padding = resolve_effective_padding(f, index);
    auto [back_char, not_of_arg] = trim_chars_for_padding(padding);
    switch (*f.trim) {
        case model::StringTrim::Right:
            ctx.line("while (!" + var + ".empty() && " + var + ".back() == " + back_char + ")");
            ctx.line("    " + var + ".pop_back();");
            break;
        case model::StringTrim::Left: {
            ctx.line("{");
            ctx.indent();
            ctx.line("auto start = " + var + ".find_first_not_of(" + not_of_arg + ");");
            ctx.line("if (start == std::string::npos) " + var + ".clear();");
            ctx.line("else if (start > 0) " + var + ".erase(0, start);");
            ctx.dedent();
            ctx.line("}");
            break;
        }
        case model::StringTrim::Both: {
            ctx.line("{");
            ctx.indent();
            ctx.line("auto start = " + var + ".find_first_not_of(" + not_of_arg + ");");
            ctx.line("if (start == std::string::npos) " + var + ".clear();");
            ctx.line("else if (start > 0) " + var + ".erase(0, start);");
            ctx.dedent();
            ctx.line("}");
            ctx.line("while (!" + var + ".empty() && " + var + ".back() == " + back_char + ")");
            ctx.line("    " + var + ".pop_back();");
            break;
        }
        case model::StringTrim::None:
            break;
    }
}

// ============================================================================
// Helper: emit read/write for a simple numeric field
// ============================================================================

std::string emit_read_expr(const FieldTypeInfo& fti, model::Endian endian,
                           const std::string& reader,
                           bool byte_aligned) {
    // Wire encoding dispatch for non-default encodings
    if (fti.wire_encoding == model::WireEncoding::BCD) {
        return reader + ".read_bcd(" + std::to_string(fti.bits) + ")";
    }
    if (fti.wire_encoding == model::WireEncoding::BCD_S) {
        return reader + ".read_bcd_signed(" + std::to_string(fti.bits) + ")";
    }
    if (fti.wire_encoding == model::WireEncoding::BNR_S) {
        return reader + ".read_sign_magnitude(" + std::to_string(fti.bits) + ")";
    }
    // CB2/BNR/Default all use the same standard read path below
    // A4: Float read (floats are always byte-aligned by convention)
    if (fti.is_float) {
        if (fti.bits == 16) {
            return reader + ".read_f16(" + endian_str(endian) + ")";
        }
        if (fti.bits <= 32) {
            return reader + ".read_f32(" + endian_str(endian) + ")";
        }
        if (fti.bits <= 48) {
            return reader + ".read_f48(" + endian_str(endian) + ")";
        }
        if (fti.bits <= 64) {
            return reader + ".read_f64(" + endian_str(endian) + ")";
        }
        // Arbitrary-width float: read raw bits, memcpy into float/double
        return reader + ".read_bits(" + std::to_string(fti.bits) + ")";
    }
    if (fti.bits > 0 && fti.bits <= 64) {
        // Byte-optimized reads (read_u8, read_u16, etc.) auto-align to byte
        // boundaries, which corrupts data when the reader is mid-byte.
        // Only use them when we know the position is byte-aligned.
        if (byte_aligned) {
            if (fti.bits == 8 && !fti.is_signed) return reader + ".read_u8()";
            if (fti.bits == 16 && !fti.is_signed) return reader + ".read_u16(" + endian_str(endian) + ")";
            if (fti.bits == 32 && !fti.is_signed) return reader + ".read_u32(" + endian_str(endian) + ")";
            if (fti.bits == 64 && !fti.is_signed) return reader + ".read_u64(" + endian_str(endian) + ")";
            if (fti.bits == 16 && fti.is_signed) {
                return "([&]() -> conduit::Result<int16_t> { auto v = " + reader + ".read_u16(" +
                       endian_str(endian) + "); if (!v) return std::unexpected(v.error()); "
                       "return static_cast<int16_t>(*v); })()";
            }
            if (fti.bits == 32 && fti.is_signed) {
                return "([&]() -> conduit::Result<int32_t> { auto v = " + reader + ".read_u32(" +
                       endian_str(endian) + "); if (!v) return std::unexpected(v.error()); "
                       "return static_cast<int32_t>(*v); })()";
            }
            if (fti.bits == 64 && fti.is_signed) {
                return "([&]() -> conduit::Result<int64_t> { auto v = " + reader + ".read_u64(" +
                       endian_str(endian) + "); if (!v) return std::unexpected(v.error()); "
                       "return static_cast<int64_t>(*v); })()";
            }
        }
        if (fti.is_signed) return reader + ".read_signed_bits(" + std::to_string(fti.bits) + ")";
        return reader + ".read_bits(" + std::to_string(fti.bits) + ")";
    }
    throw std::logic_error("emit_read_expr: unresolvable field type (bits=" +
                           std::to_string(fti.bits) + ", signed=" +
                           std::to_string(fti.is_signed) + ", float=" +
                           std::to_string(fti.is_float) + ", struct=" +
                           std::to_string(fti.is_struct) + ", bytes=" +
                           std::to_string(fti.is_bytes) + ", string=" +
                           std::to_string(fti.is_string) + ", enum=" +
                           std::to_string(fti.is_enum) + ", scale=" +
                           std::to_string(fti.has_field_scale) + ", cpp_type=" +
                           fti.cpp_type + ")");
}

void emit_write_stmt(EmitContext& ctx, const std::string& value, const FieldTypeInfo& fti,
                     model::Endian endian, bool byte_aligned) {
    // Wire encoding dispatch for non-default encodings
    if (fti.wire_encoding == model::WireEncoding::BCD) {
        ctx.line("w.write_bcd(" + value + ", " + std::to_string(fti.bits) + ");");
        return;
    }
    if (fti.wire_encoding == model::WireEncoding::BCD_S) {
        ctx.line("w.write_bcd_signed(" + value + ", " + std::to_string(fti.bits) + ");");
        return;
    }
    if (fti.wire_encoding == model::WireEncoding::BNR_S) {
        ctx.line("w.write_sign_magnitude(" + value + ", " + std::to_string(fti.bits) + ");");
        return;
    }
    // CB2/BNR/Default all use the same standard write path below
    // A4: Float write (floats are always byte-aligned by convention)
    if (fti.is_float) {
        if (fti.bits == 16) {
            ctx.line("w.write_f16(" + value + ", " + endian_str(endian) + ");");
        } else if (fti.bits <= 32) {
            ctx.line("w.write_f32(" + value + ", " + endian_str(endian) + ");");
        } else if (fti.bits <= 48) {
            ctx.line("w.write_f48(" + value + ", " + endian_str(endian) + ");");
        } else if (fti.bits <= 64) {
            ctx.line("w.write_f64(" + value + ", " + endian_str(endian) + ");");
        } else {
            // Arbitrary-width float: write raw bits
            ctx.line("w.write_bits(static_cast<uint64_t>(" + value + "), " + std::to_string(fti.bits) + ");");
        }
        return;
    }
    // Byte-optimized writes (write_u8, write_u16, etc.) auto-align to byte
    // boundaries. Only use them when we know the position is byte-aligned.
    // Helper: wrap value with static_cast only if it doesn't already have one.
    // This avoids redundant double casts like static_cast<uint16_t>(static_cast<uint16_t>(...))
    // or static_cast<uint8_t>(static_cast<uint8>(...)) which occur when callers pass
    // pre-cast scaled field or count expressions.
    auto cast_wrap = [](const std::string& v, const std::string& type) -> std::string {
        if (v.starts_with("static_cast<")) return v;
        return "static_cast<" + type + ">(" + v + ")";
    };
    if (byte_aligned) {
        if (fti.bits == 8 && !fti.is_signed) {
            ctx.line("w.write_u8(" + cast_wrap(value, "uint8_t") + ");");
            return;
        } else if (fti.bits == 16 && !fti.is_signed) {
            ctx.line("w.write_u16(" + cast_wrap(value, "uint16_t") + ", " + endian_str(endian) + ");");
            return;
        } else if (fti.bits == 32 && !fti.is_signed) {
            ctx.line("w.write_u32(" + cast_wrap(value, "uint32_t") + ", " + endian_str(endian) + ");");
            return;
        } else if (fti.bits == 64 && !fti.is_signed) {
            ctx.line("w.write_u64(" + cast_wrap(value, "uint64_t") + ", " + endian_str(endian) + ");");
            return;
        }
    }
    if (fti.is_bool) {
        ctx.line("w.write_bits(static_cast<uint8_t>(" + value + "), " + std::to_string(fti.bits) + ");");
    } else if (fti.is_signed) {
        ctx.line("w.write_signed_bits(" + value + ", " + std::to_string(fti.bits) + ");");
    } else {
        ctx.line("w.write_bits(" + value + ", " + std::to_string(fti.bits) + ");");
    }
}

PrefixTypeInfo resolve_prefix_type(const std::string& type_name, const analyzer::TypeIndex& index) {
    PrefixTypeInfo pti;
    auto it = index.types.find(type_name);
    if (it != index.types.end()) {
        pti.bits = it->second->bits;
        pti.endian = it->second->endian;
    }
    return pti;
}

std::string emit_prefix_read(const PrefixTypeInfo& pti) {
    if (pti.bits <= 8) return "r.read_u8()";
    if (pti.bits <= 16) return "r.read_u16(" + endian_str(pti.endian) + ")";
    if (pti.bits <= 32) return "r.read_u32(" + endian_str(pti.endian) + ")";
    return "r.read_u64(" + endian_str(pti.endian) + ")";
}

void emit_prefix_write(EmitContext& ctx, const PrefixTypeInfo& pti, const std::string& value) {
    if (pti.bits <= 8) {
        ctx.line("w.write_u8(static_cast<uint8_t>(" + value + "));");
    } else if (pti.bits <= 16) {
        ctx.line("w.write_u16(static_cast<uint16_t>(" + value + "), " + endian_str(pti.endian) + ");");
    } else if (pti.bits <= 32) {
        ctx.line("w.write_u32(static_cast<uint32_t>(" + value + "), " + endian_str(pti.endian) + ");");
    } else {
        ctx.line("w.write_u64(static_cast<uint64_t>(" + value + "), " + endian_str(pti.endian) + ");");
    }
}

int get_prefix_bytes(const PrefixTypeInfo& pti) {
    return (pti.bits + 7) / 8;
}

namespace {

std::string arith_op_str(model::ArithOp op) {
    switch (op) {
        case model::ArithOp::Add: return " + ";
        case model::ArithOp::Sub: return " - ";
        case model::ArithOp::Mul: return " * ";
        case model::ArithOp::Div: return " / ";
        case model::ArithOp::Mod: return " % ";
        default: return "";
    }
}

model::ArithOp inverse_op(model::ArithOp op) {
    switch (op) {
        case model::ArithOp::Add: return model::ArithOp::Sub;
        case model::ArithOp::Sub: return model::ArithOp::Add;
        case model::ArithOp::Mul: return model::ArithOp::Div;
        case model::ArithOp::Div: return model::ArithOp::Mul;
        default: return op; // Mod has no inverse
    }
}

} // anonymous namespace

std::string apply_arith(const std::string& base_expr, const model::ArithModifier& mod) {
    if (!mod.has_modifier()) return base_expr;
    std::string operand;
    if (mod.is_field_operand()) {
        operand = to_member_name(mod.field_ref);
    } else {
        operand = std::to_string(mod.literal);
    }
    // Parenthesize base_expr to avoid operator precedence issues
    // e.g., "(size - start) / 2" not "size - start / 2"
    return "((" + base_expr + ")" + arith_op_str(mod.op) + operand + ")";
}

std::string reverse_arith(const std::string& base_expr, const model::ArithModifier& mod) {
    if (!mod.has_modifier()) return base_expr;
    auto inv = inverse_op(mod.op);
    std::string operand = std::to_string(mod.literal);
    return "((" + base_expr + ")" + arith_op_str(inv) + operand + ")";
}

} // namespace bgen::codegen
