// SPDX-License-Identifier: MIT
// Bgen - JSON Serialization Code Generator
//
// Generates a json.hpp header that provides nlohmann-compatible to_json/from_json
// free functions for all generated structs and messages.

#include "cpp_json.hpp"
#include "cpp_structs_emitter.hpp"
#include "../logger.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace bgen::codegen {

namespace {

// ============================================================================
// JSON field emission context
// ============================================================================

struct JsonFieldInfo {
    std::string bmdl_name;    // BMDL field name (used as JSON key)
    std::string accessor;     // C++ accessor name
    std::string member;       // C++ member name (with trailing _)
    std::string cpp_type;     // C++ type
    bool is_optional = false;
    bool is_enum = false;
    bool is_struct = false;
    bool is_string = false;
    bool is_bytes = false;
    bool is_float = false;
    bool is_bool = false;
    bool is_variant = false;
    bool is_array = false;
    bool has_field_scale = false;
    int bits = 0;
    bool is_signed = false;
};

// Collect JSON-relevant field info from struct children (mirrors StructEmitter::collect_fields)
void collect_json_fields(const std::vector<model::StructChild>& children,
                         const analyzer::TypeIndex& index,
                         std::vector<JsonFieldInfo>& fields,
                         const std::string& parent_class,
                         bool in_fx = false) {
    for (const auto& child : children) {
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                if (c.is_inline) {
                    // Inline fields: recurse into the referenced struct's children
                    auto resolved = index.find(c.type_ref);
                    if (resolved) {
                        std::visit([&](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef> ||
                                          std::is_same_v<DT, model::MessageDef>) {
                                collect_json_fields(def->children, index, fields, parent_class, in_fx);
                            }
                        }, *resolved);
                    }
                    return;
                }
                JsonFieldInfo fi;
                fi.bmdl_name = c.name;
                fi.accessor = to_accessor_name(c.name);
                fi.member = to_member_name(c.name);
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;

                auto fti = resolve_field_type(c, index);
                fi.cpp_type = fti.cpp_type;
                fi.is_enum = fti.is_enum;
                fi.is_struct = fti.is_struct;
                fi.is_string = fti.is_string;
                fi.is_bytes = fti.is_bytes;
                fi.is_float = fti.is_float;
                fi.is_bool = fti.is_bool;
                fi.bits = fti.bits;
                fi.is_signed = fti.is_signed;
                fi.has_field_scale = fti.has_field_scale;

                // Override for inline enums
                if (!c.enum_values.empty() && c.type_ref.empty()) {
                    fi.cpp_type = to_pascal_case(parent_class) + "_" + to_pascal_case(c.name);
                    fi.is_enum = true;
                }

                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    JsonFieldInfo fi;
                    fi.bmdl_name = c.name;
                    fi.accessor = to_accessor_name(c.name);
                    fi.member = to_member_name(c.name);
                    fi.is_struct = true;
                    fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                    fields.push_back(fi);
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                JsonFieldInfo fi;
                fi.bmdl_name = c.name;
                fi.accessor = to_accessor_name(c.name);
                fi.member = to_member_name(c.name);
                fi.is_array = true;
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                JsonFieldInfo fi;
                fi.bmdl_name = c.name;
                fi.accessor = to_accessor_name(c.name);
                fi.member = to_member_name(c.name);
                fi.is_variant = true;
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                collect_json_fields(c.children, index, fields, parent_class, true);
            }
        }, child);
    }
}

