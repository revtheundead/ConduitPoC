// SPDX-License-Identifier: MIT
// Bgen - Type Resolver Implementation

#include "type_resolver.hpp"
#include "parse_utils.hpp"

namespace bgen::analyzer {

namespace {

class TypeResolverImpl {
public:
    explicit TypeResolverImpl(model::Protocol& proto) : proto_(proto) {}

    ResolveResult resolve() {
        // Build the index
        for (const auto& t : proto_.types) {
            if (index_.types.count(t.name)) {
                errors_.push_back(ResolveError{"duplicate type name: " + t.name, t.loc});
            } else {
                index_.types[t.name] = &t;
            }
        }
        for (const auto& s : proto_.structs) {
            if (index_.structs.count(s.name)) {
                errors_.push_back(ResolveError{"duplicate struct name: " + s.name, s.loc});
            } else {
                index_.structs[s.name] = &s;
            }
        }
        for (const auto& m : proto_.messages) {
            if (index_.messages.count(m.name)) {
                errors_.push_back(ResolveError{"duplicate message name: " + m.name, m.loc});
            } else {
                index_.messages[m.name] = &m;
            }
        }
        for (const auto& c : proto_.constants) {
            if (index_.constants.count(c.name)) {
                errors_.push_back(ResolveError{"duplicate constant name: " + c.name, c.loc});
            } else {
                index_.constants[c.name] = &c;
            }
            // Validate that the constant's type_ref resolves to a known type
            if (!c.type_ref.empty()) {
                validate_type_ref(c.type_ref, c.loc);
            }
        }

        for (const auto& f : proto_.frames) {
            if (index_.frames.count(f.name)) {
                errors_.push_back(ResolveError{"duplicate frame name: " + f.name, f.loc});
            } else {
                index_.frames[f.name] = &f;
            }
        }

        // Cross-category name collision detection
        for (const auto& [name, _] : index_.types) {
            if (index_.structs.count(name)) {
                errors_.push_back(ResolveError{"name '" + name + "' defined as both type and struct", index_.types.at(name)->loc});
            }
            if (index_.messages.count(name)) {
                errors_.push_back(ResolveError{"name '" + name + "' defined as both type and message", index_.types.at(name)->loc});
            }
        }
        for (const auto& [name, _] : index_.structs) {
            if (index_.messages.count(name)) {
                errors_.push_back(ResolveError{"name '" + name + "' defined as both struct and message", index_.structs.at(name)->loc});
            }
        }

        // Validate references in all fields
        for (const auto& m : proto_.messages) {
            validate_children(m.children);
        }
        for (const auto& s : proto_.structs) {
            validate_children(s.children);
        }
        for (const auto& f : proto_.frames) {
            validate_children(f.header_fields);
            validate_children(f.footer_fields);
        }

        // Apply inheritance from type definitions to fields
        for (auto& m : proto_.messages) {
            apply_inheritance_to_children(m.children);
        }
        for (auto& s : proto_.structs) {
            apply_inheritance_to_children(s.children);
        }
        for (auto& f : proto_.frames) {
            apply_inheritance_to_children(f.header_fields);
            apply_inheritance_to_children(f.footer_fields);
        }

        if (!errors_.empty()) {
            return std::unexpected(std::move(errors_));
        }
        return index_;
    }

private:
    void validate_children(const std::vector<model::StructChild>& children) {
        for (const auto& child : children) {
            std::visit([this](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    validate_field_ref(c);
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    validate_children(c.children);
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    if (!c.type_ref.empty()) {
                        validate_type_ref(c.type_ref, c.loc);
                    }
                    validate_children(c.children);
                } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    for (const auto& cs : c.cases) {
                        if (!cs.type_ref.empty()) {
                            validate_type_ref(cs.type_ref, cs.loc);
                        }
                        validate_children(cs.children);
                    }
                    if (c.otherwise) {
                        if (!c.otherwise->type_ref.empty()) {
                            validate_type_ref(c.otherwise->type_ref, c.otherwise->loc);
                        }
                        validate_children(c.otherwise->children);
                    }
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    validate_children(c.children);
                }
            }, child);
        }
    }

    void validate_field_ref(const model::Field& f) {
        if (!f.type_ref.empty()) {
            validate_type_ref(f.type_ref, f.loc);
        }
    }

    void apply_inheritance_to_children(std::vector<model::StructChild>& children) {
        for (auto& child : children) {
            std::visit([this](auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, model::Field>) {
                    apply_inheritance_to_field(c);
                } else if constexpr (std::is_same_v<T, model::StructDef>) {
                    apply_inheritance_to_children(c.children);
                } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                    apply_inheritance_to_children(c.children);
                } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                    for (auto& cs : c.cases) {
                        apply_inheritance_to_children(cs.children);
                    }
                    if (c.otherwise) {
                        apply_inheritance_to_children(c.otherwise->children);
                    }
                } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                    apply_inheritance_to_children(c.children);
                }
            }, child);
        }
    }

    void apply_inheritance_to_field(model::Field& f) {
        if (f.type_ref.empty()) return;

        auto it = index_.types.find(f.type_ref);
        if (it == index_.types.end()) return;
        const auto& td = *it->second;

        // Endian inheritance
        if (!f.endian_explicit && td.endian_explicit) {
            f.endian = td.endian;
        }

        // Format inheritance
        if (!f.format_explicit && td.format_explicit) {
            f.format = td.format;
        }

        // Scale/offset conflict
        if ((f.scale || f.offset) && (td.scale || td.offset)) {
            errors_.push_back(ResolveError{
                "field '" + f.name + "' and type '" + td.name + "' both define scale/offset",
                f.loc});
        }

        // Constraint narrowing check
        if (f.constraint && td.constraint) {
            // If type has equals and field has different equals, error
            if (td.constraint->equals && f.constraint->equals &&
                *td.constraint->equals != *f.constraint->equals) {
                errors_.push_back(ResolveError{
                    "field '" + f.name + "' constraint conflicts with type '" + td.name + "' constraint",
                    f.loc});
            }
            // If type has min/max, field should not relax it
            if (td.constraint->min && f.constraint->min) {
                auto type_min = parse_literal(*td.constraint->min);
                auto field_min = parse_literal(*f.constraint->min);
                if (type_min && field_min && *field_min < *type_min) {
                    errors_.push_back(ResolveError{
                        "field '" + f.name + "' constraint min (" + *f.constraint->min +
                        ") is less than type '" + td.name + "' min (" + *td.constraint->min + ")",
                        f.loc});
                }
            }
            if (td.constraint->max && f.constraint->max) {
                auto type_max = parse_literal(*td.constraint->max);
                auto field_max = parse_literal(*f.constraint->max);
                if (type_max && field_max && *field_max > *type_max) {
                    errors_.push_back(ResolveError{
                        "field '" + f.name + "' constraint max (" + *f.constraint->max +
                        ") exceeds type '" + td.name + "' max (" + *td.constraint->max + ")",
                        f.loc});
                }
            }
        }
    }

    void validate_type_ref(const std::string& ref, const model::SourceLoc& loc) {
        // Special built-in types
        if (ref == "bytes" || ref == "string") return;

        if (!index_.find(ref)) {
            errors_.push_back(ResolveError{"unresolved type reference: " + ref, loc});
        }
    }

    model::Protocol& proto_;
    TypeIndex index_;
    std::vector<ResolveError> errors_;
};

} // anonymous namespace

ResolveResult resolve_types(model::Protocol& protocol) {
    TypeResolverImpl resolver(protocol);
    return resolver.resolve();
}

} // namespace bgen::analyzer
