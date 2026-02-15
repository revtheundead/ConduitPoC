// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Expression, Constraint, Wrap, Utility Methods

#include "cpp_structs_emitter.hpp"
#include <cassert>
#include <set>
#include <stdexcept>

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
                    // Check outer-scope params first (e.g., params passed to decode)
                    auto osp_it = outer_scope_params_.find(segment);
                    if (osp_it != outer_scope_params_.end()) {
                        cpp_path = osp_it->second;
                        first_segment = false;
                        continue;
                    }
                    access = to_member_name(segment);  // private member, accessible within class
                } else {
                    access = to_accessor_name(segment) + "()";  // public accessor for cross-class
                }
                if (cpp_path.empty()) {
                    cpp_path = access;
                } else if (prev_was_optional) {
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
    throw std::logic_error("unhandled ExprOp in emit_expr_code: " +
                           std::to_string(static_cast<int>(expr.op)));
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

// ========================================================================
// Utility
// ========================================================================

std::string StructEmitter::resolve_padding_char(const model::Field& f) const {
    // Determine if padding is space or null
    bool is_space = false;
    if (f.padding) {
        if (*f.padding == model::StringPadding::Space) is_space = true;
        else if (*f.padding == model::StringPadding::Null) return "'\\0'";
    } else if (!f.type_ref.empty()) {
        auto it = index_.types.find(f.type_ref);
        if (it != index_.types.end() && it->second->padding == model::StringPadding::Space) {
            is_space = true;
        }
    }
    if (!is_space) return "'\\0'";

    // For EBCDIC encoding, pad with EBCDIC space (0x40) since padding
    // is applied after encoding conversion
    bool is_ebcdic = false;
    if (f.encoding && *f.encoding == model::StringEncoding::Ebcdic) {
        is_ebcdic = true;
    } else if (!f.type_ref.empty()) {
        auto it = index_.types.find(f.type_ref);
        if (it != index_.types.end() && it->second->encoding == model::StringEncoding::Ebcdic) {
            is_ebcdic = true;
        }
    }
    return is_ebcdic ? "'\\x40'" : "' '";
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

std::string StructEmitter::resolve_child_class_name(const std::string& bmdl_name,
                                                      const std::string& parent_name,
                                                      bool always_prefix) {
    std::string name = to_cpp_type_name(bmdl_name);
    if (name.empty()) return {};
    if (!parent_name.empty() && (always_prefix || emitted_classes_.count(name))) {
        name = to_cpp_type_name(parent_name) + "_" + name;
    }
    return name;
}

std::string StructEmitter::get_child_class_name(const std::string& bmdl_name) {
    std::string name = to_cpp_type_name(bmdl_name);
    if (!current_parent_.empty()) {
        std::string qualified = to_cpp_type_name(current_parent_) + "_" + name;
        if (emitted_classes_.count(qualified)) return qualified;
    }
    return name;
}

std::string StructEmitter::get_variant_alias_name(const std::string& choice_bmdl_name) {
    std::string base = to_cpp_type_name(choice_bmdl_name) + "Variant";
    if (!current_parent_.empty()) {
        std::string qualified = to_cpp_type_name(current_parent_) + "_" + base;
        if (emitted_variant_aliases_.count(qualified)) return qualified;
    }
    return base;
}

// ========================================================================
// Outer-scope analysis helpers
// ========================================================================

void StructEmitter::collect_expr_field_refs(const model::Expr* expr, std::set<std::string>& refs) {
    if (!expr) return;
    if (expr->op == model::ExprOp::FieldRef) {
        auto dot = expr->name.find('.');
        std::string root = (dot != std::string::npos) ? expr->name.substr(0, dot) : expr->name;
        refs.insert(root);
    }
    collect_expr_field_refs(expr->left.get(), refs);
    collect_expr_field_refs(expr->right.get(), refs);
}

void StructEmitter::collect_scope_field_refs(const std::vector<model::StructChild>& children,
                                              std::set<std::string>& refs) {
    for (const auto& child : children) {
        std::visit([&refs](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                collect_expr_field_refs(c.present_when.get(), refs);
                collect_expr_field_refs(c.length_from.get(), refs);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                collect_expr_field_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                collect_expr_field_refs(c.count_from.get(), refs);
                collect_expr_field_refs(c.length_from.get(), refs);
                collect_expr_field_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                collect_expr_field_refs(c.switch_expr.get(), refs);
                collect_expr_field_refs(c.present_when.get(), refs);
                collect_expr_field_refs(c.length_from.get(), refs);
                // Also check case children for outer-scope refs
                for (const auto& cs : c.cases) {
                    if (cs.type_ref.empty()) {
                        collect_scope_field_refs(cs.children, refs);
                    }
                }
                if (c.otherwise && c.otherwise->type_ref.empty()) {
                    collect_scope_field_refs(c.otherwise->children, refs);
                }
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                collect_scope_field_refs(c.children, refs);
            }
        }, child);
    }
}

void StructEmitter::collect_local_names(const std::vector<model::StructChild>& children,
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
                collect_local_names(c.children, names);
            }
        }, child);
    }
}

void StructEmitter::analyze_outer_scope(const std::string& child_bmdl_name,
                                         const std::vector<model::StructChild>& child_children,
                                         const std::vector<model::StructChild>& parent_children) {
    // Collect all FieldRef root names used in the child scope
    std::set<std::string> refs;
    collect_scope_field_refs(child_children, refs);

    // Subtract locally-defined names
    std::set<std::string> local;
    collect_local_names(child_children, local);
    for (const auto& n : local) {
        refs.erase(n);
    }

    if (refs.empty()) return;

    // Resolve remaining names against parent children to get C++ types
    std::vector<OuterScopeParam> params;
    for (const auto& ref_name : refs) {
        for (const auto& parent_child : parent_children) {
            std::visit([&](const auto& pc) {
                using T = std::decay_t<decltype(pc)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    if (pc.name == ref_name) {
                        auto fti = resolve_field_type(pc, index_);
                        OuterScopeParam p;
                        p.bmdl_name = ref_name;
                        p.cpp_type = fti.cpp_type;
                        p.pass_by_ref = fti.is_struct;
                        params.push_back(p);
                    }
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    if (pc.name == ref_name) {
                        OuterScopeParam p;
                        p.bmdl_name = ref_name;
                        p.cpp_type = to_cpp_type_name(pc.name);
                        p.pass_by_ref = true;
                        params.push_back(p);
                    }
                }
            }, parent_child);
        }
    }

    if (!params.empty()) {
        struct_decode_params_[child_bmdl_name] = std::move(params);
    }
}

} // namespace bgen::codegen
