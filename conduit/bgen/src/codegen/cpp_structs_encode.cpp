// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Encode Methods

#include "cpp_structs_emitter.hpp"
#include <cassert>

namespace bgen::codegen {

// ========================================================================
// Encode for plain struct
// ========================================================================

void StructEmitter::emit_encode(const std::vector<model::StructChild>& children) {
    ctx_.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx_.indent();
    reset_alignment();

    // Check for auto-length fields; if present, record struct start position
    bool has_auto_length = false;
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length
                && f->auto_expr->field_ref.empty()) {
                has_auto_length = true;
                break;
            }
        }
    }
    if (has_auto_length) {
        ctx_.line("auto struct_start_pos_ = w.size_bytes();");
    }
    pending_auto_length_.reset();
    pending_auto_length_ref_.reset();

    emit_encode_children(children);

    // Auto-length backpatch: write actual struct size into placeholder
    if (pending_auto_length_) {
        std::string size_expr = apply_arith("w.size_bytes() - struct_start_pos_", pending_auto_length_->modifier);
        std::string cast_type = storage_type_for_bits(pending_auto_length_->bits, false);
        std::string patch_call;
        if (pending_auto_length_->bits <= 8) {
            patch_call = "w.patch_u8(length_byte_pos_, static_cast<" + cast_type + ">(" + size_expr + "))";
        } else if (pending_auto_length_->bits <= 16) {
            patch_call = "w.patch_u16(length_byte_pos_, static_cast<" + cast_type + ">(" + size_expr + "), "
                         + endian_str(pending_auto_length_->endian) + ")";
        } else {
            patch_call = "w.patch_u32(length_byte_pos_, static_cast<" + cast_type + ">(" + size_expr + "), "
                         + endian_str(pending_auto_length_->endian) + ")";
        }
        ctx_.line("if (!" + patch_call + ")");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument, \"failed to patch auto-length\"));");
        pending_auto_length_.reset();
    }

    ctx_.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx_.line("return {};");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();
}

// Helper to get the BMDL name from a StructChild
static std::string get_child_bmdl_name(const model::StructChild& child) {
    return std::visit([](const auto& c) -> std::string {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, model::Field>) return c.name;
        else if constexpr (std::is_same_v<T, model::StructDef>) return c.name;
        else if constexpr (std::is_same_v<T, model::ArrayDef>) return c.name;
        else if constexpr (std::is_same_v<T, model::ChoiceDef>) return c.name;
        else return {};
    }, child);
}

