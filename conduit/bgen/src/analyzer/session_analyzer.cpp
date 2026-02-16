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
                si.session_name = frame.name;
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
                                    si.frame_length_field_ref = f->auto_expr->field_ref;
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
                                    si.frame_length_modifier = f->auto_expr->modifier;
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
                                case model::AutoKind::Count:
                                    si.count_field_name = f->name;
                                    break;
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

                // Compute footer size
                size_t footer_bit_offset = 0;
                for (const auto& child : frame.footer_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        footer_bit_offset += static_cast<size_t>(resolve_field_bits(*f));
                    } else if (auto* r = std::get_if<model::Reserved>(&child)) {
                        footer_bit_offset += static_cast<size_t>(r->bits);
                    } else if (auto* al = std::get_if<model::Align>(&child)) {
                        size_t align_bits = static_cast<size_t>(al->to) * 8;
                        if (align_bits > 0) {
                            footer_bit_offset = ((footer_bit_offset + align_bits - 1) / align_bits) * align_bits;
                        }
                    }
                }
                si.frame_footer_size = (footer_bit_offset + 7) / 8;

                // Collect frame-level auto-increment fields (apply to all leaf types)
                std::vector<std::string> frame_auto_fields;
                std::vector<int> frame_auto_field_bits;
                std::vector<std::string> frame_timestamp_fields;
                std::vector<int> frame_timestamp_field_bits;
                for (const auto& child : frame.header_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Increment) {
                            frame_auto_fields.push_back(f->name);
                            frame_auto_field_bits.push_back(resolve_field_bits(*f));
                        }
                        if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Timestamp) {
                            frame_timestamp_fields.push_back(f->name);
                            frame_timestamp_field_bits.push_back(resolve_field_bits(*f));
                        }
                    }
                }

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

                    // Add frame-level auto-increment fields
                    leaf.auto_fields.insert(leaf.auto_fields.end(),
                        frame_auto_fields.begin(), frame_auto_fields.end());
                    leaf.auto_field_bits.insert(leaf.auto_field_bits.end(),
                        frame_auto_field_bits.begin(), frame_auto_field_bits.end());

                    // Add frame-level auto-timestamp fields
                    leaf.timestamp_fields.insert(leaf.timestamp_fields.end(),
                        frame_timestamp_fields.begin(), frame_timestamp_fields.end());
                    leaf.timestamp_field_bits.insert(leaf.timestamp_field_bits.end(),
                        frame_timestamp_field_bits.begin(), frame_timestamp_field_bits.end());

                    // Collect auto-increment and auto-timestamp fields from message children
                    find_auto_fields(msg.children, leaf);

                    si.leaf_types.push_back(std::move(leaf));
                }

                if (!check_type_id_collisions(si)) {
                    Logger::error("Skipping session '" + si.session_name +
                                  "' due to type_id hash collisions");
                    continue;
                }

                sessions.push_back(std::move(si));
            }
            return sessions;
        }

        return sessions;
    }

private:
    void find_auto_fields(const std::vector<model::StructChild>& children, LeafTypeInfo& leaf) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Increment) {
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
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Timestamp) {
                    leaf.timestamp_fields.push_back(f->name);
                    int bits = 8; // default
                    if (!f->type_ref.empty()) {
                        auto it = index_.types.find(f->type_ref);
                        if (it != index_.types.end() && it->second->bits > 0) {
                            bits = it->second->bits;
                        }
                    } else if (f->bits) {
                        bits = *f->bits;
                    }
                    leaf.timestamp_field_bits.push_back(bits);
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

    // Returns true if no collisions found.
    bool check_type_id_collisions(const SessionInfo& si) {
        std::unordered_map<uint64_t, std::string> seen;
        bool ok = true;
        for (const auto& leaf : si.leaf_types) {
            auto [it, inserted] = seen.try_emplace(leaf.type_id, leaf.name);
            if (!inserted && it->second != leaf.name) {
                Logger::error(
                    "FNV-1a type_id collision in session '" +
                    si.session_name + "': types '" + it->second +
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
