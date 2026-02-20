// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: StructEmitter Class Declaration

#pragma once

#include "cpp_structs_helpers.hpp"
#include "emit_context.hpp"
#include "name_utils.hpp"
#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include "../analyzer/wire_sizer.hpp"
#include "../analyzer/session_analyzer.hpp"
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bgen::codegen {

// Info about a frame header/footer field to be pushed into message classes
struct FrameFieldInfo {
    FieldInfo fi;                      // name, cpp_type, is_auto_managed
    FieldTypeInfo fti;                 // is_enum, is_struct (typedef wrapper), bits
    const model::Field* source;        // for display format in to_string
};

class StructEmitter {
public:
    StructEmitter(EmitContext& ctx, const analyzer::TypeIndex& index,
                  const analyzer::WireSizeInfo& sizes, const std::string& ns);

    // Set the mapping from leaf type BMDL names to their computed type_ids
    void set_leaf_type_ids(const std::unordered_map<std::string, uint64_t>& map);

    // Set/clear the current session info (for struct emission with context)
    void set_current_session(const analyzer::SessionInfo* session);

    // Check whether the current bit position is known to be byte-aligned.
    bool is_byte_aligned() const;

    // Update bit alignment tracker after emitting a field with known bit width.
    void advance_bits(int bits);

    // After a variable-length field that operates on whole bytes.
    void advance_bits_variable();

    // Advance alignment for a Field in FX context based on its FieldTypeInfo.
    void advance_fx_field_bits(const model::Field& f, const FieldTypeInfo& fti);

    // Reset alignment tracker (e.g., at start of decode/encode)
    void reset_alignment();

    // A7: Resolve an enum value name to its qualified C++ name
    std::string resolve_enum_value(const std::string& name) const;

    // Emit using declarations for variant types used by choices in a struct/message
    void emit_variant_aliases(const std::vector<model::StructChild>& children);

    // Emit class definitions for any StructDef/ArrayDef children before the parent class.
    void emit_child_class_defs(const std::vector<model::StructChild>& children,
                               const std::string& parent_name = {});

    // Emit a synthetic struct class from a name and children reference
    void emit_synthetic_struct(const std::string& bmdl_name,
                               const std::vector<model::StructChild>& children,
                               const std::string& parent_name = {});

    void emit_doc_comment(const std::string& doc);

    void emit_struct(const model::StructDef& sd, const std::string& parent_name = {});

    void emit_message(const model::MessageDef& md,
                      const analyzer::SessionInfo* session = nullptr);

    // ========================================================================
    // Plain struct (non-bitmap) — in cpp_structs.cpp
    // ========================================================================

    void collect_frame_fields(const model::FrameDef& frame,
                             std::vector<FrameFieldInfo>& header_fields,
                             std::vector<FrameFieldInfo>& footer_fields);

    void emit_frame_field_accessors(const std::vector<FrameFieldInfo>& frame_fields);

    void emit_plain_struct(const std::vector<model::StructChild>& children,
                          const std::string& class_name,
                          const std::vector<FrameFieldInfo>& header_frame_fields = {},
                          const std::vector<FrameFieldInfo>& footer_frame_fields = {});

    void collect_fields(const std::vector<model::StructChild>& children,
                       std::vector<FieldInfo>& fields, bool& has_fx,
                       bool in_fx, int fx_depth);

    void emit_plain_accessors(const FieldInfo& fi);

    void emit_optional_accessors(const FieldInfo& fi);

    // Emit constraint validation checks for a setter (shared by plain, optional, and bitmap accessors)
    void emit_setter_constraint_checks(const std::string& name, const std::string& qual_type,
                                        const model::Constraint* constraint, bool is_signed,
                                        std::optional<int> max_length,
                                        bool is_bytes = false);

    // ========================================================================
    // Bitmap struct — in cpp_structs.cpp
    // ========================================================================

    void emit_bitmap_struct(const model::StructDef& sd, const std::string& class_name);

    void emit_bitmap_encode_fields(const std::vector<BitmapField>& bfields,
                                   int from_oct, int to_oct);

    // ========================================================================
    // Encode — in cpp_structs_encode.cpp
    // ========================================================================

    void emit_encode(const std::vector<model::StructChild>& children);
    void emit_encode_children(const std::vector<model::StructChild>& children);
    void emit_encode_constraint_check(const model::Constraint& c, const std::string& member,
                                       const std::string& field_name, bool is_signed = true);
    void emit_encode_field(const model::Field& f);
    void emit_encode_array(const model::ArrayDef& a, bool is_optional = false);
    void emit_encode_fx_array(const model::ArrayDef& a);
    void emit_encode_choice(const model::ChoiceDef& c, bool is_optional = false);
    void emit_encode_fx(const model::FxBlock& fx);
    void emit_encode_fx_children(const std::vector<model::StructChild>& children);
    void emit_fx_has_fields_check(const std::vector<model::StructChild>& children,
                                   const std::string& flag_var);

    // ========================================================================
    // Decode — in cpp_structs_decode.cpp
    // ========================================================================

    void emit_decode(const std::vector<model::StructChild>& children,
                    const std::string& class_name);
    void emit_to_string(const std::vector<model::StructChild>& children,
                        const std::vector<FieldInfo>& fields,
                        const std::string& class_name,
                        const std::vector<FrameFieldInfo>& header_frame_fields = {},
                        const std::vector<FrameFieldInfo>& footer_frame_fields = {});
    void emit_bitmap_to_string(const std::vector<BitmapField>& bfields,
                               const std::string& class_name);
    void emit_deferred_validate(const std::vector<model::StructChild>& children);
    void populate_optional_field_names(const std::vector<model::StructChild>& children);
    void populate_fx_optional_names(const std::vector<model::StructChild>& children);
    void populate_local_field_names(const std::vector<model::StructChild>& children);
    void emit_decode_children(const std::vector<model::StructChild>& children,
                             const std::string& result_var);
    void emit_decode_field(const model::Field& f, const std::string& result_var);
    void emit_decode_field_body(const model::Field& f, const std::string& result_var);
    void emit_decode_array(const model::ArrayDef& a, const std::string& result_var);
    void emit_decode_fx_array(const model::ArrayDef& a, const std::string& result_var);
    std::string emit_case_decode_call(const std::string& case_type,
                                       const std::string& reader_var,
                                       const std::string& ctx_var,
                                       const std::string& result_var = {},
                                       const std::string& case_bmdl_name = {});
    void emit_decode_choice(const model::ChoiceDef& c, const std::string& result_var);
    void emit_decode_fx(const model::FxBlock& fx, const std::string& result_var);
    void emit_decode_fx_children(const std::vector<model::StructChild>& children,
                                  const std::string& result_var);

    // ========================================================================
    // Expression / Constraint / Wrap — in cpp_structs_expr.cpp
    // ========================================================================

    void emit_constraint_check(const model::Constraint& c, const std::string& member,
                              const std::string& field_name, bool is_signed = true);
    std::string emit_expr_code(const model::Expr& expr, const std::string& result_var);
    std::string emit_field_cast(const std::string& parent_bmdl_name,
                                 const std::string& field_bmdl_name,
                                 const std::string& value_expr);

    // ========================================================================
    // Utility — in cpp_structs_expr.cpp
    // ========================================================================

    std::string resolve_padding_char(const model::Field& f) const;
    std::string qualify_type_if_shadowed(const std::string& field_name,
                                         const std::string& cpp_type) const;
    std::string resolve_field_cpp_type(const std::string& parent_name,
                                        const std::string& field_name) const;
    std::string resolve_child_class_name(const std::string& bmdl_name, const std::string& parent_name);
    std::string get_child_class_name(const std::string& bmdl_name);
    std::string get_variant_alias_name(const std::string& choice_bmdl_name);

    // ========================================================================
    // Outer-scope analysis helpers — in cpp_structs_expr.cpp
    // ========================================================================

    // Collect all FieldRef root names from an expression tree
    static void collect_expr_field_refs(const model::Expr* expr, std::set<std::string>& refs);

    // Collect FieldRef root names from all direct-child expressions
    // (choice switch, length-from, present-when, and field expressions — NOT recursing into case children)
    static void collect_scope_field_refs(const std::vector<model::StructChild>& children,
                                         std::set<std::string>& refs);

    // Collect locally-defined field names in a children list
    static void collect_local_names(const std::vector<model::StructChild>& children,
                                     std::set<std::string>& names);

    // Analyze outer-scope params for a child scope and store in struct_decode_params_
    void analyze_outer_scope(const std::string& child_bmdl_name,
                              const std::vector<model::StructChild>& child_children,
                              const std::vector<model::StructChild>& parent_children);

    // ========================================================================
    // Member data
    // ========================================================================

    EmitContext& ctx_;
    const analyzer::TypeIndex& index_;
    const analyzer::WireSizeInfo& sizes_;
    const std::string& ns_;

    // Set of already-emitted class names to avoid duplicate definitions
    std::unordered_set<std::string> emitted_classes_;
    // Set of already-emitted variant alias names to detect collisions
    std::unordered_set<std::string> emitted_variant_aliases_;
    // Current parent context for resolving child names (resolved C++ class name)
    std::string current_parent_;
    // Current parent BMDL name (for struct_decode_params_ lookup)
    std::string current_bmdl_name_;
    // Set of optional field member names for the current struct being decoded.
    std::unordered_set<std::string> optional_field_names_;
    // Set of ALL local field BMDL names in the current struct being decoded.
    std::unordered_set<std::string> local_field_names_;
    // P1: O(1) enum value name → qualified C++ name lookup
    std::unordered_map<std::string, std::string> enum_value_lookup_;
    // Leaf type BMDL name → computed type_id (for emitting TYPE_ID on leaf structs)
    std::unordered_map<std::string, uint64_t> leaf_type_ids_;
    // Current entry-point session info
    const analyzer::SessionInfo* current_session_ = nullptr;
    // FX nesting depth counter for generating unique variable names
    int fx_depth_ = 0;
    // Bit alignment tracker: cumulative bits mod 8 from struct start.
    int bit_mod8_ = 0;


    // Outer-scope field BMDL name → C++ param variable name for current struct being emitted
    std::unordered_map<std::string, std::string> outer_scope_params_;

    // Struct/case BMDL name → list of outer-scope decode parameters
    struct OuterScopeParam {
        std::string bmdl_name;   // BMDL field name (e.g., "i080", "len")
        std::string cpp_type;    // C++ type (e.g., "Cat253I080", "uint8_t")
        bool pass_by_ref;        // true for struct types, false for primitives
    };
    std::unordered_map<std::string, std::vector<OuterScopeParam>> struct_decode_params_;

    // Auto-length backpatch tracking for current struct encode
    struct AutoLengthInfo {
        int bits;
        model::Endian endian;
        model::ArithModifier modifier;
    };
    std::optional<AutoLengthInfo> pending_auto_length_;

    // Auto-length(field) backpatch tracking — length of a specific target field
    struct AutoLengthFieldRefInfo {
        int bits;
        model::Endian endian;
        model::ArithModifier modifier;
        std::string target_name;  // BMDL name of the target field/array
    };
    std::optional<AutoLengthFieldRefInfo> pending_auto_length_ref_;

    // Inline enum tracking — enum_name → already emitted
    std::set<std::string> emitted_inline_enums_;
};

} // namespace bgen::codegen
