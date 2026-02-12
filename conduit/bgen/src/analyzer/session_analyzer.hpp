// SPDX-License-Identifier: MIT
// Bgen - Session Analyzer

#pragma once

#include "../model/ast.hpp"
#include "type_resolver.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bgen::analyzer {

// One level of choice nesting from entry-point to leaf
struct AccessPathEntry {
    std::string choice_field;     // name of the choice field on the parent struct
    std::string variant_type;     // type name of the selected variant case
    // Discriminator for this choice level (switch field/value on the parent struct)
    std::string disc_field;       // switch expression field name (empty if no switch)
    std::string disc_value;       // switch case value
    // Array traversal: when is_array is true, this entry represents an array level
    bool is_array = false;
    std::string array_field;      // name of the array field on the parent struct
    std::string element_type;     // C++ type name of the array element
    // Struct traversal: when is_struct is true, this entry navigates into a struct field
    bool is_struct = false;
    std::string struct_field;     // name of the struct field on the parent
    // For choice entries: length field info from the ChoiceDef's length_from
    std::string length_field;     // field name to set (e.g. "len")
    std::string length_expr;      // the expression subtracted from the field (e.g. "3" for len - 3)
    // true if this field is optional (bitmap bit / present_when / FX)
    bool is_optional = false;
};

// Information about a leaf type reachable from an entry-point
struct LeafTypeInfo {
    std::string name;
    uint64_t type_id = 0;
    // Wrapping info: constraint fields, auto-increment fields
    std::vector<std::pair<std::string, std::string>> constraints;   // field_name -> value
    std::vector<std::string> auto_fields;                           // auto="increment" fields
    std::vector<int> auto_field_bits;                               // per-field bit widths (parallel to auto_fields)
    bool send_only = false;
    bool receive_only = false;
    bool is_batch = false;  // true when this leaf is a case wrapper type (batch dispatch)
    // Path from entry-point to this leaf through choices.
    // Each entry carries its own discriminator (switch field/value for that level).
    std::vector<AccessPathEntry> access_path;
    // Annotations from the source message definition (key -> value)
    std::vector<std::pair<std::string, std::string>> annotations;
};

// A concrete field from an entry-point, available to inner decode methods
struct EntryPointContextField {
    std::string bmdl_name;      // original field name from BMDL
    std::string type_ref;       // BMDL type reference (empty for inline)
    int bits = 0;               // bit width for inline fields
    bool is_signed = false;     // signedness for inline fields
    bool is_enum = false;       // true if this is an enum type
};

// A config field from auto="config(key)" in a frame
struct ConfigField {
    std::string key;          // BMDL key (e.g. "system-id")
    std::string field_name;   // BMDL field name
    std::string type_ref;
    int bits = 0;
    bool is_signed = false;
};

// Session metadata for an entry-point message
struct SessionInfo {
    std::string entry_point_name;
    std::vector<LeafTypeInfo> leaf_types;
    std::vector<uint8_t> sync_pattern;
    size_t min_frame_header_size = 0;
    // Expression for extracting frame length (as string for codegen)
    std::string frame_length_expr;
    // Direct-read info for the frame length field (avoids full Frame::decode)
    size_t frame_length_bit_offset = 0;
    int frame_length_bits = 0;
    bool frame_length_signed = false;
    int frame_length_offset = 0;  // auto="length - 3" → offset=-3
    model::Endian frame_length_endian = model::Endian::Big;
    // Entry-point context: concrete fields available for inner decode methods
    std::vector<EntryPointContextField> context_fields;

    // v2 frame-based session info
    const model::FrameDef* frame = nullptr;
    bool is_frame_based = false;
    bool payload_is_array = false;
    std::string id_field_name;
    std::string length_field_name;
    std::vector<ConfigField> config_fields;
};

// Compile-time 64-bit FNV-1a hash for type IDs
constexpr uint64_t fnv1a_hash(const char* str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Analyze all entry-point messages in the protocol.
std::vector<SessionInfo> analyze_sessions(const model::Protocol& protocol, const TypeIndex& index);

} // namespace bgen::analyzer