void StructEmitter::emit_encode_children(const std::vector<model::StructChild>& children) {
    for (const auto& child : children) {
        // Auto-length(field) tracking: emit start marker before the target
        std::string child_name = get_child_bmdl_name(child);
        bool is_length_ref_target = pending_auto_length_ref_.has_value() &&
                                     child_name == pending_auto_length_ref_->target_name;
        if (is_length_ref_target) {
            std::string start_var = to_accessor_name(child_name) + "_start_";
            ctx_.line("auto " + start_var + " = w.size_bytes();");
        }

        std::visit([this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                if (c.bit || c.present_when) {
                    // Optional field: wrap encode in has_value check
                    std::string member = to_member_name(c.name);
                    ctx_.line("if (" + member + ".has_value()) {");
                    ctx_.indent();
                    auto fti = resolve_field_type(c, index_);
                    // Override for inline enums (fields with <enum> values but no type_ref)
                    if (!c.enum_values.empty() && c.type_ref.empty()) {
                        std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(c.name);
                        fti.cpp_type = enum_name;
                        fti.is_enum = true;
                    }
                    // I2: Constraint check for optional field (dereference the optional)
                    // Skip for byte-array fields (bytes > 8) — constraints were already warned as ignored
                    if (c.constraint && !fti.is_struct && !fti.is_enum && !fti.is_bytes) {
                        emit_encode_constraint_check(*c.constraint, "*" + member, c.name, fti.is_signed);
                    }
                    // I2: max_length check for optional strings/bytes
                    if (c.max_length && (fti.is_string || fti.is_bytes)) {
                        ctx_.line("if (" + member + "->size() > " + std::to_string(*c.max_length) + ")");
                        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::StringTooLong,");
                        ctx_.line("        \"" + c.name + " exceeds max length " + std::to_string(*c.max_length) + "\"));");
                    }
                    if (fti.is_enum) {
                        ctx_.line("CONDUIT_TRY(encode_" + fti.cpp_type + "(*" + member + ", w));");
                    } else if (fti.has_field_scale) {
                        // Optional scaled field: inverse scale/offset, write raw
                        std::string raw_type = storage_type_for_bits(fti.raw_bits, fti.raw_signed);
                        std::string scale_str = double_literal(fti.field_scale);
                        std::string offset_str = double_literal(fti.field_offset);
                        std::string expr;
                        if (fti.field_offset != 0.0)
                            expr = "static_cast<" + raw_type + ">((*" + member + " - " + offset_str + ") / " + scale_str + ")";
                        else
                            expr = "static_cast<" + raw_type + ">(*" + member + " / " + scale_str + ")";
                        FieldTypeInfo raw_fti;
                        raw_fti.bits = fti.raw_bits;
                        raw_fti.is_signed = fti.raw_signed;
                        raw_fti.wire_encoding = fti.wire_encoding;
                        emit_write_stmt(ctx_, expr, raw_fti, fti.raw_endian, is_byte_aligned());
                    } else if (fti.is_struct || fti.is_string || fti.is_bytes) {
                        if (fti.is_string) {
                            if (c.char_bits && c.length) {
                                // Packed character encode
                                int char_bits = *c.char_bits;
                                int char_count = *c.length;
                                ctx_.line("for (int i = 0; i < " + std::to_string(char_count) + "; i++) {");
                                ctx_.indent();
                                ctx_.line("uint8_t ch = (static_cast<size_t>(i) < " + member + "->size()) ? static_cast<uint8_t>((*" + member + ")[i]) : 0x20;");
                                if (char_bits < 7) {
                                    ctx_.line("if (ch >= 'a' && ch <= 'z') ch -= 32;");
                                }
                                ctx_.line("w.write_bits(ch & ((1 << " + std::to_string(char_bits) + ") - 1), " + std::to_string(char_bits) + ");");
                                ctx_.dedent();
                                ctx_.line("}");
                            } else if (c.length) {
                                std::string pad = resolve_padding_char(c);
                                std::string val_expr = "*" + member;
                                if (field_needs_encoding(c))
                                    val_expr = "conduit::string::from_ascii(*" + member + ", " + field_encoding_enum(c) + ")";
                                ctx_.line("w.write_string(" + val_expr + ", " + std::to_string(*c.length) + ", " + pad + ");");
                            } else {
                                ctx_.line("w.write_string(*" + member + ", " + member + "->size());");
                            }
                        } else if (fti.is_bytes) {
                            ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + member + "->size()));");
                        } else {
                            ctx_.line("CONDUIT_TRY(" + member + "->encode(w));");
                        }
                    } else {
                        emit_write_stmt(ctx_, "*" + member, fti, c.endian, is_byte_aligned());
                    }
                    ctx_.dedent();
                    ctx_.line("}");
                    // Conditional: alignment unknown after optional field
                    advance_bits_variable();
                } else {
                    emit_encode_field(c);
                }
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    // A12: Optional struct encode
                    if (c.bit || c.present_when) {
                        ctx_.line("if (" + to_member_name(c.name) + ".has_value()) {");
                        ctx_.indent();
                        ctx_.line("CONDUIT_TRY(" + to_member_name(c.name) + "->encode(w));");
                        ctx_.dedent();
                        ctx_.line("}");
                        advance_bits_variable();
                    } else {
                        ctx_.line("CONDUIT_TRY(" + to_member_name(c.name) + ".encode(w));");
                        advance_bits_variable();
                    }
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                // A12: Optional array encode
                if (c.bit || c.present_when) {
                    ctx_.line("if (" + to_member_name(c.name) + ".has_value()) {");
                    ctx_.indent();
                    emit_encode_array(c, true);
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    emit_encode_array(c);
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                // A12: Optional choice encode
                if (c.bit || c.present_when) {
                    ctx_.line("if (" + to_member_name(c.name) + ".has_value()) {");
                    ctx_.indent();
                    emit_encode_choice(c, true);
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    emit_encode_choice(c);
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                emit_encode_fx(c);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::Reserved>) {
                int remaining = c.bits;
                while (remaining > 64) {
                    ctx_.line("w.write_bits(0, 64);");
                    remaining -= 64;
                }
                ctx_.line("w.write_bits(0, " + std::to_string(remaining) + ");");
                advance_bits(c.bits);
            } else if constexpr (std::is_same_v<T, model::Align>) {
                ctx_.line("w.align_to(" + std::to_string(c.to) + ");");
                bit_mod8_ = 0;
            }
        }, child);

        // Auto-length(field) backpatch: after the target field, emit the patch
        if (is_length_ref_target && pending_auto_length_ref_) {
            auto& ref = *pending_auto_length_ref_;
            std::string start_var = to_accessor_name(child_name) + "_start_";
            std::string pos_var = to_accessor_name(ref.target_name) + "_length_pos_";
            std::string length_expr = apply_arith("w.size_bytes() - " + start_var, ref.modifier);
            std::string cast_type = storage_type_for_bits(ref.bits, false);
            std::string patch_call;
            if (ref.bits <= 8) {
                patch_call = "w.patch_u8(" + pos_var + ", static_cast<" + cast_type + ">(" + length_expr + "))";
            } else if (ref.bits <= 16) {
                patch_call = "w.patch_u16(" + pos_var + ", static_cast<" + cast_type + ">(" + length_expr + "), "
                             + endian_str(ref.endian) + ")";
            } else {
                patch_call = "w.patch_u32(" + pos_var + ", static_cast<" + cast_type + ">(" + length_expr + "), "
                             + endian_str(ref.endian) + ")";
            }
            ctx_.line("if (!" + patch_call + ")");
            ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument, \"failed to patch auto-length(field)\"));");
            pending_auto_length_ref_.reset();
        }
    }
}

