// SPDX-License-Identifier: MIT
// Bgen - Code Generation Backend Interface

#pragma once

#include "../model/ast.hpp"
#include "../analyzer/type_resolver.hpp"
#include "../analyzer/wire_sizer.hpp"
#include "../analyzer/session_analyzer.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bgen::codegen {

// Abstract interface for language-specific code generation backends.
//
// CONTRACT: Every backend must handle the COMPLETE BMDL feature set.
// The backend interface receives the same fully-analyzed protocol data
// regardless of target language. All AST node types, wire encodings,
// framing constructs, and session features must be emitted. A backend
// that silently skips or stubs out a BMDL feature is a bug — the
// --language flag changes output syntax, never output capabilities.
struct CodegenBackend {
    virtual ~CodegenBackend() = default;

    // Generate all output files into the given directory.
    // Returns true on success, false on I/O or generation error.
    // The backend MUST emit code covering every type, struct, message,
    // frame, and session in the protocol — no feature may be omitted.
    virtual bool generate(
        const model::Protocol& protocol,
        const analyzer::TypeIndex& index,
        const analyzer::WireSizeInfo& sizes,
        const std::vector<analyzer::SessionInfo>& sessions,
        const std::string& ns,
        const std::filesystem::path& output_dir) = 0;

    // Return the language name (e.g. "cpp", "python", "java")
    virtual std::string_view language_name() const = 0;
};

// Factory: create backend by language name. Returns nullptr if unknown.
std::unique_ptr<CodegenBackend> create_backend(const std::string& language);

} // namespace bgen::codegen
