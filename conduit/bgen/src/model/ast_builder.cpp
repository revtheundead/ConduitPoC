// SPDX-License-Identifier: MIT
// Bgen - AST Builder Implementation

#include "ast_builder.hpp"

namespace bgen::model {

namespace {

// Apply default endian to fields/types recursively
void apply_defaults_to_children(std::vector<StructChild>& children, const Defaults& defaults);

void apply_defaults_to_field(Field& f, const Defaults& defaults) {
    if (!f.endian_explicit) f.endian = defaults.endian;
    // String defaults only apply to string/bytes fields.
    // TypeDef-based strings get their defaults via the type-level application.
    if (f.type_ref == "string" || f.type_ref == "bytes") {
        if (!f.encoding) f.encoding = defaults.string_encoding;
        if (!f.padding) f.padding = defaults.string_padding;
        if (!f.trim) f.trim = defaults.string_trim;
    }
}

void apply_defaults_to_struct(StructDef& sd, const Defaults& defaults) {
    apply_defaults_to_children(sd.children, defaults);
}

void apply_defaults_to_children(std::vector<StructChild>& children, const Defaults& defaults) {
    for (auto& child : children) {
        std::visit([&](auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Field>) {
                apply_defaults_to_field(c, defaults);
            } else if constexpr (std::is_same_v<T, StructDef>) {
                apply_defaults_to_struct(c, defaults);
            } else if constexpr (std::is_same_v<T, ArrayDef>) {
                apply_defaults_to_children(c.children, defaults);
            } else if constexpr (std::is_same_v<T, ChoiceDef>) {
                for (auto& cs : c.cases) {
                    apply_defaults_to_children(cs.children, defaults);
                }
                if (c.otherwise) {
                    apply_defaults_to_children(c.otherwise->children, defaults);
                }
            } else if constexpr (std::is_same_v<T, FxBlock>) {
                apply_defaults_to_children(c.children, defaults);
            }
        }, child);
    }
}

} // anonymous namespace

BuildResult build_protocol(const std::string& root_path) {
    // Resolve all imports
    auto resolve_result = parser::resolve_imports(root_path);
    if (!resolve_result) {
        std::vector<BuildError> errors;
        for (const auto& e : resolve_result.error()) {
            errors.push_back(BuildError{e.message, e.loc});
        }
        return std::unexpected(std::move(errors));
    }

    auto& files = *resolve_result;

    // Find the protocol file
    BmdlFile* protocol_file = nullptr;
    int protocol_count = 0;
    for (auto& f : files) {
        if (f.has_protocol) {
            protocol_file = &f;
            protocol_count++;
        }
    }

    if (protocol_count == 0) {
        return std::unexpected(std::vector<BuildError>{
            BuildError{"no <protocol> found in any file — no entry point for generation", SourceLoc{root_path, 0}}
        });
    }
    if (protocol_count > 1) {
        return std::unexpected(std::vector<BuildError>{
            BuildError{"multiple <protocol> wrappers found — ambiguous entry point", SourceLoc{root_path, 0}}
        });
    }

    // Build the protocol
    Protocol proto;
    proto.name = protocol_file->protocol_name;
    proto.version = protocol_file->protocol_version;
    proto.bmdl_version = protocol_file->bmdl_version;
    proto.defaults = protocol_file->defaults;
    proto.doc = protocol_file->doc;
    proto.loc = protocol_file->loc;

    // Merge all definitions from all files
    for (auto& f : files) {
        if (!f.has_protocol &&
            f.types.empty() && f.structs.empty() &&
            f.messages.empty() && f.constants.empty()) {
            proto.warnings.push_back({
                "imported file '" + f.file_path + "' contains no definitions",
                f.loc
            });
        }

        const Defaults& defaults = f.has_protocol ? f.defaults : protocol_file->defaults;

        // Apply defaults to types
        for (auto& td : f.types) {
            if (!td.endian_explicit) td.endian = defaults.endian;
            if (td.base == PrimitiveBase::String || td.base == PrimitiveBase::Bytes) {
                if (!td.encoding_explicit) td.encoding = defaults.string_encoding;
                if (!td.padding_explicit) td.padding = defaults.string_padding;
                if (!td.trim_explicit) td.trim = defaults.string_trim;
            }
            proto.types.push_back(std::move(td));
        }

        // Apply defaults to structs
        for (auto& sd : f.structs) {
            apply_defaults_to_struct(sd, defaults);
            proto.structs.push_back(std::move(sd));
        }

        // Apply defaults to messages
        for (auto& md : f.messages) {
            apply_defaults_to_children(md.children, defaults);
            proto.messages.push_back(std::move(md));
        }

        // Apply defaults to frames (header + footer fields)
        for (auto& fd : f.frames) {
            apply_defaults_to_children(fd.header_fields, defaults);
            apply_defaults_to_children(fd.footer_fields, defaults);
            proto.frames.push_back(std::move(fd));
        }

        // Constants don't need defaults
        for (auto& cd : f.constants) {
            proto.constants.push_back(std::move(cd));
        }

        // Collect imports
        for (auto& imp : f.imports) {
            proto.imports.push_back(std::move(imp));
        }

        // Collect warnings
        for (auto& w : f.warnings) {
            proto.warnings.push_back(std::move(w));
        }
    }

    // Warn if messages exist but none is marked as entry-point, and there are
    // library imports — indicates the user likely forgot to designate one.
    // Skip the warning for frame-based protocols (frames replace entry-points)
    // and for single-file protocols since many standalone test/utility protocols
    // legitimately have no entry-point.
    if (files.size() > 1 && proto.frames.empty()) {
        bool has_entry_point = false;
        for (const auto& m : proto.messages) {
            if (m.is_entry_point) { has_entry_point = true; break; }
        }
        if (!has_entry_point && !proto.messages.empty()) {
            proto.warnings.push_back({
                "no message with role=\"entry-point\" found — "
                "session generation will produce no output",
                proto.loc
            });
        }
    }

    return proto;
}

} // namespace bgen::model
