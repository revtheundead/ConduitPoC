// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Decode Methods

#include "cpp_structs_emitter.hpp"
#include "../analyzer/parse_utils.hpp"
#include <set>

namespace bgen::codegen {

// ========================================================================
// Decode for plain struct
// ========================================================================

void StructEmitter::emit_decode(const std::vector<model::StructChild>& children,
                                const std::string& class_name) {
    // Populate optional field names for FieldRef expression generation
    optional_field_names_.clear();
    populate_optional_field_names(children);

    // Populate all local field BMDL names for context fallback disambiguation
    local_field_names_.clear();
    populate_local_field_names(children);

    // Build decode signature with outer-scope params if any
    std::string decode_params = "conduit::io::BitReader& r";
    auto osp_it = struct_decode_params_.find(current_bmdl_name_);
    if (osp_it != struct_decode_params_.end()) {
        for (const auto& p : osp_it->second) {
            if (p.pass_by_ref) {
                decode_params += ", const " + p.cpp_type + "& " + to_accessor_name(p.bmdl_name);
            } else {
                decode_params += ", " + p.cpp_type + " " + to_accessor_name(p.bmdl_name);
            }
        }
    }
    ctx_.line("static conduit::Result<" + class_name + "> decode(" + decode_params + ") {");
    ctx_.indent();
    ctx_.line(class_name + " result;");
    reset_alignment();
    emit_decode_children(children, "result");
    ctx_.line("return result;");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();
}

// G7: Generate to_string() method
void StructEmitter::emit_to_string(const std::vector<model::StructChild>& children,
                                    const std::vector<FieldInfo>& fields,
                                    const std::string& class_name,
                                    const std::vector<FrameFieldInfo>& header_frame_fields,
                                    const std::vector<FrameFieldInfo>& footer_frame_fields) {
    // Check if any frame field is auto-managed — if so, generate the overload
    bool has_auto_managed = false;
    for (const auto& ffi : header_frame_fields) {
        if (ffi.fi.is_auto_managed) { has_auto_managed = true; break; }
    }
    if (!has_auto_managed) {
        for (const auto& ffi : footer_frame_fields) {
            if (ffi.fi.is_auto_managed) { has_auto_managed = true; break; }
        }
    }

    // Always generate the span-accepting overload so format_outbound can call it
    // regardless of whether this message has auto-managed frame fields.
    ctx_.line("std::string to_string(std::span<const std::pair<std::string, std::string>> overrides) const {");
    ctx_.indent();

    if (has_auto_managed) {
        // Lambda to look up override value by field name
        ctx_.line("auto ov = [&](std::string_view n) -> const std::string* {");
        ctx_.line("    for (const auto& [k, v] : overrides) if (k == n) return &v;");
        ctx_.line("    return nullptr;");
        ctx_.line("};");
    } else {
        ctx_.line("(void)overrides;");
    }

    ctx_.line("std::ostringstream oss;");
    ctx_.line("oss << \"" + class_name + "{\"");
    ctx_.indent();

    bool first = true;

    // Emit frame field to_string entry (shared lambda for header and footer)
    auto emit_frame_field_str = [&](const FrameFieldInfo& ffi) {
        std::string sep = first ? "" : ", ";
        std::string member = to_member_name(ffi.fi.name);
        first = false;

        if (has_auto_managed && ffi.fi.is_auto_managed) {
            // Auto-managed: use override if present, else member value
            ctx_.line("<< \"" + sep + ffi.fi.name + "=\"");
            ctx_.line(";");
            ctx_.line("if (auto* ov_val = ov(\"" + ffi.fi.name + "\")) oss << *ov_val;");
            if (ffi.fti.is_enum) {
                ctx_.line("else oss << ::" + ns_ + "::to_string(" + member + ");");
            } else if (ffi.fti.is_struct) {
                ctx_.line("else oss << +(" + member + ".raw());");
            } else {
                ctx_.line("else oss << +(" + member + ");");
            }
            ctx_.line("oss");
        } else if (ffi.fti.is_enum) {
            ctx_.line("<< \"" + sep + ffi.fi.name + "=\" << ::" + ns_ + "::to_string(" + member + ")");
        } else if (ffi.fti.is_struct) {
            // Typedef wrapper — use .raw() with unary + for safe uint8_t display
            ctx_.line("<< \"" + sep + ffi.fi.name + "=\" << +(" + member + ".raw())");
        } else {
            // Plain numeric
            ctx_.line("<< \"" + sep + ffi.fi.name + "=\" << +(" + member + ")");
        }
    };

    // Prepend frame header fields
    for (const auto& ffi : header_frame_fields) {
        emit_frame_field_str(ffi);
    }

    for (const auto& fi : fields) {
        std::string sep = first ? "" : ", ";
        first = false;
        std::string member = to_member_name(fi.name);

        // Find the original field to get display format and type info
        model::DisplayFormat fmt = model::DisplayFormat::Decimal;
        bool is_string = false;
        bool is_bytes = false;
        bool is_enum = false;
        bool is_bool = false;
        bool is_struct_type = false;
        bool is_simple_numeric = false;
        bool is_inner_struct = false;
        bool is_field_scale = false;
        bool is_scaled_wrapper = false;
        bool is_flags_wrapper = false;
        bool is_string_wrapper = false;
        bool is_typedef_wrapper = false;
        bool is_array_of_structs = false;
        int field_bits = 0;
        auto find_field_info = [&](const std::vector<model::StructChild>& cs, auto&& self) -> bool {
            for (const auto& child : cs) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    if (f->name == fi.name) {
                        fmt = f->format;
                        auto fti = resolve_field_type(*f, index_);
                        // Override for inline enums
                        if (!f->enum_values.empty() && f->type_ref.empty()) {
                            fti.is_enum = true;
                        }
                        is_string = fti.is_string;
                        is_bytes = fti.is_bytes;
                        is_enum = fti.is_enum;
                        is_bool = fti.is_bool;
                        is_struct_type = fti.is_struct;
                        is_field_scale = fti.has_field_scale;
                        field_bits = fti.bits;
                        is_simple_numeric = !fti.is_struct && !fti.is_enum && !fti.is_string && !fti.is_bytes && (fti.bits > 0 || fti.has_field_scale);
                        // Classify wrapped type for to_string output
                        if (is_struct_type && !f->type_ref.empty()) {
                            auto resolved = index_.find(f->type_ref);
                            if (resolved) {
                                std::visit([&](const auto* td) {
                                    using DT = std::decay_t<decltype(*td)>;
                                    if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                        is_typedef_wrapper = true;
                                        if (td->scale.has_value() || td->offset.has_value()) is_scaled_wrapper = true;
                                        else if (!td->flags.empty()) is_flags_wrapper = true;
                                        else if (td->base == model::PrimitiveBase::String) is_string_wrapper = true;
                                    }
                                }, *resolved);
                            }
                        }
                        return true;
                    }
                } else if (auto* s = std::get_if<model::StructDef>(&child)) {
                    if (s->name == fi.name) {
                        is_inner_struct = true;
                        return true;
                    }
                } else if (auto* a = std::get_if<model::ArrayDef>(&child)) {
                    if (a->name == fi.name) {
                        if (!a->type_ref.empty()) {
                            auto resolved = index_.find(a->type_ref);
                            if (resolved) {
                                std::visit([&](const auto* td) {
                                    using DT = std::decay_t<decltype(*td)>;
                                    if constexpr (std::is_same_v<DT, model::StructDef> ||
                                                  std::is_same_v<DT, model::MessageDef>) {
                                        is_array_of_structs = true;
                                    }
                                }, *resolved);
                            }
                        } else if (!a->children.empty()) {
                            is_array_of_structs = true;
                        }
                        return true;
                    }
                } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
                    if (self(fx->children, self)) return true;
                }
            }
            return false;
        };
        find_field_info(children, find_field_info);

        if (fi.is_optional) {
            std::string deref = "*" + member;
            if (is_bool) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? (" + deref + " ? \"true\" : \"false\") : \"<none>\")");
            } else if (is_simple_numeric) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? std::to_string(" + deref + ") : std::string(\"<none>\"))");
            } else if (is_string) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? " + deref + " : std::string(\"<none>\"))");
            } else if (is_enum) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? std::string(::" + ns_ + "::to_string(" + deref + ")) : std::string(\"<none>\"))");
            } else if (is_scaled_wrapper) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? std::to_string((" + deref + ").value()) : std::string(\"<none>\"))");
            } else if (is_typedef_wrapper || is_flags_wrapper) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? std::to_string((" + deref + ").raw()) : std::string(\"<none>\"))");
            } else if (is_inner_struct || (is_struct_type && !is_typedef_wrapper)) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? (*" + member + ").to_string() : std::string(\"<none>\"))");
            } else if (fi.is_variant) {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? std::visit([](const auto& v) { return v.to_string(); }, *" + member + ") : std::string(\"<none>\"))");
            } else {
                ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member +
                         ".has_value() ? \"set\" : \"<none>\")");
            }
        } else if (is_string) {
            ctx_.line("<< \"" + sep + fi.name + "=\" << " + member);
        } else if (is_bytes) {
            ctx_.line("<< \"" + sep + fi.name + "=[bytes]\"");
        } else if (fi.is_variant) {
            ctx_.line("<< \"" + sep + fi.name + "=\" << std::visit([](const auto& v) { return v.to_string(); }, " + member + ")");
        } else if (is_enum) {
            ctx_.line("<< \"" + sep + fi.name + "=\" << ::" + ns_ + "::to_string(" + member + ")");
        } else if (is_inner_struct) {
            ctx_.line("<< \"" + sep + fi.name + "=\" << " + member + ".to_string()");
        } else if (is_struct_type) {
            if (is_scaled_wrapper)
                ctx_.line("<< \"" + sep + fi.name + "=\" << " + member + ".value()");
            else if (is_flags_wrapper)
                ctx_.line("<< \"" + sep + fi.name + "=0x\" << std::hex << " + member + ".raw() << std::dec");
            else if (is_string_wrapper)
                ctx_.line("<< \"" + sep + fi.name + "=\" << " + member + ".value()");
            else if (is_typedef_wrapper)
                ctx_.line("<< \"" + sep + fi.name + "=\" << +(" + member + ".raw())");
            else
                ctx_.line("<< \"" + sep + fi.name + "=\" << " + member + ".to_string()");
        } else if (is_bool) {
            ctx_.line("<< \"" + sep + fi.name + "=\" << (" + member + " ? \"true\" : \"false\")");
        } else if (is_simple_numeric) {
            switch (fmt) {
                case model::DisplayFormat::Hex:
                    ctx_.line("<< \"" + sep + fi.name + "=0x\" << std::hex << static_cast<uint64_t>(" + member + ") << std::dec");
                    break;
                case model::DisplayFormat::Octal:
                    ctx_.line("<< \"" + sep + fi.name + "=0\" << std::oct << static_cast<uint64_t>(" + member + ") << std::dec");
                    break;
                case model::DisplayFormat::Binary:
                    if (field_bits > 0) {
                        ctx_.line("<< \"" + sep + fi.name + "=0b\" << std::bitset<64>(static_cast<uint64_t>(" +
                                 member + ")).to_string().substr(64 - " + std::to_string(field_bits) + ")");
                    } else {
                        ctx_.line("<< \"" + sep + fi.name + "=\" << +(" + member + ")");
                    }
                    break;
                default:
                    // Unary + promotes uint8_t/int8_t to int for numeric display
                    ctx_.line("<< \"" + sep + fi.name + "=\" << +(" + member + ")");
                    break;
            }
        } else if (fi.cpp_type.starts_with("std::vector<")) {
            if (is_array_of_structs) {
                ctx_.line("<< \"" + sep + fi.name + "=[\"");
                ctx_.line(";");
                ctx_.line("for (size_t i = 0; i < " + member + ".size(); ++i) {");
                ctx_.indent();
                ctx_.line("if (i > 0) oss << \", \";");
                ctx_.line("oss << " + member + "[i].to_string();");
                ctx_.dedent();
                ctx_.line("}");
                ctx_.line("oss << \"]\"");
            } else {
                ctx_.line("<< \"" + sep + fi.name + "=[\" << " + member + ".size() << \" items]\"");
            }
        } else {
            // Fallback: for remaining types (float wrappers, etc.)
            ctx_.line("<< \"" + sep + fi.name + "=[...]\"");
        }
    }

    // Append frame footer fields
    for (const auto& ffi : footer_frame_fields) {
        emit_frame_field_str(ffi);
    }

    ctx_.line("<< \"}\";");
    ctx_.dedent();
    ctx_.line("return oss.str();");
    ctx_.dedent();
    ctx_.line("}");

    // Always generate the no-arg delegate
    ctx_.line("std::string to_string() const { return to_string({}); }");
    ctx_.line();
}