// Emit the to_json assignment for a single field using public accessors
void emit_field_to_json(EmitContext& ctx, const JsonFieldInfo& fi) {
    std::string key = fi.bmdl_name;
    std::string acc = fi.accessor;
    std::string val = "v." + acc + "()";

    if (fi.is_optional) {
        ctx.line("if (v.has_" + acc + "()) {");
        ctx.indent();
        if (fi.is_enum) {
            ctx.line("j[\"" + key + "\"] = static_cast<std::underlying_type_t<std::decay_t<decltype(" + val + ")>>>(" + val + ");");
        } else if (fi.is_bytes) {
            ctx.line("{");
            ctx.indent();
            ctx.line("const auto& bytes_val_ = " + val + ";");
            ctx.line("nlohmann::json arr = nlohmann::json::array();");
            ctx.line("for (const auto& b : bytes_val_) arr.push_back(b);");
            ctx.line("j[\"" + key + "\"] = std::move(arr);");
            ctx.dedent();
            ctx.line("}");
        } else if (fi.is_variant) {
            ctx.line("std::visit([&](const auto& inner) { nlohmann::json vj; to_json(vj, inner); j[\"" + key + "\"] = std::move(vj); }, " + val + ");");
        } else {
            ctx.line("j[\"" + key + "\"] = " + val + ";");
        }
        ctx.dedent();
        ctx.line("}");
        return;
    }

    if (fi.is_enum) {
        ctx.line("j[\"" + key + "\"] = static_cast<std::underlying_type_t<std::decay_t<decltype(" + val + ")>>>(" + val + ");");
    } else if (fi.is_bytes) {
        ctx.line("{");
        ctx.indent();
        ctx.line("const auto& bytes_val_ = " + val + ";");
        ctx.line("nlohmann::json arr = nlohmann::json::array();");
        ctx.line("for (const auto& b : bytes_val_) arr.push_back(b);");
        ctx.line("j[\"" + key + "\"] = std::move(arr);");
        ctx.dedent();
        ctx.line("}");
    } else if (fi.is_variant) {
        ctx.line("std::visit([&](const auto& inner) { nlohmann::json vj; to_json(vj, inner); j[\"" + key + "\"] = std::move(vj); }, " + val + ");");
    } else {
        // Primitives, strings, structs, arrays — nlohmann handles these naturally
        ctx.line("j[\"" + key + "\"] = " + val + ";");
    }
}

// Emit the from_json assignment for a single field using public mutable_*() accessors
void emit_field_from_json(EmitContext& ctx, const JsonFieldInfo& fi) {
    std::string key = fi.bmdl_name;
    std::string acc = fi.accessor;
    // mutable_accessor() returns a writable reference; for optionals it creates the value if empty
    std::string mref = "v.mutable_" + acc + "()";

    if (fi.is_optional) {
        ctx.line("if (j.contains(\"" + key + "\") && !j[\"" + key + "\"].is_null()) {");
        ctx.indent();
        if (fi.is_enum) {
            std::string ref = mref;
            ctx.line("auto& ref_ = " + ref + ";");
            ctx.line("ref_ = static_cast<std::decay_t<decltype(ref_)>>(j[\"" + key + "\"].template get<std::underlying_type_t<std::decay_t<decltype(ref_)>>>());");
        } else if (fi.is_bytes) {
            ctx.line("{");
            ctx.indent();
            ctx.line("auto& dest_ = " + mref + ";");
            ctx.line("auto arr_ = j[\"" + key + "\"].template get<std::vector<uint8_t>>();");
            ctx.line("if constexpr (requires { dest_.size(); dest_.begin(); }) {");
            ctx.indent();
            ctx.line("auto n_ = std::min(arr_.size(), dest_.size());");
            ctx.line("std::copy_n(arr_.begin(), n_, dest_.begin());");
            ctx.dedent();
            ctx.line("}");
            ctx.dedent();
            ctx.line("}");
        } else if (fi.is_variant) {
            ctx.line("// variant from_json: use type-specific deserialization");
        } else {
            ctx.line("j.at(\"" + key + "\").get_to(" + mref + ");");
        }
        ctx.dedent();
        ctx.line("}");
        return;
    }

    if (fi.is_enum) {
        ctx.line("if (j.contains(\"" + key + "\")) {");
        ctx.indent();
        ctx.line("auto& ref_ = " + mref + ";");
        ctx.line("ref_ = static_cast<std::decay_t<decltype(ref_)>>(j[\"" + key + "\"].template get<std::underlying_type_t<std::decay_t<decltype(ref_)>>>());");
        ctx.dedent();
        ctx.line("}");
    } else if (fi.is_bytes) {
        ctx.line("if (j.contains(\"" + key + "\")) {");
        ctx.indent();
        ctx.line("auto& dest_ = " + mref + ";");
        ctx.line("auto arr_ = j[\"" + key + "\"].template get<std::vector<uint8_t>>();");
        ctx.line("if constexpr (requires { dest_.size(); dest_.begin(); std::tuple_size<std::decay_t<decltype(dest_)>>::value; }) {");
        ctx.indent();
        ctx.line("auto n_ = std::min(arr_.size(), dest_.size());");
        ctx.line("std::copy_n(arr_.begin(), n_, dest_.begin());");
        ctx.dedent();
        ctx.line("} else {");
        ctx.indent();
        ctx.line("dest_ = std::move(arr_);");
        ctx.dedent();
        ctx.line("}");
        ctx.dedent();
        ctx.line("}");
    } else if (fi.is_variant) {
        ctx.line("// variant \"" + key + "\": use type-specific deserialization for from_json");
    } else {
        ctx.line("if (j.contains(\"" + key + "\")) j.at(\"" + key + "\").get_to(" + mref + ");");
    }
}

