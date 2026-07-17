// SPDX-License-Identifier: MIT
// Bgen - BMDL Validator Implementation

#include "validator.hpp"
#include "parse_utils.hpp"
#include "../codegen/name_utils.hpp"
#include "../logger.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <unordered_set>

namespace bgen::analyzer {

namespace {

class ValidatorImpl {
public:
    ValidatorImpl(const model::Protocol& proto, const TypeIndex& index)
        : proto_(proto), index_(index) {}

    void validate() {
        validate_protocol();
        validate_constants();
        validate_types();
        validate_structs();
        validate_messages();
        validate_frames();
        detect_struct_cycles();
    }

    bool has_errors() const { return !errors_.empty(); }
    std::vector<ValidationError>& errors() { return errors_; }

private:
    void error(const model::SourceLoc& loc, const std::string& msg) {
        errors_.push_back(ValidationError{msg, loc});
    }

    static bool is_valid_numeric_literal(const std::string& s) {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-') start = 1;
        if (start >= s.size()) return false;
        if (s.size() > start + 2 && s[start] == '0' && (s[start + 1] == 'x' || s[start + 1] == 'X')) {
            for (size_t i = start + 2; i < s.size(); i++) {
                if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
            }
            return s.size() > start + 2;
        }
        for (size_t i = start; i < s.size(); i++) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    // Error if a BMDL name would produce a C++ keyword after name conversion
    void check_keyword_collision(const model::SourceLoc& loc, const std::string& bmdl_name,
                                 const std::string& kind) {
        auto snake = codegen::to_snake_case(bmdl_name);
        if (codegen::is_cpp_keyword(snake)) {
            error(loc, kind + " '" + bmdl_name +
                  "' produces C++ keyword '" + snake + "' — rename to avoid compilation errors");
        }
    }

    // Warn if a BMDL name produces a C++ identifier starting with a digit
    void check_cpp_name_valid(const model::SourceLoc& loc, const std::string& bmdl_name,
                              const std::string& kind) {
        std::string cpp_name = codegen::to_cpp_type_name(bmdl_name);
        if (!cpp_name.empty() && std::isdigit(static_cast<unsigned char>(cpp_name[0]))) {
            Logger::warn(loc.to_string() + ": " + kind + " '" + bmdl_name +
                         "' produces invalid C++ identifier '" + cpp_name + "' (starts with digit)");
        }
    }

    // Validate a typeName override attribute.
    // Checks: non-empty, valid C++ identifier, not a keyword, no conflicts with
    // spec-defined types/structs/messages, and no duplicate typeName values.
    void validate_type_name(const model::SourceLoc& loc, const std::string& type_name,
                            const std::string& element_kind, const std::string& element_name,
                            bool has_type_ref) {
        if (type_name.empty()) {
            error(loc, element_kind + " '" + element_name + "': typeName cannot be empty");
            return;
        }

        // typeName is only valid on inline definitions (no type_ref)
        if (has_type_ref) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName is not valid when 'type' attribute is present "
                  "(typeName overrides the generated class name for inline definitions only)");
            return;
        }

        // Check valid C++ identifier
        if (!codegen::is_valid_cpp_identifier(type_name)) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' is not a valid C++ identifier");
            return;
        }

        // Check not a C++ keyword
        if (codegen::is_cpp_keyword(type_name)) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' is a C++ keyword");
            return;
        }