// G7b: Generate to_string() method for bitmap structs
void StructEmitter::emit_bitmap_to_string(const std::vector<BitmapField>& bfields,
                                           const std::string& class_name) {
    ctx_.line();
    ctx_.line("std::string to_string() const {");
    ctx_.indent();
    ctx_.line("std::ostringstream oss;");
    ctx_.line("oss << \"" + class_name + "{\";");
    ctx_.line("bool first = true;");

    for (const auto& bf : bfields) {
        std::string member = to_member_name(bf.name);

        ctx_.line("if (" + member + ".has_value()) {");
        ctx_.indent();
        ctx_.line("if (!first) oss << \", \";");
        ctx_.line("first = false;");

        if (bf.is_enum) {
            ctx_.line("oss << \"" + bf.name + "=\" << ::" + ns_ + "::to_string(*" + member + ");");
        } else if (bf.is_string) {
            ctx_.line("oss << \"" + bf.name + "=\" << *" + member + ";");
        } else if (bf.is_bytes) {
            ctx_.line("oss << \"" + bf.name + "=[bytes]\";");
        } else if (bf.is_choice) {
            ctx_.line("oss << \"" + bf.name + "=\" << std::visit([](const auto& v) { return v.to_string(); }, *" + member + ");");
        } else if (bf.is_struct) {
            // Detect wrapper types via source_field type_ref
            bool is_typedef_wrapper = false;
            bool is_scaled_wrapper = false;
            bool is_flags_wrapper = false;
            bool is_string_wrapper = false;
            if (bf.source_field && !bf.source_field->type_ref.empty()) {
                auto resolved = index_.find(bf.source_field->type_ref);
                if (resolved) {
                    std::visit([&](const auto* td) {
                        using DT = std::decay_t<decltype(*td)>;
                        if constexpr (std::is_same_v<DT, model::TypeDef>) {
                            is_typedef_wrapper = true;
                            if (td->scale.has_value() || td->offset.has_value()) is_scaled_wrapper = true;
                            else if (!td->flags.empty()) is_flags_wrapper = true;
                            else if (td->base == model::PrimitiveBase::String) is_string_wrapper = true;
                        }
                    }, *resolved);
                }
            }

            if (is_scaled_wrapper) {
                ctx_.line("oss << \"" + bf.name + "=\" << (*" + member + ").value();");
            } else if (is_flags_wrapper) {
                ctx_.line("oss << \"" + bf.name + "=0x\" << std::hex << (*" + member + ").raw() << std::dec;");
            } else if (is_string_wrapper) {
                ctx_.line("oss << \"" + bf.name + "=\" << (*" + member + ").value();");
            } else if (is_typedef_wrapper) {
                ctx_.line("oss << \"" + bf.name + "=\" << +((*" + member + ").raw());");
            } else {
                ctx_.line("oss << \"" + bf.name + "=\" << (*" + member + ").to_string();");
            }
        } else if (bf.has_field_scale) {
            ctx_.line("oss << \"" + bf.name + "=\" << *" + member + ";");
        } else {
            // Simple numeric — cast to avoid char interpretation for uint8_t
            model::DisplayFormat fmt = bf.source_field ? bf.source_field->format : model::DisplayFormat::Decimal;
            switch (fmt) {
                case model::DisplayFormat::Hex:
                    ctx_.line("oss << \"" + bf.name + "=0x\" << std::hex << static_cast<uint64_t>(*" + member + ") << std::dec;");
                    break;
                case model::DisplayFormat::Octal:
                    ctx_.line("oss << \"" + bf.name + "=0\" << std::oct << static_cast<uint64_t>(*" + member + ") << std::dec;");
                    break;
                case model::DisplayFormat::Binary: {
                    int bf_bits = 0;
                    if (bf.source_field) {
                        auto bf_fti = resolve_field_type(*bf.source_field, index_);
                        bf_bits = bf_fti.bits;
                    }
                    if (bf_bits > 0) {
                        ctx_.line("oss << \"" + bf.name + "=0b\" << std::bitset<64>(static_cast<uint64_t>(*" +
                                 member + ")).to_string().substr(64 - " + std::to_string(bf_bits) + ");");
                    } else {
                        if (bf.is_signed)
                            ctx_.line("oss << \"" + bf.name + "=\" << static_cast<int64_t>(*" + member + ");");
                        else
                            ctx_.line("oss << \"" + bf.name + "=\" << static_cast<uint64_t>(*" + member + ");");
                    }
                    break;
                }
                default:
                    if (bf.is_signed)
                        ctx_.line("oss << \"" + bf.name + "=\" << static_cast<int64_t>(*" + member + ");");
                    else
                        ctx_.line("oss << \"" + bf.name + "=\" << static_cast<uint64_t>(*" + member + ");");
                    break;
            }
        }

        ctx_.dedent();
        ctx_.line("}");
    }

    ctx_.line("oss << \"}\";");
    ctx_.line("return oss.str();");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();
}