// A8: Emit constraint check before encoding — returns error on violation
void StructEmitter::emit_encode_constraint_check(const model::Constraint& c, const std::string& member,
                                                   const std::string& field_name, bool is_signed) {
    if (c.equals) {
        ctx_.line("if (" + member + " != static_cast<decltype(" + member + ")>(" + *c.equals + "))");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
        ctx_.line("        \"" + field_name + " constraint: expected " + *c.equals + "\"));");
    }
    if (c.max) {
        ctx_.line("if (" + member + " > " + *c.max + ")");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
        ctx_.line("        \"" + field_name + " exceeds max " + *c.max + "\"));");
    }
    // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
    if (c.min && (*c.min != "0" || is_signed)) {
        ctx_.line("if (" + member + " < " + *c.min + ")");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
        ctx_.line("        \"" + field_name + " below min " + *c.min + "\"));");
    }
}

void StructEmitter::emit_encode_field(const model::Field& f) {
    std::string member = to_member_name(f.name);
    auto fti = resolve_field_type(f, index_);

    // Override for inline enums (fields with <enum> values but no type_ref)
    if (!f.enum_values.empty() && f.type_ref.empty()) {
        std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(f.name);
        fti.cpp_type = enum_name;
        fti.is_enum = true;
    }

    // Auto-length: write zero placeholder for later backpatch
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length &&
        f.auto_expr->field_ref.empty()) {
        ctx_.line("auto length_byte_pos_ = w.size_bytes();");
        emit_write_stmt(ctx_, "0", fti, f.endian, is_byte_aligned());
        advance_bits(fti.bits);
        pending_auto_length_ = AutoLengthInfo{fti.bits, f.endian, f.auto_expr->modifier};
        return;
    }

    // Auto-length(field): write zero placeholder for target-specific backpatch
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length &&
        !f.auto_expr->field_ref.empty()) {
        std::string pos_var = to_accessor_name(f.auto_expr->field_ref) + "_length_pos_";
        ctx_.line("auto " + pos_var + " = w.size_bytes();");
        emit_write_stmt(ctx_, "0", fti, f.endian, is_byte_aligned());
        advance_bits(fti.bits);
        pending_auto_length_ref_ = AutoLengthFieldRefInfo{fti.bits, f.endian, f.auto_expr->modifier, f.auto_expr->field_ref};
        return;
    }

    // Auto-count: write the referenced array's size
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Count) {
        std::string array_member = to_member_name(f.auto_expr->field_ref);
        std::string size_expr;
        if (optional_field_names_.count(array_member)) {
            size_expr = array_member + ".has_value() ? static_cast<" + fti.cpp_type + ">(" + array_member + "->size()) : static_cast<" + fti.cpp_type + ">(0)";
        } else {
            size_expr = "static_cast<" + fti.cpp_type + ">(" + array_member + ".size())";
        }
        emit_write_stmt(ctx_, size_expr, fti, f.endian, is_byte_aligned());
        advance_bits(fti.bits);
        return;
    }

    // A8: Constraint check before encoding
    // Skip for byte-array fields (bytes > 8) — constraints were already warned as ignored
    if (f.constraint && !fti.is_struct && !fti.is_enum && !fti.is_bytes) {
        emit_encode_constraint_check(*f.constraint, member, f.name, fti.is_signed);
    }

    // G2: max_length check before encoding strings/bytes
    if (f.max_length && (fti.is_string || fti.is_bytes)) {
        ctx_.line("if (" + member + ".size() > " + std::to_string(*f.max_length) + ")");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::StringTooLong,");
        ctx_.line("        \"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\"));");
    }

    if (f.is_inline) {
        // Inline struct: encode its children directly (fields are flattened)
        // Save/restore pending_auto_length_ref_ to prevent inline children
        // from accidentally matching the outer target name
        auto resolved = index_.find(f.type_ref);
        if (resolved) {
            auto saved_ref = std::exchange(pending_auto_length_ref_, std::nullopt);
            std::visit([this](const auto* def) {
                using DT = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<DT, model::StructDef>) {
                    emit_encode_children(def->children);
                } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                    emit_encode_children(def->children);
                }
            }, *resolved);
            pending_auto_length_ref_ = saved_ref;
        }
        return;
    }

    if (fti.has_field_scale) {
        // Inline scale: convert double -> raw integer and write
        std::string raw_type = storage_type_for_bits(fti.raw_bits, fti.raw_signed);
        std::string scale_str = double_literal(fti.field_scale);
        std::string offset_str = double_literal(fti.field_offset);
        std::string expr;
        if (fti.field_offset != 0.0)
            expr = "static_cast<" + raw_type + ">((" + member + " - " + offset_str + ") / " + scale_str + ")";
        else
            expr = "static_cast<" + raw_type + ">(" + member + " / " + scale_str + ")";
        FieldTypeInfo raw_fti;
        raw_fti.bits = fti.raw_bits;
        raw_fti.is_signed = fti.raw_signed;
        raw_fti.wire_encoding = fti.wire_encoding;
        emit_write_stmt(ctx_, expr, raw_fti, fti.raw_endian, is_byte_aligned());
        advance_bits(fti.raw_bits);
    } else if (fti.is_enum) {
        // Enum: use encode_<type> function
        ctx_.line("CONDUIT_TRY(encode_" + fti.cpp_type + "(" + member + ", w));");
        advance_bits(fti.bits);
    } else if (fti.is_struct || fti.is_string || fti.is_bytes) {
        if (fti.is_string) {
            if (f.char_bits && f.length) {
                // A13: Packed character encode loop
                int char_bits = *f.char_bits;
                int char_count = *f.length;
                ctx_.line("for (int i = 0; i < " + std::to_string(char_count) + "; i++) {");
                ctx_.indent();
                ctx_.line("uint8_t ch = (static_cast<size_t>(i) < " + member + ".size()) ? static_cast<uint8_t>(" + member + "[i]) : 0x20;");
                if (char_bits < 7) {
                    ctx_.line("if (ch >= 'a' && ch <= 'z') ch -= 32;");
                }
                ctx_.line("w.write_bits(ch & ((1 << " + std::to_string(char_bits) + ") - 1), " + std::to_string(char_bits) + ");");
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.terminated) {
                // A1: Terminated string encode — write bytes then terminator
                ctx_.line("w.write_string(" + member + ", " + member + ".size());");
                std::string term = *f.terminated;
                if (term == "crlf") {
                    ctx_.line("w.write_u8(0x0D);");
                    ctx_.line("w.write_u8(0x0A);");
                } else if (term == "null") {
                    ctx_.line("w.write_u8(0x00);");
                } else if (term == "newline") {
                    ctx_.line("w.write_u8(0x0A);");
                } else {
                    ctx_.line("w.write_u8(" + term + ");");
                }
            } else if (f.length) {
                std::string pad = resolve_padding_char(f);
                if (field_needs_encoding(f)) {
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto wire = conduit::string::from_ascii(" + member + ", " + field_encoding_enum(f) + ");");
                    ctx_.line("w.write_string(wire, " + std::to_string(*f.length) + ", " + pad + ");");
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    ctx_.line("w.write_string(" + member + ", " + std::to_string(*f.length) + ", " + pad + ");");
                }
            } else if (f.length_prefix) {
                // A2/A3: Write length prefix then string
                auto pti = resolve_prefix_type(*f.length_prefix, index_);
                std::string val_expr = member;
                if (field_needs_encoding(f)) {
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto wire = conduit::string::from_ascii(" + member + ", " + field_encoding_enum(f) + ");");
                    val_expr = "wire";
                    if (f.length_includes_prefix) {
                        int prefix_bytes = get_prefix_bytes(pti);
                        emit_prefix_write(ctx_, pti, "wire.size() + " + std::to_string(prefix_bytes));
                    } else {
                        emit_prefix_write(ctx_, pti, "wire.size()");
                    }
                    ctx_.line("w.write_string(wire, wire.size());");
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    if (f.length_includes_prefix) {
                        int prefix_bytes = get_prefix_bytes(pti);
                        emit_prefix_write(ctx_, pti, member + ".size() + " + std::to_string(prefix_bytes));
                    } else {
                        emit_prefix_write(ctx_, pti, member + ".size()");
                    }
                    ctx_.line("w.write_string(" + member + ", " + member + ".size());");
                }
            } else if (f.length_from) {
                ctx_.line("w.write_string(" + member + ", " + member + ".size());");
            } else {
                ctx_.line("w.write_string(" + member + ", " + member + ".size());");
            }
        } else if (fti.is_bytes) {
            if (f.length) {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + ".data(), " +
                         std::to_string(*f.length) + "));");
            } else if (f.length_from) {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + ".data(), " + member + ".size()));");
            } else if (f.length_star) {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + ".data(), " + member + ".size()));");
            } else {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + ".data(), " + member + ".size()));");
            }
        } else {
            // Check if it's an enum
            auto resolved = index_.find(f.type_ref);
            bool is_enum = false;
            if (resolved) {
                std::visit([&is_enum](const auto* def) {
                    using DT = std::decay_t<decltype(*def)>;
                    if constexpr (std::is_same_v<DT, model::TypeDef>) {
                        is_enum = !def->enum_values.empty();
                    }
                }, *resolved);
            }
            if (is_enum) {
                ctx_.line("CONDUIT_TRY(encode_" + fti.cpp_type + "(" + member + ", w));");
            } else {
                ctx_.line("CONDUIT_TRY(" + member + ".encode(w));");
            }
        }
        advance_bits_variable();
    } else {
        // Simple numeric
        emit_write_stmt(ctx_, member, fti, f.endian, is_byte_aligned());
        advance_bits(fti.bits);
    }
}

