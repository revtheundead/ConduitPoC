// SPDX-License-Identifier: MIT
// Bgen - Wire Size Computation Implementation

#include "wire_sizer.hpp"
#include <unordered_set>

namespace bgen::analyzer {

namespace {

class WireSizerImpl {
public:
    WireSizerImpl(const model::Protocol& proto, const TypeIndex& index)
        : proto_(proto), index_(index) {}

    WireSizeInfo compute() {
        // Compute type sizes
        for (const auto& t : proto_.types) {
            info_.sizes[t.name] = compute_type_size(t);
        }

        // Compute struct sizes with fixed-point iteration
        // (handles forward references between structs)
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& s : proto_.structs) {
                if (info_.sizes.count(s.name) && info_.sizes[s.name].has_value()) continue;
                visiting_.clear();  // Clear cycle detection for each top-level call
                auto sz = compute_struct_size(s);
                if (sz.has_value() && (!info_.sizes.count(s.name) || !info_.sizes[s.name].has_value())) {
                    info_.sizes[s.name] = sz;
                    changed = true;
                }
            }
        }
        // Ensure all structs have an entry (even if dynamic/nullopt)
        for (const auto& s : proto_.structs) {
            if (!info_.sizes.count(s.name)) {
                info_.sizes[s.name] = std::nullopt;
            }
        }

        // Compute message sizes after all structs resolved
        for (const auto& m : proto_.messages) {
            visiting_.clear();  // Clear cycle detection for each top-level call
            info_.sizes[m.name] = compute_children_size(m.children);
        }

        return info_;
    }

private:
    WireSize compute_type_size(const model::TypeDef& t) {
        if (t.base == model::PrimitiveBase::String) {
            if (t.length) {
                if (t.char_bits) {
                    // Packed chars: length * char_bits, rounded up to bytes
                    size_t total_bits = static_cast<size_t>(*t.length) * static_cast<size_t>(*t.char_bits);
                    return (total_bits + 7) / 8;
                }
                return static_cast<size_t>(*t.length);
            }
            return std::nullopt; // Variable-length string
        }
        if (t.base == model::PrimitiveBase::Bytes) {
            if (t.length) return static_cast<size_t>(*t.length);
            return std::nullopt;
        }
        if (t.base == model::PrimitiveBase::Bool) {
            if (t.bits > 0) {
                return (static_cast<size_t>(t.bits) + 7) / 8;
            }
            return 1;
        }
        // Numeric: bits / 8
        if (t.bits > 0) {
            return (static_cast<size_t>(t.bits) + 7) / 8;
        }
        return std::nullopt;
    }

    WireSize compute_struct_size(const model::StructDef& s) {
        if (s.is_bitmap) {
            return std::nullopt; // Bitmap structs are always dynamic (presence-dependent)
        }
        return compute_children_size(s.children);
    }

    // All internal accumulation uses bits. Returns size in bits.
    WireSize compute_children_size_bits(const std::vector<model::StructChild>& children) {
        size_t total_bits = 0;
        bool all_fixed = true;

        for (const auto& child : children) {
            auto sz = std::visit([this, &all_fixed](const auto& c) -> WireSize {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    return compute_field_size_bits(c);
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    if (c.bit || c.present_when) {
                        all_fixed = false;
                        return std::nullopt;
                    }
                    if (c.is_bitmap) {
                        all_fixed = false;
                        return std::nullopt;
                    }
                    return compute_children_size_bits(c.children);
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    return compute_array_size_bits(c);
                } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    all_fixed = false;
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    all_fixed = false;
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, model::Reserved>) {
                    return static_cast<size_t>(c.bits);
                } else if constexpr (std::is_same_v<T, model::Align>) {
                    all_fixed = false;
                    return std::nullopt;
                }
                return std::nullopt;
            }, child);