// G8: Generate validate() method for deferred constraints
void StructEmitter::emit_deferred_validate(const std::vector<model::StructChild>& children) {
    // Collect fields with deferred constraints
    struct DeferredField {
        std::string name;
        model::Constraint constraint;
        bool is_signed = true;
        bool is_optional = false;
    };
    std::vector<DeferredField> deferred;
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->constraint && f->constraint->validate == model::ValidateTiming::Deferred) {
                auto fti = resolve_field_type(*f, index_);
                // Skip deferred validation for byte-array fields (bytes > 8) — numeric constraints not applicable
                if (fti.is_bytes) continue;
                bool opt = f->present_when || f->bit;
                deferred.push_back({f->name, *f->constraint, fti.is_signed, opt});
            }
        }
    }
    if (deferred.empty()) return;

    ctx_.line("conduit::VoidResult validate() const {");
    ctx_.indent();
    for (const auto& df : deferred) {
        std::string member = to_member_name(df.name);
        // For optional fields, skip validation if absent
        if (df.is_optional) {
            ctx_.line("if (" + member + ".has_value()) {");
            ctx_.indent();
        }
        std::string val = df.is_optional ? ("*" + member) : member;
        if (df.constraint.equals) {
            ctx_.line("if (" + val + " != static_cast<decltype(" + val + ")>(" +
                     *df.constraint.equals + ")) {");
            ctx_.indent();
            ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolationDeferred,");
            ctx_.line("    \"" + df.name + " constraint violation: expected " + *df.constraint.equals + "\"));");
            ctx_.dedent();
            ctx_.line("}");
        }
        if (df.constraint.max) {
            ctx_.line("if (" + val + " > " + *df.constraint.max + ") {");
            ctx_.indent();
            ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolationDeferred,");
            ctx_.line("    \"" + df.name + " exceeds max " + *df.constraint.max + "\"));");
            ctx_.dedent();
            ctx_.line("}");
        }
        // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
        if (df.constraint.min && (*df.constraint.min != "0" || df.is_signed)) {
            ctx_.line("if (" + val + " < " + *df.constraint.min + ") {");
            ctx_.indent();
            ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolationDeferred,");
            ctx_.line("    \"" + df.name + " below min " + *df.constraint.min + "\"));");
            ctx_.dedent();
            ctx_.line("}");
        }
        if (df.is_optional) {
            ctx_.dedent();
            ctx_.line("}");
        }
    }
    ctx_.line("return {};");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();
}

void StructEmitter::populate_optional_field_names(const std::vector<model::StructChild>& children) {
    for (const auto& child : children) {
        std::visit([this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                if (c.bit.has_value() || c.present_when != nullptr) {
                    optional_field_names_.insert(to_member_name(c.name));
                }
                // Recurse into inline fields
                if (c.is_inline && !c.type_ref.empty()) {
                    auto resolved = index_.find(c.type_ref);
                    if (resolved) {
                        std::visit([this](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef>) {
                                populate_optional_field_names(def->children);
                            } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                populate_optional_field_names(def->children);
                            }
                        }, *resolved);
                    }
                }
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (c.bit.has_value() || c.present_when != nullptr || c.is_bitmap) {
                    optional_field_names_.insert(to_member_name(c.name));
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                if (c.bit.has_value() || c.present_when != nullptr) {
                    optional_field_names_.insert(to_member_name(c.name));
                }
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                if (c.bit.has_value() || c.present_when != nullptr) {
                    optional_field_names_.insert(to_member_name(c.name));
                }
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                // All fields inside FX blocks are optional
                populate_fx_optional_names(c.children);
            }
        }, child);
    }
}

void StructEmitter::populate_fx_optional_names(const std::vector<model::StructChild>& children) {
    for (const auto& child : children) {
        std::visit([this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                optional_field_names_.insert(to_member_name(c.name));
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    optional_field_names_.insert(to_member_name(c.name));
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                optional_field_names_.insert(to_member_name(c.name));
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                optional_field_names_.insert(to_member_name(c.name));
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                populate_fx_optional_names(c.children);
            }
            // Reserved and Align don't have names
        }, child);
    }
}


void StructEmitter::populate_local_field_names(const std::vector<model::StructChild>& children) {
    for (const auto& child : children) {
        std::visit([this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                local_field_names_.insert(c.name);
                // Recurse into inline fields (their fields appear as local)
                if (c.is_inline && !c.type_ref.empty()) {
                    auto resolved = index_.find(c.type_ref);
                    if (resolved) {
                        std::visit([this](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef> ||
                                          std::is_same_v<DT, model::MessageDef>) {
                                populate_local_field_names(def->children);
                            }
                        }, *resolved);
                    }
                }
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    local_field_names_.insert(c.name);
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                local_field_names_.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                local_field_names_.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                populate_local_field_names(c.children);
            }
        }, child);
    }
}


