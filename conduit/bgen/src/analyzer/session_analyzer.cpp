// SPDX-License-Identifier: MIT
// Bgen - Session Analyzer Implementation

#include "session_analyzer.hpp"
#include "../logger.hpp"
#include <cctype>
#include <charconv>
#include <cstdio>
#include <system_error>
#include <unordered_map>

namespace bgen::analyzer {

namespace {

class SessionAnalyzerImpl {
public:
    SessionAnalyzerImpl(const model::Protocol& proto, const TypeIndex& index)
        : proto_(proto), index_(index) {}

    std::vector<SessionInfo> analyze() {
        std::vector<SessionInfo> sessions;

        // v2 frame-based path
        if (!proto_.frames.empty()) {
            for (const auto& frame : proto_.frames) {
                SessionInfo si;
                si.entry_point_name = frame.name;
                si.is_frame_based = true;
                si.frame = &frame;
                si.payload_is_array = frame.payload.is_array;

                // Scan header fields for auto expressions
                size_t header_bit_offset = 0;
                for (const auto& child : frame.header_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        int field_bits = resolve_field_bits(*f);

                        if (f->auto_expr) {
                            switch (f->auto_expr->kind) {
                                case model::AutoKind::Id:
                                    si.id_field_name = f->name;
                                    break;
                                case model::AutoKind::Length:
                                    si.length_field_name = f->name;
                                    si.frame_length_expr = f->name;
                                    si.frame_length_bit_offset = header_bit_offset;
                                    si.frame_length_bits = field_bits;
                                    // Resolve signedness: field attribute takes priority,
                                    // otherwise check through TypeDef base type.
                                    si.frame_length_signed = f->is_signed;
                                    if (!si.frame_length_signed && !f->type_ref.empty()) {
                                        auto resolved = index_.find(f->type_ref);
                                        if (resolved) {
                                            std::visit([&si](const auto* def) {
                                                using DT = std::decay_t<decltype(*def)>;
                                                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                                    if (def->base == model::PrimitiveBase::Int) {
                                                        si.frame_length_signed = true;
                                                    }
                                                }
                                            }, *resolved);
                                        }
                                    }
                                    si.frame_length_offset = f->auto_expr->offset;
                                    si.frame_length_endian = f->endian;
                                    break;
                                case model::AutoKind::Config: {
                                    ConfigField cf;
                                    cf.key = f->auto_expr->key;
                                    cf.field_name = f->name;
                                    cf.type_ref = f->type_ref;
                                    cf.bits = field_bits;
                                    cf.is_signed = f->is_signed;
                                    si.config_fields.push_back(std::move(cf));
                                    break;
                                }
                                default:
                                    break;
                            }
                        }

                        // Check for sync pattern on constrained fields
                        if (f->constraint && f->constraint->equals) {
                            const auto& eq = *f->constraint->equals;
                            auto it = index_.constants.find(eq);
                            if (it != index_.constants.end()) {
                                parse_sync_bytes(it->second->value, it->second->type_ref, f->endian, si);
                            } else {
                                parse_sync_bytes(eq, f->type_ref, f->endian, si);
                            }
                        }

                        header_bit_offset += static_cast<size_t>(field_bits);
                    } else if (auto* r = std::get_if<model::Reserved>(&child)) {
                        header_bit_offset += static_cast<size_t>(r->bits);
                    } else if (auto* al = std::get_if<model::Align>(&child)) {
                        size_t align_bits = static_cast<size_t>(al->to) * 8;
                        if (align_bits > 0) {
                            header_bit_offset = ((header_bit_offset + align_bits - 1) / align_bits) * align_bits;
                        }
                    } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
                        if (!sd->bit && !sd->present_when) {
                            header_bit_offset += compute_fixed_size_bits(sd->children);
                        }
                    }
                }
                si.min_frame_header_size = (header_bit_offset + 7) / 8;