void StructEmitter::emit_encode_array(const model::ArrayDef& a, bool is_optional) {
    std::string member = to_member_name(a.name);
    std::string ref = is_optional ? ("*" + member) : member;

    // Check if element type is primitive (simple alias), enum, or struct
    bool elem_is_primitive = false;
    bool elem_is_enum = false;
    std::string elem_type_name;
    model::Endian elem_endian = model::Endian::Big;
    FieldTypeInfo elem_fti;
    if (!a.type_ref.empty()) {
        auto resolved = index_.find(a.type_ref);
        if (resolved) {
            std::visit([&](const auto* def) {
                using DT = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                    bool has_enum = !def->enum_values.empty();
                    bool has_flags = !def->flags.empty();
                    bool has_scale = def->scale.has_value() || def->offset.has_value();
                    bool is_string_type = (def->base == model::PrimitiveBase::String);
                    bool has_constraint = def->constraint.has_value();
                    bool has_wrapper = has_enum || has_flags || has_scale || is_string_type || has_constraint;
                    if (has_enum) {
                        elem_is_enum = true;
                        elem_type_name = to_cpp_type_name(def->name);
                    } else if (!has_wrapper && def->bits > 0) {
                        elem_is_primitive = true;
                        elem_fti.bits = def->bits;
                        elem_fti.is_signed = (def->base == model::PrimitiveBase::Int);
                        elem_fti.is_float = (def->base == model::PrimitiveBase::Float);
                        elem_fti.wire_encoding = def->wire_encoding;
                        elem_endian = def->endian;
                    }
                }
            }, *resolved);
        }
    }

    ctx_.line("for (const auto& elem : " + ref + ") {");
    ctx_.indent();
    if (elem_is_enum) {
        ctx_.line("CONDUIT_TRY(encode_" + elem_type_name + "(elem, w));");
    } else if (elem_is_primitive) {
        emit_write_stmt(ctx_, "elem", elem_fti, elem_endian, is_byte_aligned());
    } else {
        ctx_.line("CONDUIT_TRY(elem.encode(w));");
    }
    ctx_.dedent();
    ctx_.line("}");
}