void StructEmitter::emit_decode_children(const std::vector<model::StructChild>& children,
                                          const std::string& result_var) {
    for (const auto& child : children) {
        std::visit([this, &result_var](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                emit_decode_field(c, result_var);
                // Alignment tracking for fields is done inside emit_decode_field_body
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    std::string member = result_var + "." + to_member_name(c.name);
                    std::string type = get_child_class_name(c.name);
                    // Build decode call with outer-scope params if any
                    std::string decode_call = type + "::decode(r";
                    auto sit = struct_decode_params_.find(c.name);
                    if (sit != struct_decode_params_.end()) {
                        for (const auto& p : sit->second) {
                            std::string arg = result_var + "." + to_member_name(p.bmdl_name);
                            if (optional_field_names_.count(to_member_name(p.bmdl_name))) {
                                arg = "(*" + arg + ")";
                            }
                            decode_call += ", " + arg;
                        }
                    }
                    decode_call += ")";
                    if (c.present_when) {
                        std::string cond = emit_expr_code(*c.present_when, result_var);
                        ctx_.line("if (" + cond + ") {");
                        ctx_.indent();
                        ctx_.line("{");
                        ctx_.indent();
                        ctx_.line("auto val = " + decode_call + ";");
                        ctx_.line("if (!val) return std::unexpected(val.error());");
                        ctx_.line(member + " = std::move(*val);");
                        ctx_.dedent();
                        ctx_.line("}");
                        ctx_.dedent();
                        ctx_.line("}");
                        // Conditional struct: alignment becomes unknown if not byte-aligned
                        advance_bits_variable();
                    } else {
                        ctx_.line("{");
                        ctx_.indent();
                        ctx_.line("auto val = " + decode_call + ";");
                        ctx_.line("if (!val) return std::unexpected(val.error());");
                        ctx_.line(member + " = std::move(*val);");
                        ctx_.dedent();
                        ctx_.line("}");
                        // Sub-structs decode whole bytes (their own decode starts at byte 0)
                        advance_bits_variable();
                    }
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                if (c.present_when) {
                    std::string cond = emit_expr_code(*c.present_when, result_var);
                    ctx_.line("if (" + cond + ") {");
                    ctx_.indent();
                    emit_decode_array(c, result_var);
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    emit_decode_array(c, result_var);
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                if (c.present_when) {
                    std::string cond = emit_expr_code(*c.present_when, result_var);
                    ctx_.line("if (" + cond + ") {");
                    ctx_.indent();
                    emit_decode_choice(c, result_var);
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    emit_decode_choice(c, result_var);
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                emit_decode_fx(c, result_var);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::Reserved>) {
                int remaining = c.bits;
                while (remaining > 64) {
                    ctx_.line("CONDUIT_TRY(r.skip_bits(64));");
                    remaining -= 64;
                }
                ctx_.line("CONDUIT_TRY(r.skip_bits(" + std::to_string(remaining) + "));");
                advance_bits(c.bits);
            } else if constexpr (std::is_same_v<T, model::Align>) {
                ctx_.line("r.align_to(" + std::to_string(c.to) + ");");
                bit_mod8_ = 0; // align resets to byte boundary
            }
        }, child);
    }
}

void StructEmitter::emit_decode_field(const model::Field& f, const std::string& result_var) {
    // A5: present-when condition wrapper
    if (f.present_when) {
        std::string cond = emit_expr_code(*f.present_when, result_var);
        ctx_.line("if (" + cond + ") {");
        ctx_.indent();
        emit_decode_field_body(f, result_var);
        ctx_.dedent();
        ctx_.line("}");
        // Conditional field: alignment unknown after (field may or may not have been decoded)
        advance_bits_variable();
        return;
    }
    emit_decode_field_body(f, result_var);
}

void StructEmitter::emit_decode_field_body(const model::Field& f, const std::string& result_var) {
    std::string member = result_var + "." + to_member_name(f.name);
    auto fti = resolve_field_type(f, index_);

    // Override for inline enums (fields with <enum> values but no type_ref)
    if (!f.enum_values.empty() && f.type_ref.empty()) {
        std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(f.name);
        fti.cpp_type = enum_name;
        fti.is_enum = true;
    }

    if (f.is_inline) {
        // Inline: decode struct/message fields directly into result
        auto resolved = index_.find(f.type_ref);
        if (resolved) {
            std::visit([this, &result_var](const auto* def) {
                using DT = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<DT, model::StructDef>) {
                    emit_decode_children(def->children, result_var);
                } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                    // A11: Inline decode for MessageDef
                    emit_decode_children(def->children, result_var);
                }
            }, *resolved);
        }
        return;
    }

    std::string field_ctx = ".with_context(\"field '" + f.name + "'\")";

    if (fti.has_field_scale) {
        // Inline scale: read raw integer and apply scale+offset
        FieldTypeInfo raw_fti;
        raw_fti.bits = fti.raw_bits;
        raw_fti.is_signed = fti.raw_signed;
        raw_fti.wire_encoding = fti.wire_encoding;
        std::string read = emit_read_expr(raw_fti, fti.raw_endian, "r", is_byte_aligned());
        std::string scale_str = double_literal(fti.field_scale);
        std::string offset_str = double_literal(fti.field_offset);
        ctx_.line("{");
        ctx_.indent();
        ctx_.line("auto val = " + read + ";");
        ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
        ctx_.line(member + " = static_cast<double>(*val) * " + scale_str + " + " + offset_str + ";");
        ctx_.dedent();
        ctx_.line("}");
        advance_bits(fti.raw_bits);
    } else if (fti.is_enum) {
        // Enum: use decode_<type> function
        ctx_.line("{");
        ctx_.indent();
        ctx_.line("auto val = decode_" + fti.cpp_type + "(r);");
        ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
        ctx_.line(member + " = std::move(*val);");
        // G3: Constraint check on enum decode (applied to underlying value)
        if (f.constraint) {
            std::string cast_member = "static_cast<std::underlying_type_t<" + fti.cpp_type + ">>(" + member + ")";
            if (f.constraint->equals) {
                ctx_.line("if (" + cast_member + " != " + *f.constraint->equals + ") {");
                ctx_.indent();
                ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
                ctx_.line("    \"" + f.name + " constraint violation: expected " + *f.constraint->equals + "\"));");
                ctx_.dedent();
                ctx_.line("}");
            }
            if (f.constraint->max) {
                ctx_.line("if (" + cast_member + " > " + *f.constraint->max + ") {");
                ctx_.indent();
                ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
                ctx_.line("    \"" + f.name + " exceeds max " + *f.constraint->max + "\"));");
                ctx_.dedent();
                ctx_.line("}");
            }
            if (f.constraint->min) {
                // Determine enum underlying signedness from TypeDef
                bool enum_signed = false;
                auto enum_resolved = index_.find(f.type_ref);
                if (enum_resolved) {
                    std::visit([&enum_signed](const auto* def) {
                        using DT = std::decay_t<decltype(*def)>;
                        if constexpr (std::is_same_v<DT, model::TypeDef>) {
                            enum_signed = (def->base == model::PrimitiveBase::Int);
                        }
                    }, *enum_resolved);
                }
                // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
                if (*f.constraint->min != "0" || enum_signed) {
                    ctx_.line("if (" + cast_member + " < " + *f.constraint->min + ") {");
                    ctx_.indent();
                    ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
                    ctx_.line("    \"" + f.name + " below min " + *f.constraint->min + "\"));");
                    ctx_.dedent();
                    ctx_.line("}");
                }
            }
        }
        ctx_.dedent();
        ctx_.line("}");
        advance_bits(fti.bits);
    } else if (fti.is_struct || fti.is_string || fti.is_bytes) {
        if (fti.is_string) {
            if (f.char_bits && f.length) {
                // I3: Packed character decode loop
                int char_bits = *f.char_bits;
                int char_count = *f.length;
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("std::string s;");
                ctx_.line("s.reserve(" + std::to_string(char_count) + ");");
                ctx_.line("for (int i = 0; i < " + std::to_string(char_count) + "; i++) {");
                ctx_.indent();
                ctx_.line("auto bits = r.read_bits(" + std::to_string(char_bits) + ");");
                ctx_.line("if (!bits) return std::unexpected(bits.error()" + field_ctx + ");");
                if (char_bits < 7) {
                    ctx_.line("uint8_t raw = static_cast<uint8_t>(*bits);");
                    ctx_.line("s += (raw == 0) ? '\\0' : static_cast<char>(raw < 32 ? raw + 0x40 : raw);");
                } else {
                    ctx_.line("s += static_cast<char>(*bits);");
                }
                ctx_.dedent();
                ctx_.line("}");
                ctx_.line(member + " = std::move(s);");
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto val = r.read_string(" + std::to_string(*f.length) + ");");
                ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
                if (field_needs_encoding(f)) {
                    ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(f) + ");");
                } else {
                    ctx_.line(member + " = std::move(*val);");
                }
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length_prefix) {
                ctx_.line("{");
                ctx_.indent();
                auto pti = resolve_prefix_type(*f.length_prefix, index_);
                ctx_.line("auto len = " + emit_prefix_read(pti) + ";");
                ctx_.line("if (!len) return std::unexpected(len.error()" + field_ctx + ");");
                if (f.length_includes_prefix) {
                    // A3: Subtract prefix bytes from length
                    int prefix_bytes = get_prefix_bytes(pti);
                    ctx_.line("if (static_cast<size_t>(*len) < " + std::to_string(prefix_bytes) + ")");
                    ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::BufferOverrun, \"string length-prefix value smaller than prefix size\"));");
                    ctx_.line("auto str_len = static_cast<size_t>(*len) - " + std::to_string(prefix_bytes) + ";");
                    ctx_.line("auto val = r.read_string(str_len);");
                } else {
                    ctx_.line("auto val = r.read_string(*len);");
                }
                ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
                if (field_needs_encoding(f)) {
                    ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(f) + ");");
                } else {
                    ctx_.line(member + " = std::move(*val);");
                }
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length_from) {
                ctx_.line("{");
                ctx_.indent();
                std::string expr = emit_expr_code(*f.length_from, result_var);
                ctx_.line("auto nbytes_ = static_cast<size_t>(" + expr + ");");
                ctx_.line("auto val = r.read_string(nbytes_);");
                ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
                if (field_needs_encoding(f)) {
                    ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(f) + ");");
                } else {
                    ctx_.line(member + " = std::move(*val);");
                }
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.terminated) {
                // A1: Terminated string decode
                ctx_.line("{");
                ctx_.indent();
                std::string term = *f.terminated;
                int max_len = f.max_length ? *f.max_length : 65535;
                if (term == "crlf") {
                    ctx_.line("std::string s;");
                    ctx_.line("s.reserve(64);");
                    ctx_.line("for (size_t i = 0; i < " + std::to_string(max_len) + "; i++) {");
                    ctx_.indent();
                    ctx_.line("auto b = r.read_u8();");
                    ctx_.line("if (!b) return std::unexpected(b.error()" + field_ctx + ");");
                    ctx_.line("if (*b == 0x0D) {");
                    ctx_.indent();
                    ctx_.line("auto b2 = r.read_u8();");
                    ctx_.line("if (!b2) return std::unexpected(b2.error()" + field_ctx + ");");
                    ctx_.line("if (*b2 == 0x0A) break;");
                    ctx_.line("s += static_cast<char>(*b);");
                    ctx_.line("s += static_cast<char>(*b2);");
                    ctx_.dedent();
                    ctx_.line("} else {");
                    ctx_.indent();
                    ctx_.line("s += static_cast<char>(*b);");
                    ctx_.dedent();
                    ctx_.line("}");
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    std::string term_byte;
                    if (term == "null") term_byte = "0x00";
                    else if (term == "newline") term_byte = "0x0A";
                    else term_byte = term; // e.g. "0x1A"
                    ctx_.line("std::string s;");
                    ctx_.line("s.reserve(64);");
                    ctx_.line("for (size_t i = 0; i < " + std::to_string(max_len) + "; i++) {");
                    ctx_.indent();
                    ctx_.line("auto b = r.read_u8();");
                    ctx_.line("if (!b) return std::unexpected(b.error()" + field_ctx + ");");
                    ctx_.line("if (*b == " + term_byte + ") break;");
                    ctx_.line("s += static_cast<char>(*b);");
                    ctx_.dedent();
                    ctx_.line("}");
                }
                if (field_needs_encoding(f)) {
                    ctx_.line(member + " = conduit::string::to_ascii(s, " + field_encoding_enum(f) + ");");
                } else {
                    ctx_.line(member + " = std::move(s);");
                }
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length_star) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto val = r.read_string(r.remaining_bytes());");
                ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
                if (field_needs_encoding(f)) {
                    ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(f) + ");");
                } else {
                    ctx_.line(member + " = std::move(*val);");
                }
                emit_field_trim(ctx_, member, f, index_);
                ctx_.dedent();
                ctx_.line("}");
            }
        } else if (fti.is_bytes) {
            if (f.length) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto span = r.read_bytes(" + std::to_string(*f.length) + ");");
                ctx_.line("if (!span) return std::unexpected(span.error()" + field_ctx + ");");
                ctx_.line("std::copy(span->begin(), span->end(), " + member + ".begin());");
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length_from) {
                // length-from expression
                std::string expr = emit_expr_code(*f.length_from, result_var);
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto nbytes_ = static_cast<size_t>(" + expr + ");");
                ctx_.line("auto span = r.read_bytes(nbytes_);");
                ctx_.line("if (!span) return std::unexpected(span.error()" + field_ctx + ");");
                ctx_.line(member + ".assign(span->begin(), span->end());");
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.length_star) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto span = r.read_bytes(r.remaining_bytes());");
                ctx_.line("if (!span) return std::unexpected(span.error()" + field_ctx + ");");
                ctx_.line(member + ".assign(span->begin(), span->end());");
                ctx_.dedent();
                ctx_.line("}");
            } else if (f.bytes_attr) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto span = r.read_bytes(" + std::to_string(*f.bytes_attr) + ");");
                ctx_.line("if (!span) return std::unexpected(span.error()" + field_ctx + ");");
                ctx_.line("std::copy(span->begin(), span->end(), " + member + ".begin());");
                ctx_.dedent();
                ctx_.line("}");
            }
        } else {
            // Struct/message/enum type
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

            ctx_.line("{");
            ctx_.indent();
            if (is_enum) {
                ctx_.line("auto val = decode_" + fti.cpp_type + "(r);");
            } else {
                ctx_.line("auto val = " + fti.cpp_type + "::decode(r);");
            }
            ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
            ctx_.line(member + " = std::move(*val);");
            ctx_.dedent();
            ctx_.line("}");
        }
        advance_bits_variable();
    } else {
        // Simple numeric
        std::string read = emit_read_expr(fti, f.endian, "r", is_byte_aligned());
        ctx_.line("{");
        ctx_.indent();
        ctx_.line("auto val = " + read + ";");
        ctx_.line("if (!val) return std::unexpected(val.error()" + field_ctx + ");");
        ctx_.line(member + " = static_cast<" + fti.cpp_type + ">(*val);");

        // Constraint check
        if (f.constraint) {
            emit_constraint_check(*f.constraint, member, f.name, fti.is_signed);
        }

        ctx_.dedent();
        ctx_.line("}");
        advance_bits(fti.bits);
    }

    // G2: max_length check after decode
    if (f.max_length && (fti.is_string || fti.is_bytes)) {
        ctx_.line("if (" + member + ".size() > " + std::to_string(*f.max_length) + ") {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::MaxLengthExceeded,");
        ctx_.line("    \"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\"));");
        ctx_.dedent();
        ctx_.line("}");
    }
}

