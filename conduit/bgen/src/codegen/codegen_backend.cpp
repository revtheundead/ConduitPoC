// SPDX-License-Identifier: MIT
// Bgen - Code Generation Backend Factory

#include "codegen_backend.hpp"
#include "cpp_backend.hpp"
#include "python_backend.hpp"
#include "java_backend.hpp"

namespace bgen::codegen {

std::unique_ptr<CodegenBackend> create_backend(const std::string& language) {
    if (language == "cpp")
        return std::make_unique<CppBackend>();
    if (language == "python")
        return std::make_unique<PythonBackend>();
    if (language == "java")
        return std::make_unique<JavaBackend>();
    return nullptr;
}

} // namespace bgen::codegen
