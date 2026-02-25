// SPDX-License-Identifier: MIT
// Bgen - C++ Code Generation Backend

#pragma once

#include "codegen_backend.hpp"

namespace bgen::codegen {

// C++ backend: wraps existing generate_* functions as a CodegenBackend.
// Produces identical output to the pre-refactor bgen pipeline.
struct CppBackend final : CodegenBackend {
    bool generate(
        const model::Protocol& protocol,
        const analyzer::TypeIndex& index,
        const analyzer::WireSizeInfo& sizes,
        const std::vector<analyzer::SessionInfo>& sessions,
        const std::string& ns,
        const std::filesystem::path& output_dir) override;

    std::string_view language_name() const override { return "cpp"; }
};

} // namespace bgen::codegen