void StructEmitter::emit_decode_array(const model::ArrayDef& a, const std::string& result_var) {
    std::string member = result_var + "." + to_member_name(a.name);
    std::string elem_type = !a.type_ref.empty() ? to_cpp_type_name(a.type_ref)
                                                  : get_child_class_name(a.name + "Element");
    std::string array_ctx = ".with_context(\"array '" + a.name + "'\")";

    // Optional arrays (present_when / bit): emplace the optional and dereference for vector ops
    if (a.present_when || a.bit) {
        ctx_.line(member + ".emplace();");
        member = "(*" + member + ")";
    }

    // Check if element type is primitive (simple alias), enum, or struct
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
                    bool has_enum = !def->enum_values.empty();
                    bool has_flags = !def->flags.empty();
                    bool has_scale = def->scale.has_value() || def->offset.has_value();
                    bool is_string_type = (def->base == model::PrimitiveBase::String);
                    bool has_constraint = def->constraint.has_value();
                    bool has_wrapper = has_enum || has_flags || has_scale || is_string_type || has_constraint;
                    if (has_enum) {
                        elem_is_enum = true;
                        enum_type_name = to_cpp_type_name(def->name);
                    } else if (!has_wrapper && def->bits > 0) {
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

    if (a.fixed_count) {
        ctx_.line(member + ".reserve(" + std::to_string(*a.fixed_count) + ");");
        ctx_.line("for (int i = 0; i < " + std::to_string(*a.fixed_count) + "; i++) {");
    } else if (a.count_from) {
        std::string expr = emit_expr_code(*a.count_from, result_var);
        ctx_.line("{");
        ctx_.indent();
        ctx_.line("auto count = static_cast<size_t>(" + expr + ");");
        ctx_.line("if (count > static_cast<size_t>(INT32_MAX)) return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument, \"invalid array count\")" + array_ctx + ");");
        ctx_.line(member + ".reserve(count);");
        ctx_.line("for (size_t i = 0; i < count; i++) {");
    } else if (a.count_star) {
        if (a.length_from) {
            // Bounded: create sub-reader from length-from expression
            std::string len_expr = emit_expr_code(*a.length_from, result_var);
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("auto sub_len = static_cast<size_t>(" + len_expr + ");");
            ctx_.line("auto sub = r.sub_reader(sub_len);");
            ctx_.line("if (!sub) return std::unexpected(sub.error()" + array_ctx + ");");
            ctx_.line("while (!sub->at_end()) {");
            ctx_.indent();
            if (elem_is_enum) {
                ctx_.line("auto elem = decode_" + enum_type_name + "(*sub);");
                ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
                ctx_.line(member + ".push_back(std::move(*elem));");
            } else if (elem_is_primitive) {
                std::string read = emit_read_expr(elem_fti, elem_endian, "(*sub)");
                ctx_.line("auto elem = " + read + ";");
                ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
                ctx_.line(member + ".push_back(static_cast<" + elem_fti.cpp_type + ">(*elem));");
            } else {
                ctx_.line("auto elem = " + elem_type + "::decode(*sub);");
                ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
                ctx_.line(member + ".push_back(std::move(*elem));");
            }
            ctx_.dedent();
            ctx_.line("}");
            ctx_.dedent();
            ctx_.line("}");
            return;
        }
        ctx_.line("while (!r.at_end()) {");
    }

    ctx_.indent();
    if (elem_is_enum) {
        ctx_.line("auto elem = decode_" + enum_type_name + "(r);");
        ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
        ctx_.line(member + ".push_back(std::move(*elem));");
    } else if (elem_is_primitive) {
        std::string read = emit_read_expr(elem_fti, elem_endian, "r", is_byte_aligned());
        ctx_.line("auto elem = " + read + ";");
        ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
        ctx_.line(member + ".push_back(static_cast<" + elem_fti.cpp_type + ">(*elem));");
    } else {
        ctx_.line("auto elem = " + elem_type + "::decode(r);");
        ctx_.line("if (!elem) return std::unexpected(elem.error()" + array_ctx + ");");
        ctx_.line(member + ".push_back(std::move(*elem));");
    }
    ctx_.dedent();
    ctx_.line("}");

    if (a.count_from) {
        ctx_.dedent();
        ctx_.line("}");
    }
}

// Decode an array inside an FX block. The member is optional<vector>.
void StructEmitter::emit_decode_fx_array(const model::ArrayDef& a, const std::string& result_var) {
    std::string member = result_var + "." + to_member_name(a.name);
    std::string elem_type = !a.type_ref.empty() ? to_cpp_type_name(a.type_ref)
                                                  : get_child_class_name(a.name + "Element");

    // Check if element type is primitive, enum, or struct
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
                    bool has_enum = !def->enum_values.empty();
                    bool has_flags = !def->flags.empty();
                    bool has_scale = def->scale.has_value() || def->offset.has_value();
                    bool is_string_type = (def->base == model::PrimitiveBase::String);
                    bool has_constraint = def->constraint.has_value();
                    bool has_wrapper = has_enum || has_flags || has_scale || is_string_type || has_constraint;
                    if (has_enum) {
                        elem_is_enum = true;
                        enum_type_name = to_cpp_type_name(def->name);
                    } else if (!has_wrapper && def->bits > 0) {
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

    // Emplace the optional to create the vector
    ctx_.line(member + ".emplace();");
    // Use *member (the inner vector) for all operations
    std::string vec = "(*" + member + ")";

    if (a.fixed_count) {
        ctx_.line(vec + ".reserve(" + std::to_string(*a.fixed_count) + ");");
        ctx_.line("for (int i = 0; i < " + std::to_string(*a.fixed_count) + "; i++) {");
    } else if (a.count_from) {
        std::string expr = emit_expr_code(*a.count_from, result_var);
        ctx_.line("{");
        ctx_.indent();
        ctx_.line("auto count = static_cast<size_t>(" + expr + ");");
        ctx_.line(vec + ".reserve(count);");
        ctx_.line("for (size_t i = 0; i < count; i++) {");
    } else if (a.count_star) {
        if (a.length_from) {
            std::string len_expr = emit_expr_code(*a.length_from, result_var);
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("auto sub_len = static_cast<size_t>(" + len_expr + ");");
            ctx_.line("auto sub = r.sub_reader(sub_len);");
            ctx_.line("if (!sub) return std::unexpected(sub.error());");
            ctx_.line("while (!sub->at_end()) {");
            ctx_.indent();
            if (elem_is_enum) {
                ctx_.line("auto elem = decode_" + enum_type_name + "(*sub);");
                ctx_.line("if (!elem) return std::unexpected(elem.error());");
                ctx_.line(vec + ".push_back(std::move(*elem));");
            } else if (elem_is_primitive) {
                std::string read = emit_read_expr(elem_fti, elem_endian, "(*sub)");
                ctx_.line("auto elem = " + read + ";");
                ctx_.line("if (!elem) return std::unexpected(elem.error());");
                ctx_.line(vec + ".push_back(static_cast<" + elem_fti.cpp_type + ">(*elem));");
            } else {
                ctx_.line("auto elem = " + elem_type + "::decode(*sub);");
                ctx_.line("if (!elem) return std::unexpected(elem.error());");
                ctx_.line(vec + ".push_back(std::move(*elem));");
            }
            ctx_.dedent();
            ctx_.line("}");
            ctx_.dedent();
            ctx_.line("}");
            return;
        }
        ctx_.line("while (!r.at_end()) {");
    }

    ctx_.indent();
    if (elem_is_enum) {
        ctx_.line("auto elem = decode_" + enum_type_name + "(r);");
        ctx_.line("if (!elem) return std::unexpected(elem.error());");
        ctx_.line(vec + ".push_back(std::move(*elem));");
    } else if (elem_is_primitive) {
        std::string read = emit_read_expr(elem_fti, elem_endian, "r", is_byte_aligned());
        ctx_.line("auto elem = " + read + ";");
        ctx_.line("if (!elem) return std::unexpected(elem.error());");
        ctx_.line(vec + ".push_back(static_cast<" + elem_fti.cpp_type + ">(*elem));");
    } else {
        ctx_.line("auto elem = " + elem_type + "::decode(r);");
        ctx_.line("if (!elem) return std::unexpected(elem.error());");
        ctx_.line(vec + ".push_back(std::move(*elem));");
    }
    ctx_.dedent();
    ctx_.line("}");

    if (a.count_from) {
        ctx_.dedent();
        ctx_.line("}");
    }
}

// Helper: generate decode call for a case type, with outer-scope params if any
std::string StructEmitter::emit_case_decode_call(const std::string& case_type,
                                                   const std::string& reader_var,
                                                   const std::string& /*ctx_var*/,
                                                   const std::string& result_var,
                                                   const std::string& case_bmdl_name) {
    std::string call = case_type + "::decode(" + reader_var;
    if (!case_bmdl_name.empty()) {
        auto it = struct_decode_params_.find(case_bmdl_name);
        if (it != struct_decode_params_.end()) {
            for (const auto& p : it->second) {
                std::string arg = result_var + "." + to_member_name(p.bmdl_name);
                if (optional_field_names_.count(to_member_name(p.bmdl_name))) {
                    arg = "(*" + arg + ")";
                }
                call += ", " + arg;
            }
        }
    }
    call += ")";
    return call;
}

void StructEmitter::emit_decode_choice(const model::ChoiceDef& c, const std::string& result_var) {
    std::string member = result_var + "." + to_member_name(c.name);
    std::string choice_ctx = ".with_context(\"choice '" + c.name + "'\")";

    // Evaluate switch expression
    std::string switch_expr;
    if (c.switch_expr) {
        switch_expr = emit_expr_code(*c.switch_expr, result_var);
    }

    std::string ctx_ptr_var;

    // Create sub-reader if length-bounded
    bool bounded = c.length_from != nullptr || c.length.has_value();
    if (bounded) {
        ctx_.line("{");
        ctx_.indent();
        if (c.length_from) {
            std::string len_expr = emit_expr_code(*c.length_from, result_var);
            ctx_.line("auto sub_len = static_cast<size_t>(" + len_expr + ");");
            ctx_.line("auto sub = r.sub_reader(sub_len);");
            ctx_.line("if (!sub) return std::unexpected(sub.error()" + choice_ctx + ");");
            ctx_.line("auto& cr = *sub;");
        } else {
            ctx_.line("auto sub = r.sub_reader(" + std::to_string(*c.length) + ");");
            ctx_.line("if (!sub) return std::unexpected(sub.error()" + choice_ctx + ");");
            ctx_.line("auto& cr = *sub;");
        }
    }

    std::string reader_var = bounded ? "cr" : "r";

    ctx_.line("{");
    ctx_.indent();
    if (c.switch_expr && c.switch_expr->op == model::ExprOp::FieldRef) {
        const std::string& path = c.switch_expr->name;
        if (path.find('.') == std::string::npos &&
            optional_field_names_.count(to_member_name(path))) {
            switch_expr = "*(" + switch_expr + ")";
        }
    }
    ctx_.line("auto switch_val = " + switch_expr + ";");

    // Pre-scan: find resolved integer values covered by receive/both cases.
    // Send-only cases whose value collides with a receive/both case are
    // skipped in decode (the receive variant takes priority).
    auto resolve_case_int = [&](const std::string& val_str) -> std::optional<int64_t> {
        auto v = analyzer::parse_literal(val_str);
        if (v) return v;
        auto it = index_.constants.find(val_str);
        if (it != index_.constants.end())
            return analyzer::parse_literal(it->second->value);
        return std::nullopt;
    };

    std::set<int64_t> recv_values;
    std::set<std::string> recv_ranges;
    for (const auto& cs : c.cases) {
        if (cs.direction == model::Direction::Send) continue;
        if (cs.value) {
            if (auto v = resolve_case_int(*cs.value)) recv_values.insert(*v);
        }
        if (cs.range) {
            recv_ranges.insert(*cs.range);
        }
    }

    bool first_branch = true;
    for (size_t i = 0; i < c.cases.size(); i++) {
        const auto& cs = c.cases[i];

        // Skip send-only cases when a receive/both case exists for the same value/range
        if (cs.direction == model::Direction::Send && cs.value) {
            if (auto v = resolve_case_int(*cs.value); v && recv_values.count(*v))
                continue;
        }
        if (cs.direction == model::Direction::Send && cs.range) {
            if (recv_ranges.count(*cs.range))
                continue;
        }

        std::string cond;

        if (cs.value) {
            auto const_it = index_.constants.find(*cs.value);
            if (const_it != index_.constants.end()) {
                cond = "switch_val == static_cast<decltype(switch_val)>(" + *cs.value + ")";
            } else {
                std::string val = *cs.value;
                bool is_numeric = !val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) ||
                                  val[0] == '-' || (val.size() > 2 && val[0] == '0' && val[1] == 'x'));
                if (is_numeric) {
                    val = "static_cast<decltype(switch_val)>(" + val + ")";
                } else {
                    val = "static_cast<decltype(switch_val)>(" + resolve_enum_value(val) + ")";
                }
                cond = "switch_val == " + val;
            }
        } else if (cs.range) {
            auto dot_pos = cs.range->find("..");
            if (dot_pos != std::string::npos) {
                std::string min_s = cs.range->substr(0, dot_pos);
                std::string max_s = cs.range->substr(dot_pos + 2);
                std::string min_cast = "static_cast<decltype(switch_val)>(" + min_s + ")";
                std::string max_cast = "static_cast<decltype(switch_val)>(" + max_s + ")";
                if (min_s == "0") {
                    cond = "switch_val <= " + max_cast;
                } else {
                    cond = "switch_val >= " + min_cast + " && switch_val <= " + max_cast;
                }
            } else {
                cond = "switch_val == static_cast<decltype(switch_val)>(" + *cs.range + ")";
            }
        }

        std::string prefix = first_branch ? "if" : "} else if";
        first_branch = false;
        ctx_.line(prefix + " (" + cond + ") {");
        ctx_.indent();

        std::string case_type = !cs.type_ref.empty() ? to_cpp_type_name(cs.type_ref)
                                                      : get_child_class_name(cs.name);
        std::string case_bmdl = cs.type_ref.empty() ? cs.name : cs.type_ref;
        ctx_.line("auto val = " + emit_case_decode_call(case_type, reader_var, ctx_ptr_var, result_var, case_bmdl) + ";");
        ctx_.line("if (!val) return std::unexpected(val.error()" + choice_ctx + ");");
        ctx_.line(member + " = std::move(*val);");
        ctx_.dedent();
    }

    if (c.otherwise) {
        ctx_.line("} else {");
        ctx_.indent();
        if (!c.otherwise->type_ref.empty()) {
            std::string type = to_cpp_type_name(c.otherwise->type_ref);
            ctx_.line("auto val = " + emit_case_decode_call(type, reader_var, ctx_ptr_var, result_var, c.otherwise->type_ref) + ";");
            ctx_.line("if (!val) return std::unexpected(val.error()" + choice_ctx + ");");
            ctx_.line(member + " = std::move(*val);");
        } else if (!c.otherwise->children.empty()) {
            std::string otherwise_type = get_child_class_name(c.name + "Otherwise");
            std::string otherwise_bmdl = c.name + "Otherwise";
            ctx_.line("auto val = " + emit_case_decode_call(otherwise_type, reader_var, ctx_ptr_var, result_var, otherwise_bmdl) + ";");
            ctx_.line("if (!val) return std::unexpected(val.error()" + choice_ctx + ");");
            ctx_.line(member + " = std::move(*val);");
        } else {
            ctx_.line("auto remaining = " + reader_var + ".remaining_bytes();");
            ctx_.line("CONDUIT_TRY(" + reader_var + ".skip_bits(remaining * 8));");
        }
        ctx_.dedent();
    } else {
        ctx_.line("} else {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownDiscriminator,");
        ctx_.line("    \"choice '" + c.name + "': no case matched switch value\"));");
        ctx_.dedent();
    }

    ctx_.line("}");
    ctx_.dedent();
    ctx_.line("}");

    if (bounded) {
        ctx_.line("if (!cr.at_end()) {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ExactConsumptionFailed,");
        ctx_.line("    \"bounded choice has unconsumed bytes\"));");
        ctx_.dedent();
        ctx_.line("}");
        ctx_.dedent();
        ctx_.line("}");
    }
}