        // Check conflict with spec-defined types
        if (index_.types.count(type_name)) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' conflicts with existing type definition");
            return;
        }

        // Check conflict with spec-defined structs
        if (index_.structs.count(type_name)) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' conflicts with existing struct definition");
            return;
        }

        // Check conflict with spec-defined messages
        if (index_.messages.count(type_name)) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' conflicts with existing message definition");
            return;
        }

        // Check duplicate typeName across the protocol
        if (!used_type_names_.insert(type_name).second) {
            error(loc, element_kind + " '" + element_name +
                  "': typeName '" + type_name + "' is already used by another element");
            return;
        }
    }

    model::PrimitiveBase resolve_base(const model::Field& f) const {
        if (f.base) return *f.base;
        if (!f.type_ref.empty()) {
            auto it = index_.types.find(f.type_ref);
            if (it != index_.types.end()) return it->second->base;
        }
        if (f.is_signed) return model::PrimitiveBase::Int;
        return model::PrimitiveBase::Uint;
    }

    bool is_struct_or_message_type(const std::string& type_ref) const {
        return index_.structs.count(type_ref) || index_.messages.count(type_ref);
    }

    // Collect all FieldRef names from an expression tree
    static void collect_field_refs(const model::Expr* expr, std::vector<std::pair<std::string, model::SourceLoc>>& refs) {
        if (!expr) return;
        if (expr->op == model::ExprOp::FieldRef) {
            // For dotted paths like "header.length", only the root name matters
            auto dot = expr->name.find('.');
            std::string root = (dot != std::string::npos) ? expr->name.substr(0, dot) : expr->name;
            refs.push_back({root, expr->loc});
        }
        collect_field_refs(expr->left.get(), refs);
        collect_field_refs(expr->right.get(), refs);
    }

    // Check that an expression only references previously-declared fields
    // If parent_scope is provided, refs can also resolve against parent scope fields
    void check_no_forward_refs(const model::Expr* expr, const std::set<std::string>& declared,
                               const std::string& context,
                               const std::set<std::string>* parent_scope = nullptr) {
        if (!expr) return;
        std::vector<std::pair<std::string, model::SourceLoc>> refs;
        collect_field_refs(expr, refs);
        for (const auto& [name, loc] : refs) {
            if (!declared.count(name) &&
                !(parent_scope && parent_scope->count(name))) {
                error(loc, context + ": forward reference to undeclared field '" + name + "'");
            }
        }
    }

    // V3: Check that ConstantRef nodes in expressions reference existing constants
    void check_constant_refs(const model::Expr* expr, const std::string& context) {
        if (!expr) return;
        if (expr->op == model::ExprOp::ConstantRef) {
            if (index_.constants.find(expr->name) == index_.constants.end()) {
                error(expr->loc, context + ": references unknown constant '" + expr->name + "'");
            }
        }
        check_constant_refs(expr->left.get(), context);
        check_constant_refs(expr->right.get(), context);
    }

    // Check expression safety: division/modulo by zero, shift overflow
    void check_expr_safety(const model::Expr* expr, const std::string& context) {
        if (!expr) return;
        // Check binary ops with NumberLit on the right
        if (expr->right && expr->right->op == model::ExprOp::NumberLit) {
            if (expr->op == model::ExprOp::Div && expr->right->number_value == 0) {
                error(expr->loc, context + ": division by zero in expression");
            }
            if (expr->op == model::ExprOp::Mod && expr->right->number_value == 0) {
                error(expr->loc, context + ": modulo by zero in expression");
            }
            if ((expr->op == model::ExprOp::ShiftLeft || expr->op == model::ExprOp::ShiftRight) &&
                (expr->right->number_value < 0 || expr->right->number_value >= 64)) {
                error(expr->loc, context + ": shift amount out of range in expression");
            }
        }
        // Recurse into children
        check_expr_safety(expr->left.get(), context);
        check_expr_safety(expr->right.get(), context);
    }

    // Check whether an expression uses 'remaining' keyword
    static bool uses_remaining(const model::Expr* expr) {
        if (!expr) return false;
        if (expr->op == model::ExprOp::Remaining) return true;
        return uses_remaining(expr->left.get()) || uses_remaining(expr->right.get());
    }

    // ========================================================================
    // Protocol-level validation
    // ========================================================================

    void validate_protocol() {
        if (proto_.name.empty()) {
            error(proto_.loc, "protocol name is empty");
        }
        if (proto_.version.empty()) {
            error(proto_.loc, "protocol version is empty");
        }
    }

    // ========================================================================
    // Constant validation
    // ========================================================================

    void validate_constants() {
        std::set<std::string> names;
        for (const auto& c : proto_.constants) {
            if (c.name.empty()) {
                error(c.loc, "constant has empty name");
            }
            check_keyword_collision(c.loc, c.name, "constant");
            if (!names.insert(c.name).second) {
                error(c.loc, "duplicate constant name: " + c.name);
            }
            if (c.type_ref.empty()) {
                error(c.loc, "constant '" + c.name + "' missing 'type' attribute");
            }
            if (c.value.empty()) {
                error(c.loc, "constant '" + c.name + "' missing 'value' attribute");
            }
            // C9: Validate numeric constant value format and range
            if (!c.type_ref.empty() && !c.value.empty()) {
                auto it = index_.types.find(c.type_ref);
                if (it != index_.types.end()) {
                    auto base = it->second->base;
                    if (base == model::PrimitiveBase::Uint || base == model::PrimitiveBase::Int) {
                        if (!is_valid_numeric_literal(c.value)) {
                            error(c.loc, "constant '" + c.name +
                                  "': value must be a valid numeric literal");
                        } else if (it->second->bits > 0 && it->second->bits <= 64) {
                            // Validate value fits in the type's bit width
                            auto v = parse_literal(c.value);
                            if (v) {
                                bool is_signed = (base == model::PrimitiveBase::Int);
                                int bits = it->second->bits;
                                bool out_of_range = false;
                                if (bits == 64) {
                                    // All int64_t values fit in signed 64-bit;
                                    // for unsigned, only reject negative
                                    out_of_range = !is_signed && *v < 0;
                                } else {
                                    int64_t min_val = is_signed ? -(1LL << (bits - 1)) : 0;
                                    int64_t max_val = is_signed ? (1LL << (bits - 1)) - 1
                                                                : static_cast<int64_t>((1ULL << bits) - 1);
                                    out_of_range = (*v < min_val || *v > max_val);
                                }
                                if (out_of_range) {
                                    error(c.loc, "constant '" + c.name +
                                          "': value " + c.value + " exceeds " +
                                          std::to_string(bits) + "-bit range");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ========================================================================
    // Type validation
    // ========================================================================

    void validate_types() {
        for (const auto& t : proto_.types) {
            validate_type(t);
        }
    }

    void validate_type(const model::TypeDef& t) {
        if (t.name.empty()) {
            error(t.loc, "type has empty name");
        }
        check_keyword_collision(t.loc, t.name, "type");
        check_cpp_name_valid(t.loc, t.name, "type");

        // V2: Numeric types must have bits > 0
        if ((t.base == model::PrimitiveBase::Int ||
             t.base == model::PrimitiveBase::Uint ||
             t.base == model::PrimitiveBase::Float) && t.bits == 0) {
            error(t.loc, "type '" + t.name + "': numeric types (int, uint, float) must have bits > 0");
        }

        // Float types must have a valid bit width (any positive value is allowed)
        // Common widths: 16 (half), 32 (single), 64 (double)

        // Enum validation
        if (!t.enum_values.empty()) {
            std::set<int64_t> ids;
            std::set<std::string> names;
            for (const auto& ev : t.enum_values) {
                if (ev.name.empty()) {
                    error(ev.loc, "enum value has empty name in type '" + t.name + "'");
                }
                check_keyword_collision(ev.loc, ev.name, "enum value");
                {
                    auto converted = codegen::to_enum_value_name(ev.name);
                    if (!codegen::is_valid_cpp_identifier(converted)) {
                        error(ev.loc, "enum value name '" + ev.name +
                              "' is not a valid C++ identifier after conversion");
                    }
                }
                if (!ids.insert(ev.id).second) {
                    error(ev.loc, "duplicate enum id " + std::to_string(ev.id) + " in type '" + t.name + "'");
                }
                if (!names.insert(ev.name).second) {
                    error(ev.loc, "duplicate enum name '" + ev.name + "' in type '" + t.name + "'");
                }
                if (ev.id < 0) {
                    error(ev.loc, "enum id must be non-negative in type '" + t.name + "'");
                }
            }
        }

        // Flags validation
        if (!t.flags.empty()) {
            std::set<int> bits;
            std::set<std::string> names;
            for (const auto& f : t.flags) {
                if (f.name.empty()) {
                    error(f.loc, "flag has empty name in type '" + t.name + "'");
                }
                if (t.bits > 0 && (f.bit < 0 || f.bit >= t.bits)) {
                    error(f.loc, "flag bit " + std::to_string(f.bit) +
                        " out of range [0, " + std::to_string(t.bits - 1) +
                        "] in type '" + t.name + "'");
                }
                if (!bits.insert(f.bit).second) {
                    error(f.loc, "duplicate flag bit " + std::to_string(f.bit) + " in type '" + t.name + "'");
                }
                if (!names.insert(f.name).second) {
                    error(f.loc, "duplicate flag name '" + f.name + "' in type '" + t.name + "'");
                }
            }
        }

        // Mutual exclusion: enum, flags, scale/offset
        int trait_count = 0;
        if (!t.enum_values.empty()) trait_count++;
        if (!t.flags.empty()) trait_count++;
        if (t.scale || t.offset) trait_count++;
        if (trait_count > 1) {
            error(t.loc, "type '" + t.name + "': enum, flags, and scale/offset are mutually exclusive");
        }

        // Scale/offset only on numeric types
        if ((t.scale || t.offset) && t.base != model::PrimitiveBase::Uint &&
            t.base != model::PrimitiveBase::Int && t.base != model::PrimitiveBase::Float) {
            error(t.loc, "type '" + t.name + "': scale/offset only valid on numeric types");
        }

        // char-bits validation
        if (t.char_bits) {
            if (*t.char_bits < 1 || *t.char_bits > 8) {
                error(t.loc, "type '" + t.name + "': char-bits must be 1-8");
            }
            if (!t.length) {
                error(t.loc, "type '" + t.name + "': char-bits requires fixed length");
            }
        }

        // Constraint validation
        if (t.constraint) {
            validate_constraint(*t.constraint, t.name);
        }

        // Wire encoding validation
        if (t.wire_encoding_explicit) {
            // Wire encodings are only meaningful on integer types (int/uint)
            if (t.base == model::PrimitiveBase::Float ||
                t.base == model::PrimitiveBase::Bool ||
                t.base == model::PrimitiveBase::String) {
                error(t.loc, "type '" + t.name + "': wire-encoding is not applicable to " +
                      std::string(t.base == model::PrimitiveBase::Float ? "float" :
                                  t.base == model::PrimitiveBase::Bool ? "bool" : "string") + " types");
            }
            switch (t.wire_encoding) {
                case model::WireEncoding::BCD:
                    if (t.base != model::PrimitiveBase::Uint) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bcd' requires base='uint'");
                    }
                    if (t.bits > 0 && t.bits % 4 != 0) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bcd' requires bits divisible by 4");
                    }
                    break;
                case model::WireEncoding::BCD_S:
                    if (t.base != model::PrimitiveBase::Int) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bcd-s' requires base='int'");
                    }
                    if (t.bits > 0 && t.bits < 5) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bcd-s' requires bits >= 5");
                    }
                    if (t.bits > 0 && (t.bits - 1) % 4 != 0) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bcd-s' requires (bits - 1) divisible by 4");
                    }
                    break;
                case model::WireEncoding::BNR_S:
                    if (t.base != model::PrimitiveBase::Int) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bnr-s' requires base='int'");
                    }
                    if (t.bits > 0 && t.bits < 2) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bnr-s' requires bits >= 2");
                    }
                    break;
                case model::WireEncoding::CB2:
                    if (t.base != model::PrimitiveBase::Int) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'cb2' requires base='int'");
                    }
                    break;
                case model::WireEncoding::BNR:
                    if (t.base != model::PrimitiveBase::Uint) {
                        error(t.loc, "type '" + t.name + "': wire-encoding 'bnr' requires base='uint'");
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void validate_constraint(const model::Constraint& c, const std::string& context) {
        if (c.equals && (c.min || c.max)) {
            error(c.loc, context + ": constraint 'equals' and 'min/max' are mutually exclusive");
        }
        // V26: Validate min <= max when both are specified
        if (c.min && c.max) {
            auto min_val = parse_literal(*c.min);
            auto max_val = parse_literal(*c.max);
            if (min_val && max_val && *min_val > *max_val) {
                error(c.loc, context + ": constraint min (" + *c.min + ") > max (" + *c.max + ")");
            }
        }
    }

    // ========================================================================
    // Struct validation
    // ========================================================================

    void validate_structs() {
        for (const auto& s : proto_.structs) {
            validate_struct_def(s);
        }
    }

    void validate_struct_def(const model::StructDef& s) {
        if (!s.name.empty()) {
            check_cpp_name_valid(s.loc, s.name, "struct");
        }
        // typeName is not valid on top-level structs (they already have proper names)
        if (s.type_name) {
            error(s.loc, "struct '" + s.name + "': typeName is not valid on top-level struct definitions "
                  "(typeName overrides the generated class name for inline definitions only)");
        }
        if (s.children.empty() && !s.has_explicit_empty) {
            Logger::warn(s.loc.to_string() + ": struct '" + s.name + "' has no fields");
        }
        validate_children(s.children, s.is_bitmap, s.name, false, false);

        // Bitmap bit uniqueness and range checking
        if (s.is_bitmap) {
            if (s.bitmap_ext && (*s.bitmap_ext < 0 || *s.bitmap_ext >= 8)) {
                error(s.loc, "bitmap ext bit must be 0-7 (per-octet), got " +
                      std::to_string(*s.bitmap_ext) + " in " + s.name);
            }
            // V15: Determine bitmap_bits for range checking (default 8 bits per octet)
            int bitmap_bits = s.bitmap_bits;
            std::set<int> used_bits;
            for (const auto& child : s.children) {
                std::visit([&](const auto& c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<T, model::Field>) {
                        if (c.bit) {
                            // V15: Check bit value is within valid range
                            if (*c.bit < 0 || *c.bit >= bitmap_bits) {
                                error(c.loc, "field '" + c.name + "' bit " + std::to_string(*c.bit) +
                                      " out of range [0, " + std::to_string(bitmap_bits - 1) +
                                      "] in " + s.name);
                            }
                            if (!used_bits.insert(*c.bit).second) {
                                error(c.loc, "duplicate bitmap bit " + std::to_string(*c.bit) +
                                      " in " + s.name);
                            }
                            if (s.bitmap_ext && (*c.bit % 8) == *s.bitmap_ext) {
                                error(c.loc, "field '" + c.name + "' uses extension bit position " +
                                      std::to_string(*s.bitmap_ext) + " in " + s.name);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, model::StructDef>) {
                        if (c.bit) {
                            // V15: Check bit value is within valid range
                            if (*c.bit < 0 || *c.bit >= bitmap_bits) {
                                error(c.loc, "struct '" + c.name + "' bit " + std::to_string(*c.bit) +
                                      " out of range [0, " + std::to_string(bitmap_bits - 1) +
                                      "] in " + s.name);
                            }
                            if (!used_bits.insert(*c.bit).second) {
                                error(c.loc, "duplicate bitmap bit " + std::to_string(*c.bit) +
                                      " in " + s.name);
                            }
                            if (s.bitmap_ext && (*c.bit % 8) == *s.bitmap_ext) {
                                error(c.loc, "struct '" + c.name + "' uses extension bit position " +
                                      std::to_string(*s.bitmap_ext) + " in " + s.name);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                        if (c.bit) {
                            // V15: Check bit value is within valid range
                            if (*c.bit < 0 || *c.bit >= bitmap_bits) {
                                error(c.loc, "choice '" + c.name + "' bit " + std::to_string(*c.bit) +
                                      " out of range [0, " + std::to_string(bitmap_bits - 1) +
                                      "] in " + s.name);
                            }
                            if (!used_bits.insert(*c.bit).second) {
                                error(c.loc, "duplicate bitmap bit " + std::to_string(*c.bit) +
                                      " in " + s.name);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                        if (c.bit) {
                            // V15: Check bit value is within valid range
                            if (*c.bit < 0 || *c.bit >= bitmap_bits) {
                                error(c.loc, "array '" + c.name + "' bit " + std::to_string(*c.bit) +
                                      " out of range [0, " + std::to_string(bitmap_bits - 1) +
                                      "] in " + s.name);
                            }
                            if (!used_bits.insert(*c.bit).second) {
                                error(c.loc, "duplicate bitmap bit " + std::to_string(*c.bit) +
                                      " in " + s.name);
                            }
                            if (s.bitmap_ext && (*c.bit % 8) == *s.bitmap_ext) {
                                error(c.loc, "array '" + c.name + "' uses extension bit position " +
                                      std::to_string(*s.bitmap_ext) + " in " + s.name);
                            }
                        }
                    }
                }, child);
            }
        }
    }

    // ========================================================================
    // Message validation
    // ========================================================================

    void validate_messages() {
        for (const auto& m : proto_.messages) {
            if (!m.name.empty()) {
                check_cpp_name_valid(m.loc, m.name, "message");
            }
            if (m.children.empty() && !m.has_explicit_empty) {
                Logger::warn(m.loc.to_string() + ": message '" + m.name + "' has no fields");
            }
            // Messages are top-level bounded containers (wire size known)
            validate_children(m.children, false, m.name, false, false, true, nullptr);
        }
    }

    // ========================================================================
    // Children validation (recursive)
    // ========================================================================

    void validate_children(const std::vector<model::StructChild>& children,
                          bool in_bitmap, const std::string& parent_name,
                          bool in_array = false, bool in_choice = false,
                          bool in_bounded_container = false,
                          const std::set<std::string>* parent_scope = nullptr,
                          bool in_fx = false, bool in_frame = false) {
        std::set<std::string> field_names;
        std::set<std::string> array_names;  // For validating auto="count(field)" targets
        int fx_count = 0;
        bool seen_star_field = false;
        std::string star_field_name;

        for (const auto& child : children) {
            std::visit([&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    // Check that no data elements follow a length="*" or count="*" field
                    if (seen_star_field) {
                        error(c.loc, "field '" + c.name + "' follows '" + star_field_name +
                              "' which consumes remaining bytes; it must be the last element");
                    }
                    // Forward reference checks on field expressions
                    check_no_forward_refs(c.present_when.get(), field_names,
                                        "field '" + c.name + "' present-when", parent_scope);
                    check_no_forward_refs(c.length_from.get(), field_names,
                                        "field '" + c.name + "' length-from", parent_scope);
                    // Expression safety checks
                    check_expr_safety(c.present_when.get(), "field '" + c.name + "' present-when");
                    check_expr_safety(c.length_from.get(), "field '" + c.name + "' length-from");
                    check_constant_refs(c.present_when.get(), "field '" + c.name + "' present-when");
                    check_constant_refs(c.length_from.get(), "field '" + c.name + "' length-from");
                    // 'remaining' context validation on field
                    if (c.length_star && !in_bounded_container) {
                        error(c.loc, "field '" + c.name +
                              "': length=\"*\" requires a bounded container (message, length-delimited array/choice)");
                    }
                    if (c.length_from && uses_remaining(c.length_from.get()) && !in_bounded_container) {
                        error(c.loc, "field '" + c.name +
                              "': 'remaining' in length-from requires a bounded container");
                    }
                    validate_field(c, in_bitmap, parent_name, in_fx, in_frame);
                    if (!c.name.empty()) {
                        if (!field_names.insert(c.name).second) {
                            error(c.loc, "duplicate field name '" + c.name + "' in " + parent_name);
                        }
                    }
                    // Track length="*" fields — must be last data element
                    if (c.length_star) {
                        seen_star_field = true;
                        star_field_name = c.name;
                    }
                    // C8: Check inline fields don't introduce duplicate names
                    if (c.is_inline && !c.type_ref.empty()) {
                        std::set<std::string> visiting;
                        check_inline_duplicates(c, field_names, parent_name, visiting);
                    }
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    // Check that no data elements follow a length="*" or count="*" field
                    if (seen_star_field) {
                        error(c.loc, "struct '" + c.name + "' follows '" + star_field_name +
                              "' which consumes remaining bytes; it must be the last element");
                    }
                    // Forward reference check on present-when
                    check_no_forward_refs(c.present_when.get(), field_names,
                                        "struct '" + c.name + "' present-when", parent_scope);
                    // Expression safety check
                    check_expr_safety(c.present_when.get(), "struct '" + c.name + "' present-when");
                    check_constant_refs(c.present_when.get(), "struct '" + c.name + "' present-when");
                    if (!c.name.empty()) {
                        if (!field_names.insert(c.name).second) {
                            error(c.loc, "duplicate name '" + c.name + "' in " + parent_name);
                        }
                    }
                    // bit and present-when mutually exclusive
                    if (c.bit && c.present_when) {
                        error(c.loc, "'" + c.name + "': 'bit' and 'present-when' are mutually exclusive");
                    }
                    // bit only valid in bitmap
                    if (c.bit && !in_bitmap) {
                        error(c.loc, "'" + c.name + "': 'bit' only valid inside presence=\"bitmap\" struct");
                    }
                    // Validate typeName if present
                    if (c.type_name) {
                        validate_type_name(c.loc, *c.type_name, "struct", c.name, false);
                    }
                    // Pass current field_names as parent scope to child struct
                    validate_children(c.children, c.is_bitmap,
                                     c.name.empty() ? parent_name : c.name,
                                     in_array, in_choice, in_bounded_container,
                                     &field_names, in_fx, in_frame);
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    // Check that no data elements follow a length="*" or count="*" field
                    if (seen_star_field) {
                        error(c.loc, "array '" + c.name + "' follows '" + star_field_name +
                              "' which consumes remaining bytes; it must be the last element");
                    }
                    // Forward reference checks on array expressions
                    check_no_forward_refs(c.count_from.get(), field_names,
                                        "array '" + c.name + "' count-from", parent_scope);
                    check_no_forward_refs(c.length_from.get(), field_names,
                                        "array '" + c.name + "' length-from", parent_scope);
                    check_no_forward_refs(c.present_when.get(), field_names,
                                        "array '" + c.name + "' present-when", parent_scope);
                    // Expression safety checks
                    check_expr_safety(c.count_from.get(), "array '" + c.name + "' count-from");
                    check_expr_safety(c.length_from.get(), "array '" + c.name + "' length-from");
                    check_expr_safety(c.present_when.get(), "array '" + c.name + "' present-when");
                    check_constant_refs(c.count_from.get(), "array '" + c.name + "' count-from");
                    check_constant_refs(c.length_from.get(), "array '" + c.name + "' length-from");
                    check_constant_refs(c.present_when.get(), "array '" + c.name + "' present-when");
                    // 'remaining' context: count="*" requires bounded container
                    if (c.count_star && !in_bounded_container) {
                        error(c.loc, "array '" + c.name +
                              "': count=\"*\" requires a bounded container (message, length-delimited array/choice)");
                    }
                    validate_array(c, parent_name);
                    if (!c.name.empty()) {
                        if (!field_names.insert(c.name).second) {
                            error(c.loc, "duplicate name '" + c.name + "' in " + parent_name);
                        }
                        array_names.insert(c.name);
                    }
                    // Validate typeName if present
                    if (c.type_name) {
                        validate_type_name(c.loc, *c.type_name, "array", c.name, !c.type_ref.empty());
                    }
                    // Track count="*" arrays — must be last data element
                    if (c.count_star) {
                        seen_star_field = true;
                        star_field_name = c.name;
                    }
                    // Array with length-from creates a bounded container for its children
                    bool array_bounded = c.length_from != nullptr || c.length;
                    validate_children(c.children, false, c.name, true, in_choice,
                                     array_bounded || in_bounded_container,
                                     nullptr, in_fx, in_frame);
                } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    // Check that no data elements follow a length="*" or count="*" field
                    if (seen_star_field) {
                        error(c.loc, "choice '" + c.name + "' follows '" + star_field_name +
                              "' which consumes remaining bytes; it must be the last element");
                    }
                    // Forward reference checks on choice expressions
                    check_no_forward_refs(c.switch_expr.get(), field_names,
                                        "choice '" + c.name + "' switch", parent_scope);
                    check_no_forward_refs(c.present_when.get(), field_names,
                                        "choice '" + c.name + "' present-when", parent_scope);
                    check_no_forward_refs(c.length_from.get(), field_names,
                                        "choice '" + c.name + "' length-from", parent_scope);
                    // Expression safety checks
                    check_expr_safety(c.switch_expr.get(), "choice '" + c.name + "' switch");
                    check_expr_safety(c.present_when.get(), "choice '" + c.name + "' present-when");
                    check_expr_safety(c.length_from.get(), "choice '" + c.name + "' length-from");
                    check_constant_refs(c.switch_expr.get(), "choice '" + c.name + "' switch");
                    check_constant_refs(c.present_when.get(), "choice '" + c.name + "' present-when");
                    check_constant_refs(c.length_from.get(), "choice '" + c.name + "' length-from");
                    // Merge local field_names with parent_scope so nested choices
                    // can reference fields from any ancestor scope.
                    std::set<std::string> merged_scope = field_names;
                    if (parent_scope)
                        merged_scope.insert(parent_scope->begin(), parent_scope->end());
                    validate_choice(c, parent_name, in_bounded_container, &merged_scope);
                    check_case_value_range(c, children);
                    if (!c.name.empty()) {
                        if (!field_names.insert(c.name).second) {
                            error(c.loc, "duplicate name '" + c.name + "' in " + parent_name);
                        }
                    }
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    fx_count++;
                    if (fx_count > 1) {
                        error(c.loc, "multiple <fx> elements at same level in " + parent_name);
                    }
                    if (c.children.empty()) {
                        error(c.loc, "<fx> block is empty in " + parent_name);
                    }
                    // FX cannot appear inside array, choice, case, otherwise, or bitmap
                    if (in_array) {
                        error(c.loc, "<fx> cannot appear inside <array> in " + parent_name);
                    }
                    if (in_choice) {
                        error(c.loc, "<fx> cannot appear inside <choice>/<case>/<otherwise> in " + parent_name);
                    }
                    if (in_bitmap) {
                        error(c.loc, "<fx> cannot appear inside presence=\"bitmap\" struct in " + parent_name);
                    }
                    validate_children(c.children, false, parent_name + ".<fx>",
                                     in_array, in_choice, in_bounded_container,
                                     nullptr, /*in_fx=*/true, in_frame);
                } else if constexpr (std::is_same_v<T, model::Reserved>) {
                    if (c.bits <= 0) {
                        error(c.loc, "<reserved> has no size in " + parent_name);
                    }
                } else if constexpr (std::is_same_v<T, model::Align>) {
                    // V4/V8: Validate Align elements
                    validate_align(c, parent_name);
                }
            }, child);
        }

        // Check auto expression field references exist in sibling scope
        // (field_names is now fully populated after the main loop)
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && !f->auto_expr->field_ref.empty()) {
                    // "payload" is valid in frame context (handled separately)
                    if (f->auto_expr->field_ref != "payload" &&
                        field_names.count(f->auto_expr->field_ref) == 0) {
                        error(f->loc, "field '" + f->name + "': auto expression references "
                              "unknown sibling '" + f->auto_expr->field_ref + "'");
                    }
                    // auto="count(X)" target must be an array, not a scalar field
                    if (f->auto_expr->kind == model::AutoKind::Count &&
                        f->auto_expr->field_ref != "payload" &&
                        array_names.count(f->auto_expr->field_ref) == 0 &&
                        field_names.count(f->auto_expr->field_ref) > 0) {
                        error(f->loc, "field '" + f->name + "': auto=\"count(" +
                              f->auto_expr->field_ref + ")\" target must be an array");
                    }
                }
                // Also validate modifier field operands exist in scope
                if (f->auto_expr && f->auto_expr->modifier.is_field_operand()) {
                    if (field_names.count(f->auto_expr->modifier.field_ref) == 0) {
                        error(f->loc, "field '" + f->name + "': auto modifier references "
                              "unknown sibling '" + f->auto_expr->modifier.field_ref + "'");
                    }
                }
            }
        }
    }

    // V4/V8: Validate alignment element
    void validate_align(const model::Align& a, const std::string& parent_name) {
        if (a.to <= 0) {
            error(a.loc, "<align> 'to' must be positive in " + parent_name);
        } else if ((a.to & (a.to - 1)) != 0) {
            error(a.loc, "<align> 'to' (" + std::to_string(a.to) + ") is not a power of 2 in " + parent_name);
        } else if (a.to > 8) {
            error(a.loc, "<align> 'to' (" + std::to_string(a.to) + ") exceeds maximum of 8 bytes in " + parent_name);
        }
    }

    // C8: Check that inlining a struct doesn't produce duplicate field names
    void check_inline_duplicates(const model::Field& f, std::set<std::string>& field_names,
                                  const std::string& parent_name,
                                  std::set<std::string>& visiting) {
        if (f.type_ref.empty()) return;
        // Cycle detection: prevent infinite recursion on self-referential inline structs
        if (!visiting.insert(f.type_ref).second) return;
        auto resolved = index_.find(f.type_ref);
        if (!resolved) return;
        std::visit([this, &field_names, &parent_name, &visiting](const auto* def) {
            using DT = std::decay_t<decltype(*def)>;
            if constexpr (std::is_same_v<DT, model::StructDef> || std::is_same_v<DT, model::MessageDef>) {
                for (const auto& child : def->children) {
                    if (auto* cf = std::get_if<model::Field>(&child)) {
                        if (!cf->name.empty() && !field_names.insert(cf->name).second) {
                            error(cf->loc, "inline of '" + def->name +
                                  "' introduces duplicate field name '" + cf->name +
                                  "' in " + parent_name);
                        }
                        // Recurse into nested inline fields with cycle protection
                        if (cf->is_inline && !cf->type_ref.empty()) {
                            check_inline_duplicates(*cf, field_names, parent_name, visiting);
                        }
                    }
                }
            }
        }, *resolved);
        visiting.erase(f.type_ref);
    }

    void validate_field(const model::Field& f, bool in_bitmap, const std::string& parent_name,
                        bool in_fx = false, bool in_frame = false) {
        if (f.name.empty()) {
            error(f.loc, "field has empty name in " + parent_name);
        }
        check_keyword_collision(f.loc, f.name, "field");
        check_cpp_name_valid(f.loc, f.name, "field");

        // Must have type, bits, bytes, or base
        if (f.type_ref.empty() && !f.bits && !f.bytes_attr && !f.base) {
            error(f.loc, "field '" + f.name + "' must have 'type', 'bits', 'bytes', or 'base' attribute");
        }

        // base cannot combine with type (it's for inline definitions only)
        if (f.base && !f.type_ref.empty()) {
            error(f.loc, "field '" + f.name + "': 'base' and 'type' are mutually exclusive");
        }

        // Validate inline base types
        if (f.base) {
            switch (*f.base) {
                case model::PrimitiveBase::Float:
                    if (!f.bits || *f.bits <= 0) {
                        error(f.loc, "field '" + f.name + "': base=\"float\" requires bits > 0");
                    }
                    if (f.is_signed) {
                        error(f.loc, "field '" + f.name + "': base=\"float\" cannot combine with signed");
                    }
                    if (f.wire_encoding) {
                        error(f.loc, "field '" + f.name + "': base=\"float\" cannot combine with wire-encoding");
                    }
                    if (!f.enum_values.empty()) {
                        error(f.loc, "field '" + f.name + "': base=\"float\" cannot combine with inline enum");
                    }
                    if (!f.flags.empty()) {
                        error(f.loc, "field '" + f.name + "': base=\"float\" cannot combine with inline flags");
                    }
                    break;
                case model::PrimitiveBase::String:
                    if (f.bits) {
                        error(f.loc, "field '" + f.name + "': base=\"string\" cannot have 'bits'");
                    }
                    if (!f.length && !f.length_from && !f.length_prefix && !f.terminated && !f.length_star) {
                        error(f.loc, "field '" + f.name + "': base=\"string\" requires length, length-from, length-prefix, terminated, or length=\"*\"");
                    }
                    break;
                case model::PrimitiveBase::Bytes:
                    if (f.bits) {
                        error(f.loc, "field '" + f.name + "': base=\"bytes\" cannot have 'bits'; use bytes size attribute instead");
                    }
                    if (!f.length && !f.length_from && !f.bytes_attr && !f.length_star) {
                        error(f.loc, "field '" + f.name + "': base=\"bytes\" requires length, length-from, bytes, or length=\"*\"");
                    }
                    break;
                case model::PrimitiveBase::Bool:
                    // If bits absent, bool defaults to 1 bit (handled in codegen)
                    break;
                case model::PrimitiveBase::Int:
                case model::PrimitiveBase::Uint:
                    if (f.bits && *f.bits == 0) {
                        error(f.loc, "field '" + f.name + "': inline numeric type must have bits > 0");
                    }
                    break;
            }
        }

        // bit and present-when mutually exclusive
        if (f.bit && f.present_when) {
            error(f.loc, "field '" + f.name + "': 'bit' and 'present-when' are mutually exclusive");
        }

        // bit only valid in bitmap
        if (f.bit && !in_bitmap) {
            error(f.loc, "field '" + f.name + "': 'bit' only valid inside presence=\"bitmap\" struct");
        }

        // Auto field validation (5a)
        if (f.auto_attr) {
            if (!f.auto_expr) {
                // auto_expr parsing failed — unknown auto expression
                error(f.loc, "field '" + f.name + "': unknown auto expression '" + *f.auto_attr + "'");
            } else {
                switch (f.auto_expr->kind) {
                    case model::AutoKind::Increment: {
                        auto base = resolve_base(f);
                        if (base != model::PrimitiveBase::Uint) {
                            error(f.loc, "field '" + f.name + "': auto=\"increment\" only valid on unsigned integer fields");
                        }
                        if (f.constraint && (f.constraint->equals || f.constraint->min || f.constraint->max)) {
                            error(f.loc, "field '" + f.name + "': auto=\"increment\" fields cannot have constraints");
                        }
                        break;
                    }
                    case model::AutoKind::Length: {
                        // auto="length" not valid inside FX blocks (backpatch would be corrupted)
                        if (in_fx) {
                            error(f.loc, "field '" + f.name + "': auto=\"length\" is not valid inside <fx> blocks "
                                  "(dynamic FX layout would corrupt backpatch offsets)");
                        }
                        // auto="length" valid in both frame and struct context
                        // Verify integer type (signed or unsigned), <= 32 bits
                        auto base = resolve_base(f);
                        if (base != model::PrimitiveBase::Uint && base != model::PrimitiveBase::Int) {
                            error(f.loc, "field '" + f.name + "': auto=\"length\" requires integer type");
                        }
                        int auto_bits = 0;
                        if (f.bits) auto_bits = *f.bits;
                        else if (f.bytes_attr) auto_bits = *f.bytes_attr * 8;
                        else if (!f.type_ref.empty()) {
                            auto it = index_.types.find(f.type_ref);
                            if (it != index_.types.end()) auto_bits = it->second->bits;
                        }
                        if (auto_bits > 32) {
                            error(f.loc, "field '" + f.name + "': auto=\"length\" maximum supported is 32 bits");
                        }
                        if (f.constraint && (f.constraint->equals || f.constraint->min || f.constraint->max)) {
                            error(f.loc, "field '" + f.name +
                                  "': auto=\"length\" fields cannot have constraints");
                        }
                        // Validate arithmetic modifier
                        if (f.auto_expr->modifier.has_modifier()) {
                            auto mod_op = f.auto_expr->modifier.op;
                            if ((mod_op == model::ArithOp::Div || mod_op == model::ArithOp::Mod) &&
                                !f.auto_expr->modifier.is_field_operand() &&
                                f.auto_expr->modifier.literal == 0) {
                                error(f.loc, "field '" + f.name + "': division/modulo by zero in auto expression");
                            }
                            if (mod_op == model::ArithOp::Mul &&
                                !f.auto_expr->modifier.is_field_operand() &&
                                f.auto_expr->modifier.literal == 0) {
                                error(f.loc, "field '" + f.name + "': multiplication by zero in auto expression "
                                      "(inverse would divide by zero at decode)");
                            }
                        }
                        break;
                    }
                    case model::AutoKind::Id:
                        if (!in_frame) {
                            error(f.loc, "field '" + f.name + "': auto=\"id\" is only valid inside <frame> definitions");
                        }
                        if (f.constraint && (f.constraint->equals || f.constraint->min || f.constraint->max)) {
                            error(f.loc, "field '" + f.name + "': auto=\"id\" fields cannot have constraints");
                        }
                        break;
                    case model::AutoKind::Config:
                        if (f.constraint && (f.constraint->equals || f.constraint->min || f.constraint->max)) {
                            error(f.loc, "field '" + f.name + "': auto=\"config\" fields cannot have constraints");
                        }
                        break;
                    case model::AutoKind::Count: {
                        auto base = resolve_base(f);
                        if (base != model::PrimitiveBase::Uint && base != model::PrimitiveBase::Int) {
                            error(f.loc, "field '" + f.name + "': auto=\"count\" requires integer type");
                        }
                        if (f.auto_expr->field_ref.empty()) {
                            error(f.loc, "field '" + f.name + "': auto=\"count\" requires a field reference, e.g. auto=\"count(items)\"");
                        }
                        break;
                    }
                    case model::AutoKind::Timestamp: {
                        auto base = resolve_base(f);
                        if (base != model::PrimitiveBase::Uint) {
                            error(f.loc, "field '" + f.name + "': auto=\"timestamp\" only valid on unsigned integer fields");
                        }
                        if (f.constraint && (f.constraint->equals || f.constraint->min || f.constraint->max)) {
                            error(f.loc, "field '" + f.name + "': auto=\"timestamp\" fields cannot have constraints");
                        }
                        break;
                    }
                }
            }
        }

        // default not valid on struct/message/bytes fields
        if (f.default_value) {
            if (!f.type_ref.empty() && is_struct_or_message_type(f.type_ref)) {
                error(f.loc, "field '" + f.name + "': 'default' not valid on struct/message fields");
            }
            if (f.type_ref == "bytes" || f.bytes_attr) {
                error(f.loc, "field '" + f.name + "': 'default' not valid on bytes fields");
            }
            // Type compatibility checks for default value
            // First check if field type is an enum — enum defaults are validated by name
            bool is_enum_type = false;
            if (!f.type_ref.empty()) {
                auto resolved = index_.find(f.type_ref);
                if (resolved) {
                    std::visit([&](const auto* def) {
                        using DT = std::decay_t<decltype(*def)>;
                        if constexpr (std::is_same_v<DT, model::TypeDef>) {
                            if (!def->enum_values.empty()) {
                                is_enum_type = true;
                                bool found_enum = false;
                                for (const auto& ev : def->enum_values) {
                                    if (ev.name == *f.default_value) {
                                        found_enum = true;
                                        break;
                                    }
                                }
                                if (!found_enum) {
                                    error(f.loc, "field '" + f.name + "': default value '" +
                                          *f.default_value + "' does not match any enum value");
                                }
                            }
                        }
                    }, *resolved);
                }
            }
            // Also check inline enum values
            if (!is_enum_type && !f.enum_values.empty()) {
                is_enum_type = true;
                bool found_enum = false;
                for (const auto& ev : f.enum_values) {
                    if (ev.name == *f.default_value) {
                        found_enum = true;
                        break;
                    }
                }
                if (!found_enum) {
                    error(f.loc, "field '" + f.name + "': default value '" +
                          *f.default_value + "' does not match any enum value");
                }
            }
            // Non-enum type compatibility checks
            if (!is_enum_type) {
                auto base = resolve_base(f);
                if (base == model::PrimitiveBase::Uint || base == model::PrimitiveBase::Int) {
                    try {
                        // Support hex (0x...) and decimal default values
                        int field_bits = 0;
                        if (f.bits) field_bits = *f.bits;
                        else if (f.bytes_attr) field_bits = *f.bytes_attr * 8;
                        else if (!f.type_ref.empty()) {
                            auto it = index_.types.find(f.type_ref);
                            if (it != index_.types.end()) field_bits = it->second->bits;
                        }
                        if (field_bits > 0) {
                            if (base == model::PrimitiveBase::Uint) {
                                // Use stoull for unsigned to handle values > LLONG_MAX (e.g. UINT64_MAX)
                                auto uv = std::stoull(*f.default_value, nullptr, 0);
                                if (field_bits >= 64) {
                                    (void)uv; // Any value fits in uint64_t if it parsed
                                } else if (uv >= (1ULL << field_bits)) {
                                    error(f.loc, "field '" + f.name + "': default value " +
                                          *f.default_value + " out of range for " +
                                          std::to_string(field_bits) + "-bit unsigned field");
                                }
                            } else {
                                long long v = std::stoll(*f.default_value, nullptr, 0);
                                if (field_bits < 64) {
                                    long long max_val = (1LL << (field_bits - 1)) - 1;
                                    long long min_val = -(1LL << (field_bits - 1));
                                    if (v < min_val || v > max_val) {
                                        error(f.loc, "field '" + f.name + "': default value " +
                                              *f.default_value + " out of range for " +
                                              std::to_string(field_bits) + "-bit signed field");
                                    }
                                }
                            }
                        } else {
                            // No bits known — just verify it parses as an integer
                            if (base == model::PrimitiveBase::Uint)
                                (void)std::stoull(*f.default_value, nullptr, 0);
                            else
                                (void)std::stoll(*f.default_value, nullptr, 0);
                        }
                    } catch (...) {
                        error(f.loc, "field '" + f.name + "': default value '" +
                              *f.default_value + "' is not a valid integer");
                    }
                } else if (base == model::PrimitiveBase::Bool) {
                    auto& dv = *f.default_value;
                    if (dv != "true" && dv != "false" && dv != "0" && dv != "1") {
                        error(f.loc, "field '" + f.name + "': default value '" +
                              dv + "' is not valid for a bool field (use true/false/0/1)");
                    }
                } else if (base == model::PrimitiveBase::Float) {
                    try {
                        (void)std::stod(*f.default_value);
                    } catch (...) {
                        error(f.loc, "field '" + f.name + "': default value '" +
                              *f.default_value + "' is not a valid float");
                    }
                }
            }
        }

        // inline field must reference struct or message (5c)
        if (f.is_inline && !f.type_ref.empty()) {
            if (!is_struct_or_message_type(f.type_ref)) {
                error(f.loc, "field '" + f.name + "': inline type must be struct or message");
            }
        }

        // char-bits range validation (5d)
        if (f.char_bits) {
            auto cb_base = resolve_base(f);
            if (cb_base != model::PrimitiveBase::String && cb_base != model::PrimitiveBase::Bytes) {
                error(f.loc, "field '" + f.name + "': char-bits only valid on string or bytes fields");
            }
            if (*f.char_bits < 1 || *f.char_bits > 8) {
                error(f.loc, "field '" + f.name + "': char-bits must be 1-8");
            }
            if (f.terminated) {
                error(f.loc, "field '" + f.name + "': char-bits not combinable with terminated");
            }
            if (f.length_prefix) {
                error(f.loc, "field '" + f.name + "': char-bits not combinable with length-prefix");
            }
            if (f.length_from) {
                error(f.loc, "field '" + f.name + "': char-bits not combinable with length-from");
            }
            if (f.length_star) {
                error(f.loc, "field '" + f.name + "': char-bits not combinable with length=\"*\"");
            }
        }

        // length-prefix must reference unsigned integer type (5e)
        if (f.length_prefix) {
            auto it = index_.types.find(*f.length_prefix);
            if (it == index_.types.end()) {
                error(f.loc, "field '" + f.name + "': length-prefix references unknown type '" +
                      *f.length_prefix + "'");
            } else if (it->second->base != model::PrimitiveBase::Uint) {
                error(f.loc, "field '" + f.name + "': length-prefix must reference unsigned integer type");
            }
        }

        // Constant value format validation (5h)
        if (f.constraint && f.constraint->equals) {
            const auto& eq = *f.constraint->equals;
            // Check if it looks like a constant reference (starts with uppercase)
            bool is_const_ref = !eq.empty() && std::isupper(static_cast<unsigned char>(eq[0]));
            if (is_const_ref) {
                // Verify the referenced constant exists and type is compatible
                auto cit = index_.constants.find(eq);
                if (cit == index_.constants.end()) {
                    error(f.constraint->loc, "field '" + f.name +
                          "': constraint references unknown constant '" + eq + "'");
                } else {
                    // Check that the constant's type is compatible with the field's type
                    auto field_base = resolve_base(f);
                    if (!cit->second->type_ref.empty()) {
                        auto tit = index_.types.find(cit->second->type_ref);
                        if (tit != index_.types.end()) {
                            auto const_base = tit->second->base;
                            bool compatible =
                                (field_base == const_base) ||
                                (field_base == model::PrimitiveBase::Int && const_base == model::PrimitiveBase::Uint) ||
                                (field_base == model::PrimitiveBase::Uint && const_base == model::PrimitiveBase::Int);
                            if (!compatible) {
                                error(f.constraint->loc, "field '" + f.name +
                                      "': constraint constant '" + eq + "' type mismatch");
                            }
                        }
                    }
                }
            } else if (!is_valid_numeric_literal(eq)) {
                // Could be an enum value reference — check field's type and inline enums
                bool is_enum_val = false;
                // Check inline enum values on the field itself
                for (const auto& ev : f.enum_values) {
                    if (ev.name == eq) { is_enum_val = true; break; }
                }
                // Check the referenced type's enum values
                if (!is_enum_val && !f.type_ref.empty()) {
                    auto it = index_.types.find(f.type_ref);
                    if (it != index_.types.end()) {
                        for (const auto& ev : it->second->enum_values) {
                            if (ev.name == eq) { is_enum_val = true; break; }
                        }
                    }
                }
                if (!is_enum_val) {
                    error(f.constraint->loc, "field '" + f.name +
                          "': constraint equals value must be a numeric literal, constant, or enum value");
                }
            }
        }

        // length-includes-prefix requires length-prefix
        if (f.length_includes_prefix && !f.length_prefix) {
            error(f.loc, "field '" + f.name + "': length-includes-prefix requires length-prefix");
        }

        // C5: scale/offset on field only valid on numeric types
        if (f.scale || f.offset) {
            auto base = resolve_base(f);
            if (base != model::PrimitiveBase::Uint &&
                base != model::PrimitiveBase::Int &&
                base != model::PrimitiveBase::Float) {
                error(f.loc, "field '" + f.name + "': scale/offset only valid on numeric types");
            }
        }

        // Mutual exclusion: enum, flags, scale/offset on field
        int trait_count = 0;
        if (!f.enum_values.empty()) trait_count++;
        if (!f.flags.empty()) trait_count++;
        if (f.scale || f.offset) trait_count++;
        if (trait_count > 1) {
            error(f.loc, "field '" + f.name + "': enum, flags, and scale/offset are mutually exclusive");
        }

        // Constraint
        if (f.constraint) {
            validate_constraint(*f.constraint, "field '" + f.name + "'");
        }

        // V32/V33: Validate inline flags and enums on field
        // Determine the field's bit width for validation
        int field_bits = 0;
        if (f.bits) {
            field_bits = *f.bits;
        } else if (f.bytes_attr) {
            field_bits = *f.bytes_attr * 8;
        } else if (!f.type_ref.empty()) {
            auto it = index_.types.find(f.type_ref);
            if (it != index_.types.end()) {
                field_bits = it->second->bits;
            }
        }

        // V32: Validate inline flags - bit values must be within field's bit width
        if (!f.flags.empty() && field_bits > 0) {
            std::set<int> bits;
            std::set<std::string> names;
            for (const auto& fl : f.flags) {
                if (fl.name.empty()) {
                    error(fl.loc, "flag has empty name in field '" + f.name + "'");
                }
                if (fl.bit < 0 || fl.bit >= field_bits) {
                    error(fl.loc, "flag bit " + std::to_string(fl.bit) +
                        " out of range [0, " + std::to_string(field_bits - 1) +
                        "] in field '" + f.name + "'");
                }
                if (!bits.insert(fl.bit).second) {
                    error(fl.loc, "duplicate flag bit " + std::to_string(fl.bit) +
                        " in field '" + f.name + "'");
                }
                if (!names.insert(fl.name).second) {
                    error(fl.loc, "duplicate flag name '" + fl.name +
                        "' in field '" + f.name + "'");
                }
            }
        }

        // V33: Validate inline enums - duplicate ids, duplicate names, negative ids, keywords, identifiers
        if (!f.enum_values.empty()) {
            std::set<int64_t> ids;
            std::set<std::string> names;
            for (const auto& ev : f.enum_values) {
                if (ev.name.empty()) {
                    error(ev.loc, "enum value has empty name in field '" + f.name + "'");
                }
                check_keyword_collision(ev.loc, ev.name, "inline enum value");
                {
                    auto converted = codegen::to_enum_value_name(ev.name);
                    if (!codegen::is_valid_cpp_identifier(converted)) {
                        error(ev.loc, "enum value name '" + ev.name +
                              "' is not a valid C++ identifier after conversion");
                    }
                }
                if (!ids.insert(ev.id).second) {
                    error(ev.loc, "duplicate enum id " + std::to_string(ev.id) +
                        " in field '" + f.name + "'");
                }
                if (!names.insert(ev.name).second) {
                    error(ev.loc, "duplicate enum name '" + ev.name +
                        "' in field '" + f.name + "'");
                }
                if (ev.id < 0) {
                    error(ev.loc, "enum id must be non-negative in field '" + f.name + "'");
                }
                if (field_bits > 0 && field_bits < 64 && ev.id >= (1LL << field_bits)) {
                    error(ev.loc, "enum id " + std::to_string(ev.id) +
                          " exceeds " + std::to_string(field_bits) +
                          "-bit range in field '" + f.name + "'");
                }
            }
        }

        // V34: Constraint value range check against field bit width
        if (f.constraint && field_bits > 0 && field_bits <= 64) {
            auto base = resolve_base(f);
            bool is_signed = (base == model::PrimitiveBase::Int);

            auto check_range = [&](const std::string& val_str, const char* label) {
                auto v = parse_literal(val_str);
                if (!v) return;
                bool out_of_range = false;
                if (field_bits == 64) {
                    out_of_range = !is_signed && *v < 0;
                } else {
                    int64_t min_val = is_signed ? -(1LL << (field_bits - 1)) : 0;
                    int64_t max_val = is_signed ? (1LL << (field_bits - 1)) - 1
                                                : static_cast<int64_t>((1ULL << field_bits) - 1);
                    out_of_range = (*v < min_val || *v > max_val);
                }
                if (out_of_range) {
                    error(f.constraint->loc, "field '" + f.name + "': constraint " +
                          label + " value " + val_str + " exceeds " +
                          std::to_string(field_bits) + "-bit range");
                }
            };

            if (f.constraint->equals && is_valid_numeric_literal(*f.constraint->equals)) {
                check_range(*f.constraint->equals, "equals");
            }
            if (f.constraint->min) {
                check_range(*f.constraint->min, "min");
            }
            if (f.constraint->max) {
                check_range(*f.constraint->max, "max");
            }
        }

        // Wire encoding validation on field
        if (f.wire_encoding) {
            auto base = resolve_base(f);
            // Use field_bits calculated above
            switch (*f.wire_encoding) {
                case model::WireEncoding::BCD:
                    if (base != model::PrimitiveBase::Uint) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bcd' requires unsigned type");
                    }
                    if (field_bits > 0 && field_bits % 4 != 0) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bcd' requires bits divisible by 4");
                    }
                    break;
                case model::WireEncoding::BCD_S:
                    if (base != model::PrimitiveBase::Int) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bcd-s' requires signed type");
                    }
                    if (field_bits > 0 && field_bits < 5) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bcd-s' requires bits >= 5");
                    }
                    if (field_bits > 0 && (field_bits - 1) % 4 != 0) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bcd-s' requires (bits - 1) divisible by 4");
                    }
                    break;
                case model::WireEncoding::BNR_S:
                    if (base != model::PrimitiveBase::Int) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bnr-s' requires signed type");
                    }
                    if (field_bits > 0 && field_bits < 2) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bnr-s' requires bits >= 2");
                    }
                    break;
                case model::WireEncoding::CB2:
                    if (base != model::PrimitiveBase::Int) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'cb2' requires signed type");
                    }
                    break;
                case model::WireEncoding::BNR:
                    if (base != model::PrimitiveBase::Uint) {
                        error(f.loc, "field '" + f.name + "': wire-encoding 'bnr' requires unsigned type");
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // Conservative fixed-size check for an array element. Used to gate
    // count="fx": the FX continuation bit is read at a fixed offset after each
    // element, so a variable-length element is not permitted. Returns false
    // (variable / unknown) for anything not provably fixed-size.
    bool child_fixed_size(const model::StructChild& child) {
        return std::visit([this](const auto& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                // Explicit bit/byte width is always fixed.
                if (c.bits || c.bytes_attr) return true;
                // Open-ended or length-delimited (string/bytes) fields are variable.
                if (c.length_star || c.length_from || c.length || c.terminated ||
                    c.max_length || c.length_prefix) {
                    return false;
                }
                if (c.type_ref.empty()) {
                    // Inline field with neither width nor type: not provably fixed.
                    return false;
                }
                auto resolved = index_.find(c.type_ref);
                if (!resolved) return false;
                return std::visit([this](const auto* def) -> bool {
                    using DT = std::decay_t<decltype(*def)>;
                    if constexpr (std::is_same_v<DT, model::TypeDef>) {
                        // String types are variable unless they carry a fixed length.
                        if (def->base == model::PrimitiveBase::String) {
                            return def->length.has_value();
                        }
                        return def->bits > 0;
                    } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                        for (const auto& sc : def->children) {
                            if (!child_fixed_size(sc)) return false;
                        }
                        return true;
                    } else {
                        return false;
                    }
                }, *resolved);
            } else if constexpr (std::is_same_v<T, model::Reserved> ||
                                 std::is_same_v<T, model::Align>) {
                return true;
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                for (const auto& sc : c.children) {
                    if (!child_fixed_size(sc)) return false;
                }
                return true;
            } else {
                // Arrays, choices, nested FX blocks: variable / not provably fixed.
                return false;
            }
        }, child);
    }

    bool array_element_fixed_size(const model::ArrayDef& a) {
        if (!a.type_ref.empty()) {
            auto resolved = index_.find(a.type_ref);
            if (!resolved) return false;
            return std::visit([this](const auto* def) -> bool {
                using DT = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                    if (def->base == model::PrimitiveBase::String) {
                        return def->length.has_value();
                    }
                    return def->bits > 0;
                } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                    for (const auto& sc : def->children) {
                        if (!child_fixed_size(sc)) return false;
                    }
                    return true;
                } else {
                    return false;
                }
            }, *resolved);
        }
        // Inline element children.
        if (a.children.empty()) return false;
        for (const auto& sc : a.children) {
            if (!child_fixed_size(sc)) return false;
        }
        return true;
    }

    void validate_array(const model::ArrayDef& a, const std::string& parent_name) {
        if (a.name.empty()) {
            error(a.loc, "array has empty name in " + parent_name);
        }

        // Must have exactly one of count, count-from, count="*"
        int count_specs = 0;
        if (a.fixed_count) count_specs++;
        if (a.count_from) count_specs++;
        if (a.count_star) count_specs++;
        if (a.count_fx) count_specs++;
        if (count_specs == 0) {
            error(a.loc, "array '" + a.name + "' must have 'count', 'count-from', count=\"*\", or count=\"fx\"");
        } else if (count_specs > 1) {
            error(a.loc, "array '" + a.name + "' has multiple count specifications");
        }

        // type or inline children, not both
        if (!a.type_ref.empty() && !a.children.empty()) {
            error(a.loc, "array '" + a.name + "' has both 'type' and inline children");
        }

        // FX-terminated arrays must have a fixed-size element: the decoder reads
        // one element, then a single FX continuation bit, and repeats. A
        // variable-length element (e.g. an unbounded string, a count="*" child,
        // or a nested FX-terminated array) would make the FX bit position
        // ambiguous.
        if (a.count_fx && !array_element_fixed_size(a)) {
            error(a.loc, "array '" + a.name + "' with count=\"fx\" requires a "
                  "fixed-size element type (the FX bit follows each element at a "
                  "fixed bit position)");
        }
        // Note: children are validated by the caller (validate_children) which
        // passes the correct in_bounded_container context
    }

    void validate_choice(const model::ChoiceDef& c, const std::string& parent_name,
                        bool in_bounded_container = false,
                        const std::set<std::string>* parent_field_names = nullptr) {
        if (c.name.empty()) {
            error(c.loc, "choice has empty name in " + parent_name);
        }

        if (!c.switch_expr) {
            error(c.loc, "choice '" + c.name + "' missing 'switch' attribute");
        }

        if (c.cases.empty()) {
            error(c.loc, "choice '" + c.name + "' must have at least one <case>");
        }

        // Validate typeName on choice element itself (variant alias override)
        if (c.type_name) {
            validate_type_name(c.loc, *c.type_name, "choice", c.name, false);
        }

        std::set<std::string> case_names;
        for (const auto& cs : c.cases) {
            // Each case needs value or range
            if (!cs.value && !cs.range) {
                error(cs.loc, "case '" + cs.name + "' must have 'value' or 'range'");
            }
            if (cs.value && cs.range) {
                error(cs.loc, "case '" + cs.name + "' cannot have both 'value' and 'range'");
            }
            // Duplicate case name detection
            if (!cs.name.empty() && !case_names.insert(cs.name).second) {
                error(cs.loc, "duplicate case name '" + cs.name + "' in choice '" + c.name + "'");
            }

            // Validate typeName if present
            if (cs.type_name) {
                validate_type_name(cs.loc, *cs.type_name, "case", cs.name, !cs.type_ref.empty());
            }

            bool choice_bounded = c.length_from != nullptr || c.length;
            validate_children(cs.children, false, c.name + "." + cs.name, false, true,
                            choice_bounded || in_bounded_container, parent_field_names);
        }

        if (c.otherwise) {
            // Validate typeName on otherwise if present
            if (c.otherwise->type_name) {
                validate_type_name(c.otherwise->loc, *c.otherwise->type_name,
                                  "otherwise", c.otherwise->name.empty() ? "otherwise" : c.otherwise->name,
                                  !c.otherwise->type_ref.empty());
            }

            bool choice_bounded = c.length_from != nullptr || c.length;
            validate_children(c.otherwise->children, false, c.name + ".otherwise", false, true,
                            choice_bounded || in_bounded_container, parent_field_names);
        }

        // C6: Choice case overlap detection
        check_case_overlap(c);

        // bit and present-when mutually exclusive
        if (c.bit && c.present_when) {
            error(c.loc, "choice '" + c.name + "': 'bit' and 'present-when' are mutually exclusive");
        }
    }

    // V1: Validate case values fit within the switch expression's type range
    void check_case_value_range(const model::ChoiceDef& c,
                                const std::vector<model::StructChild>& siblings) {
        if (!c.switch_expr || c.switch_expr->op != model::ExprOp::FieldRef) return;
        const auto& field_name = c.switch_expr->name;
        if (field_name.find('.') != std::string::npos) return; // dotted path, skip

        // Find the switch field among siblings
        int field_bits = 0;
        bool is_signed = false;
        for (const auto& sib : siblings) {
            if (auto* f = std::get_if<model::Field>(&sib)) {
                if (f->name == field_name) {
                    if (f->bits) {
                        field_bits = *f->bits;
                        is_signed = f->is_signed;
                    } else if (f->bytes_attr) {
                        field_bits = *f->bytes_attr * 8;
                        is_signed = f->is_signed;
                    } else if (!f->type_ref.empty()) {
                        auto it = index_.types.find(f->type_ref);
                        if (it != index_.types.end()) {
                            field_bits = it->second->bits;
                            is_signed = (it->second->base == model::PrimitiveBase::Int);
                        }
                    }
                    break;
                }
            }
        }
        if (field_bits <= 0 || field_bits > 63) return; // cannot determine or too wide

        int64_t min_val = is_signed ? -(int64_t{1} << (field_bits - 1)) : 0;
        int64_t max_val;
        if (is_signed) {
            max_val = (int64_t{1} << (field_bits - 1)) - 1;
        } else if (field_bits >= 63) {
            max_val = INT64_MAX; // Cannot represent full unsigned 63+ bit range in int64_t
        } else {
            max_val = (int64_t{1} << field_bits) - 1;
        }

        for (const auto& cs : c.cases) {
            if (!cs.value) continue;
            auto v = parse_literal(*cs.value);
            if (!v) {
                // Try resolving as constant reference
                auto it = index_.constants.find(*cs.value);
                if (it != index_.constants.end()) {
                    v = parse_literal(it->second->value);
                }
            }
            if (v && (*v < min_val || *v > max_val)) {
                Logger::warn(cs.loc.to_string() + ": case value " + std::to_string(*v) +
                             " out of range [" + std::to_string(min_val) + ", " +
                             std::to_string(max_val) + "] for " + std::to_string(field_bits) +
                             "-bit " + (is_signed ? "signed" : "unsigned") +
                             " switch field '" + field_name + "' in choice '" + c.name + "'");
            }
        }
    }

    void check_case_overlap(const model::ChoiceDef& c) {
        struct Range { int64_t lo; int64_t hi; model::SourceLoc loc; model::Direction dir; };
        std::vector<Range> ranges;

        for (const auto& cs : c.cases) {
            if (cs.value) {
                auto v = parse_literal(*cs.value);
                if (!v) {
                    // Try resolving as constant reference
                    auto it = index_.constants.find(*cs.value);
                    if (it != index_.constants.end()) {
                        v = parse_literal(it->second->value);
                    }
                }
                if (v) ranges.push_back({*v, *v, cs.loc, cs.direction});
            } else if (cs.range) {
                auto dot_pos = cs.range->find("..");
                if (dot_pos == std::string::npos) {
                    error(cs.loc, "choice '" + c.name + "': case range '" + *cs.range +
                          "' missing '..' separator");
                } else {
                    auto lo_str = cs.range->substr(0, dot_pos);
                    auto hi_str = cs.range->substr(dot_pos + 2);
                    // Validate format: both boundaries must be non-empty
                    if (lo_str.empty() || hi_str.empty()) {
                        error(cs.loc, "choice '" + c.name + "': case range '" + *cs.range +
                              "' has empty boundary");
                    } else {
                        auto lo = parse_literal(lo_str);
                        if (!lo) {
                            auto it = index_.constants.find(std::string(lo_str));
                            if (it != index_.constants.end()) {
                                lo = parse_literal(it->second->value);
                            } else if (!lo_str.empty()) {
                                error(cs.loc, "choice '" + c.name + "': case range low boundary '" +
                                      std::string(lo_str) + "' is not a valid literal or constant");
                            }
                        }
                        auto hi = parse_literal(hi_str);
                        if (!hi) {
                            auto it = index_.constants.find(std::string(hi_str));
                            if (it != index_.constants.end()) {
                                hi = parse_literal(it->second->value);
                            } else if (!hi_str.empty()) {
                                error(cs.loc, "choice '" + c.name + "': case range high boundary '" +
                                      std::string(hi_str) + "' is not a valid literal or constant");
                            }
                        }
                        // V9: Check for inverted range (lo > hi)
                        if (lo && hi) {
                            if (*lo > *hi) {
                                error(cs.loc, "choice '" + c.name + "': case range '" + *cs.range +
                                      "' is inverted (lo > hi)");
                            }
                            ranges.push_back({*lo, *hi, cs.loc, cs.direction});
                        }
                    }
                }
            }
        }

        // Split ranges into direction lanes: send and receive.
        // Overlaps within a lane are errors; overlaps across lanes
        // (send-only vs receive-only) are allowed for direction-qualified dispatch.
        auto check_lane = [&](std::vector<Range>& lane) {
            std::sort(lane.begin(), lane.end(), [](const Range& a, const Range& b) {
                return a.lo < b.lo;
            });
            for (size_t i = 1; i < lane.size(); i++) {
                if (lane[i].lo <= lane[i-1].hi) {
                    error(lane[i].loc, "choice '" + c.name + "': case values overlap");
                }
            }
        };

        std::vector<Range> send_lane, recv_lane;
        for (auto& r : ranges) {
            if (r.dir == model::Direction::Both || r.dir == model::Direction::Send)
                send_lane.push_back(r);
            if (r.dir == model::Direction::Both || r.dir == model::Direction::Receive)
                recv_lane.push_back(r);
        }
        check_lane(send_lane);
        check_lane(recv_lane);
    }

    // ========================================================================
    // ========================================================================
    // Frame validation (v2)
    // ========================================================================

    void validate_frames() {
        if (proto_.frames.empty()) return;

        // Rule: at most one frame per protocol
        if (proto_.frames.size() > 1) {
            error(proto_.frames[1].loc,
                  "multiple <frame> definitions found; only one frame per protocol is supported");
        }

        for (const auto& frame : proto_.frames) {
            // Validate header/footer children like struct fields
            validate_children(frame.header_fields, false, "frame '" + frame.name + "'", false, false, false,
                             nullptr, /*in_fx=*/false, /*in_frame=*/true);
            validate_children(frame.footer_fields, false, "frame '" + frame.name + "'", false, false, false,
                             nullptr, /*in_fx=*/false, /*in_frame=*/true);

            // Rule: frame fields must be scalar (no structs, arrays, choices)
            auto check_scalar_fields = [&](const std::vector<model::StructChild>& children,
                                           const std::string& section) {
                for (const auto& child : children) {
                    if (std::holds_alternative<model::StructDef>(child)) {
                        auto& sd = std::get<model::StructDef>(child);
                        error(sd.loc, "frame " + section + " may not contain struct '" + sd.name +
                              "'; only scalar fields are allowed in frame headers/footers");
                    } else if (std::holds_alternative<model::ChoiceDef>(child)) {
                        auto& cd = std::get<model::ChoiceDef>(child);
                        error(cd.loc, "frame " + section + " may not contain choice '" + cd.name +
                              "'; only scalar fields are allowed in frame headers/footers");
                    } else if (std::holds_alternative<model::ArrayDef>(child)) {
                        auto& ad = std::get<model::ArrayDef>(child);
                        error(ad.loc, "frame " + section + " may not contain array '" + ad.name +
                              "'; only scalar fields are allowed in frame headers/footers");
                    } else if (auto* f = std::get_if<model::Field>(&child)) {
                        if (!f->type_ref.empty() && is_struct_or_message_type(f->type_ref)) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' references struct/message type '" + f->type_ref +
                                  "'; only scalar types are allowed in frame headers/footers");
                        }
                        if (f->present_when) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' may not use present-when; frame fields must be unconditional");
                        }
                        if (f->terminated) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' may not use terminated; frame fields must have fixed size");
                        }
                        if (f->length_prefix) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' may not use length-prefix; frame fields must have fixed size");
                        }
                        if (f->length_from) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' may not use length-from; frame fields must have fixed size");
                        }
                        if (f->length_star) {
                            error(f->loc, "frame " + section + " field '" + f->name +
                                  "' may not use open-ended length; frame fields must have fixed size");
                        }
                    }
                }
            };
            check_scalar_fields(frame.header_fields, "header");
            check_scalar_fields(frame.footer_fields, "footer");

            // Count auto="id" and auto="length" fields in the frame
            int auto_id_count = 0;
            int auto_length_count = 0;
            int id_field_bits = 0;
            int length_field_bits = 0;
            std::set<std::string> config_keys;

            auto check_auto_fields = [&](const std::vector<model::StructChild>& children) {
                for (const auto& child : children) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        if (f->auto_expr) {
                            switch (f->auto_expr->kind) {
                                case model::AutoKind::Id: {
                                    auto_id_count++;
                                    // Resolve bit width for ID range checking
                                    if (f->bits) {
                                        id_field_bits = *f->bits;
                                    } else if (f->bytes_attr) {
                                        id_field_bits = *f->bytes_attr * 8;
                                    } else if (!f->type_ref.empty()) {
                                        auto it = index_.types.find(f->type_ref);
                                        if (it != index_.types.end()) id_field_bits = it->second->bits;
                                    }
                                    break;
                                }
                                case model::AutoKind::Length: {
                                    auto_length_count++;
                                    // Resolve bit width for backpatch validation
                                    if (f->bits) {
                                        length_field_bits = *f->bits;
                                    } else if (f->bytes_attr) {
                                        length_field_bits = *f->bytes_attr * 8;
                                    } else if (!f->type_ref.empty()) {
                                        auto it = index_.types.find(f->type_ref);
                                        if (it != index_.types.end()) length_field_bits = it->second->bits;
                                    }
                                    if (length_field_bits > 32) {
                                        error(f->loc, "auto=\"length\" field '" + f->name +
                                              "' is " + std::to_string(length_field_bits) +
                                              " bits; maximum supported is 32 bits");
                                    }
                                    if (f->constraint && (f->constraint->equals || f->constraint->min || f->constraint->max)) {
                                        error(f->loc, "field '" + f->name +
                                              "': auto=\"length\" fields cannot have constraints");
                                    }
                                    // Frame-level restrictions on arithmetic modifiers:
                                    // - Field operands disallowed (extract_frame_length only reads raw bytes)
                                    // - Modulo disallowed (no clean inverse for frame length recovery)
                                    if (f->auto_expr->modifier.has_modifier()) {
                                        if (f->auto_expr->modifier.is_field_operand()) {
                                            error(f->loc, "field '" + f->name +
                                                  "': field operands in auto=\"length\" arithmetic are not supported "
                                                  "at frame level (extract_frame_length cannot resolve field values)");
                                        }
                                        if (f->auto_expr->modifier.op == model::ArithOp::Mod) {
                                            error(f->loc, "field '" + f->name +
                                                  "': modulo operator in auto=\"length\" is not supported "
                                                  "at frame level (no inverse for frame length recovery)");
                                        }
                                    }
                                    break;
                                }
                                case model::AutoKind::Config: {
                                    if (f->constraint && (f->constraint->equals || f->constraint->min || f->constraint->max)) {
                                        error(f->loc, "field '" + f->name +
                                              "': auto=\"config\" fields cannot have constraints");
                                    }
                                    auto [_, inserted] = config_keys.insert(f->auto_expr->key);
                                    if (!inserted) {
                                        error(f->loc, "duplicate config key '" + f->auto_expr->key +
                                              "' in frame '" + frame.name + "'");
                                    }
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                    }
                }
            };

            check_auto_fields(frame.header_fields);
            check_auto_fields(frame.footer_fields);

            // Also check for duplicate config keys across messages
            // (all config keys share a single Config struct in the session)
            std::function<void(const std::vector<model::StructChild>&)> scan_msg_configs;
            scan_msg_configs = [&](const std::vector<model::StructChild>& children) {
                for (const auto& child : children) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Config) {
                            auto [_, inserted] = config_keys.insert(f->auto_expr->key);
                            if (!inserted) {
                                error(f->loc, "duplicate config key '" + f->auto_expr->key +
                                      "' (keys must be unique across frame and all messages)");
                            }
                        }
                        // Recurse into inline fields
                        if (f->is_inline && !f->type_ref.empty()) {
                            auto resolved = index_.find(f->type_ref);
                            if (resolved) {
                                std::visit([&](const auto* def) {
                                    using DT = std::decay_t<decltype(*def)>;
                                    if constexpr (std::is_same_v<DT, model::StructDef> ||
                                                 std::is_same_v<DT, model::MessageDef>) {
                                        scan_msg_configs(def->children);
                                    }
                                }, *resolved);
                            }
                        }
                    }
                }
            };
            for (const auto& msg : proto_.messages) {
                scan_msg_configs(msg.children);
            }

            // Rule: exactly one auto="id" field
            if (auto_id_count == 0) {
                error(frame.loc, "frame '" + frame.name + "' must have exactly one auto=\"id\" field");
            } else if (auto_id_count > 1) {
                error(frame.loc, "frame '" + frame.name + "' has " + std::to_string(auto_id_count) +
                      " auto=\"id\" fields; exactly one is required");
            }

            // Rule: at most one auto="length" field
            if (auto_length_count > 1) {
                error(frame.loc, "frame '" + frame.name + "' has " + std::to_string(auto_length_count) +
                      " auto=\"length\" fields; at most one is allowed");
            }

            // Rule: auto field references must resolve to existing fields
            {
                std::set<std::string> frame_field_names;
                auto collect_names = [&](const std::vector<model::StructChild>& children) {
                    for (const auto& child : children) {
                        if (auto* f = std::get_if<model::Field>(&child)) {
                            frame_field_names.insert(f->name);
                        }
                    }
                };
                collect_names(frame.header_fields);
                collect_names(frame.footer_fields);

                auto check_refs = [&](const std::vector<model::StructChild>& children) {
                    for (const auto& child : children) {
                        if (auto* f = std::get_if<model::Field>(&child)) {
                            if (f->auto_expr && !f->auto_expr->field_ref.empty()) {
                                bool valid = frame_field_names.count(f->auto_expr->field_ref) > 0;
                                // "payload" is a valid ref only for length/count in frame context
                                if (!valid && f->auto_expr->field_ref == "payload" &&
                                    (f->auto_expr->kind == model::AutoKind::Length ||
                                     f->auto_expr->kind == model::AutoKind::Count)) {
                                    valid = true;
                                }
                                if (!valid) {
                                    error(f->loc, "field '" + f->name + "': auto expression references "
                                          "unknown field '" + f->auto_expr->field_ref + "'");
                                }
                            }
                        }
                    }
                };
                check_refs(frame.header_fields);
                check_refs(frame.footer_fields);
            }

            // Rule: at least one header field before payload
            if (frame.header_fields.empty()) {
                error(frame.loc, "frame '" + frame.name + "' has no header fields before <payload/>");
            }

            // Rule: at least one message must exist when a frame is defined
            if (proto_.messages.empty()) {
                error(frame.loc, "frame '" + frame.name +
                      "' is defined but no messages exist; at least one <message> is required");
            }

            // Validate message id values fit within id field bit width
            if (id_field_bits > 0 && id_field_bits < 64) {
                uint64_t max_id_val = (uint64_t(1) << id_field_bits) - 1;
                for (const auto& m : proto_.messages) {
                    if (m.id.empty()) continue;
                    if (is_valid_numeric_literal(m.id)) {
                        try {
                            uint64_t id_val = std::stoull(m.id, nullptr, 0);
                            if (id_val > max_id_val) {
                                error(m.loc, "message '" + m.name + "' id=" + m.id +
                                      " exceeds the " + std::to_string(id_field_bits) +
                                      "-bit id field range (max " + std::to_string(max_id_val) + ")");
                            }
                        } catch (...) { // NOLINT(bugprone-empty-catch)
                            // Non-numeric id (e.g., constant ref) — skip range check
                        }
                    }
                }
            }

            // Rule: message field names must not collide with frame field names
            {
                std::set<std::string> frame_names;
                for (const auto& child : frame.header_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        frame_names.insert(f->name);
                    }
                }
                for (const auto& child : frame.footer_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        frame_names.insert(f->name);
                    }
                }
                for (const auto& m : proto_.messages) {
                    for (const auto& child : m.children) {
                        if (auto* f = std::get_if<model::Field>(&child)) {
                            if (frame_names.count(f->name)) {
                                error(f->loc, "message '" + m.name + "' field '" + f->name +
                                      "' conflicts with frame field of the same name");
                            }
                        }
                    }
                }
            }
        }

        // Message v2 rules (when frame exists)
        if (!proto_.frames.empty()) {
            // Collect message id+direction pairs for uniqueness check
            struct IdDir {
                std::string id;
                model::Direction direction;
                const model::MessageDef* msg;
            };
            std::vector<IdDir> id_dirs;

            for (const auto& m : proto_.messages) {
                // Rule: every message must have an id when frame exists
                if (m.id.empty()) {
                    error(m.loc, "message '" + m.name +
                          "' must have an 'id' attribute when a frame is defined");
                    continue;
                }

                id_dirs.push_back({m.id, m.direction, &m});
            }

            // Check id uniqueness per direction lane
            for (size_t i = 0; i < id_dirs.size(); ++i) {
                for (size_t j = i + 1; j < id_dirs.size(); ++j) {
                    if (id_dirs[i].id != id_dirs[j].id) continue;

                    // Same id — check direction compatibility
                    auto d1 = id_dirs[i].direction;
                    auto d2 = id_dirs[j].direction;

                    // Same id + both direction=both → error
                    if (d1 == model::Direction::Both || d2 == model::Direction::Both) {
                        error(id_dirs[j].msg->loc,
                              "message '" + id_dirs[j].msg->name + "' has same id '" + id_dirs[j].id +
                              "' as '" + id_dirs[i].msg->name +
                              "'; both must have explicit direction (send/receive) to share an id");
                    }
                    // Same id + same direction → error
                    else if (d1 == d2) {
                        error(id_dirs[j].msg->loc,
                              "message '" + id_dirs[j].msg->name + "' has same id '" + id_dirs[j].id +
                              "' and direction as '" + id_dirs[i].msg->name + "'");
                    }
                    // Same id + different explicit directions → OK
                }
            }
        }
    }

    // ========================================================================
    // Struct cycle detection
    // ========================================================================

    void detect_struct_cycles() {
        // Build adjacency: for each struct/message, collect struct/message type_refs
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> in_stack;

        for (const auto& s : proto_.structs) {
            if (!visited.count(s.name)) {
                detect_cycle_dfs(s.name, visited, in_stack);
            }
        }
        for (const auto& m : proto_.messages) {
            if (!visited.count(m.name)) {
                detect_cycle_dfs(m.name, visited, in_stack);
            }
        }
    }

    void detect_cycle_dfs(const std::string& name,
                          std::unordered_set<std::string>& visited,
                          std::unordered_set<std::string>& in_stack) {
        visited.insert(name);
        in_stack.insert(name);

        // Get children of this struct/message
        const std::vector<model::StructChild>* children = nullptr;
        model::SourceLoc loc;

        auto sit = index_.structs.find(name);
        if (sit != index_.structs.end()) {
            children = &sit->second->children;
            loc = sit->second->loc;
        } else {
            auto mit = index_.messages.find(name);
            if (mit != index_.messages.end()) {
                children = &mit->second->children;
                loc = mit->second->loc;
            }
        }

        if (!children) {
            in_stack.erase(name);
            return;
        }

        auto refs = collect_struct_refs(*children);
        for (const auto& ref : refs) {
            if (in_stack.count(ref)) {
                error(loc, "recursive type cycle detected: '" + name +
                      "' eventually contains itself via '" + ref + "'");
                // Don't recurse further on this path
                continue;
            }
            if (!visited.count(ref)) {
                detect_cycle_dfs(ref, visited, in_stack);
            }
        }

        in_stack.erase(name);
    }

    // Collect all non-optional struct/message type references from children
    std::vector<std::string> collect_struct_refs(
            const std::vector<model::StructChild>& children) const {
        std::vector<std::string> refs;
        for (const auto& child : children) {
            std::visit([this, &refs](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    if (!c.type_ref.empty() && is_struct_or_message_type(c.type_ref)) {
                        refs.push_back(c.type_ref);
                    }
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    auto inner = collect_struct_refs(c.children);
                    refs.insert(refs.end(), inner.begin(), inner.end());
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    // Arrays contain references but they're heap-allocated,
                    // so they don't cause infinite recursion in the struct layout.
                    // Skip array children for cycle detection.
                } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    for (const auto& cs : c.cases) {
                        auto inner = collect_struct_refs(cs.children);
                        refs.insert(refs.end(), inner.begin(), inner.end());
                    }
                    if (c.otherwise) {
                        auto inner = collect_struct_refs(c.otherwise->children);
                        refs.insert(refs.end(), inner.begin(), inner.end());
                    }
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    auto inner = collect_struct_refs(c.children);
                    refs.insert(refs.end(), inner.begin(), inner.end());
                }
            }, child);
        }
        return refs;
    }

    const model::Protocol& proto_;
    const TypeIndex& index_;
    std::vector<ValidationError> errors_;
    std::unordered_set<std::string> used_type_names_;  // Track typeName uniqueness
};

} // anonymous namespace

ValidationResult validate(const model::Protocol& protocol, const TypeIndex& index) {
    ValidatorImpl v(protocol, index);
    v.validate();

    if (v.has_errors()) {
        return std::unexpected(std::move(v.errors()));
    }
    return {};
}

} // namespace bgen::analyzer
