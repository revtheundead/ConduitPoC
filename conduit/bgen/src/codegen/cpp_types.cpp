// SPDX-License-Identifier: MIT
// Bgen - Types Code Generator Implementation

#include "cpp_types.hpp"
#include "emit_context.hpp"
#include "name_utils.hpp"
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace bgen::codegen {

namespace {

std::string base_to_string(model::PrimitiveBase base) {
    switch (base) {
        case model::PrimitiveBase::Uint: return "uint";
        case model::PrimitiveBase::Int: return "int";
        case model::PrimitiveBase::Float: return "float";
        case model::PrimitiveBase::Bool: return "bool";
        case model::PrimitiveBase::Bytes: return "bytes";
        case model::PrimitiveBase::String: return "string";
    }
    return "uint";
}

std::string endian_enum(model::Endian e) {
    return e == model::Endian::Big ? "conduit::io::Endian::Big" : "conduit::io::Endian::Little";
}

bool needs_encoding_conversion(model::StringEncoding enc) {
    return enc == model::StringEncoding::Ia5 || enc == model::StringEncoding::Ebcdic;
}

std::string encoding_enum_str(model::StringEncoding enc) {
    switch (enc) {
        case model::StringEncoding::Ascii: return "conduit::string::Encoding::ASCII";
        case model::StringEncoding::Ia5: return "conduit::string::Encoding::IA5";
        case model::StringEncoding::Ebcdic: return "conduit::string::Encoding::EBCDIC";
        case model::StringEncoding::Utf8: return "conduit::string::Encoding::UTF8";
    }
    return "conduit::string::Encoding::ASCII";
}

// Emit the read expression for a type's wire encoding.
// Type-level wrappers (enum, scaled, constrained, flags) can't know the
// alignment context of the caller, so we always use read_bits/read_signed_bits
// instead of byte-optimized variants (read_u8, read_u16, etc.).  read_bits
// works correctly at byte boundaries too, just slightly less optimal.
std::string type_read_expr(const model::TypeDef& t) {
    bool is_signed = (t.base == model::PrimitiveBase::Int);
    // Non-default wire encodings
    if (t.wire_encoding == model::WireEncoding::BCD) {
        return "r.read_bcd(" + std::to_string(t.bits) + ")";
    }
    if (t.wire_encoding == model::WireEncoding::BCD_S) {
        return "r.read_bcd_signed(" + std::to_string(t.bits) + ")";
    }
    if (t.wire_encoding == model::WireEncoding::BNR_S) {
        return "r.read_sign_magnitude(" + std::to_string(t.bits) + ")";
    }
    // Default/CB2/BNR: always use bit-level reads for alignment safety
    if (is_signed) return "r.read_signed_bits(" + std::to_string(t.bits) + ")";
    return "r.read_bits(" + std::to_string(t.bits) + ")";
}

// Emit the write statement for a type's wire encoding.
// Type-level wrappers can't know alignment context, so we always use
// write_bits/write_signed_bits instead of byte-optimized variants.
void type_write_stmt(EmitContext& ctx, const std::string& value, const model::TypeDef& t) {
    bool is_signed = (t.base == model::PrimitiveBase::Int);
    // Non-default wire encodings
    if (t.wire_encoding == model::WireEncoding::BCD) {
        ctx.line("w.write_bcd(" + value + ", " + std::to_string(t.bits) + ");");
        return;
    }
    if (t.wire_encoding == model::WireEncoding::BCD_S) {
        ctx.line("w.write_bcd_signed(" + value + ", " + std::to_string(t.bits) + ");");
        return;
    }
    if (t.wire_encoding == model::WireEncoding::BNR_S) {
        ctx.line("w.write_sign_magnitude(" + value + ", " + std::to_string(t.bits) + ");");
        return;
    }
    // Default/CB2/BNR: always use bit-level writes for alignment safety
    if (is_signed) {
        ctx.line("w.write_signed_bits(" + value + ", " + std::to_string(t.bits) + ");");
    } else {
        ctx.line("w.write_bits(" + value + ", " + std::to_string(t.bits) + ");");
    }
}

void emit_simple_alias(EmitContext& ctx, const model::TypeDef& t) {
    std::string cpp = primitive_cpp_type(base_to_string(t.base), t.bits,
                                         t.base == model::PrimitiveBase::Int);
    if (cpp.empty()) return;

    std::string name = to_cpp_type_name(t.name);
    ctx.line("using " + name + " = " + cpp + ";");
}

void emit_enum_type(EmitContext& ctx, const model::TypeDef& t) {
    std::string name = to_cpp_type_name(t.name);
    std::string underlying = storage_type_for_bits(t.bits, false);

    if (!t.doc.empty()) {
        ctx.comment(t.doc);
    }

    ctx.line("enum class " + name + " : " + underlying + " {");
    ctx.indent();
    for (size_t i = 0; i < t.enum_values.size(); i++) {
        const auto& ev = t.enum_values[i];
        ctx.line(to_enum_value_name(ev.name) + " = " + std::to_string(ev.id) + ",");
    }
    ctx.dedent();
    ctx.line("};");
    ctx.line();

    // to_string
    ctx.line("inline std::string_view to_string(" + name + " v) {");
    ctx.indent();
    ctx.line("switch (v) {");
    ctx.indent();
    for (const auto& ev : t.enum_values) {
        ctx.line("case " + name + "::" + to_enum_value_name(ev.name) +
                 ": return \"" + ev.name + "\";");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line("return \"unknown\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decode
    ctx.line("inline conduit::Result<" + name + "> decode_" + name + "(conduit::io::BitReader& r) {");
    ctx.indent();
    ctx.line("auto raw = " + type_read_expr(t) + ";");
    ctx.line("if (!raw) return std::unexpected(raw.error());");
    ctx.line("auto val = static_cast<" + name + ">(*raw);");
    ctx.line("switch (val) {");
    ctx.indent();
    for (const auto& ev : t.enum_values) {
        ctx.line("case " + name + "::" + to_enum_value_name(ev.name) + ":");
    }
    ctx.indent();
    ctx.line("return val;");
    ctx.dedent();
    ctx.dedent();
    ctx.line("}");
    ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownEnumValue,");
    ctx.line("    \"unknown " + name + " value: \" + std::to_string(static_cast<" + underlying + ">(val))));");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encode
    ctx.line("inline conduit::VoidResult encode_" + name + "(" + name + " v, conduit::io::BitWriter& w) {");
    ctx.indent();
    // Validate enum value before encoding
    ctx.line("switch (v) {");
    ctx.indent();
    for (const auto& ev : t.enum_values) {
        ctx.line("case " + name + "::" + to_enum_value_name(ev.name) + ":");
    }
    ctx.indent();
    ctx.line("break;");
    ctx.dedent();
    ctx.line("default:");
    ctx.indent();
    ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
    ctx.line("    \"invalid " + name + " enum value: \" + std::to_string(static_cast<" + underlying + ">(v))));");
    ctx.dedent();
    ctx.dedent();
    ctx.line("}");
    type_write_stmt(ctx, "static_cast<" + underlying + ">(v)", t);
    ctx.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

void emit_flags_type(EmitContext& ctx, const model::TypeDef& t) {
    std::string name = to_cpp_type_name(t.name);
    std::string underlying = storage_type_for_bits(t.bits, false);

    if (!t.doc.empty()) {
        ctx.comment(t.doc);
    }

    ctx.line("class " + name + " {");
    ctx.line("public:");
    ctx.indent();

    for (const auto& f : t.flags) {
        std::string acc = to_accessor_name(f.name);
        std::string bit_str = std::to_string(f.bit);
        ctx.line("bool " + acc + "() const { return (raw_ >> " + bit_str + ") & 1; }");
        ctx.line("void set_" + acc + "(bool v) { if (v) raw_ |= (static_cast<" + underlying + ">(1) << " + bit_str +
                 "); else raw_ &= static_cast<" + underlying + ">(~(static_cast<" + underlying + ">(1) << " + bit_str + ")); }");
    }

    ctx.line();
    ctx.line(underlying + " raw() const { return raw_; }");
    ctx.line("void set_raw(" + underlying + " v) { raw_ = v; }");
    ctx.line();
    ctx.line("bool operator==(const " + name + "&) const = default;");
    ctx.line();
    // Encode - dispatch based on wire encoding
    ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx.indent();
    type_write_stmt(ctx, "raw_", t);
    ctx.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");

    // Decode - dispatch based on wire encoding
    ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
    ctx.indent();
    ctx.line("auto raw = " + type_read_expr(t) + ";");
    ctx.line("if (!raw) return std::unexpected(raw.error());");
    ctx.line(name + " result;");
    ctx.line("result.raw_ = static_cast<" + underlying + ">(*raw);");
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");

    ctx.dedent();
    ctx.line("private:");
    ctx.indent();
    ctx.line(underlying + " raw_ = 0;");
    ctx.dedent();
    ctx.line("};");
    ctx.line();
}

void emit_scaled_type(EmitContext& ctx, const model::TypeDef& t) {
    std::string name = to_cpp_type_name(t.name);
    bool is_signed = (t.base == model::PrimitiveBase::Int);
    std::string raw_type = storage_type_for_bits(t.bits, is_signed);
    std::string wire_size = std::to_string((t.bits + 7) / 8);
    std::string endian = endian_enum(t.endian);

    if (!t.doc.empty()) {
        ctx.comment(t.doc);
    }

    ctx.line("class " + name + " {");
    ctx.line("public:");
    ctx.indent();

    // Scale/offset constants - use full precision via double_literal
    if (t.scale) {
        ctx.line("static constexpr double SCALE = " + double_literal(*t.scale) + ";");
    }
    if (t.offset) {
        ctx.line("static constexpr double OFFSET = " + double_literal(*t.offset) + ";");
    }

    // Value accessors (getters)
    if (t.scale && t.offset) {
        ctx.line("double value() const { return static_cast<double>(raw_) * SCALE + OFFSET; }");
    } else if (t.scale) {
        ctx.line("double value() const { return static_cast<double>(raw_) * SCALE; }");
    } else if (t.offset) {
        ctx.line("double value() const { return static_cast<double>(raw_) + OFFSET; }");
    }
    ctx.line(raw_type + " raw() const { return raw_; }");

    // Value setters — validate constraint on raw value if present
    if (t.constraint && (t.constraint->equals || t.constraint->min || t.constraint->max)) {
        // set_value: compute raw, validate, assign
        {
            std::string inverse;
            if (t.scale && t.offset) inverse = "static_cast<" + raw_type + ">((v - OFFSET) / SCALE)";
            else if (t.scale) inverse = "static_cast<" + raw_type + ">(v / SCALE)";
            else inverse = "static_cast<" + raw_type + ">(v - OFFSET)";

            ctx.line("[[nodiscard]] conduit::VoidResult set_value(double v) {");
            ctx.indent();
            ctx.line("auto raw = " + inverse + ";");
            if (t.constraint->equals) {
                ctx.line("if (raw != static_cast<" + raw_type + ">(" + *t.constraint->equals + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " constraint: expected " + *t.constraint->equals + "\"));");
            }
            if (t.constraint->max) {
                ctx.line("if (raw > static_cast<" + raw_type + ">(" + *t.constraint->max + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " exceeds max " + *t.constraint->max + "\"));");
            }
            if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
                ctx.line("if (raw < static_cast<" + raw_type + ">(" + *t.constraint->min + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " below min " + *t.constraint->min + "\"));");
            }
            ctx.line("raw_ = raw;");
            ctx.line("return {};");
            ctx.dedent();
            ctx.line("}");
        }
        // set_raw: validate directly
        {
            ctx.line("[[nodiscard]] conduit::VoidResult set_raw(" + raw_type + " v) {");
            ctx.indent();
            if (t.constraint->equals) {
                ctx.line("if (v != static_cast<" + raw_type + ">(" + *t.constraint->equals + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " constraint: expected " + *t.constraint->equals + "\"));");
            }
            if (t.constraint->max) {
                ctx.line("if (v > static_cast<" + raw_type + ">(" + *t.constraint->max + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " exceeds max " + *t.constraint->max + "\"));");
            }
            if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
                ctx.line("if (v < static_cast<" + raw_type + ">(" + *t.constraint->min + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " below min " + *t.constraint->min + "\"));");
            }
            ctx.line("raw_ = v;");
            ctx.line("return {};");
            ctx.dedent();
            ctx.line("}");
        }
    } else {
        // No constraint: simple setters
        if (t.scale && t.offset) {
            ctx.line("void set_value(double v) { raw_ = static_cast<" + raw_type + ">((v - OFFSET) / SCALE); }");
        } else if (t.scale) {
            ctx.line("void set_value(double v) { raw_ = static_cast<" + raw_type + ">(v / SCALE); }");
        } else if (t.offset) {
            ctx.line("void set_value(double v) { raw_ = static_cast<" + raw_type + ">(v - OFFSET); }");
        }
        ctx.line("void set_raw(" + raw_type + " v) { raw_ = v; }");
    }

    ctx.line();
    ctx.line("bool operator==(const " + name + "&) const = default;");
    ctx.line();

    // Encode
    ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx.indent();
    type_write_stmt(ctx, "raw_", t);
    ctx.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");

    // Decode
    ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
    ctx.indent();
    ctx.line("auto raw = " + type_read_expr(t) + ";");
    ctx.line("if (!raw) return std::unexpected(raw.error());");
    ctx.line(name + " result;");
    ctx.line("result.raw_ = static_cast<" + raw_type + ">(*raw);");
    // B2: Emit constraint checks in decode for scaled types
    if (t.constraint) {
        if (t.constraint->equals) {
            ctx.line("if (result.raw_ != static_cast<" + raw_type + ">(" + *t.constraint->equals + ")) {");
            ctx.indent();
            ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
            ctx.line("    \"" + name + " constraint violation: expected " + *t.constraint->equals + "\"));");
            ctx.dedent();
            ctx.line("}");
        }
        if (t.constraint->max) {
            ctx.line("if (result.raw_ > static_cast<" + raw_type + ">(" + *t.constraint->max + ")) {");
            ctx.indent();
            ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
            ctx.line("    \"" + name + " exceeds max " + *t.constraint->max + "\"));");
            ctx.dedent();
            ctx.line("}");
        }
        // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
        if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
            ctx.line("if (result.raw_ < static_cast<" + raw_type + ">(" + *t.constraint->min + ")) {");
            ctx.indent();
            ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
            ctx.line("    \"" + name + " below min " + *t.constraint->min + "\"));");
            ctx.dedent();
            ctx.line("}");
        }
    }
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");

    ctx.dedent();
    ctx.line("private:");
    ctx.indent();
    ctx.line(raw_type + " raw_ = 0;");
    ctx.dedent();
    ctx.line("};");
    ctx.line();
}

void emit_trim_code(EmitContext& ctx, const std::string& var,
                    model::StringTrim trim, model::StringPadding padding) {
    // Determine the character to strip based on the padding type.
    // Null-padded fields strip only '\0'; space-padded fields strip only ' '.
    std::string back_char;
    std::string not_of_arg;
    switch (padding) {
        case model::StringPadding::Space:
            back_char = "' '";
            not_of_arg = "\" \"";
            break;
        case model::StringPadding::Null:
        default:
            back_char = "'\\0'";
            not_of_arg = "std::string_view(\"\\0\", 1)";
            break;
    }

    switch (trim) {
        case model::StringTrim::Right:
            ctx.line("// Trim trailing padding");
            ctx.line("while (!" + var + ".empty() && " + var + ".back() == " + back_char + ")");
            ctx.line("    " + var + ".pop_back();");
            break;
        case model::StringTrim::Left:
            ctx.line("// Trim leading padding");
            ctx.line("{");
            ctx.indent();
            ctx.line("auto start = " + var + ".find_first_not_of(" + not_of_arg + ");");
            ctx.line("if (start == std::string::npos) " + var + ".clear();");
            ctx.line("else if (start > 0) " + var + ".erase(0, start);");
            ctx.dedent();
            ctx.line("}");
            break;
        case model::StringTrim::Both:
            ctx.line("// Trim leading padding");
            ctx.line("{");
            ctx.indent();
            ctx.line("auto start = " + var + ".find_first_not_of(" + not_of_arg + ");");
            ctx.line("if (start == std::string::npos) " + var + ".clear();");
            ctx.line("else if (start > 0) " + var + ".erase(0, start);");
            ctx.dedent();
            ctx.line("}");
            ctx.line("// Trim trailing padding");
            ctx.line("while (!" + var + ".empty() && " + var + ".back() == " + back_char + ")");
            ctx.line("    " + var + ".pop_back();");
            break;
        case model::StringTrim::None:
            // No trimming
            break;
    }
}

void emit_string_type(EmitContext& ctx, const model::TypeDef& t) {
    // String types with char-bits get a wrapper class
    if (t.char_bits && t.length) {
        std::string name = to_cpp_type_name(t.name);
        size_t total_bits = static_cast<size_t>(*t.length) * static_cast<size_t>(*t.char_bits);
        size_t wire_bytes = (total_bits + 7) / 8;

        ctx.comment(t.doc.empty() ? name : t.doc);
        ctx.line("class " + name + " {");
        ctx.line("public:");
        ctx.indent();
        ctx.line("static constexpr size_t CHAR_COUNT = " + std::to_string(*t.length) + ";");
        ctx.line("static constexpr int CHAR_BITS = " + std::to_string(*t.char_bits) + ";");
        ctx.line("static constexpr size_t WIRE_SIZE = " + std::to_string(wire_bytes) + ";");
        ctx.line();
        ctx.line("std::string value() const { return value_; }");
        ctx.line("void set_value(const std::string& v) { value_ = v; }");
        ctx.line();
        ctx.line("bool operator==(const " + name + "&) const = default;");
        ctx.line();

        // Encode: pack each character to char_bits
        ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
        ctx.indent();
        ctx.line("for (size_t i = 0; i < CHAR_COUNT; i++) {");
        ctx.indent();
        {
            std::string pad_char = "0x20";
            if (t.padding == model::StringPadding::Null) pad_char = "0x00";
            else if (t.padding == model::StringPadding::Space) pad_char = "0x20";
            ctx.line("uint8_t ch = (i < value_.size()) ? static_cast<uint8_t>(value_[i]) : " + pad_char + ";");
            // For packed characters (< 7 bits), convert lowercase to uppercase
            // since the 6-bit IA-5 encoding only supports uppercase letters.
            if (*t.char_bits < 7) {
                ctx.line("if (ch >= 'a' && ch <= 'z') ch -= 32;");
            }
        }
        ctx.line("w.write_bits(ch & ((1 << CHAR_BITS) - 1), CHAR_BITS);");
        ctx.dedent();
        ctx.line("}");
        ctx.line("if (w.has_error()) return std::unexpected(w.error());");
        ctx.line("return {};");
        ctx.dedent();
        ctx.line("}");

        // Decode: unpack each character from char_bits
        ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
        ctx.indent();
        ctx.line(name + " result;");
        ctx.line("result.value_.reserve(CHAR_COUNT);");
        ctx.line("for (size_t i = 0; i < CHAR_COUNT; i++) {");
        ctx.indent();
        ctx.line("auto bits = r.read_bits(CHAR_BITS);");
        ctx.line("if (!bits) return std::unexpected(bits.error());");
        if (*t.char_bits < 7) {
            // For packed characters (< 7 bits), reconstruct ASCII from the N-bit code.
            // Encode strips high bits (ch & mask); decode must restore them.
            // Values 1-31 came from ASCII 0x41-0x5F (letters), values 32-63 are already correct.
            ctx.line("uint8_t raw = static_cast<uint8_t>(*bits & ((1 << CHAR_BITS) - 1));");
            ctx.line("char ch = (raw == 0) ? '\\0' : static_cast<char>(raw < 32 ? raw + 0x40 : raw);");
        } else {
            ctx.line("char ch = static_cast<char>(*bits & ((1 << CHAR_BITS) - 1));");
        }
        ctx.line("result.value_ += ch;");
        ctx.dedent();
        ctx.line("}");
        emit_trim_code(ctx, "result.value_", t.trim, t.padding);
        ctx.line("return result;");
        ctx.dedent();
        ctx.line("}");

        ctx.dedent();
        ctx.line("private:");
        ctx.indent();
        ctx.line("std::string value_;");
        ctx.dedent();
        ctx.line("};");
        ctx.line();
    } else if (t.length) {
        // Fixed-length string: simple type alias or wrapper
        std::string name = to_cpp_type_name(t.name);
        int wire_bytes = *t.length;

        ctx.comment(t.doc.empty() ? name : t.doc);
        ctx.line("class " + name + " {");
        ctx.line("public:");
        ctx.indent();
        ctx.line("static constexpr size_t WIRE_SIZE = " + std::to_string(wire_bytes) + ";");
        ctx.line();
        ctx.line("std::string value() const { return value_; }");
        ctx.line("void set_value(const std::string& v) { value_ = v; }");
        ctx.line();
        ctx.line("bool operator==(const " + name + "&) const = default;");
        ctx.line();
        {
            std::string pad_char = "'\\0'";
            if (t.padding == model::StringPadding::Space) {
                // EBCDIC space is 0x40; padding is applied after encoding conversion
                pad_char = (t.encoding == model::StringEncoding::Ebcdic) ? "'\\x40'" : "' '";
            }
            ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
            ctx.indent();
            if (needs_encoding_conversion(t.encoding)) {
                ctx.line("auto wire = conduit::string::from_ascii(value_, " + encoding_enum_str(t.encoding) + ");");
                ctx.line("w.write_string(wire, " + std::to_string(wire_bytes) + ", " + pad_char + ");");
            } else {
                ctx.line("w.write_string(value_, " + std::to_string(wire_bytes) + ", " + pad_char + ");");
            }
            ctx.line("if (w.has_error()) return std::unexpected(w.error());");
            ctx.line("return {};");
            ctx.dedent();
            ctx.line("}");
        }
        ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
        ctx.indent();
        ctx.line("auto s = r.read_string(" + std::to_string(wire_bytes) + ");");
        ctx.line("if (!s) return std::unexpected(s.error());");
        ctx.line(name + " result;");
        if (needs_encoding_conversion(t.encoding)) {
            ctx.line("result.value_ = conduit::string::to_ascii(*s, " + encoding_enum_str(t.encoding) + ");");
        } else {
            ctx.line("result.value_ = std::move(*s);");
        }
        emit_trim_code(ctx, "result.value_", t.trim, t.padding);
        ctx.line("return result;");
        ctx.dedent();
        ctx.line("}");
        ctx.dedent();
        ctx.line("private:");
        ctx.indent();
        ctx.line("std::string value_;");
        ctx.dedent();
        ctx.line("};");
        ctx.line();
    } else if (t.terminated) {
        // G5: Terminated type-level string
        std::string name = to_cpp_type_name(t.name);
        std::string term_str = *t.terminated;
        std::string term_byte;
        if (term_str == "null") term_byte = "0x00";
        else if (term_str == "newline") term_byte = "0x0A";
        else if (term_str == "crlf") term_byte = "0x0D"; // first byte of CRLF
        else term_byte = term_str;
        int max_len = t.max_length ? *t.max_length : 65535;

        ctx.comment(t.doc.empty() ? name : t.doc);
        ctx.line("class " + name + " {");
        ctx.line("public:");
        ctx.indent();
        ctx.line("std::string value() const { return value_; }");
        if (t.max_length) {
            ctx.line("[[nodiscard]] conduit::VoidResult set_value(const std::string& v) {");
            ctx.indent();
            ctx.line("if (v.size() > " + std::to_string(*t.max_length) + ")");
            ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::StringTooLong,");
            ctx.line("        \"" + name + " exceeds max length " + std::to_string(*t.max_length) + "\"));");
            ctx.line("value_ = v;");
            ctx.line("return {};");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("void set_value(const std::string& v) { value_ = v; }");
        }
        ctx.line();
        ctx.line("bool operator==(const " + name + "&) const = default;");
        ctx.line();

        // Encode: write string bytes + terminator
        ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
        ctx.indent();
        ctx.line("w.write_string(value_, value_.size());");
        if (term_str == "crlf") {
            ctx.line("w.write_u8(0x0D);");
            ctx.line("w.write_u8(0x0A);");
        } else {
            ctx.line("w.write_u8(" + term_byte + ");");
        }
        ctx.line("if (w.has_error()) return std::unexpected(w.error());");
        ctx.line("return {};");
        ctx.dedent();
        ctx.line("}");

        // Decode: read until terminator
        ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
        ctx.indent();
        ctx.line(name + " result;");
        ctx.line("std::string s;");
        ctx.line("s.reserve(64);");
        if (term_str == "crlf") {
            ctx.line("for (size_t i = 0; i < " + std::to_string(max_len) + "; i++) {");
            ctx.indent();
            ctx.line("auto b = r.read_u8();");
            ctx.line("if (!b) return std::unexpected(b.error());");
            ctx.line("if (*b == 0x0D) {");
            ctx.indent();
            ctx.line("auto b2 = r.read_u8();");
            ctx.line("if (!b2) return std::unexpected(b2.error());");
            ctx.line("if (*b2 == 0x0A) break;");
            ctx.line("s += static_cast<char>(*b);");
            ctx.line("s += static_cast<char>(*b2);");
            ctx.dedent();
            ctx.line("} else {");
            ctx.indent();
            ctx.line("s += static_cast<char>(*b);");
            ctx.dedent();
            ctx.line("}");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("for (size_t i = 0; i < " + std::to_string(max_len) + "; i++) {");
            ctx.indent();
            ctx.line("auto b = r.read_u8();");
            ctx.line("if (!b) return std::unexpected(b.error());");
            ctx.line("if (*b == " + term_byte + ") break;");
            ctx.line("s += static_cast<char>(*b);");
            ctx.dedent();
            ctx.line("}");
        }
        emit_trim_code(ctx, "s", t.trim, t.padding);
        ctx.line("result.value_ = std::move(s);");
        ctx.line("return result;");
        ctx.dedent();
        ctx.line("}");

        ctx.dedent();
        ctx.line("private:");
        ctx.indent();
        ctx.line("std::string value_;");
        ctx.dedent();
        ctx.line("};");
        ctx.line();
    }
}

// B2: Emit a wrapper class for constrained (non-scaled, non-enum, non-flags) types
void emit_constrained_type(EmitContext& ctx, const model::TypeDef& t) {
    std::string name = to_cpp_type_name(t.name);
    bool is_signed = (t.base == model::PrimitiveBase::Int);
    std::string raw_type = storage_type_for_bits(t.bits, is_signed);
    std::string endian = endian_enum(t.endian);

    if (!t.doc.empty()) {
        ctx.comment(t.doc);
    }

    ctx.line("class " + name + " {");
    ctx.line("public:");
    ctx.indent();

    ctx.line(raw_type + " value() const { return raw_; }");
    ctx.line(raw_type + " raw() const { return raw_; }");

    // Validating set_value / set_raw
    {
        auto emit_setter_body = [&](const std::string& setter_name, const std::string& param_type) {
            ctx.line("[[nodiscard]] conduit::VoidResult " + setter_name + "(" + param_type + " v) {");
            ctx.indent();
            if (t.constraint->equals) {
                ctx.line("if (v != static_cast<" + raw_type + ">(" + *t.constraint->equals + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " constraint: expected " + *t.constraint->equals + "\"));");
            }
            if (t.constraint->max) {
                ctx.line("if (v > static_cast<" + raw_type + ">(" + *t.constraint->max + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " exceeds max " + *t.constraint->max + "\"));");
            }
            if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
                ctx.line("if (v < static_cast<" + raw_type + ">(" + *t.constraint->min + "))");
                ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx.line("        \"" + name + " below min " + *t.constraint->min + "\"));");
            }
            ctx.line("raw_ = v;");
            ctx.line("return {};");
            ctx.dedent();
            ctx.line("}");
        };
        emit_setter_body("set_value", raw_type);
        emit_setter_body("set_raw", raw_type);
    }

    ctx.line();
    ctx.line("bool operator==(const " + name + "&) const = default;");
    ctx.line();

    // Encode
    ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx.indent();
    type_write_stmt(ctx, "raw_", t);
    ctx.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");

    // Decode with constraint validation
    ctx.line("static conduit::Result<" + name + "> decode(conduit::io::BitReader& r) {");
    ctx.indent();
    ctx.line("auto raw = " + type_read_expr(t) + ";");
    ctx.line("if (!raw) return std::unexpected(raw.error());");
    ctx.line(name + " result;");
    ctx.line("result.raw_ = static_cast<" + raw_type + ">(*raw);");
    // Constraint checks
    if (t.constraint->equals) {
        ctx.line("if (result.raw_ != static_cast<" + raw_type + ">(" + *t.constraint->equals + ")) {");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx.line("    \"" + name + " constraint violation: expected " + *t.constraint->equals + "\"));");
        ctx.dedent();
        ctx.line("}");
    }
    if (t.constraint->max) {
        ctx.line("if (result.raw_ > static_cast<" + raw_type + ">(" + *t.constraint->max + ")) {");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx.line("    \"" + name + " exceeds max " + *t.constraint->max + "\"));");
        ctx.dedent();
        ctx.line("}");
    }
    // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
    if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
        ctx.line("if (result.raw_ < static_cast<" + raw_type + ">(" + *t.constraint->min + ")) {");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx.line("    \"" + name + " below min " + *t.constraint->min + "\"));");
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");

    ctx.dedent();
    ctx.line("private:");
    ctx.indent();
    ctx.line(raw_type + " raw_ = 0;");
    ctx.dedent();
    ctx.line("};");
    ctx.line();
}

} // anonymous namespace