            if (sz) {
                total_bits += *sz;
            } else {
                all_fixed = false;
            }
        }

        if (all_fixed) {
            return total_bits;
        }
        return std::nullopt;
    }

    WireSize compute_children_size(const std::vector<model::StructChild>& children) {
        auto bits = compute_children_size_bits(children);
        if (bits) return (*bits + 7) / 8;
        return std::nullopt;
    }

    // Returns size in bits
    WireSize compute_field_size_bits(const model::Field& f) {
        if (f.bit || f.present_when) {
            return std::nullopt; // Optional fields
        }

        // E1: Inline fields — compute size from referenced struct/message
        if (f.is_inline && !f.type_ref.empty()) {
            auto resolved = index_.find(f.type_ref);
            if (resolved) {
                return std::visit([this](const auto* def) -> WireSize {
                    using DT = std::decay_t<decltype(*def)>;
                    if constexpr (std::is_same_v<DT, model::StructDef>) {
                        // Cycle detection: if we're already computing this struct, break the cycle
                        if (visiting_.count(def->name)) {
                            return std::nullopt;
                        }
                        visiting_.insert(def->name);
                        auto result = compute_children_size_bits(def->children);
                        visiting_.erase(def->name);
                        return result;
                    } else if constexpr (std::is_same_v<DT, model::MessageDef>) {
                        // Cycle detection: if we're already computing this message, break the cycle
                        if (visiting_.count(def->name)) {
                            return std::nullopt;
                        }
                        visiting_.insert(def->name);
                        auto result = compute_children_size_bits(def->children);
                        visiting_.erase(def->name);
                        return result;
                    }
                    return std::nullopt;
                }, *resolved);
            }
            return std::nullopt;
        }

        // Inline bits
        if (f.bits) {
            return static_cast<size_t>(std::max(0, *f.bits));
        }

        // Inline bytes
        if (f.bytes_attr) {
            return static_cast<size_t>(std::max(0, *f.bytes_attr)) * 8;
        }

        // Type reference
        if (!f.type_ref.empty()) {
            if (f.type_ref == "bytes" || f.type_ref == "string") {
                if (f.length) return static_cast<size_t>(std::max(0, *f.length)) * 8;
                return std::nullopt;
            }

            auto resolved = index_.find(f.type_ref);
            if (resolved) {
                return std::visit([this](const auto* def) -> WireSize {
                    using T = std::decay_t<decltype(*def)>;
                    if constexpr (std::is_same_v<T, model::TypeDef>) {
                        // Compute actual bit count directly from TypeDef properties
                        // instead of going through the byte-rounded sizes map,
                        // to avoid inflating sub-byte types (e.g. 6-bit → 1 byte → 8 bits).
                        if (def->base == model::PrimitiveBase::String) {
                            if (def->length) {
                                if (def->char_bits) return size_t(*def->length) * size_t(*def->char_bits);
                                return size_t(*def->length) * 8;
                            }
                            return std::nullopt;
                        }
                        if (def->base == model::PrimitiveBase::Bytes) {
                            if (def->length) return size_t(*def->length) * 8;
                            return std::nullopt;
                        }
                        if (def->base == model::PrimitiveBase::Bool) {
                            return (def->bits > 0) ? size_t(def->bits) : size_t(1);
                        }
                        if (def->bits > 0) return size_t(def->bits);
                        return std::nullopt;
                    } else if constexpr (std::is_same_v<T, model::StructDef>) {
                        // Cycle detection: if we're already computing this struct, break the cycle
                        if (visiting_.count(def->name)) {
                            return std::nullopt;
                        }
                        visiting_.insert(def->name);
                        auto result = compute_children_size_bits(def->children);
                        visiting_.erase(def->name);
                        return result;
                    } else if constexpr (std::is_same_v<T, model::MessageDef>) {
                        // Cycle detection: if we're already computing this message, break the cycle
                        if (visiting_.count(def->name)) {
                            return std::nullopt;
                        }
                        visiting_.insert(def->name);
                        auto result = compute_children_size_bits(def->children);
                        visiting_.erase(def->name);
                        return result;
                    }
                    return std::nullopt;
                }, *resolved);
            }
        }

        return std::nullopt;
    }

    // Returns size in bits for array
    WireSize compute_array_size_bits(const model::ArrayDef& a) {
        if (a.count_star || a.count_from) return std::nullopt;
        if (a.bit || a.present_when) return std::nullopt;  // optional arrays are dynamic

        if (a.fixed_count) {
            WireSize elem_bits;
            if (!a.type_ref.empty()) {
                // Resolve element type directly to get actual bit count,
                // avoiding the byte-rounded info map which inflates sub-byte types.
                auto resolved = index_.find(a.type_ref);
                if (resolved) {
                    elem_bits = std::visit([this](const auto* def) -> WireSize {
                        using T = std::decay_t<decltype(*def)>;
                        if constexpr (std::is_same_v<T, model::TypeDef>) {
                            // Use actual bit count from TypeDef
                            if (def->base == model::PrimitiveBase::String) {
                                if (def->length) {
                                    if (def->char_bits) return size_t(*def->length) * size_t(*def->char_bits);
                                    return size_t(*def->length) * 8;
                                }
                                return std::nullopt;
                            }
                            if (def->base == model::PrimitiveBase::Bytes) {
                                if (def->length) return size_t(*def->length) * 8;
                                return std::nullopt;
                            }
                            if (def->base == model::PrimitiveBase::Bool) {
                                return (def->bits > 0) ? size_t(def->bits) : size_t(1);
                            }
                            if (def->bits > 0) return size_t(def->bits);
                            return std::nullopt;
                        } else if constexpr (std::is_same_v<T, model::StructDef>) {
                            // Cycle detection for struct references
                            if (visiting_.count(def->name)) {
                                return std::nullopt;
                            }
                            visiting_.insert(def->name);
                            auto result = compute_children_size_bits(def->children);
                            visiting_.erase(def->name);
                            return result;
                        } else if constexpr (std::is_same_v<T, model::MessageDef>) {
                            // Cycle detection for message references
                            if (visiting_.count(def->name)) {
                                return std::nullopt;
                            }
                            visiting_.insert(def->name);
                            auto result = compute_children_size_bits(def->children);
                            visiting_.erase(def->name);
                            return result;
                        }
                        return std::nullopt;
                    }, *resolved);
                }
            } else {
                elem_bits = compute_children_size_bits(a.children);
            }
            if (elem_bits) {
                return static_cast<size_t>(*a.fixed_count) * *elem_bits;
            }
        }

        return std::nullopt;
    }

    const model::Protocol& proto_;
    const TypeIndex& index_;
    WireSizeInfo info_;
    std::unordered_set<std::string> visiting_;  // Cycle detection for recursive type resolution
};

} // anonymous namespace

WireSizeInfo compute_wire_sizes(const model::Protocol& protocol, const TypeIndex& index) {
    WireSizerImpl sizer(protocol, index);
    return sizer.compute();
}

} // namespace bgen::analyzer