                // Create leaf types from messages
                for (const auto& msg : proto_.messages) {
                    LeafTypeInfo leaf;
                    leaf.name = msg.name;
                    leaf.type_id = fnv1a_hash(msg.name.c_str());
                    leaf.send_only = (msg.direction == model::Direction::Send);
                    leaf.receive_only = (msg.direction == model::Direction::Receive);
                    // Flat dispatch: no access path

                    // Store id as constraint for dispatch
                    if (!msg.id.empty() && !si.id_field_name.empty()) {
                        leaf.constraints.push_back({si.id_field_name, msg.id});
                    }

                    // Copy annotations
                    for (const auto& ann : msg.annotations) {
                        leaf.annotations.push_back({ann.name, ann.value});
                    }

                    // Collect auto-increment fields from message children
                    find_auto_fields(msg.children, leaf);

                    si.leaf_types.push_back(std::move(leaf));
                }

                if (!check_type_id_collisions(si)) {
                    Logger::error("Skipping session '" + si.entry_point_name +
                                  "' due to type_id hash collisions");
                    continue;
                }

                sessions.push_back(std::move(si));
            }
            return sessions;
        }

        // v1 entry-point-based path
        for (const auto& msg : proto_.messages) {
            if (!msg.is_entry_point) continue;

            SessionInfo si;
            si.entry_point_name = msg.name;

            // Walk the message tree to discover leaf types
            discover_leaves(msg.children, si);

            // Collect entry-point-level auto and constraint fields for all leaves
            find_entry_point_fields(msg.children, si);

            // Look for sync pattern (constraint equals on first field)
            find_sync_pattern(msg.children, si);

            // Compute min frame header size
            compute_min_header(msg.children, si);

            // D2: Compute frame length expression
            compute_frame_length_expr(msg.children, si);

            // Collect context fields for inner decode methods
            collect_context_fields(msg.children, si);

            // Check for type_id hash collisions — skip session if collisions found
            if (!check_type_id_collisions(si)) {
                Logger::error("Skipping session '" + si.entry_point_name +
                              "' due to type_id hash collisions");
                continue;
            }

            sessions.push_back(std::move(si));
        }

        return sessions;
    }

