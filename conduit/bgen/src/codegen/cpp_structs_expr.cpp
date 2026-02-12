// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Expression, Constraint, Wrap, Utility Methods

#include "cpp_structs_emitter.hpp"
#include <cassert>

namespace bgen::codegen {

// ========================================================================
// Constraint check
// ========================================================================

void StructEmitter::emit_constraint_check(const model::Constraint& c, const std::string& member,
                                           const std::string& field_name, bool is_signed) {
    if (c.validate == model::ValidateTiming::Deferred) return;

    if (c.equals) {
        std::string val = *c.equals;
        ctx_.line("if (" + member + " != static_cast<decltype(" + member + ")>(" + val + ")) {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx_.line("    \"" + field_name + " constraint violation: expected " + *c.equals + "\"));");
        ctx_.dedent();
        ctx_.line("}");
    }
    if (c.max) {
        ctx_.line("if (" + member + " > " + *c.max + ") {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx_.line("    \"" + field_name + " exceeds max " + *c.max + "\"));");
        ctx_.dedent();
        ctx_.line("}");
    }
    // Skip min=0 for unsigned types (always true, triggers -Wtype-limits)
    if (c.min && (*c.min != "0" || is_signed)) {
        ctx_.line("if (" + member + " < " + *c.min + ") {");
        ctx_.indent();
        ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::ConstraintViolation,");
        ctx_.line("    \"" + field_name + " below min " + *c.min + "\"));");
        ctx_.dedent();
        ctx_.line("}");
    }
}

// ========================================================================
// Expression code generation
// ========================================================================

std::string StructEmitter::emit_expr_code(const model::Expr& expr, const std::string& result_var) {
    switch (expr.op) {
        case model::ExprOp::NumberLit:
            return std::to_string(expr.number_value);
        case model::ExprOp::BoolLit:
            return expr.bool_value ? "true" : "false";
        case model::ExprOp::FieldRef: {
            // Convert dotted path to member access
            std::string path = expr.name;

            // Extract the root segment to check context fallback
            size_t first_dot = path.find('.');
            std::string root_segment = (first_dot == std::string::npos) ? path : path.substr(0, first_dot);

            // Context fallback: if root segment is not a local field but is in context
            bool use_context = false;
            if (has_context_ && !context_field_names_.empty()) {
                bool is_local = local_field_names_.count(root_segment) > 0 ||
                                optional_field_names_.count(to_member_name(root_segment)) > 0;
                if (!is_local && context_field_names_.count(root_segment)) {
                    use_context = true;
                }
            }

            if (use_context) {
                return "ctx->" + to_accessor_name(root_segment);
            }

            std::string cpp_path = result_var;
            size_t pos = 0;
            bool first_segment = true;
            bool prev_was_optional = false;
            while (pos < path.size()) {
                size_t dot = path.find('.', pos);
                std::string segment;
                if (dot == std::string::npos) {
                    segment = path.substr(pos);
                    pos = path.size();
                } else {
                    segment = path.substr(pos, dot - pos);
                    pos = dot + 1;
                }
                std::string access;
                if (first_segment) {
                    access = to_member_name(segment);  // private member, accessible within class
                } else {
                    access = to_accessor_name(segment) + "()";  // public accessor for cross-class
                }
                if (prev_was_optional) {
                    cpp_path += "->" + access;
                    prev_was_optional = false;
                } else {
                    cpp_path += "." + access;
                }
                if (first_segment && optional_field_names_.count(to_member_name(segment))) {
                    prev_was_optional = true;
                }
                first_segment = false;
            }
            return cpp_path;
        }
        case model::ExprOp::ConstantRef:
            return expr.name;
        case model::ExprOp::Remaining:
            return "(r.remaining_bits() / 8)";
        case model::ExprOp::Add:
            return "(" + emit_expr_code(*expr.left, result_var) + " + " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Sub:
            return "(" + emit_expr_code(*expr.left, result_var) + " - " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Mul:
            return "(" + emit_expr_code(*expr.left, result_var) + " * " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Div:
            return "(" + emit_expr_code(*expr.left, result_var) + " / " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Mod:
            return "(" + emit_expr_code(*expr.left, result_var) + " % " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Eq:
            return "(" + emit_expr_code(*expr.left, result_var) + " == " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Neq:
            return "(" + emit_expr_code(*expr.left, result_var) + " != " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Lt:
            return "(" + emit_expr_code(*expr.left, result_var) + " < " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Lte:
            return "(" + emit_expr_code(*expr.left, result_var) + " <= " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Gt:
            return "(" + emit_expr_code(*expr.left, result_var) + " > " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Gte:
            return "(" + emit_expr_code(*expr.left, result_var) + " >= " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::LogAnd:
            return "(" + emit_expr_code(*expr.left, result_var) + " && " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::LogOr:
            return "(" + emit_expr_code(*expr.left, result_var) + " || " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::BitAnd:
            return "(" + emit_expr_code(*expr.left, result_var) + " & " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::BitOr:
            return "(" + emit_expr_code(*expr.left, result_var) + " | " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::BitXor:
            return "(" + emit_expr_code(*expr.left, result_var) + " ^ " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::ShiftLeft:
            return "(" + emit_expr_code(*expr.left, result_var) + " << " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::ShiftRight:
            return "(" + emit_expr_code(*expr.left, result_var) + " >> " +
                   emit_expr_code(*expr.right, result_var) + ")";
        case model::ExprOp::Negate:
            return "(-" + emit_expr_code(*expr.left, result_var) + ")";
        case model::ExprOp::BitNot:
            return "(~" + emit_expr_code(*expr.left, result_var) + ")";
        case model::ExprOp::LogNot:
            return "(!" + emit_expr_code(*expr.left, result_var) + ")";
    }
    assert(false && "unhandled ExprOp in emit_expr_code");
    return "0";
}

// ========================================================================
// wrap() overloads for entry-point messages
// ========================================================================

std::string StructEmitter::emit_field_cast(const std::string& parent_bmdl_name,
                                             const std::string& field_bmdl_name,
                                             const std::string& value_expr) {
    std::string cpp_type = resolve_field_cpp_type(parent_bmdl_name, field_bmdl_name);
    if (cpp_type.empty()) {
        return value_expr; // Fallback: no cast
    }
    std::string qual_type = (!ns_.empty()) ? "::" + ns_ + "::" + cpp_type : cpp_type;
    return "static_cast<" + qual_type + ">(" + value_expr + ")";
}

void StructEmitter::emit_wrap_overloads(const model::MessageDef& md,
                                         const analyzer::SessionInfo& session,
                                         const std::string& frame_class) {
    for (const auto& lt : session.leaf_types) {
        std::string leaf_type = to_cpp_type_name(lt.name);
        ctx_.line();
        ctx_.line("// Wrap a " + lt.name + " leaf into a " + frame_class + " frame");
        ctx_.line("static " + frame_class + " wrap(const " + leaf_type + "& leaf) {");
        ctx_.indent();
        ctx_.line(frame_class + " frame;");

        // Set constraints on frame (void-cast: wrap uses known-valid constants)
        for (const auto& [field_name, value] : lt.constraints) {
            std::string acc = to_accessor_name(field_name);
            ctx_.line("(void)frame.set_" + acc + "(" +
                     emit_field_cast(md.name, field_name, value) + ");");
        }

        if (lt.access_path.empty()) {
            ctx_.line("// No access path — direct assignment not applicable for wrap");
        } else {
            std::vector<std::string> level_vars;
            std::vector<std::string> level_bmdl;
            level_vars.push_back("frame");
            level_bmdl.push_back(md.name);

            size_t last_choice_idx = lt.access_path.size(); // sentinel = no optimization
            if (!lt.access_path.empty()) {
                size_t last = lt.access_path.size() - 1;
                if (!lt.access_path[last].is_array && !lt.access_path[last].is_struct) {
                    last_choice_idx = last;
                }
            }

            for (size_t i = 0; i < lt.access_path.size(); i++) {
                const auto& entry = lt.access_path[i];
                std::string var = "level_" + std::to_string(i + 1);

                if (entry.is_struct) {
                    std::string ref_expr = level_vars.back() + ".mutable_" + to_accessor_name(entry.struct_field) + "()";
                    ctx_.line("auto& " + var + " = " + ref_expr + ";");
                    level_vars.push_back(var);
                    level_bmdl.push_back(entry.struct_field);
                } else if (entry.is_array) {
                    std::string elem_type = to_cpp_type_name(entry.element_type);
                    ctx_.line(elem_type + " " + var + ";");
                    level_vars.push_back(var);
                    level_bmdl.push_back(entry.element_type);
                } else {
                    std::string parent_var = level_vars.back();
                    std::string parent_bmdl_name = level_bmdl.back();

                    if (!entry.disc_field.empty()) {
                        auto dot_pos = entry.disc_field.find('.');
                        if (dot_pos != std::string::npos) {
                            std::string outer = entry.disc_field.substr(0, dot_pos);
                            std::string inner = entry.disc_field.substr(dot_pos + 1);
                            std::string outer_acc = to_accessor_name(outer);
                            ctx_.line("(void)" + parent_var + ".set_" + outer_acc + "(" +
                                      "std::decay_t<decltype(" + parent_var + "." + outer_acc + "())>{});");
                            ctx_.line("(void)" + parent_var + ".mutable_" + outer_acc + "().set_" +
                                     to_accessor_name(inner) + "(" + entry.disc_value + ");");
                        } else {
                            ctx_.line("(void)" + parent_var + ".set_" + to_accessor_name(entry.disc_field) + "(" +
                                     emit_field_cast(parent_bmdl_name, entry.disc_field, entry.disc_value) + ");");
                        }
                    }

                    if (i == last_choice_idx) {
                        level_vars.push_back(""); // placeholder
                        level_bmdl.push_back(entry.variant_type);
                    } else {
                        std::string inter_type = to_cpp_type_name(entry.variant_type);
                        ctx_.line(inter_type + " " + var + ";");
                        level_vars.push_back(var);
                        level_bmdl.push_back(entry.variant_type);
                    }
                }
            }

            if (!lt.access_path.empty() && lt.access_path.back().is_array) {
                ctx_.line(level_vars.back() + " = leaf;");
            }

            // Chain from innermost back to frame
            for (int i = static_cast<int>(lt.access_path.size()) - 1; i >= 0; i--) {
                const auto& entry = lt.access_path[static_cast<size_t>(i)];
                if (entry.is_struct) continue;
                std::string parent_var = level_vars[static_cast<size_t>(i)];

                if (entry.is_array) {
                    std::string child_var = level_vars[static_cast<size_t>(i) + 1];
                    ctx_.line(parent_var + ".mutable_" + to_accessor_name(entry.array_field) +
                             "().push_back(std::move(" + child_var + "));");
                } else if (static_cast<size_t>(i) == last_choice_idx) {
                    std::string variant_type = to_cpp_type_name(entry.choice_field) + "Variant";
                    ctx_.line(parent_var + ".set_" + to_accessor_name(entry.choice_field) +
                             "(" + variant_type + "{leaf});");
                } else {
                    std::string child_var = level_vars[static_cast<size_t>(i) + 1];
                    std::string variant_type = to_cpp_type_name(entry.choice_field) + "Variant";
                    ctx_.line(parent_var + ".set_" + to_accessor_name(entry.choice_field) +
                             "(" + variant_type + "{std::move(" + child_var + ")});");
                }

                if (!entry.is_array && !entry.length_field.empty()) {
                    std::string length_acc = to_accessor_name(entry.length_field);
                    std::string choice_acc = to_accessor_name(entry.choice_field);
                    ctx_.line("{");
                    ctx_.indent();
                    ctx_.line("conduit::io::BitWriter lw;");
                    ctx_.line("std::visit([&lw](const auto& v) { (void)v.encode(lw); }, " +
                              parent_var + "." + choice_acc + "());");
                    if (entry.length_expr.empty()) {
                        ctx_.line(parent_var + ".set_" + length_acc + "(" +
                                  emit_field_cast(level_bmdl[static_cast<size_t>(i)], entry.length_field, "lw.size_bytes()") + ");");
                    } else {
                        ctx_.line(parent_var + ".set_" + length_acc + "(" +
                                  emit_field_cast(level_bmdl[static_cast<size_t>(i)], entry.length_field,
                                      "lw.size_bytes() + " + entry.length_expr) + ");");
                    }
                    ctx_.dedent();
                    ctx_.line("}");
                }
            }
        }

        ctx_.line("return frame;");
        ctx_.dedent();
        ctx_.line("}");
    }
}

// ========================================================================
// Utility
// ========================================================================

std::string StructEmitter::resolve_padding_char(const model::Field& f) const {
    if (f.padding) {
        if (*f.padding == model::StringPadding::Space) return "' '";
        if (*f.padding == model::StringPadding::Null) return "'\\0'";
    }
    if (!f.type_ref.empty()) {
        auto it = index_.types.find(f.type_ref);
        if (it != index_.types.end()) {
            if (it->second->padding == model::StringPadding::Space) return "' '";
        }
    }
    return "'\\0'";
}

std::string StructEmitter::qualify_type_if_shadowed(const std::string& field_name,
                                                     const std::string& cpp_type) const {
    std::string acc = to_accessor_name(field_name);
    if (acc == cpp_type) {
        if (!ns_.empty()) {
            return "::" + ns_ + "::" + cpp_type;
        }
        // No namespace — use global scope qualifier to disambiguate
        return "::" + cpp_type;
    }
    return cpp_type;
}

std::string StructEmitter::resolve_field_cpp_type(const std::string& parent_name,
                                                    const std::string& field_name) const {
    auto resolved = index_.find(parent_name);
    if (!resolved) return "";
    std::string result;
    std::visit([&](const auto* def) {
        using DT = std::decay_t<decltype(*def)>;
        if constexpr (std::is_same_v<DT, model::StructDef> ||
                     std::is_same_v<DT, model::MessageDef>) {
            for (const auto& child : def->children) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    if (f->name == field_name) {
                        auto fti = resolve_field_type(*f, index_);
                        result = fti.cpp_type;
                        return;
                    }
                    if (f->is_inline) {
                        auto inner = index_.find(f->type_ref);
                        if (inner) {
                            std::visit([&](const auto* idef) {
                                using IT = std::decay_t<decltype(*idef)>;
                                if constexpr (std::is_same_v<IT, model::StructDef>) {
                                    for (const auto& ic : idef->children) {
                                        if (auto* ff = std::get_if<model::Field>(&ic)) {
                                            if (ff->name == field_name) {
                                                auto fti = resolve_field_type(*ff, index_);
                                                result = fti.cpp_type;
                                                return;
                                            }
                                        }
                                    }
                                }
                            }, *inner);
                        }
                    }
                }
            }
        }
    }, *resolved);
    return result;
}

std::string StructEmitter::resolve_child_class_name(const std::string& bmdl_name, const std::string& parent_name) {
    std::string name = to_cpp_type_name(bmdl_name);
    if (name.empty()) return {};
    if (emitted_classes_.count(name) && !parent_name.empty()) {
        name = to_cpp_type_name(parent_name) + name;
    }
    return name;
}

std::string StructEmitter::get_child_class_name(const std::string& bmdl_name) {
    std::string name = to_cpp_type_name(bmdl_name);
    if (!current_parent_.empty() && emitted_classes_.count(name)) {
        std::string qualified = to_cpp_type_name(current_parent_) + name;
        if (emitted_classes_.count(qualified)) {
            return qualified;
        }
    }
    return name;
}

} // namespace bgen::codegen