// Check whether a TypeDef has enum values (producing an enum class)
bool type_has_enum(const model::TypeDef& td) {
    return !td.enum_values.empty();
}

// Check whether a TypeDef has flags (producing a flags wrapper class)
bool type_has_flags(const model::TypeDef& td) {
    return !td.flags.empty();
}

// Check whether a TypeDef generates a wrapper class (scaled, constrained, etc.)
bool type_has_wrapper(const model::TypeDef& td) {
    return td.scale.has_value() || td.offset.has_value() ||
           td.constraint.has_value() ||
           td.base == model::PrimitiveBase::String;
}

// Emit to_json/from_json for a type-level enum (enum class in types.hpp)
void emit_enum_json(EmitContext& ctx, const model::TypeDef& td) {
    std::string name = to_cpp_type_name(td.name);
    std::string underlying = storage_type_for_bits(td.bits, td.base == model::PrimitiveBase::Int);

    ctx.line("inline void to_json(nlohmann::json& j, " + name + " v) {");
    ctx.indent();
    ctx.line("j = static_cast<" + underlying + ">(v);");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    ctx.line("inline void from_json(const nlohmann::json& j, " + name + "& v) {");
    ctx.indent();
    ctx.line("v = static_cast<" + name + ">(j.template get<" + underlying + ">());");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

// Emit to_json/from_json for a type-level flags class
void emit_flags_json(EmitContext& ctx, const model::TypeDef& td) {
    std::string name = to_cpp_type_name(td.name);
    std::string underlying = storage_type_for_bits(td.bits, false);

    ctx.line("inline void to_json(nlohmann::json& j, const " + name + "& v) {");
    ctx.indent();
    ctx.line("j = static_cast<" + underlying + ">(v.raw());");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    ctx.line("inline void from_json(const nlohmann::json& j, " + name + "& v) {");
    ctx.indent();
    ctx.line("v.set_raw(j.template get<" + underlying + ">());");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

// Emit to_json/from_json for a scaled/constrained wrapper class
void emit_wrapper_json(EmitContext& ctx, const model::TypeDef& td) {
    std::string name = to_cpp_type_name(td.name);

    if (td.base == model::PrimitiveBase::String) {
        // String types: serialize as string, use set_value() to deserialize
        ctx.line("inline void to_json(nlohmann::json& j, const " + name + "& v) {");
        ctx.indent();
        ctx.line("j = v.value();");
        ctx.dedent();
        ctx.line("}");
        ctx.line();

        ctx.line("inline void from_json(const nlohmann::json& j, " + name + "& v) {");
        ctx.indent();
        ctx.line("v.set_value(j.template get<std::string>());");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    } else if (td.scale.has_value() || td.offset.has_value()) {
        // Scaled types: serialize as the scaled double value, use set_value() to deserialize
        ctx.line("inline void to_json(nlohmann::json& j, const " + name + "& v) {");
        ctx.indent();
        ctx.line("j = v.value();");
        ctx.dedent();
        ctx.line("}");
        ctx.line();

        ctx.line("inline void from_json(const nlohmann::json& j, " + name + "& v) {");
        ctx.indent();
        ctx.line("v.set_value(j.template get<double>());");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    } else {
        // Constrained types: serialize as the raw value, use set_raw() to deserialize
        std::string raw_type = storage_type_for_bits(td.bits, td.base == model::PrimitiveBase::Int);
        ctx.line("inline void to_json(nlohmann::json& j, const " + name + "& v) {");
        ctx.indent();
        ctx.line("j = v.raw();");
        ctx.dedent();
        ctx.line("}");
        ctx.line();

        ctx.line("inline void from_json(const nlohmann::json& j, " + name + "& v) {");
        ctx.indent();
        ctx.line("v.set_raw(j.template get<" + raw_type + ">());");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }
}

// Emit to_json/from_json for a struct or message class
void emit_class_json(EmitContext& ctx,
                     const std::string& class_name,
                     const std::vector<model::StructChild>& children,
                     const analyzer::TypeIndex& index,
                     const std::vector<FrameFieldInfo>& header_frame_fields = {},
                     const std::vector<FrameFieldInfo>& footer_frame_fields = {}) {
    std::vector<JsonFieldInfo> fields;
    collect_json_fields(children, index, fields, class_name);

    // to_json
    ctx.line("inline void to_json(nlohmann::json& j, const " + class_name + "& v) {");
    ctx.indent();
    ctx.line("j = nlohmann::json::object();");

    // Frame header fields
    for (const auto& ffi : header_frame_fields) {
        std::string key = ffi.fi.name;
        std::string val = "v." + to_accessor_name(ffi.fi.name) + "()";
        if (ffi.fti.is_enum) {
            ctx.line("j[\"" + key + "\"] = static_cast<std::underlying_type_t<std::decay_t<decltype(" + val + ")>>>(" + val + ");");
        } else {
            ctx.line("j[\"" + key + "\"] = " + val + ";");
        }
    }

    for (const auto& fi : fields) {
        emit_field_to_json(ctx, fi);
    }

    // Frame footer fields
    for (const auto& ffi : footer_frame_fields) {
        std::string key = ffi.fi.name;
        std::string val = "v." + to_accessor_name(ffi.fi.name) + "()";
        if (ffi.fti.is_enum) {
            ctx.line("j[\"" + key + "\"] = static_cast<std::underlying_type_t<std::decay_t<decltype(" + val + ")>>>(" + val + ");");
        } else {
            ctx.line("j[\"" + key + "\"] = " + val + ";");
        }
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // from_json — uses public mutable_*() accessors
    ctx.line("inline void from_json(const nlohmann::json& j, " + class_name + "& v) {");
    ctx.indent();

    // Check if all non-variant fields are empty; if so suppress unused-parameter warnings
    bool has_non_variant = !header_frame_fields.empty() || !footer_frame_fields.empty();
    if (!has_non_variant) {
        for (const auto& fi : fields) {
            if (!fi.is_variant) { has_non_variant = true; break; }
        }
    }
    if (!has_non_variant) {
        ctx.line("(void)j; (void)v;");
    }

    // Frame header fields — use mutable accessor
    for (const auto& ffi : header_frame_fields) {
        std::string key = ffi.fi.name;
        std::string mref = "v.mutable_" + to_accessor_name(ffi.fi.name) + "()";
        if (ffi.fti.is_enum) {
            ctx.line("if (j.contains(\"" + key + "\")) {");
            ctx.indent();
            ctx.line("auto& ref_ = " + mref + ";");
            ctx.line("ref_ = static_cast<std::decay_t<decltype(ref_)>>(j[\"" + key + "\"].template get<std::underlying_type_t<std::decay_t<decltype(ref_)>>>());");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("if (j.contains(\"" + key + "\")) j.at(\"" + key + "\").get_to(" + mref + ");");
        }
    }

    for (const auto& fi : fields) {
        emit_field_from_json(ctx, fi);
    }

    // Frame footer fields — use mutable accessor
    for (const auto& ffi : footer_frame_fields) {
        std::string key = ffi.fi.name;
        std::string mref = "v.mutable_" + to_accessor_name(ffi.fi.name) + "()";
        if (ffi.fti.is_enum) {
            ctx.line("if (j.contains(\"" + key + "\")) {");
            ctx.indent();
            ctx.line("auto& ref_ = " + mref + ";");
            ctx.line("ref_ = static_cast<std::decay_t<decltype(ref_)>>(j[\"" + key + "\"].template get<std::underlying_type_t<std::decay_t<decltype(ref_)>>>());");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("if (j.contains(\"" + key + "\")) j.at(\"" + key + "\").get_to(" + mref + ");");
        }
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

// Emit to_json/from_json for bitmap structs
void emit_bitmap_json(EmitContext& ctx,
                      const std::string& class_name,
                      const model::StructDef& sd,
                      const analyzer::TypeIndex& index) {
    // For bitmaps, all fields are optional; use public has_*() and accessor() methods
    ctx.line("inline void to_json(nlohmann::json& j, const " + class_name + "& v) {");
    ctx.indent();
    ctx.line("j = nlohmann::json::object();");

    for (const auto& child : sd.children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            std::string key = f->name;
            std::string acc = to_accessor_name(f->name);
            auto fti = resolve_field_type(*f, index);
            ctx.line("if (v.has_" + acc + "()) {");
            ctx.indent();
            std::string val = "v." + acc + "()";
            if (fti.is_enum) {
                ctx.line("j[\"" + key + "\"] = static_cast<std::underlying_type_t<std::decay_t<decltype(" + val + ")>>>(" + val + ");");
            } else if (fti.is_bytes) {
                ctx.line("nlohmann::json arr = nlohmann::json::array();");
                ctx.line("for (const auto& b : " + val + ") arr.push_back(b);");
                ctx.line("j[\"" + key + "\"] = std::move(arr);");
            } else {
                ctx.line("j[\"" + key + "\"] = " + val + ";");
            }
            ctx.dedent();
            ctx.line("}");
        } else if (auto* inner_sd = std::get_if<model::StructDef>(&child)) {
            if (!inner_sd->name.empty()) {
                std::string key = inner_sd->name;
                std::string acc = to_accessor_name(inner_sd->name);
                ctx.line("if (v.has_" + acc + "())");
                ctx.line("    j[\"" + key + "\"] = v." + acc + "();");
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string key = cd->name;
            std::string acc = to_accessor_name(cd->name);
            ctx.line("if (v.has_" + acc + "())");
            ctx.line("    std::visit([&](const auto& inner) { nlohmann::json vj; to_json(vj, inner); j[\"" + key + "\"] = std::move(vj); }, v." + acc + "());");
        }
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // from_json for bitmaps — uses public mutable_*() accessors
    ctx.line("inline void from_json(const nlohmann::json& j, " + class_name + "& v) {");
    ctx.indent();

    for (const auto& child : sd.children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            std::string key = f->name;
            std::string acc = to_accessor_name(f->name);
            std::string mref = "v.mutable_" + acc + "()";
            auto fti = resolve_field_type(*f, index);

            ctx.line("if (j.contains(\"" + key + "\") && !j[\"" + key + "\"].is_null()) {");
            ctx.indent();
            if (fti.is_enum) {
                ctx.line("auto& ref_ = " + mref + ";");
                ctx.line("ref_ = static_cast<std::decay_t<decltype(ref_)>>(j[\"" + key + "\"].template get<std::underlying_type_t<std::decay_t<decltype(ref_)>>>());");
            } else if (fti.is_bytes) {
                ctx.line("auto& dest_ = " + mref + ";");
                ctx.line("auto arr_ = j[\"" + key + "\"].template get<std::vector<uint8_t>>();");
                ctx.line("if constexpr (requires { dest_.size(); }) {");
                ctx.indent();
                ctx.line("auto n_ = std::min(arr_.size(), dest_.size());");
                ctx.line("std::copy_n(arr_.begin(), n_, dest_.begin());");
                ctx.dedent();
                ctx.line("}");
            } else {
                ctx.line("j.at(\"" + key + "\").get_to(" + mref + ");");
            }
            ctx.dedent();
            ctx.line("}");
        } else if (auto* inner_sd = std::get_if<model::StructDef>(&child)) {
            if (!inner_sd->name.empty()) {
                std::string key = inner_sd->name;
                std::string mref = "v.mutable_" + to_accessor_name(inner_sd->name) + "()";
                ctx.line("if (j.contains(\"" + key + "\") && !j[\"" + key + "\"].is_null())");
                ctx.line("    j.at(\"" + key + "\").get_to(" + mref + ");");
            }
        }
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

// Resolve child class name — matches cpp_structs.cpp's resolve_child_class_name logic
std::string resolve_child_name(const std::string& parent_cpp_name, const std::string& bmdl_name,
                               const std::optional<std::string>& type_name_override = {}) {
    if (type_name_override) {
        return to_pascal_case(*type_name_override);
    }
    std::string child = to_cpp_type_name(bmdl_name);
    if (!parent_cpp_name.empty()) {
        return parent_cpp_name + "_" + child;
    }
    return child;
}

// Recursively emit JSON functions for child structs/cases
void emit_child_class_json(EmitContext& ctx,
                           const std::vector<model::StructChild>& children,
                           const analyzer::TypeIndex& index,
                           const std::string& parent_cpp_name,
                           std::unordered_set<std::string>& emitted) {
    for (const auto& child : children) {
        if (auto* sd = std::get_if<model::StructDef>(&child)) {
            if (!sd->name.empty()) {
                std::string class_name = resolve_child_name(parent_cpp_name, sd->name, sd->type_name);
                if (emitted.insert(class_name).second) {
                    emit_child_class_json(ctx, sd->children, index, class_name, emitted);
                    if (sd->is_bitmap) {
                        emit_bitmap_json(ctx, class_name, *sd, index);
                    } else {
                        emit_class_json(ctx, class_name, sd->children, index);
                    }
                }
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                std::string elem_name = resolve_child_name(parent_cpp_name, ad->name + "Element", ad->type_name);
                if (emitted.insert(elem_name).second) {
                    emit_child_class_json(ctx, ad->children, index, elem_name, emitted);
                    emit_class_json(ctx, elem_name, ad->children, index);
                }
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    std::string case_name = resolve_child_name(parent_cpp_name, cs.name, cs.type_name);
                    if (emitted.insert(case_name).second) {
                        emit_child_class_json(ctx, cs.children, index, case_name, emitted);
                        emit_class_json(ctx, case_name, cs.children, index);
                    }
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                std::string otherwise_name = resolve_child_name(parent_cpp_name,
                    cd->name + "Otherwise", cd->otherwise->type_name);
                if (emitted.insert(otherwise_name).second) {
                    emit_child_class_json(ctx, cd->otherwise->children, index, otherwise_name, emitted);
                    emit_class_json(ctx, otherwise_name, cd->otherwise->children, index);
                }
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_child_class_json(ctx, fx->children, index, parent_cpp_name, emitted);
        }
    }
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::string generate_json(const model::Protocol& protocol,
                          const analyzer::TypeIndex& index,
                          [[maybe_unused]] const analyzer::WireSizeInfo& sizes,
                          const std::vector<analyzer::SessionInfo>& sessions,
                          const std::string& ns) {
    EmitContext ctx;

    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("// JSON serialization support (nlohmann/json)");
    ctx.line("#pragma once");
    ctx.line();
    ctx.line("#include \"messages.hpp\"");
    ctx.line("#include <nlohmann/json.hpp>");
    ctx.line("#include <type_traits>");
    ctx.line();
    ctx.line("namespace " + ns + " {");
    ctx.line();

    // Track emitted classes to avoid duplicates
    std::unordered_set<std::string> emitted;

    // 1. Type-level enums and wrappers
    for (const auto& td : protocol.types) {
        if (type_has_enum(td)) {
            emit_enum_json(ctx, td);
        }
        if (type_has_flags(td)) {
            emit_flags_json(ctx, td);
        }
        if (type_has_wrapper(td)) {
            emit_wrapper_json(ctx, td);
        }
    }

    // 2. Structs (including inline child class defs)
    for (const auto& s : protocol.structs) {
        if (s.name.empty()) continue;
        std::string class_name = to_cpp_type_name(s.name);

        // Emit child structs first
        emit_child_class_json(ctx, s.children, index, class_name, emitted);

        if (emitted.insert(class_name).second) {
            if (s.is_bitmap) {
                emit_bitmap_json(ctx, class_name, s, index);
            } else {
                emit_class_json(ctx, class_name, s.children, index);
            }
        }
    }

    // 3. Messages (including inline child class defs)
    // Find frame-based session for frame field info
    const analyzer::SessionInfo* frame_session = nullptr;
    for (const auto& si : sessions) {
        if (si.is_frame_based) { frame_session = &si; break; }
    }

    for (const auto& m : protocol.messages) {
        std::string class_name = to_cpp_type_name(m.name);

        // Emit child structs first
        emit_child_class_json(ctx, m.children, index, class_name, emitted);

        if (emitted.insert(class_name).second) {
            // Collect frame fields if applicable
            std::vector<FrameFieldInfo> header_frame_fields, footer_frame_fields;
            if (frame_session && frame_session->frame) {
                // We need the frame fields for message JSON —
                // these are the extra members that frames push into messages
                for (const auto& child : frame_session->frame->header_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        FrameFieldInfo ffi;
                        ffi.fi.name = f->name;
                        ffi.fti = resolve_field_type(*f, index);
                        ffi.fi.cpp_type = ffi.fti.cpp_type;
                        ffi.fi.is_auto_managed = f->auto_expr.has_value();
                        ffi.source = f;
                        header_frame_fields.push_back(ffi);
                    }
                }
                for (const auto& child : frame_session->frame->footer_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        FrameFieldInfo ffi;
                        ffi.fi.name = f->name;
                        ffi.fti = resolve_field_type(*f, index);
                        ffi.fi.cpp_type = ffi.fti.cpp_type;
                        ffi.fi.is_auto_managed = f->auto_expr.has_value();
                        ffi.source = f;
                        footer_frame_fields.push_back(ffi);
                    }
                }
            }
            emit_class_json(ctx, class_name, m.children, index,
                            header_frame_fields, footer_frame_fields);
        }
    }

    ctx.line("} // namespace " + ns);
    ctx.line();

    return ctx.str();
}

} // namespace bgen::codegen
