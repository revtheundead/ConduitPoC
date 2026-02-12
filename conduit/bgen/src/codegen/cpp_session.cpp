// SPDX-License-Identifier: MIT
// Bgen - Session Code Generator Implementation

#include "cpp_session.hpp"
#include "emit_context.hpp"
#include "name_utils.hpp"
#include <map>
#include <sstream>

namespace bgen::codegen {

namespace {

std::string to_hex64(uint64_t val) {
    std::ostringstream ss;
    ss << std::hex << val << "ULL";
    return ss.str();
}

// Trie node for grouping shared access path prefixes in decode_frame
struct PathTrieNode {
    std::map<std::string, PathTrieNode> children;
    std::vector<const analyzer::LeafTypeInfo*> leaves;
    analyzer::AccessPathEntry entry;
    size_t depth = 0;
};

std::string path_entry_key(const analyzer::AccessPathEntry& e) {
    if (e.is_array) return "arr:" + e.array_field;
    if (e.is_struct) return "str:" + e.struct_field;
    return "cho:" + e.choice_field + ":" + e.variant_type;
}

void emit_decode_trie(EmitContext& ctx, const PathTrieNode& node,
                      const std::string& current_expr, bool is_root) {
    // Emit terminal leaves at this node
    for (const auto* lt : node.leaves) {
        std::string tid_hex = type_id_literal(lt->type_id);
        ctx.line("{");
        ctx.indent();
        if (lt->send_only) {
            ctx.line("LOG_WARNF(\"Received send-only message type '{}' (type_id=0x{:016x})\""
                     ", \"" + lt->name + "\", " + tid_hex + ");");
        }
        ctx.line("conduit::traits::DecodedMessage dm;");
        ctx.line("dm.type_id = " + tid_hex + ";");
        ctx.line("dm.type_name = \"" + lt->name + "\";");
        if (is_root && current_expr.empty()) {
            ctx.line("dm.payload = *frame;");
        } else {
            ctx.line("dm.payload = " + current_expr + ";");
        }
        ctx.line("messages.push_back(std::move(dm));");
        ctx.dedent();
        ctx.line("}");
    }

    // Emit children
    for (const auto& [key, child] : node.children) {
        const auto& ape = child.entry;
        size_t pi = child.depth;

        if (ape.is_array) {
            std::string arr_acc = to_accessor_name(ape.array_field);
            std::string arr_expr = is_root
                ? "frame->" + arr_acc + "()"
                : current_expr + "." + arr_acc + "()";
            std::string elem_var = "arr_elem_" + std::to_string(pi);
            ctx.line("for (const auto& " + elem_var + " : " + arr_expr + ") {");
            ctx.indent();
            emit_decode_trie(ctx, child, elem_var, false);
            ctx.dedent();
            ctx.line("}");
        } else if (ape.is_struct) {
            std::string struct_acc = to_accessor_name(ape.struct_field);
            std::string next_expr = is_root
                ? "frame->" + struct_acc + "()"
                : current_expr + "." + struct_acc + "()";
            emit_decode_trie(ctx, child, next_expr, false);
        } else {
            // Choice
            std::string accessor = to_accessor_name(ape.choice_field);
            std::string cpp_vt = to_cpp_type_name(ape.variant_type);
            std::string variant_expr = is_root
                ? "frame->" + accessor + "()"
                : current_expr + "." + accessor + "()";
            if (ape.is_optional) {
                std::string has_expr = is_root
                    ? "frame->has_" + accessor + "()"
                    : current_expr + ".has_" + accessor + "()";
                ctx.line("if (" + has_expr + " && std::holds_alternative<" + cpp_vt + ">(" + variant_expr + ")) {");
            } else {
                ctx.line("if (std::holds_alternative<" + cpp_vt + ">(" + variant_expr + ")) {");
            }
            ctx.indent();
            std::string get_expr = "std::get<" + cpp_vt + ">(" + variant_expr + ")";
            emit_decode_trie(ctx, child, get_expr, false);
            ctx.dedent();
            ctx.line("}");
        }
    }
}

// Check if any leaf type has direction constraints (for logger include)
bool has_direction_constraints(const analyzer::SessionInfo& si) {
    for (const auto& lt : si.leaf_types) {
        if (lt.send_only || lt.receive_only) return true;
    }
    return false;
}

void emit_session(EmitContext& ctx, const analyzer::SessionInfo& si, [[maybe_unused]] const std::string& ns) {
    std::string entry = to_cpp_type_name(si.entry_point_name);
    std::string session_class = entry + "Session";
    std::string factory_func = "create_" + to_lower_snake_case(si.entry_point_name) + "_session";

    ctx.line("// Session for entry-point: " + si.entry_point_name);
    ctx.line("class " + session_class + " : public conduit::traits::ISession {");
    ctx.line("public:");
    ctx.indent();

    // decode_frame
    ctx.line("[[nodiscard]] conduit::Result<std::vector<conduit::traits::DecodedMessage>>");
    ctx.line("decode_frame(std::span<const uint8_t> data) override {");
    ctx.indent();
    ctx.line("conduit::io::BitReader r(data);");
    ctx.line("auto frame = " + entry + "::decode(r);");
    ctx.line("if (!frame) return std::unexpected(frame.error());");
    ctx.line("std::vector<conduit::traits::DecodedMessage> messages;");

    // Build trie from leaf access paths to merge shared prefixes
    PathTrieNode decode_root;
    for (const auto& lt : si.leaf_types) {
        if (lt.access_path.empty()) {
            decode_root.leaves.push_back(&lt);
            continue;
        }
        PathTrieNode* node = &decode_root;
        for (size_t pi = 0; pi < lt.access_path.size(); pi++) {
            std::string key = path_entry_key(lt.access_path[pi]);
            auto& child = node->children[key];
            child.entry = lt.access_path[pi];
            child.depth = pi;
            node = &child;
        }
        node->leaves.push_back(&lt);
    }

    emit_decode_trie(ctx, decode_root, "", true);

    ctx.line("return messages;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encode_wrap — delegates to EntryPoint::wrap() overloads
    ctx.line("[[nodiscard]] conduit::Result<std::vector<uint8_t>>");
    ctx.line("encode_wrap(uint64_t type_id, const std::any& payload) override {");
    ctx.indent();
    ctx.line("conduit::io::BitWriter w;");

    bool first_branch = true;
    for (const auto& lt : si.leaf_types) {
        std::string leaf_type = to_cpp_type_name(lt.name);
        std::string tid_hex = type_id_literal(lt.type_id);
        std::string prefix = first_branch ? "if" : "} else if";
        first_branch = false;
        ctx.line(prefix + " (type_id == " + tid_hex + ") {");
        ctx.indent();
        ctx.line("auto* leaf = std::any_cast<" + leaf_type + ">(&payload);");
        ctx.line("if (!leaf) return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument,");
        ctx.line("    \"payload type mismatch for " + lt.name + "\"));");
        ctx.line("auto frame = " + entry + "::wrap(*leaf);");
        // Set auto-increment fields (session-stateful, not in wrap())
        for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
            int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
            uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
            std::string mask = "0x" + to_hex64(mask_val);
            ctx.line("frame.set_" + to_accessor_name(lt.auto_fields[ai]) +
                     "(sequence_counter_++ & " + mask + ");");
        }
        ctx.line("CONDUIT_TRY(frame.encode(w));");
        ctx.dedent();
    }
    if (!first_branch) {
        ctx.line("} else {");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownTypeId,");
        ctx.line("    \"unknown type_id: \" + std::to_string(type_id)));");
        ctx.dedent();
        ctx.line("}");
    }

    ctx.line("return w.finish();");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // sync_pattern
    ctx.line("[[nodiscard]] std::span<const uint8_t> sync_pattern() const override {");
    ctx.indent();
    if (!si.sync_pattern.empty()) {
        ctx.line("static constexpr uint8_t pattern[] = {");
        ctx.indent();
        std::string bytes;
        for (size_t i = 0; i < si.sync_pattern.size(); i++) {
            if (i > 0) bytes += ", ";
            std::ostringstream ss;
            ss << "0x" << std::hex << static_cast<int>(si.sync_pattern[i]);
            bytes += ss.str();
        }
        ctx.line(bytes);
        ctx.dedent();
        ctx.line("};");
        ctx.line("return pattern;");
    } else {
        ctx.line("return {};");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // min_frame_header_size
    ctx.line("[[nodiscard]] size_t min_frame_header_size() const override {");
    ctx.indent();
    ctx.line("return " + std::to_string(si.min_frame_header_size) + ";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // extract_frame_length — reads the length field directly from the header
    // without doing a full Frame::decode (which would fail on partial data).
    ctx.line("[[nodiscard]] size_t extract_frame_length(std::span<const uint8_t> header) const override {");
    ctx.indent();
    if (!si.frame_length_expr.empty() && si.frame_length_bits > 0) {
        ctx.line("if (header.size() < " + std::to_string(si.min_frame_header_size) + ") return 0;");
        ctx.line("conduit::io::BitReader r(header);");
        if (si.frame_length_bit_offset > 0) {
            ctx.line("if (!r.skip_bits(" + std::to_string(si.frame_length_bit_offset) + ")) return 0;");
        }
        std::string endian = (si.frame_length_endian == model::Endian::Big)
            ? "conduit::io::Endian::Big" : "conduit::io::Endian::Little";
        std::string read_call;
        if (si.frame_length_bits <= 8) {
            read_call = "r.read_u8()";
        } else if (si.frame_length_bits <= 16) {
            read_call = "r.read_u16(" + endian + ")";
        } else if (si.frame_length_bits <= 32) {
            read_call = "r.read_u32(" + endian + ")";
        } else {
            read_call = "r.read_u64(" + endian + ")";
        }
        ctx.line("auto val = " + read_call + ";");
        ctx.line("if (!val) return 0;");
        if (si.frame_length_offset != 0) {
            // Reverse the offset applied during encode: if encode wrote (size - 3),
            // we need to add 3 back to get the actual frame length.
            ctx.line("return static_cast<size_t>(static_cast<int64_t>(*val) + (" +
                     std::to_string(-si.frame_length_offset) + "));");
        } else {
            ctx.line("return static_cast<size_t>(*val);");
        }
    } else if (!si.frame_length_expr.empty()) {
        // Fallback: full decode (for cases where bit info isn't available)
        ctx.line("if (header.size() < " + std::to_string(si.min_frame_header_size) + ") return 0;");
        ctx.line("conduit::io::BitReader r(header);");
        ctx.line("auto frame = " + entry + "::decode(r);");
        ctx.line("if (!frame) return 0;");
        ctx.line("return static_cast<size_t>(frame->" + to_accessor_name(si.frame_length_expr) + "());");
    } else {
        ctx.line("return header.size();");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // leaf_type_ids
    ctx.line("[[nodiscard]] std::span<const uint64_t> leaf_type_ids() const override {");
    ctx.indent();
    if (!si.leaf_types.empty()) {
        ctx.line("static constexpr uint64_t ids[] = {");
        ctx.indent();
        for (size_t i = 0; i < si.leaf_types.size(); i++) {
            const auto& lt = si.leaf_types[i];
            std::string comma = (i + 1 < si.leaf_types.size()) ? "," : "";
            ctx.line(type_id_literal(lt.type_id) + comma + " // " + lt.name);
        }
        ctx.dedent();
        ctx.line("};");
        ctx.line("return ids;");
    } else {
        ctx.line("return {};");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // type_name
    ctx.line("[[nodiscard]] std::string_view type_name(uint64_t type_id) const override {");
    ctx.indent();
    ctx.line("switch (type_id) {");
    ctx.indent();
    for (const auto& lt : si.leaf_types) {
        ctx.line("case " + type_id_literal(lt.type_id) + ": return \"" + lt.name + "\";");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line("return \"unknown\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // is_receive_only — only generated when there are receive-only types
    {
        bool has_recv_only = false;
        for (const auto& lt : si.leaf_types) {
            if (lt.receive_only) { has_recv_only = true; break; }
        }
        if (has_recv_only) {
            ctx.line("[[nodiscard]] bool is_receive_only(uint64_t type_id) const override {");
            ctx.indent();
            ctx.line("switch (type_id) {");
            ctx.indent();
            for (const auto& lt : si.leaf_types) {
                if (lt.receive_only) {
                    ctx.line("case " + type_id_literal(lt.type_id) + ": return true; // " + lt.name);
                }
            }
            ctx.dedent();
            ctx.line("}");
            ctx.line("return false;");
            ctx.dedent();
            ctx.line("}");
            ctx.line();
        }
    }

    // Check if any leaf type has auto-increment fields
    bool has_auto_fields = false;
    for (const auto& lt : si.leaf_types) {
        if (!lt.auto_fields.empty()) {
            has_auto_fields = true;
            break;
        }
    }

    // reset
    ctx.line("void reset() override {");
    ctx.indent();
    if (has_auto_fields) {
        ctx.line("sequence_counter_ = 0;");
    }
    ctx.dedent();
    ctx.line("}");

    // G4: Determine counter type from max auto field bit width across all leaves
    std::string counter_type;
    if (has_auto_fields) {
        int auto_bits = 8;
        for (const auto& lt : si.leaf_types) {
            for (int b : lt.auto_field_bits) {
                if (b > auto_bits) auto_bits = b;
            }
        }
        counter_type = storage_type_for_bits(auto_bits, false);
        ctx.line(counter_type + " sequence_counter() const { return sequence_counter_; }");
    }
    ctx.dedent();
    if (has_auto_fields) {
        ctx.line("private:");
        ctx.indent();
        ctx.line(counter_type + " sequence_counter_ = 0;");
        ctx.dedent();
    }
    ctx.line("};");
    ctx.line();

    // Factory function
    ctx.line("inline std::unique_ptr<conduit::traits::ISession> " + factory_func + "() {");
    ctx.indent();
    ctx.line("return std::make_unique<" + session_class + ">();");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
}

// ============================================================================
// Frame-based session emission (v2)
// ============================================================================

void emit_frame_session(EmitContext& ctx, const analyzer::SessionInfo& si, [[maybe_unused]] const std::string& ns) {
    std::string frame_class = to_cpp_type_name(si.frame->name);
    std::string session_class = frame_class + "Session";
    std::string factory_func = "create_" + to_lower_snake_case(si.frame->name) + "_session";

    ctx.line("// Session for frame: " + si.frame->name);

    // Config struct if there are config fields
    bool has_config = !si.config_fields.empty();

    ctx.line("class " + session_class + " : public conduit::traits::ISession {");
    ctx.line("public:");
    ctx.indent();

    // Constructor
    if (has_config) {
        // Config struct defined in protocol.hpp; forward-reference here via ProtocolDescriptor::Config
        ctx.line("struct Config {");
        ctx.indent();
        for (const auto& cf : si.config_fields) {
            std::string cpp_type;
            if (!cf.type_ref.empty()) {
                cpp_type = to_cpp_type_name(cf.type_ref);
            } else if (cf.bits > 0) {
                cpp_type = storage_type_for_bits(cf.bits, cf.is_signed);
            } else {
                cpp_type = "uint8_t";
            }
            ctx.line(cpp_type + " " + to_accessor_name(cf.key) + "{};");
        }
        ctx.dedent();
        ctx.line("};");
        ctx.line();
        ctx.line("explicit " + session_class + "(const Config& config) : config_(config) {}");
        ctx.line();
    }

    // decode_frame
    ctx.line("[[nodiscard]] conduit::Result<std::vector<conduit::traits::DecodedMessage>>");
    ctx.line("decode_frame(std::span<const uint8_t> data) override {");
    ctx.indent();
    ctx.line("auto frame = " + frame_class + "::decode_bytes(data);");
    ctx.line("if (!frame) return std::unexpected(frame.error());");
    ctx.line("std::vector<conduit::traits::DecodedMessage> messages;");

    if (si.payload_is_array) {
        ctx.line("for (const auto& item : frame->payload()) {");
        ctx.indent();
        ctx.line("std::visit([&messages](const auto& msg) {");
        ctx.indent();
        ctx.line("conduit::traits::DecodedMessage dm;");
        ctx.line("dm.type_id = std::decay_t<decltype(msg)>::TYPE_ID;");
        ctx.line("dm.type_name = std::decay_t<decltype(msg)>::TYPE_NAME;");
        ctx.line("dm.payload = msg;");
        ctx.line("messages.push_back(std::move(dm));");
        ctx.dedent();
        ctx.line("}, item);");
        ctx.dedent();
        ctx.line("}");
    } else {
        ctx.line("std::visit([&messages](const auto& msg) {");
        ctx.indent();
        ctx.line("conduit::traits::DecodedMessage dm;");
        ctx.line("dm.type_id = std::decay_t<decltype(msg)>::TYPE_ID;");
        ctx.line("dm.type_name = std::decay_t<decltype(msg)>::TYPE_NAME;");
        ctx.line("dm.payload = msg;");
        ctx.line("messages.push_back(std::move(dm));");
        ctx.dedent();
        ctx.line("}, frame->payload());");
    }

    // Warn when receiving send-only message types
    bool has_send_only = false;
    for (const auto& lt : si.leaf_types) {
        if (lt.send_only) { has_send_only = true; break; }
    }
    if (has_send_only) {
        ctx.line("for (const auto& dm : messages) {");
        ctx.indent();
        for (const auto& lt : si.leaf_types) {
            if (lt.send_only) {
                std::string tid_hex = type_id_literal(lt.type_id);
                ctx.line("if (dm.type_id == " + tid_hex + ") {");
                ctx.indent();
                ctx.line("LOG_WARNF(\"Received send-only message type '{}' (type_id=0x{:016x})\""
                         ", \"" + lt.name + "\", " + tid_hex + ");");
                ctx.dedent();
                ctx.line("}");
            }
        }
        ctx.dedent();
        ctx.line("}");
    }

    ctx.line("return messages;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encode_wrap
    ctx.line("[[nodiscard]] conduit::Result<std::vector<uint8_t>>");
    ctx.line("encode_wrap(uint64_t type_id, const std::any& payload) override {");
    ctx.indent();

    bool first_branch = true;
    for (const auto& lt : si.leaf_types) {
        std::string leaf_type = to_cpp_type_name(lt.name);
        std::string tid_hex = type_id_literal(lt.type_id);
        std::string prefix = first_branch ? "if" : "} else if";
        first_branch = false;
        ctx.line(prefix + " (type_id == " + tid_hex + ") {");
        ctx.indent();
        ctx.line("auto* msg = std::any_cast<" + leaf_type + ">(&payload);");
        ctx.line("if (!msg) return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument,");
        ctx.line("    \"payload type mismatch for " + lt.name + "\"));");
        ctx.line("auto frame = " + frame_class + "::wrap(*msg);");
        // Set config fields on the frame
        for (const auto& cf : si.config_fields) {
            ctx.line("frame.set_" + to_accessor_name(cf.field_name) + "(config_." + to_accessor_name(cf.key) + ");");
        }
        // Set auto-increment fields
        for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
            int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
            uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
            std::string mask = "0x" + to_hex64(mask_val);
            ctx.line("frame.set_" + to_accessor_name(lt.auto_fields[ai]) +
                     "(sequence_counter_++ & " + mask + ");");
        }
        ctx.line("return frame.encode_bytes();");
        ctx.dedent();
    }
    if (!first_branch) {
        ctx.line("} else {");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownTypeId,");
        ctx.line("    \"unknown type_id: \" + std::to_string(type_id)));");
        ctx.dedent();
        ctx.line("}");
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // sync_pattern
    ctx.line("[[nodiscard]] std::span<const uint8_t> sync_pattern() const override {");
    ctx.indent();
    if (!si.sync_pattern.empty()) {
        ctx.line("static constexpr uint8_t pattern[] = {");
        ctx.indent();
        std::string bytes;
        for (size_t i = 0; i < si.sync_pattern.size(); i++) {
            if (i > 0) bytes += ", ";
            std::ostringstream ss;
            ss << "0x" << std::hex << static_cast<int>(si.sync_pattern[i]);
            bytes += ss.str();
        }
        ctx.line(bytes);
        ctx.dedent();
        ctx.line("};");
        ctx.line("return pattern;");
    } else {
        ctx.line("return {};");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // min_frame_header_size
    ctx.line("[[nodiscard]] size_t min_frame_header_size() const override {");
    ctx.indent();
    ctx.line("return " + std::to_string(si.min_frame_header_size) + ";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // extract_frame_length
    ctx.line("[[nodiscard]] size_t extract_frame_length(std::span<const uint8_t> header) const override {");
    ctx.indent();
    if (si.frame_length_bits > 0) {
        ctx.line("if (header.size() < " + std::to_string(si.min_frame_header_size) + ") return 0;");
        ctx.line("conduit::io::BitReader r(header);");
        if (si.frame_length_bit_offset > 0) {
            ctx.line("if (!r.skip_bits(" + std::to_string(si.frame_length_bit_offset) + ")) return 0;");
        }
        std::string endian = (si.frame_length_endian == model::Endian::Big)
            ? "conduit::io::Endian::Big" : "conduit::io::Endian::Little";
        std::string read_call;
        if (si.frame_length_bits <= 8) {
            read_call = "r.read_u8()";
        } else if (si.frame_length_bits <= 16) {
            read_call = "r.read_u16(" + endian + ")";
        } else if (si.frame_length_bits <= 32) {
            read_call = "r.read_u32(" + endian + ")";
        } else {
            read_call = "r.read_u64(" + endian + ")";
        }
        ctx.line("auto val = " + read_call + ";");
        ctx.line("if (!val) return 0;");
        if (si.frame_length_offset != 0) {
            ctx.line("return static_cast<size_t>(static_cast<int64_t>(*val) + (" +
                     std::to_string(-si.frame_length_offset) + "));");
        } else {
            ctx.line("return static_cast<size_t>(*val);");
        }
    } else {
        ctx.line("return header.size();");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // leaf_type_ids
    ctx.line("[[nodiscard]] std::span<const uint64_t> leaf_type_ids() const override {");
    ctx.indent();
    if (!si.leaf_types.empty()) {
        ctx.line("static constexpr uint64_t ids[] = {");
        ctx.indent();
        for (size_t i = 0; i < si.leaf_types.size(); i++) {
            const auto& lt = si.leaf_types[i];
            std::string comma = (i + 1 < si.leaf_types.size()) ? "," : "";
            ctx.line(type_id_literal(lt.type_id) + comma + " // " + lt.name);
        }
        ctx.dedent();
        ctx.line("};");
        ctx.line("return ids;");
    } else {
        ctx.line("return {};");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // type_name
    ctx.line("[[nodiscard]] std::string_view type_name(uint64_t type_id) const override {");
    ctx.indent();
    ctx.line("switch (type_id) {");
    ctx.indent();
    for (const auto& lt : si.leaf_types) {
        ctx.line("case " + type_id_literal(lt.type_id) + ": return \"" + lt.name + "\";");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line("return \"unknown\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // is_receive_only — only generated when there are receive-only types
    {
        bool has_recv_only = false;
        for (const auto& lt : si.leaf_types) {
            if (lt.receive_only) { has_recv_only = true; break; }
        }
        if (has_recv_only) {
            ctx.line("[[nodiscard]] bool is_receive_only(uint64_t type_id) const override {");
            ctx.indent();
            ctx.line("switch (type_id) {");
            ctx.indent();
            for (const auto& lt : si.leaf_types) {
                if (lt.receive_only) {
                    ctx.line("case " + type_id_literal(lt.type_id) + ": return true; // " + lt.name);
                }
            }
            ctx.dedent();
            ctx.line("}");
            ctx.line("return false;");
            ctx.dedent();
            ctx.line("}");
            ctx.line();
        }
    }

    // Check if any leaf type has auto-increment fields
    bool has_auto_fields = false;
    for (const auto& lt : si.leaf_types) {
        if (!lt.auto_fields.empty()) {
            has_auto_fields = true;
            break;
        }
    }

    // reset
    ctx.line("void reset() override {");
    ctx.indent();
    if (has_auto_fields) {
        ctx.line("sequence_counter_ = 0;");
    }
    ctx.dedent();
    ctx.line("}");

    // Private section
    std::string counter_type;
    if (has_auto_fields) {
        int auto_bits = 8;
        for (const auto& lt : si.leaf_types) {
            for (int b : lt.auto_field_bits) {
                if (b > auto_bits) auto_bits = b;
            }
        }
        counter_type = storage_type_for_bits(auto_bits, false);
        ctx.line(counter_type + " sequence_counter() const { return sequence_counter_; }");
    }

    ctx.dedent();
    ctx.line("private:");
    ctx.indent();
    if (has_auto_fields) {
        ctx.line(counter_type + " sequence_counter_ = 0;");
    }
    if (has_config) {
        ctx.line("Config config_;");
    }
    ctx.dedent();
    ctx.line("};");
    ctx.line();

    // Factory function
    if (has_config) {
        ctx.line("inline std::unique_ptr<conduit::traits::ISession> " + factory_func + "(const " + session_class + "::Config& config) {");
        ctx.indent();
        ctx.line("return std::make_unique<" + session_class + ">(config);");
        ctx.dedent();
        ctx.line("}");
    } else {
        ctx.line("inline std::unique_ptr<conduit::traits::ISession> " + factory_func + "() {");
        ctx.indent();
        ctx.line("return std::make_unique<" + session_class + ">();");
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line();
}

} // anonymous namespace

std::string generate_sessions([[maybe_unused]] const model::Protocol& protocol,
                              [[maybe_unused]] const analyzer::TypeIndex& index,
                              const std::vector<analyzer::SessionInfo>& sessions,
                              const std::string& ns) {
    EmitContext ctx;

    // Check if any session has direction-constrained types (for logger include)
    bool needs_logger = false;
    for (const auto& si : sessions) {
        if (has_direction_constraints(si)) {
            needs_logger = true;
            break;
        }
    }

    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("#pragma once");
    ctx.line();
    ctx.line("#include \"messages.hpp\"");
    ctx.line("#include <conduit/traits/session_traits.hpp>");
    if (needs_logger) {
        ctx.line("#include <conduit/logging/logger.hpp>");
    }
    ctx.line("#include <any>");
    ctx.line("#include <memory>");
    ctx.line("#include <span>");
    ctx.line("#include <string_view>");
    ctx.line("#include <vector>");
    ctx.line();
    ctx.line("namespace " + ns + " {");
    ctx.line();

    for (const auto& si : sessions) {
        if (si.is_frame_based) {
            emit_frame_session(ctx, si, ns);
        } else {
            emit_session(ctx, si, ns);
        }
    }

    ctx.line("} // namespace " + ns);
    ctx.line();

    return ctx.str();
}

} // namespace bgen::codegen
