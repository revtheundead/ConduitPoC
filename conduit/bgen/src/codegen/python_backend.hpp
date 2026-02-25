// SPDX-License-Identifier: MIT
// Bgen - Python Code Generation Backend

#pragma once

#include "codegen_backend.hpp"

namespace bgen::codegen {

// Python backend: generates a complete, self-contained Python codec package
// from BMDL protocol definitions. Wire-compatible with C++ generated code.
struct PythonBackend final : CodegenBackend {
    bool generate(
        const model::Protocol& protocol,
        const analyzer::TypeIndex& index,
        const analyzer::WireSizeInfo& sizes,
        const std::vector<analyzer::SessionInfo>& sessions,
        const std::string& ns,
        const std::filesystem::path& output_dir) override;

    std::string_view language_name() const override { return "python"; }
};

} // namespace bgen::codegen
