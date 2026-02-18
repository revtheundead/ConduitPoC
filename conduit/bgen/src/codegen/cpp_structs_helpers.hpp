// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Free Helper Functions

#pragma once

#include "emit_context.hpp"
#include "name_utils.hpp"
#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include <string>

namespace bgen::codegen {

// ============================================================================
// Helper types
// ============================================================================

struct FieldTypeInfo {
    std::string cpp_type;      // The C++ type name
    bool is_struct = false;    // True if type is a struct/message
    bool is_enum = false;      // True if type is an enum
    bool is_string = false;
    bool is_bytes = false;
    bool is_float = false;     // A4: True if float type
    bool is_bool = false;      // True if bool type
    int bits = 0;              // Bit width (for inline bits)
    bool is_signed = false;
    // Field-level scale/offset (inline scaled access)
    bool has_field_scale = false;
    double field_scale = 1.0;
    double field_offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian raw_endian = model::Endian::Big;
    model::WireEncoding wire_encoding = model::WireEncoding::Default;
};

struct FieldInfo {
    std::string name;
    std::string cpp_type;
    bool is_optional = false;
    bool is_variant = false;                        // choice/variant fields
    bool is_signed = false;                         // for skipping unsigned min=0 checks
    const model::Constraint* constraint = nullptr;  // A9: for setter validation
    std::optional<std::string> default_value;       // A10: for optional default
    std::optional<int> max_length;                   // G2: for setter length validation
    bool is_auto_managed = false;                    // frame field with auto_expr → deprecated setter
    bool is_enum = false;                            // enum types → pass by value in accessors
};

struct BitmapField {
    std::string name;
    std::string cpp_type;
    int bit = 0;
    bool is_struct = false;
    bool is_choice = false;
    bool is_enum = false;
    bool is_string = false;
    bool is_bytes = false;
    const model::ChoiceDef* choice_def = nullptr;
    int type_bits = 0;         // A6: bit width for primitive bitmap fields
    bool is_signed = false;    // A6
    bool is_float = false;     // A6
    model::Endian endian = model::Endian::Big; // A6
    model::WireEncoding wire_encoding = model::WireEncoding::Default;
    // Field-level scale/offset for bitmap fields with inline scaling
    bool has_field_scale = false;
    double field_scale = 1.0;
    double field_offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian raw_endian = model::Endian::Big;
    // String/bytes field attributes for bitmap fields
    std::optional<int> length;
    std::optional<int> bytes_attr;
    // Source model field for trim/encoding propagation
    const model::Field* source_field = nullptr;
};

// A2: Helper to get prefix type read/write info
struct PrefixTypeInfo {
    int bits = 8;
    model::Endian endian = model::Endian::Big;
};

// ============================================================================
// Free helper function declarations
// ============================================================================

FieldTypeInfo resolve_field_type(const model::Field& f, const analyzer::TypeIndex& index);

std::string endian_str(model::Endian e);

bool field_needs_encoding(const model::Field& f);
std::string field_encoding_enum(const model::Field& f);

void emit_field_trim(EmitContext& ctx, const std::string& var, const model::Field& f);

std::string emit_read_expr(const FieldTypeInfo& fti, model::Endian endian,
                           const std::string& reader = "r",
                           bool byte_aligned = true);

void emit_write_stmt(EmitContext& ctx, const std::string& value, const FieldTypeInfo& fti,
                     model::Endian endian, bool byte_aligned = true);

PrefixTypeInfo resolve_prefix_type(const std::string& type_name, const analyzer::TypeIndex& index);
std::string emit_prefix_read(const PrefixTypeInfo& pti);
void emit_prefix_write(EmitContext& ctx, const PrefixTypeInfo& pti, const std::string& value);
int get_prefix_bytes(const PrefixTypeInfo& pti);

// Generate a C++ expression that applies an ArithModifier to a base expression.
// E.g., apply_arith("payload_size", {Mul, 2, ""}) → "(payload_size * 2)"
std::string apply_arith(const std::string& base_expr, const model::ArithModifier& mod);

// Generate a C++ expression that reverses an ArithModifier (for frame length recovery).
// Add↔Sub, Mul↔Div. Mod has no inverse (should be rejected at validation).
// Only used by session framing layer with literal operands.
std::string reverse_arith(const std::string& base_expr, const model::ArithModifier& mod);

} // namespace bgen::codegen