// Encode an array inside an FX block. The member is optional<vector>.
void StructEmitter::emit_encode_fx_array(const model::ArrayDef& a) {
    std::string member = to_member_name(a.name);

    if (!a.fixed_count) {
        // Dynamic arrays in FX: encode what's there (or nothing)
        ctx_.line("if (" + member + ".has_value()) {");
        ctx_.indent();
        emit_encode_array(a, /*is_optional=*/true);
        ctx_.dedent();
        ctx_.line("}");
        return;
    }

    // Fixed-count array: always encode exactly N elements
    int count = *a.fixed_count;
    ctx_.line("for (int i = 0; i < " + std::to_string(count) + "; i++) {");
    ctx_.indent();
    ctx_.line("if (" + member + ".has_value() && i < static_cast<int>(" + member + "->size())) {");
    ctx_.indent();

    // Determine element encoding
    bool elem_is_primitive = false;
    bool elem_is_enum = false;
    std::string enum_type_name;
    model::Endian elem_endian = model::Endian::Big;
    FieldTypeInfo elem_fti;
    if (!a.type_ref.empty()) {
        auto resolved = index_.find(a.type_ref);
        if (resolved) {
            std::visit([&](const auto* def) {
                using DT = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                    if (!def->enum_values.empty()) {
                        elem_is_enum = true;
                        enum_type_name = to_cpp_type_name(def->name);
                        elem_fti.bits = def->bits;
                        elem_fti.is_signed = (def->base == model::PrimitiveBase::Int);
                        elem_fti.wire_encoding = def->wire_encoding;
                        elem_endian = def->endian;
                    } else if (def->bits > 0) {
                        elem_is_primitive = true;
                        elem_fti.bits = def->bits;
                        elem_fti.is_signed = (def->base == model::PrimitiveBase::Int);
                        elem_fti.is_float = (def->base == model::PrimitiveBase::Float);
                        elem_fti.wire_encoding = def->wire_encoding;
                        elem_fti.cpp_type = storage_type_for_bits(def->bits, def->base == model::PrimitiveBase::Int);
                        elem_endian = def->endian;
                    }
                }
            }, *resolved);
        }
    }

    if (elem_is_enum) {
        ctx_.line("CONDUIT_TRY(encode_" + enum_type_name + "((*" + member + ")[static_cast<size_t>(i)], w));");
    } else if (elem_is_primitive) {
        emit_write_stmt(ctx_, "(*" + member + ")[static_cast<size_t>(i)]", elem_fti, elem_endian, is_byte_aligned());
    } else {
        ctx_.line("CONDUIT_TRY((*" + member + ")[static_cast<size_t>(i)].encode(w));");
    }

    ctx_.dedent();
    ctx_.line("} else {");
    ctx_.indent();
    // Default: write zeros for the element
    if (elem_is_enum || elem_is_primitive) {
        emit_write_stmt(ctx_, "0", elem_fti, elem_endian, is_byte_aligned());
    } else {
        // Struct element: default-construct and encode
        std::string elem_type = !a.type_ref.empty() ? to_cpp_type_name(a.type_ref)
                                                     : get_child_class_name(a.name + "Element");
        ctx_.line("CONDUIT_TRY(" + elem_type + "{}.encode(w));");
    }
    ctx_.dedent();
    ctx_.line("}");
    ctx_.dedent();
    ctx_.line("}");
}