std::string generate_types(const model::Protocol& protocol,
                           const std::string& ns) {
    EmitContext ctx;

    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("#pragma once");
    ctx.line();
    ctx.line("#include <conduit/core/error.hpp>");
    ctx.line("#include <conduit/io/bit_reader.hpp>");
    ctx.line("#include <conduit/io/bit_writer.hpp>");
    ctx.line("#include <conduit/io/endian.hpp>");
    // G6: Conditionally include encoding header if any string type needs conversion
    {
        bool needs_enc = false;
        for (const auto& t : protocol.types) {
            if (t.base == model::PrimitiveBase::String && needs_encoding_conversion(t.encoding)) {
                needs_enc = true;
                break;
            }
        }
        if (needs_enc) {
            ctx.line("#include <conduit/string/encoding.hpp>");
        }
    }
    ctx.line("#include <cstdint>");
    ctx.line("#include <string>");
    ctx.line("#include <string_view>");
    ctx.line();
    ctx.line("namespace " + ns + " {");
    ctx.line();

    for (const auto& t : protocol.types) {
        bool is_enum = !t.enum_values.empty();
        bool is_flags = !t.flags.empty();
        bool has_scale = t.scale.has_value() || t.offset.has_value();
        bool is_string = (t.base == model::PrimitiveBase::String);

        if (is_enum) {
            emit_enum_type(ctx, t);
        } else if (is_flags) {
            emit_flags_type(ctx, t);
        } else if (has_scale) {
            emit_scaled_type(ctx, t);
        } else if (is_string) {
            emit_string_type(ctx, t);
        } else if (t.constraint) {
            // B2: Non-scaled types with constraints get a wrapper class
            emit_constrained_type(ctx, t);
        } else {
            // Simple alias
            emit_simple_alias(ctx, t);
        }
    }

    ctx.line();
    ctx.line("} // namespace " + ns);
    ctx.line();

    return ctx.str();
}

} // namespace bgen::codegen
