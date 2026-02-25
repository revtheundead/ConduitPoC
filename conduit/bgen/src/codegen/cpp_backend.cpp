// SPDX-License-Identifier: MIT
// Bgen - C++ Code Generation Backend Implementation

#include "cpp_backend.hpp"
#include "cpp_constants.hpp"
#include "cpp_types.hpp"
#include "cpp_structs.hpp"
#include "cpp_session.hpp"
#include "cpp_protocol.hpp"
#include "cpp_umbrella.hpp"
#include "name_utils.hpp"
#include "../logger.hpp"

#include <fstream>

namespace bgen::codegen {

namespace {

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        bgen::Logger::error("cannot write to " + path.string());
        return false;
    }
    out << content;
    out.flush();
    if (!out) {
        bgen::Logger::error("write failed for " + path.string());
        return false;
    }
    return true;
}

} // namespace

bool CppBackend::generate(
    const model::Protocol& protocol,
    const analyzer::TypeIndex& index,
    const analyzer::WireSizeInfo& sizes,
    const std::vector<analyzer::SessionInfo>& sessions,
    const std::string& ns,
    const std::filesystem::path& output_dir) {

    bool ok = true;

    auto constants_code = generate_constants(protocol, index, ns);
    ok &= write_file(output_dir / "constants.hpp", constants_code);

    auto types_code = generate_types(protocol, ns);
    ok &= write_file(output_dir / "types.hpp", types_code);

    auto structs_code = generate_structs(protocol, index, sizes, sessions, ns);
    ok &= write_file(output_dir / "structs.hpp", structs_code);

    auto messages_code = generate_messages(protocol, index, sizes, sessions, ns);
    ok &= write_file(output_dir / "messages.hpp", messages_code);

    auto sessions_code = generate_sessions(protocol, index, sessions, ns);
    ok &= write_file(output_dir / "sessions.hpp", sessions_code);

    auto protocol_code = generate_protocol(protocol, sessions, ns);
    ok &= write_file(output_dir / "protocol.hpp", protocol_code);

    auto umbrella_name = to_lower_snake_case(protocol.name);
    auto umbrella_code = generate_umbrella(umbrella_name);
    ok &= write_file(output_dir / (umbrella_name + ".hpp"), umbrella_code);

    if (ok) {
        Logger::info("generated 7 files in " + output_dir.string());
    }

    return ok;
}

} // namespace bgen::codegen