void StructEmitter::emit_encode_choice(const model::ChoiceDef& c, bool is_optional) {
    // Choice encoding: validate variant/discriminator match, then visit
    std::string member = to_member_name(c.name);
    std::string ref = is_optional ? ("*" + member) : member;

    // Validate discriminator matches the active variant alternative.
    // Only when switch expression is a local FieldRef (not outer-scope).
    if (c.switch_expr && c.switch_expr->op == model::ExprOp::FieldRef) {
        std::string root = c.switch_expr->name;
        auto dot = root.find('.');
        if (dot != std::string::npos) root = root.substr(0, dot);

        if (outer_scope_params_.count(root) == 0) {
            std::string switch_val = emit_expr_code(*c.switch_expr, {});
            // Dereference optional switch field
            if (optional_field_names_.count(to_member_name(root))) {
                switch_val = "*(" + switch_val + ")";
            }
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("auto _sw = " + switch_val + ";");
            ctx_.line("auto _idx = (" + ref + ").index();");

            for (size_t i = 0; i < c.cases.size(); i++) {
                const auto& cs = c.cases[i];
                std::string prefix = (i == 0) ? "if" : "} else if";
                ctx_.line(prefix + " (_idx == " + std::to_string(i) + ") {");
                ctx_.indent();

                std::string cond;
                if (cs.value) {
                    std::string val = *cs.value;
                    auto const_it = index_.constants.find(val);
                    if (const_it == index_.constants.end()) {
                        bool is_numeric = !val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) ||
                                          val[0] == '-' || (val.size() > 2 && val[0] == '0' && val[1] == 'x'));
                        if (!is_numeric) {
                            val = resolve_enum_value(val);
                        }
                    }
                    cond = "_sw != static_cast<decltype(_sw)>(" + val + ")";
                } else if (cs.range) {
                    auto dot_pos = cs.range->find("..");
                    if (dot_pos != std::string::npos) {
                        std::string lo = cs.range->substr(0, dot_pos);
                        std::string hi = cs.range->substr(dot_pos + 2);
                        cond = "_sw < static_cast<decltype(_sw)>(" + lo + ") || "
                               "_sw > static_cast<decltype(_sw)>(" + hi + ")";
                    } else {
                        cond = "_sw != static_cast<decltype(_sw)>(" + *cs.range + ")";
                    }
                }
                if (!cond.empty()) {
                    ctx_.line("if (" + cond + ")");
                    ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                    ctx_.line("        \"choice '" + c.name + "': variant does not match discriminator\"));");
                }
                ctx_.dedent();
            }

            // Otherwise: switch value must NOT match any known case
            if (c.otherwise) {
                std::string prefix = c.cases.empty() ? "if" : "} else if";
                ctx_.line(prefix + " (_idx == " + std::to_string(c.cases.size()) + ") {");
                ctx_.indent();
                std::string bad;
                for (const auto& cs : c.cases) {
                    if (cs.value) {
                        std::string val = *cs.value;
                        auto const_it = index_.constants.find(val);
                        if (const_it == index_.constants.end()) {
                            bool is_numeric = !val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) ||
                                              val[0] == '-' || (val.size() > 2 && val[0] == '0' && val[1] == 'x'));
                            if (!is_numeric) {
                                val = resolve_enum_value(val);
                            }
                        }
                        if (!bad.empty()) bad += " || ";
                        bad += "_sw == static_cast<decltype(_sw)>(" + val + ")";
                    } else if (cs.range) {
                        auto dot_pos = cs.range->find("..");
                        if (dot_pos != std::string::npos) {
                            std::string lo = cs.range->substr(0, dot_pos);
                            std::string hi = cs.range->substr(dot_pos + 2);
                            if (!bad.empty()) bad += " || ";
                            bad += "(_sw >= static_cast<decltype(_sw)>(" + lo + ") && "
                                   "_sw <= static_cast<decltype(_sw)>(" + hi + "))";
                        }
                    }
                }
                if (!bad.empty()) {
                    ctx_.line("if (" + bad + ")");
                    ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                    ctx_.line("        \"choice '" + c.name + "': variant does not match discriminator\"));");
                }
                ctx_.dedent();
            }

            ctx_.line("}");
            ctx_.dedent();
            ctx_.line("}");
        }
    }

    ctx_.line("{");
    ctx_.indent();
    ctx_.line("auto _enc_r = std::visit([&w](const auto& v) -> conduit::VoidResult { return v.encode(w); }, " + ref + ");");
    ctx_.line("if (!_enc_r) return std::unexpected(_enc_r.error());");
    ctx_.dedent();
    ctx_.line("}");
}

