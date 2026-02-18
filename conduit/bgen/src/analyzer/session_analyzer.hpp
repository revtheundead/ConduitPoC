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

// A config field from auto="config(key)"
struct ConfigField {
    std::string key;          // BMDL key (e.g. "system-id")
    std::string field_name;   // BMDL field name
    std::string type_ref;
    int bits = 0;
    bool is_signed = false;
};

// Information about a leaf type reachable from an entry-point/frame
struct LeafTypeInfo {
    std::string name;
    uint64_t type_id = 0;
    // Wrapping info: constraint fields, auto-increment fields
    std::vector<std::pair<std::string, std::string>> constraints;   // field_name -> value
    std::vector<std::string> auto_fields;                           // auto="increment" fields
    std::vector<int> auto_field_bits;                               // per-field bit widths (parallel to auto_fields)
    std::vector<std::string> timestamp_fields;                      // auto="timestamp" fields
    std::vector<int> timestamp_field_bits;                          // per-field bit widths (parallel to timestamp_fields)
    std::vector<ConfigField> config_fields;                         // auto="config" fields in this message
    bool send_only = false;
    bool receive_only = false;
    // Annotations from the source message definition (key -> value)
    std::vector<std::pair<std::string, std::string>> annotations;
};

// Session metadata for an entry-point message
struct SessionInfo {
    std::string session_name;
    std::vector<LeafTypeInfo> leaf_types;
    std::vector<uint8_t> sync_pattern;
    size_t min_frame_header_size = 0;
    // Expression for extracting frame length (as string for codegen)
    std::string frame_length_expr;
    // Direct-read info for the frame length field (avoids full Frame::decode)
    size_t frame_length_bit_offset = 0;
    int frame_length_bits = 0;
    bool frame_length_signed = false;
    model::ArithModifier frame_length_modifier;  // auto="length * 2", "length - 3", etc.
    model::Endian frame_length_endian = model::Endian::Big;

    // v2 frame-based session info
    const model::FrameDef* frame = nullptr;
    bool is_frame_based = false;
    bool payload_is_array = false;
    std::string id_field_name;
    std::string length_field_name;
    std::string frame_length_field_ref;  // empty = total frame, "payload" = payload only
    std::string count_field_name;
    const model::Expr* payload_length_from = nullptr;  // <payload length-from="expr"/>
    size_t frame_footer_size = 0;
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