private:
    // Discover leaf types by walking the message tree.
    // path_ctx accumulates AccessPathEntry elements as we descend through choices.
    void discover_leaves(const std::vector<model::StructChild>& children,
                        SessionInfo& si,
                        std::vector<AccessPathEntry> path_ctx = {}) {
        for (const auto& child : children) {
            std::visit([this, &si, &path_ctx](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    // Extract switch expression field name for discriminator tracking
                    std::string switch_str;
                    if (c.switch_expr) {
                        if (c.switch_expr->op == model::ExprOp::FieldRef) {
                            switch_str = c.switch_expr->name;
                        }
                    }

                    // Capture length_from info for this choice level
                    std::string choice_length_field;
                    std::string choice_length_expr;
                    if (c.length_from) {
                        if (c.length_from->op == model::ExprOp::FieldRef) {
                            choice_length_field = c.length_from->name;
                        } else if (c.length_from->op == model::ExprOp::Sub &&
                                   c.length_from->left && c.length_from->left->op == model::ExprOp::FieldRef &&
                                   c.length_from->right && c.length_from->right->op == model::ExprOp::NumberLit) {
                            choice_length_field = c.length_from->left->name;
                            choice_length_expr = std::to_string(c.length_from->right->number_value);
                        }
                    }

                    for (const auto& cs : c.cases) {
                        // Build access path entry for this choice level
                        AccessPathEntry entry;
                        entry.choice_field = c.name;
                        entry.variant_type = !cs.type_ref.empty() ? cs.type_ref : cs.name;
                        if (!switch_str.empty() && cs.value) {
                            entry.disc_field = switch_str;
                            entry.disc_value = *cs.value;
                        } else if (!switch_str.empty() && cs.range) {
                            // For range-based cases, use the min value for wrap()
                            auto dot_pos = cs.range->find("..");
                            if (dot_pos != std::string::npos) {
                                entry.disc_field = switch_str;
                                entry.disc_value = cs.range->substr(0, dot_pos);
                            }
                        }
                        entry.length_field = choice_length_field;
                        entry.length_expr = choice_length_expr;
                        if (c.bit.has_value() || c.present_when != nullptr) {
                            entry.is_optional = true;
                        }

                        auto case_path = path_ctx;
                        case_path.push_back(entry);

                        if (!cs.type_ref.empty()) {
                            // Follow to the referenced type
                            auto resolved = index_.find(cs.type_ref);
                            if (resolved) {
                                std::visit([this, &si, &cs, &case_path](const auto* def) {
                                    using DT = std::decay_t<decltype(*def)>;
                                    if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                        add_leaf(*def, cs.direction, si, case_path);
                                    } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                                        add_leaf_struct(*def, cs.direction, si, case_path);
                                    }
                                }, *resolved);
                            } else {
                                // Type not in index — add as leaf directly
                                LeafTypeInfo leaf;
                                leaf.name = cs.type_ref;
                                leaf.type_id = fnv1a_hash(cs.type_ref.c_str());
                                leaf.send_only = (cs.direction == model::Direction::Send);
                                leaf.receive_only = (cs.direction == model::Direction::Receive);
                                leaf.access_path = case_path;
                                si.leaf_types.push_back(std::move(leaf));
                            }
                        } else {
                            // Inline case — check if any array child uses batch dispatch
                            bool has_batch_array = false;
                            for (const auto& ch : cs.children) {
                                if (auto* ad = std::get_if<model::ArrayDef>(&ch)) {
                                    // Default is Batch; explicit PerRecord overrides
                                    auto eff = ad->dispatch.value_or(model::Dispatch::Batch);
                                    if (eff == model::Dispatch::Batch) { has_batch_array = true; break; }
                                }
                            }

                            if (has_batch_array) {
                                // Batch: case wrapper type IS the leaf
                                LeafTypeInfo leaf;
                                leaf.name = cs.name;
                                leaf.type_id = fnv1a_hash(cs.name.c_str());
                                leaf.send_only = (cs.direction == model::Direction::Send);
                                leaf.receive_only = (cs.direction == model::Direction::Receive);
                                leaf.access_path = case_path;
                                leaf.is_batch = true;
                                for (const auto& ann : cs.annotations) {
                                    leaf.annotations.emplace_back(ann.name, ann.value);
                                }
                                si.leaf_types.push_back(std::move(leaf));
                            } else {
                                // Per-record: existing behavior — recurse into children
                                discover_leaves(cs.children, si, case_path);
                            }
                        }
                    }
                    if (c.otherwise) {
                        if (!c.otherwise->type_ref.empty()) {
                            AccessPathEntry entry;
                            entry.choice_field = c.name;
                            entry.variant_type = c.otherwise->type_ref;
                            entry.length_field = choice_length_field;
                            entry.length_expr = choice_length_expr;
                            if (c.bit.has_value() || c.present_when != nullptr) {
                                entry.is_optional = true;
                            }
                            // No discriminator for otherwise case
                            auto otherwise_path = path_ctx;
                            otherwise_path.push_back(entry);
                            auto resolved = index_.find(c.otherwise->type_ref);
                            if (resolved) {
                                std::visit([this, &si, &otherwise_path](const auto* def) {
                                    using DT = std::decay_t<decltype(*def)>;
                                    if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                        add_leaf(*def, model::Direction::Both, si, otherwise_path);
                                    } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                                        add_leaf_struct(*def, model::Direction::Both, si, otherwise_path);
                                    }
                                }, *resolved);
                            } else {
                                LeafTypeInfo leaf;
                                leaf.name = c.otherwise->type_ref;
                                leaf.type_id = fnv1a_hash(c.otherwise->type_ref.c_str());
                                leaf.access_path = otherwise_path;
                                si.leaf_types.push_back(std::move(leaf));
                            }
                        } else if (!c.otherwise->children.empty()) {
                            auto otherwise_path = path_ctx;
                            AccessPathEntry entry;
                            entry.choice_field = c.name;
                            entry.variant_type = c.otherwise->name.empty() ? "otherwise" : c.otherwise->name;
                            entry.length_field = choice_length_field;
                            entry.length_expr = choice_length_expr;
                            if (c.bit.has_value() || c.present_when != nullptr) {
                                entry.is_optional = true;
                            }
                            otherwise_path.push_back(entry);
                            discover_leaves(c.otherwise->children, si, otherwise_path);
                        }
                    }
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    // Add an array marker to the access path
                    AccessPathEntry arr_entry;
                    arr_entry.is_array = true;
                    arr_entry.array_field = c.name;
                    arr_entry.element_type = !c.type_ref.empty() ? c.type_ref : (c.name + "Element");
                    if (c.bit.has_value() || c.present_when != nullptr) {
                        arr_entry.is_optional = true;
                    }
                    auto array_path = path_ctx;
                    array_path.push_back(arr_entry);

                    if (!c.type_ref.empty()) {
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &si, &array_path](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                    add_leaf(*def, model::Direction::Both, si, array_path);
                                } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                                    add_leaf_struct(*def, model::Direction::Both, si, array_path);
                                }
                            }, *resolved);
                        }
                    } else {
                        discover_leaves(c.children, si, array_path);
                    }
                } else if constexpr (std::is_same_v<T, model::Field>) {
                    if (!c.type_ref.empty()) {
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &si, &path_ctx](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                    discover_leaves(def->children, si, path_ctx);
                                } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                                    discover_leaves(def->children, si, path_ctx);
                                }
                            }, *resolved);
                        }
                    }
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    // Check if this struct contains dispatch children (choice/array).
                    // If so, add a struct traversal entry to the access path so that
                    // codegen can navigate through the struct (e.g. via mutable_items()).
                    bool has_dispatch = false;
                    for (const auto& sc : c.children) {
                        if (std::holds_alternative<model::ChoiceDef>(sc) ||
                            std::holds_alternative<model::ArrayDef>(sc)) {
                            has_dispatch = true;
                            break;
                        }
                    }
                    if (has_dispatch) {
                        AccessPathEntry struct_entry;
                        struct_entry.is_struct = true;
                        struct_entry.struct_field = c.name;
                        if (c.bit.has_value() || c.present_when != nullptr) {
                            struct_entry.is_optional = true;
                        }
                        auto struct_path = path_ctx;
                        struct_path.push_back(struct_entry);
                        discover_leaves(c.children, si, struct_path);
                    } else {
                        discover_leaves(c.children, si, path_ctx);
                    }
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    // FX extension blocks may contain choices or arrays — recurse
                    discover_leaves(c.children, si, path_ctx);
                }
            }, child);
        }
    }

    void add_leaf(const model::MessageDef& msg, model::Direction dir, SessionInfo& si,
                  const std::vector<AccessPathEntry>& path_ctx = {}) {
        // Check if this message has choices (not a leaf)
        bool has_dispatch = false;
        for (const auto& child : msg.children) {
            if (std::holds_alternative<model::ChoiceDef>(child) ||
                std::holds_alternative<model::ArrayDef>(child)) {
                has_dispatch = true;
                break;
            }
        }

        if (has_dispatch) {
            auto before = si.leaf_types.size();
            discover_leaves(msg.children, si, path_ctx);
            if (si.leaf_types.size() == before) {
                // No deeper leaves found — this message IS the leaf
                LeafTypeInfo leaf;
                leaf.name = msg.name;
                leaf.type_id = fnv1a_hash(msg.name.c_str());
                leaf.send_only = (dir == model::Direction::Send);
                leaf.receive_only = (dir == model::Direction::Receive);
                leaf.access_path = path_ctx;
                for (const auto& ann : msg.annotations) {
                    leaf.annotations.push_back({ann.name, ann.value});
                }
                find_auto_fields(msg.children, leaf);
                find_constraint_fields(msg.children, leaf);
                si.leaf_types.push_back(std::move(leaf));
            }
        } else {
            LeafTypeInfo leaf;
            leaf.name = msg.name;
            leaf.type_id = fnv1a_hash(msg.name.c_str());
            leaf.send_only = (dir == model::Direction::Send);
            leaf.receive_only = (dir == model::Direction::Receive);
            leaf.access_path = path_ctx;
            // Copy annotations from message definition
            for (const auto& ann : msg.annotations) {
                leaf.annotations.push_back({ann.name, ann.value});
            }
            find_auto_fields(msg.children, leaf);
            // Record constraint fields from message children
            find_constraint_fields(msg.children, leaf);
            si.leaf_types.push_back(std::move(leaf));
        }
    }

    void add_leaf_struct(const model::StructDef& sd, model::Direction dir, SessionInfo& si,
                         const std::vector<AccessPathEntry>& path_ctx = {}) {
        // Check if this struct has choices (not a leaf — recurse)
        bool has_dispatch = false;
        for (const auto& child : sd.children) {
            if (std::holds_alternative<model::ChoiceDef>(child) ||
                std::holds_alternative<model::ArrayDef>(child)) {
                has_dispatch = true;
                break;
            }
        }

        if (has_dispatch) {
            auto before = si.leaf_types.size();
            discover_leaves(sd.children, si, path_ctx);
            if (si.leaf_types.size() == before) {
                // No deeper leaves found — this struct IS the leaf
                LeafTypeInfo leaf;
                leaf.name = sd.name;
                leaf.type_id = fnv1a_hash(sd.name.c_str());
                leaf.send_only = (dir == model::Direction::Send);
                leaf.receive_only = (dir == model::Direction::Receive);
                leaf.access_path = path_ctx;
                for (const auto& ann : sd.annotations) {
                    leaf.annotations.push_back({ann.name, ann.value});
                }
                find_auto_fields(sd.children, leaf);
                find_constraint_fields(sd.children, leaf);
                si.leaf_types.push_back(std::move(leaf));
            }
        } else {
            LeafTypeInfo leaf;
            leaf.name = sd.name;
            leaf.type_id = fnv1a_hash(sd.name.c_str());
            leaf.send_only = (dir == model::Direction::Send);
            leaf.receive_only = (dir == model::Direction::Receive);
            leaf.access_path = path_ctx;
            // Copy annotations from struct definition
            for (const auto& ann : sd.annotations) {
                leaf.annotations.push_back({ann.name, ann.value});
            }
            find_auto_fields(sd.children, leaf);
            find_constraint_fields(sd.children, leaf);
            si.leaf_types.push_back(std::move(leaf));
        }
    }

    void find_entry_point_fields(const std::vector<model::StructChild>& children, SessionInfo& si) {
        // Temporary leaf to collect fields from entry-point
        LeafTypeInfo temp;
        find_auto_fields(children, temp);
        find_constraint_fields(children, temp);

        // Propagate to all discovered leaves
        for (auto& lt : si.leaf_types) {
            for (const auto& af : temp.auto_fields) {
                lt.auto_fields.push_back(af);
            }
            for (const auto& ab : temp.auto_field_bits) {
                lt.auto_field_bits.push_back(ab);
            }
            for (const auto& cf : temp.constraints) {
                lt.constraints.push_back(cf);
            }
        }
    }

    void find_constraint_fields(const std::vector<model::StructChild>& children, LeafTypeInfo& leaf) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->constraint && f->constraint->equals) {
                    leaf.constraints.push_back({f->name, *f->constraint->equals});
                }
                // Recurse into inline fields
                if (f->is_inline && !f->type_ref.empty()) {
                    auto resolved = index_.find(f->type_ref);
                    if (resolved) {
                        std::visit([this, &leaf](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef> ||
                                         std::is_same_v<DT, model::MessageDef>) {
                                find_constraint_fields(def->children, leaf);
                            }
                        }, *resolved);
                    }
                }
            }
        }
    }

    void find_auto_fields(const std::vector<model::StructChild>& children, LeafTypeInfo& leaf) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_attr && *f->auto_attr == "increment") {
                    leaf.auto_fields.push_back(f->name);
                    // G4: Record per-field bit width for counter type
                    int bits = 8; // default
                    if (!f->type_ref.empty()) {
                        auto it = index_.types.find(f->type_ref);
                        if (it != index_.types.end() && it->second->bits > 0) {
                            bits = it->second->bits;
                        }
                    } else if (f->bits) {
                        bits = *f->bits;
                    }
                    leaf.auto_field_bits.push_back(bits);
                }
                // Recurse into inline fields
                if (f->is_inline && !f->type_ref.empty()) {
                    auto resolved = index_.find(f->type_ref);
                    if (resolved) {
                        std::visit([this, &leaf](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef> ||
                                         std::is_same_v<DT, model::MessageDef>) {
                                find_auto_fields(def->children, leaf);
                            }
                        }, *resolved);
                    }
                }
            }
        }
    }

    void find_sync_pattern(const std::vector<model::StructChild>& children, SessionInfo& si) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                // Stop at optional or variable-length fields
                if (f->bit || f->present_when || f->length_star || f->length_from) {
                    return;
                }
                if (f->constraint && f->constraint->equals) {
                    const auto& eq = *f->constraint->equals;
                    auto it = index_.constants.find(eq);
                    if (it != index_.constants.end()) {
                        parse_sync_bytes(it->second->value, it->second->type_ref, f->endian, si);
                    } else {
                        // Try parsing as a literal value directly
                        parse_sync_bytes(eq, f->type_ref, f->endian, si);
                    }
                }
                if (f->is_inline) {
                    auto resolved = index_.find(f->type_ref);
                    if (resolved) {
                        std::visit([this, &si](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef>) {
                                find_sync_pattern(def->children, si);
                            } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                find_sync_pattern(def->children, si);
                            }
                        }, *resolved);
                    }
                }
            } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
                // Stop at optional structs
                if (sd->bit || sd->present_when) return;
                find_sync_pattern(sd->children, si);
            } else if (std::holds_alternative<model::ChoiceDef>(child) ||
                       std::holds_alternative<model::ArrayDef>(child) ||
                       std::holds_alternative<model::FxBlock>(child)) {
                // Stop scanning at variable-length elements
                return;
            }
        }
    }

    void parse_sync_bytes(const std::string& value_str, const std::string& type_ref,
                          model::Endian endian, SessionInfo& si) {
        int64_t val = 0;
        std::errc ec{};
        const char* end = value_str.data() + value_str.size();
        if (value_str.size() > 2 && value_str[0] == '0' && (value_str[1] == 'x' || value_str[1] == 'X')) {
            auto [ptr, e] = std::from_chars(value_str.data() + 2, end, val, 16);
            ec = e;
            if (ptr != end) ec = std::errc::invalid_argument;
        } else {
            auto [ptr, e] = std::from_chars(value_str.data(), end, val);
            ec = e;
            if (ptr != end) ec = std::errc::invalid_argument;
        }
        if (ec != std::errc{}) {
            return; // Invalid literal, don't modify sync_pattern
        }

        // Determine byte count from type
        size_t bytes = 1;
        auto it = index_.types.find(type_ref);
        if (it != index_.types.end()) {
            bytes = (static_cast<size_t>(it->second->bits) + 7) / 8;
        }

        // Write bytes in wire order (respecting field endianness)
        if (endian == model::Endian::Little) {
            for (size_t i = 0; i < bytes; i++) {
                si.sync_pattern.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
            }
        } else {
            for (size_t i = bytes; i > 0; i--) {
                si.sync_pattern.push_back(static_cast<uint8_t>((val >> ((i - 1) * 8)) & 0xFF));
            }
        }
    }

    void compute_min_header(const std::vector<model::StructChild>& children, SessionInfo& si) {
        size_t total_bits = 0;
        for (const auto& child : children) {
            bool should_break = false;
            std::visit([this, &total_bits, &should_break](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    // Stop at optional or variable-length fields
                    if (c.bit || c.present_when || c.length_star || c.length_from) {
                        should_break = true;
                        return;
                    }
                    if (c.is_inline) {
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &total_bits](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::StructDef>) {
                                    total_bits += compute_fixed_size_bits(def->children);
                                }
                            }, *resolved);
                        }
                    } else if (c.bits) {
                        total_bits += static_cast<size_t>(*c.bits);
                    } else if (c.bytes_attr) {
                        total_bits += static_cast<size_t>(*c.bytes_attr) * 8;
                    } else if (!c.type_ref.empty()) {
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &total_bits](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                    total_bits += static_cast<size_t>(def->bits);
                                }
                            }, *resolved);
                        }
                    }
                } else if constexpr (std::is_same_v<T, model::ChoiceDef> ||
                                     std::is_same_v<T, model::FxBlock>) {
                    should_break = true;
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    // Dynamic arrays break the header scan
                    if (c.count_star || c.count_from) {
                        should_break = true;
                    }
                } else if constexpr (std::is_same_v<T, model::Reserved>) {
                    total_bits += static_cast<size_t>(c.bits);
                } else if constexpr (std::is_same_v<T, model::Align>) {
                    size_t align_bits = static_cast<size_t>(c.to) * 8;
                    if (align_bits > 0) {
                        total_bits = ((total_bits + align_bits - 1) / align_bits) * align_bits;
                    }
                }
            }, child);
            if (should_break) break;
        }
        si.min_frame_header_size = (total_bits + 7) / 8;
    }

    size_t compute_fixed_size_bits(const std::vector<model::StructChild>& children) {
        size_t total_bits = 0;
        for (const auto& child : children) {
            std::visit([this, &total_bits](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    if (c.bits) {
                        total_bits += static_cast<size_t>(*c.bits);
                    } else if (c.bytes_attr) {
                        total_bits += static_cast<size_t>(*c.bytes_attr) * 8;
                    } else if (!c.type_ref.empty()) {
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &total_bits](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                    total_bits += static_cast<size_t>(def->bits);
                                }
                            }, *resolved);
                        }
                    }
                } else if constexpr (std::is_same_v<T, model::Reserved>) {
                    total_bits += static_cast<size_t>(c.bits);
                } else if constexpr (std::is_same_v<T, model::Align>) {
                    size_t align_bits = static_cast<size_t>(c.to) * 8;
                    if (align_bits > 0) {
                        total_bits = ((total_bits + align_bits - 1) / align_bits) * align_bits;
                    }
                }
            }, child);
        }
        return total_bits;
    }

    // D2: Scan top-level fields for one named with "length" or "size"
    // Also tracks cumulative bit offset so extract_frame_length can read
    // the field directly without a full Frame::decode.
    void compute_frame_length_expr(const std::vector<model::StructChild>& children,
                                   SessionInfo& si, size_t bit_offset = 0) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                // Stop at optional or variable-length
                if (f->bit || f->present_when || f->length_star) return;

                // Determine this field's bit width for offset tracking
                int field_bits = 0;
                if (f->bits) {
                    field_bits = *f->bits;
                } else if (f->bytes_attr) {
                    field_bits = *f->bytes_attr * 8;
                } else if (!f->type_ref.empty()) {
                    auto resolved = index_.find(f->type_ref);
                    if (resolved) {
                        std::visit([this, &field_bits](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                field_bits = def->bits;
                            } else if constexpr (std::is_same_v<DT, model::StructDef>) {
                                field_bits = static_cast<int>(compute_fixed_size_bits(def->children));
                            } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                field_bits = static_cast<int>(compute_fixed_size_bits(def->children));
                            }
                        }, *resolved);
                    }
                }

                // Recurse into inline fields to find length/size fields
                if (f->is_inline && !f->type_ref.empty()) {
                    auto resolved = index_.find(f->type_ref);
                    if (resolved) {
                        std::visit([this, &si, bit_offset](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef>) {
                                compute_frame_length_expr(def->children, si, bit_offset);
                            } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                                compute_frame_length_expr(def->children, si, bit_offset);
                            }
                        }, *resolved);
                        if (!si.frame_length_expr.empty()) return;
                    }
                    bit_offset += static_cast<size_t>(field_bits);
                    continue;
                }

                std::string lower_name = f->name;
                for (auto& ch : lower_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

                if (lower_name.find("length") != std::string::npos ||
                    lower_name.find("size") != std::string::npos) {
                    si.frame_length_expr = f->name;
                    si.frame_length_bit_offset = bit_offset;
                    si.frame_length_bits = field_bits;
                    // Resolve signedness: field attribute takes priority,
                    // otherwise check through TypeDef base type.
                    bool is_signed = f->is_signed;
                    if (!is_signed && !f->type_ref.empty()) {
                        auto resolved = index_.find(f->type_ref);
                        if (resolved) {
                            std::visit([&is_signed](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                    if (def->base == model::PrimitiveBase::Int) {
                                        is_signed = true;
                                    }
                                }
                            }, *resolved);
                        }
                    }
                    si.frame_length_signed = is_signed;
                    si.frame_length_endian = f->endian;
                    return;
                }

                bit_offset += static_cast<size_t>(field_bits);
            } else if (auto* r = std::get_if<model::Reserved>(&child)) {
                bit_offset += static_cast<size_t>(r->bits);
            } else if (auto* al = std::get_if<model::Align>(&child)) {
                size_t align_bits = static_cast<size_t>(al->to) * 8;
                if (align_bits > 0) {
                    bit_offset = ((bit_offset + align_bits - 1) / align_bits) * align_bits;
                }
            } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
                if (sd->bit || sd->present_when) return;
                // Check inline struct children
                compute_frame_length_expr(sd->children, si, bit_offset);
                if (!si.frame_length_expr.empty()) return;
                bit_offset += compute_fixed_size_bits(sd->children);
            } else if (std::holds_alternative<model::ChoiceDef>(child) ||
                       std::holds_alternative<model::ArrayDef>(child) ||
                       std::holds_alternative<model::FxBlock>(child)) {
                return;
            }
        }
    }

    // Collect concrete (non-conditional, non-choice, non-FX) fields from entry-point
    void collect_context_fields(const std::vector<model::StructChild>& children,
                                SessionInfo& si) {
        for (const auto& child : children) {
            std::visit([this, &si](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    // Skip conditional fields
                    if (c.bit.has_value() || c.present_when != nullptr) return;
                    // Skip variable-length fields
                    if (c.length_star || c.length_from) return;

                    if (c.is_inline && !c.type_ref.empty()) {
                        // Flatten inline struct fields
                        auto resolved = index_.find(c.type_ref);
                        if (resolved) {
                            std::visit([this, &si](const auto* def) {
                                using DT = std::decay_t<decltype(*def)>;
                                if constexpr (std::is_same_v<DT, model::StructDef> ||
                                             std::is_same_v<DT, model::MessageDef>) {
                                    collect_context_fields(def->children, si);
                                }
                            }, *resolved);
                        }
                        return;
                    }

                    EntryPointContextField cf;
                    cf.bmdl_name = c.name;
                    cf.type_ref = c.type_ref;
                    cf.is_signed = c.is_signed;
                    if (c.bits) cf.bits = *c.bits;

                    // Determine if enum type
                    if (!c.type_ref.empty()) {
                        auto it = index_.types.find(c.type_ref);
                        if (it != index_.types.end()) {
                            cf.is_enum = !it->second->enum_values.empty();
                            if (cf.bits == 0) cf.bits = it->second->bits;
                            if (!cf.is_signed && it->second->base == model::PrimitiveBase::Int)
                                cf.is_signed = true;
                        }
                    }
                    // Also check inline enum values on the field itself
                    if (!c.enum_values.empty()) cf.is_enum = true;

                    si.context_fields.push_back(std::move(cf));
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    // Skip conditional structs
                    if (c.bit.has_value() || c.present_when != nullptr) return;
                    // Recurse into non-conditional nested structs
                    collect_context_fields(c.children, si);
                }
                // Skip ChoiceDef, ArrayDef (dynamic), FxBlock
            }, child);
        }
    }

    // Returns true if no collisions found.
    bool check_type_id_collisions(const SessionInfo& si) {
        std::unordered_map<uint64_t, std::string> seen;
        bool ok = true;
        for (const auto& leaf : si.leaf_types) {
            auto [it, inserted] = seen.try_emplace(leaf.type_id, leaf.name);
            if (!inserted && it->second != leaf.name) {
                Logger::error(
                    "FNV-1a type_id collision in session '" +
                    si.entry_point_name + "': types '" + it->second +
                    "' and '" + leaf.name +
                    "' produce the same hash " +
                    [](uint64_t v) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "0x%016llx",
                                      static_cast<unsigned long long>(v));
                        return std::string(buf);
                    }(leaf.type_id));
                ok = false;
            }
        }
        return ok;
    }

    int resolve_field_bits(const model::Field& f) {
        if (f.bits) return *f.bits;
        if (f.bytes_attr) return std::max(0, *f.bytes_attr) * 8;
        if (!f.type_ref.empty()) {
            auto it = index_.types.find(f.type_ref);
            if (it != index_.types.end()) return it->second->bits;
        }
        return 0;
    }

    const model::Protocol& proto_;
    const TypeIndex& index_;
};

} // anonymous namespace

std::vector<SessionInfo> analyze_sessions(const model::Protocol& protocol, const TypeIndex& index) {
    SessionAnalyzerImpl analyzer(protocol, index);
    return analyzer.analyze();
}

} // namespace bgen::analyzer