void StructEmitter::emit_decode_fx(const model::FxBlock& fx, const std::string& result_var) {
    int depth = fx_depth_++;
    std::string var = "fx_bit" + (depth > 0 ? ("_" + std::to_string(depth)) : std::string{});
    ctx_.line("// FX extension");
    ctx_.line("{");
    ctx_.indent();
    ctx_.line("auto " + var + " = r.read_bits(1);");
    ctx_.line("if (!" + var + ") return std::unexpected(" + var + ".error());");
    advance_bits(1);
    ctx_.line("if (*" + var + ") {");
    ctx_.indent();
    emit_decode_fx_children(fx.children, result_var);
    ctx_.dedent();
    ctx_.line("}");
    ctx_.dedent();
    ctx_.line("}");
    fx_depth_ = depth;
}

void StructEmitter::emit_decode_fx_children(const std::vector<model::StructChild>& children,
                                              const std::string& result_var) {
    bool has_nested_fx = false;
    for (const auto& child : children) {
        std::visit([this, &result_var, &has_nested_fx](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                std::string member = result_var + "." + to_member_name(c.name);
                auto fti = resolve_field_type(c, index_);
                // Override for inline enums
                if (!c.enum_values.empty() && c.type_ref.empty()) {
                    std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(c.name);
                    fti.cpp_type = enum_name;
                    fti.is_enum = true;
                }
                if (fti.is_enum) {
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto val = decode_" + fti.cpp_type + "(r);");
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    ctx_.line(member + " = std::move(*val);");
                    ctx_.dedent();
                    ctx_.line("}");
                } else if (fti.is_struct) {
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto val = " + fti.cpp_type + "::decode(r);");
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    ctx_.line(member + " = std::move(*val);");
                    ctx_.dedent();
                    ctx_.line("}");
                } else if (fti.has_field_scale) {
                    // Scaled field in FX: read raw, apply scale/offset
                    ctx_.line("{");
                    ctx_.indent();
                    FieldTypeInfo raw_fti;
                    raw_fti.bits = fti.raw_bits;
                    raw_fti.is_signed = fti.raw_signed;
                    raw_fti.wire_encoding = fti.wire_encoding;
                    std::string read = emit_read_expr(raw_fti, fti.raw_endian, "r", is_byte_aligned());
                    std::string scale_str = double_literal(fti.field_scale);
                    std::string offset_str = double_literal(fti.field_offset);
                    ctx_.line("auto val = " + read + ";");
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    ctx_.line(member + " = static_cast<double>(*val) * " + scale_str + " + " + offset_str + ";");
                    ctx_.dedent();
                    ctx_.line("}");
                } else if (fti.is_string) {
                    ctx_.line("{");
                    ctx_.indent();
                    if (c.length) {
                        ctx_.line("auto val = r.read_string(" + std::to_string(*c.length) + ");");
                    } else {
                        ctx_.line("auto val = r.read_string(r.remaining_bytes());");
                    }
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    if (field_needs_encoding(c)) {
                        ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(c) + ");");
                    } else {
                        ctx_.line(member + " = std::move(*val);");
                    }
                    // FX string fields are optional<string> — trim on dereferenced value
                    emit_field_trim(ctx_, "(*" + member + ")", c, index_);
                    ctx_.dedent();
                    ctx_.line("}");
                } else if (fti.is_bytes) {
                    ctx_.line("{");
                    ctx_.indent();
                    if (c.length) {
                        ctx_.line("auto span = r.read_bytes(" + std::to_string(*c.length) + ");");
                        ctx_.line("if (!span) return std::unexpected(span.error());");
                        ctx_.line(member + ".emplace();");
                        ctx_.line("std::copy(span->begin(), span->end(), " + member + "->begin());");
                    } else if (c.bytes_attr) {
                        ctx_.line("auto span = r.read_bytes(" + std::to_string(*c.bytes_attr) + ");");
                        ctx_.line("if (!span) return std::unexpected(span.error());");
                        ctx_.line(member + ".emplace();");
                        ctx_.line("std::copy(span->begin(), span->end(), " + member + "->begin());");
                    } else {
                        ctx_.line("auto span = r.read_bytes(r.remaining_bytes());");
                        ctx_.line("if (!span) return std::unexpected(span.error());");
                        ctx_.line(member + ".emplace(span->begin(), span->end());");
                    }
                    ctx_.dedent();
                    ctx_.line("}");
                } else {
                    std::string read = emit_read_expr(fti, c.endian, "r", is_byte_aligned());
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto val = " + read + ";");
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    ctx_.line(member + " = static_cast<" + fti.cpp_type + ">(*val);");
                    ctx_.dedent();
                    ctx_.line("}");
                }
                advance_fx_field_bits(c, fti);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    std::string member = result_var + "." + to_member_name(c.name);
                    std::string type = get_child_class_name(c.name);
                    // Build decode call with outer-scope params if any
                    std::string decode_call = type + "::decode(r";
                    auto sit = struct_decode_params_.find(c.name);
                    if (sit != struct_decode_params_.end()) {
                        for (const auto& p : sit->second) {
                            std::string arg = result_var + "." + to_member_name(p.bmdl_name);
                            if (optional_field_names_.count(to_member_name(p.bmdl_name))) {
                                arg = "(*" + arg + ")";
                            }
                            decode_call += ", " + arg;
                        }
                    }
                    decode_call += ")";
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("auto val = " + decode_call + ";");
                    ctx_.line("if (!val) return std::unexpected(val.error());");
                    ctx_.line(member + " = std::move(*val);");
                    ctx_.dedent();
                    ctx_.line("}");
                }
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                emit_decode_fx_array(c, result_var);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                emit_decode_choice(c, result_var);
                advance_bits_variable();
            } else if constexpr (std::is_same_v<T, model::Align>) {
                ctx_.line("r.align_to(" + std::to_string(c.to) + ");");
                bit_mod8_ = 0;
            } else if constexpr (std::is_same_v<T, model::Reserved>) {
                int remaining = c.bits;
                while (remaining > 64) {
                    ctx_.line("CONDUIT_TRY(r.skip_bits(64));");
                    remaining -= 64;
                }
                ctx_.line("CONDUIT_TRY(r.skip_bits(" + std::to_string(remaining) + "));");
                advance_bits(c.bits);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                has_nested_fx = true;
                emit_decode_fx(c, result_var);
                advance_bits_variable();
            }
        }, child);
    }
    // If this extent has no nested FxBlock, read and discard the terminal FX=0 bit
    if (!has_nested_fx) {
        ctx_.line("CONDUIT_TRY(r.skip_bits(1)); // Terminal FX=0");
        advance_bits(1);
    }
}

} // namespace bgen::codegen