void StructEmitter::emit_encode_fx(const model::FxBlock& fx) {
    // FX encoding: check if any extension fields are set, write FX bit, then fields
    int depth = fx_depth_++;
    std::string var = "fx_continue" + (depth > 0 ? ("_" + std::to_string(depth)) : std::string{});
    ctx_.line("{");
    ctx_.indent();
    ctx_.line("bool " + var + " = false;");
    emit_fx_has_fields_check(fx.children, var);
    ctx_.line("w.write_bits(" + var + " ? 1 : 0, 1);");
    // Track the FX bit in alignment so child writes use correct byte_aligned
    advance_bits(1);
    ctx_.line("if (" + var + ") {");
    ctx_.indent();
    emit_encode_fx_children(fx.children);
    ctx_.dedent();
    ctx_.line("}");
    ctx_.dedent();
    ctx_.line("}");
    fx_depth_ = depth;
}

void StructEmitter::emit_encode_fx_children(const std::vector<model::StructChild>& children) {
    bool has_nested_fx = false;
    for (const auto& child : children) {
        std::visit([this, &has_nested_fx](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                std::string member = to_member_name(c.name);
                auto fti = resolve_field_type(c, index_);
                // Override for inline enums
                if (!c.enum_values.empty() && c.type_ref.empty()) {
                    std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(c.name);
                    fti.cpp_type = enum_name;
                    fti.is_enum = true;
                }
                // Within an FX extension, ALL fields must be written at their fixed
                // bit positions unconditionally. Use value_or(0) for primitives/enums
                // and conditional with zero-fill for structs.
                if (fti.is_enum) {
                    ctx_.line("CONDUIT_TRY(encode_" + fti.cpp_type + "(" + member + ".value_or(static_cast<" + fti.cpp_type + ">(0)), w));");
                } else if (fti.is_struct && !fti.is_string && !fti.is_bytes) {
                    // Struct: conditional write, default-construct for zero-fill
                    ctx_.line("if (" + member + ".has_value()) {");
                    ctx_.indent();
                    ctx_.line("CONDUIT_TRY(" + member + "->encode(w));");
                    ctx_.dedent();
                    ctx_.line("} else {");
                    ctx_.indent();
                    ctx_.line("CONDUIT_TRY(" + fti.cpp_type + "{}.encode(w));");
                    ctx_.dedent();
                    ctx_.line("}");
                } else if (fti.is_string) {
                    // String in FX: handle packed chars, encoding, and padding
                    if (c.char_bits && c.length) {
                        // Packed character encode (e.g. ICAO 6-bit)
                        int char_bits = *c.char_bits;
                        int char_count = *c.length;
                        std::string val_expr = member + ".value_or(\"\")";
                        ctx_.line("{");
                        ctx_.indent();
                        ctx_.line("const auto& _s = " + val_expr + ";");
                        ctx_.line("for (int i = 0; i < " + std::to_string(char_count) + "; i++) {");
                        ctx_.indent();
                        ctx_.line("uint8_t ch = (static_cast<size_t>(i) < _s.size()) ? static_cast<uint8_t>(_s[i]) : 0x20;");
                        if (char_bits < 7) {
                            ctx_.line("if (ch >= 'a' && ch <= 'z') ch -= 32;");
                        }
                        ctx_.line("w.write_bits(ch & ((1 << " + std::to_string(char_bits) + ") - 1), " + std::to_string(char_bits) + ");");
                        ctx_.dedent();
                        ctx_.line("}");
                        ctx_.dedent();
                        ctx_.line("}");
                    } else if (c.length) {
                        std::string pad = resolve_padding_char(c);
                        if (field_needs_encoding(c)) {
                            ctx_.line("{");
                            ctx_.indent();
                            ctx_.line("auto wire = conduit::string::from_ascii(" + member + ".value_or(\"\"), " + field_encoding_enum(c) + ");");
                            ctx_.line("w.write_string(wire, " + std::to_string(*c.length) + ", " + pad + ");");
                            ctx_.dedent();
                            ctx_.line("}");
                        } else {
                            ctx_.line("w.write_string(" + member + ".value_or(\"\"), " + std::to_string(*c.length) + ", " + pad + ");");
                        }
                    } else {
                        if (field_needs_encoding(c)) {
                            ctx_.line("{");
                            ctx_.indent();
                            ctx_.line("auto wire = conduit::string::from_ascii(" + member + ".value_or(\"\"), " + field_encoding_enum(c) + ");");
                            ctx_.line("w.write_string(wire, wire.size());");
                            ctx_.dedent();
                            ctx_.line("}");
                        } else {
                            ctx_.line("w.write_string(" + member + ".value_or(\"\"), " + member + ".value_or(\"\").size());");
                        }
                    }
                } else if (fti.is_bytes) {
                    // Bytes in FX: write fixed-length zero-filled or actual bytes
                    if (c.length) {
                        ctx_.line("if (" + member + ".has_value()) {");
                        ctx_.indent();
                        ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + std::to_string(*c.length) + "));");
                        ctx_.dedent();
                        ctx_.line("} else {");
                        ctx_.indent();
                        ctx_.line("w.write_bits(0, " + std::to_string(*c.length * 8) + ");");
                        ctx_.dedent();
                        ctx_.line("}");
                    } else {
                        ctx_.line("if (" + member + ".has_value()) {");
                        ctx_.indent();
                        ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + member + "->size()));");
                        ctx_.dedent();
                        ctx_.line("}");
                    }
                } else if (fti.has_field_scale) {
                    // Scaled field in FX: inverse scale/offset, write raw
                    ctx_.line("{");
                    ctx_.indent();
                    std::string raw_type = storage_type_for_bits(fti.raw_bits, fti.raw_signed);
                    std::string scale_str = double_literal(fti.field_scale);
                    std::string offset_str = double_literal(fti.field_offset);
                    std::string val_expr = member + ".value_or(0.0)";
                    std::string expr;
                    if (fti.field_offset != 0.0)
                        expr = "static_cast<" + raw_type + ">((" + val_expr + " - " + offset_str + ") / " + scale_str + ")";
                    else
                        expr = "static_cast<" + raw_type + ">(" + val_expr + " / " + scale_str + ")";
                    FieldTypeInfo raw_fti;
                    raw_fti.bits = fti.raw_bits;
                    raw_fti.is_signed = fti.raw_signed;
                    raw_fti.wire_encoding = fti.wire_encoding;
                    emit_write_stmt(ctx_, expr, raw_fti, fti.raw_endian, is_byte_aligned());
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    // Primitive: use value_or(0)
                    emit_write_stmt(ctx_, member + ".value_or(0)", fti, c.endian, is_byte_aligned());
                }
                advance_fx_field_bits(c, fti);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                // Struct inside FX block
                if (!c.name.empty()) {
                    std::string member = to_member_name(c.name);
                    std::string type = get_child_class_name(c.name);
                    ctx_.line("if (" + member + ".has_value()) {");
                    ctx_.indent();
                    ctx_.line("CONDUIT_TRY(" + member + "->encode(w));");
                    ctx_.dedent();
                    ctx_.line("} else {");
                    ctx_.indent();
                    ctx_.line("CONDUIT_TRY(" + type + "{}.encode(w));");
                    ctx_.dedent();
                    ctx_.line("}");
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                // Array inside FX block
                emit_encode_fx_array(c);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                // Choice inside FX block
                emit_encode_choice(c, true);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::Align>) {
                ctx_.line("w.align_to(" + std::to_string(c.to) + ");");
                bit_mod8_ = 0;
            } else if constexpr (std::is_same_v<T, model::Reserved>) {
                int remaining = c.bits;
                while (remaining > 64) {
                    ctx_.line("w.write_bits(0, 64);");
                    remaining -= 64;
                }
                ctx_.line("w.write_bits(0, " + std::to_string(remaining) + ");");
                advance_bits(c.bits);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                has_nested_fx = true;
                // Nested FX: reuse emit_encode_fx for unique variable names
                emit_encode_fx(c);
                advance_bits_variable();
            }
        }, child);
    }
    // If this extent has no nested FxBlock, write terminal FX=0 bit
    if (!has_nested_fx) {
        ctx_.line("w.write_bits(0, 1); // Terminal FX=0");
        advance_bits(1);
    }
}

void StructEmitter::emit_fx_has_fields_check(const std::vector<model::StructChild>& children,
                                               const std::string& flag_var) {
    for (const auto& child : children) {
        std::visit([this, &flag_var](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                ctx_.line("if (" + to_member_name(c.name) + ".has_value()) " + flag_var + " = true;");
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    ctx_.line("if (" + to_member_name(c.name) + ".has_value()) " + flag_var + " = true;");
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                ctx_.line("if (" + to_member_name(c.name) + ".has_value()) " + flag_var + " = true;");
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                ctx_.line("if (" + to_member_name(c.name) + ".has_value()) " + flag_var + " = true;");
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                emit_fx_has_fields_check(c.children, flag_var);
            }
            // Reserved and Align don't contribute to FX presence
        }, child);
    }
}

} // namespace bgen::codegen
