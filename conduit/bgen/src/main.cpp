// SPDX-License-Identifier: MIT
// Bgen - BMDL Code Generator CLI

#include "logger.hpp"
#include "model/ast.hpp"
#include "model/ast_builder.hpp"
#include "model/ast_dump.hpp"
#include "analyzer/type_resolver.hpp"
#include "analyzer/validator.hpp"
#include "analyzer/wire_sizer.hpp"
#include "analyzer/session_analyzer.hpp"
#include "codegen/cpp_constants.hpp"
#include "codegen/cpp_types.hpp"
#include "codegen/cpp_structs.hpp"
#include "codegen/cpp_session.hpp"
#include "codegen/cpp_umbrella.hpp"
#include "codegen/cpp_protocol.hpp"
#include "codegen/name_utils.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

// ============================================================================
// CLI argument parsing
// ============================================================================

#ifndef BGEN_VERSION
#define BGEN_VERSION "dev"
#endif

struct CliArgs {
    std::string input;
    std::string output;
    std::string ns;
    bool verbose = false;
    bool validate_only = false;
    bool dump_ast = false;
};

bool is_valid_namespace(const std::string& ns) {
    if (ns.empty()) return false;
    // Split by :: and validate each segment
    std::string segment;
    for (size_t i = 0; i < ns.size(); ) {
        if (ns[i] == ':' && i + 1 < ns.size() && ns[i+1] == ':') {
            if (segment.empty()) return false; // starts with :: or has ::::
            segment.clear();
            i += 2;
            continue;
        }
        char ch = ns[i];
        // Allow hyphens (converted to underscores later)
        if (ch == '-') ch = '_';
        if (segment.empty()) {
            if (!std::isalpha(static_cast<unsigned char>(ch)) && ch != '_') return false;
        } else {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
        }
        segment += ch;
        i++;
    }
    return !segment.empty(); // must not end with ::
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " --input <root.bmdl.xml> --output <dir> [--namespace <ns>]\n"
              << "\n"
              << "Options:\n"
              << "  --input          Path to root BMDL XML file (required)\n"
              << "  --output         Output directory for generated code (required unless --validate-only)\n"
              << "  --namespace      Override C++ namespace (default: protocol name)\n"
              << "  --validate-only  Parse and validate without generating code\n"
              << "  --dump-ast       Parse, resolve, validate, then print AST and exit\n"
              << "  --verbose        Show informational and diagnostic output\n"
              << "  --version        Show version information\n"
              << "  --help           Show this help message\n";
}

std::optional<CliArgs> parse_args(int argc, char* argv[], int& exit_code) {
    CliArgs args;
    exit_code = 1; // Default to error exit code

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit_code = 0;
            return std::nullopt;
        }
        if (arg == "--version") {
            std::cout << "bgen " << BGEN_VERSION << "\n";
            exit_code = 0;
            return std::nullopt;
        }
        if (arg == "--input" && i + 1 < argc) {
            args.input = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.output = argv[++i];
        } else if (arg == "--namespace" && i + 1 < argc) {
            args.ns = argv[++i];
            if (!is_valid_namespace(args.ns)) {
                std::cerr << "error: invalid namespace '" << args.ns
                          << "' (must be valid C++ identifier(s) separated by ::)\n";
                return std::nullopt;
            }
        } else if (arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--validate-only") {
            args.validate_only = true;
        } else if (arg == "--dump-ast") {
            args.dump_ast = true;
        } else {
            std::cerr << "error: unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return std::nullopt;
        }
    }

    if (args.input.empty()) {
        std::cerr << "error: --input is required\n";
        print_usage(argv[0]);
        return std::nullopt;
    }
    if (args.output.empty() && !args.validate_only && !args.dump_ast) {
        std::cerr << "error: --output is required (or use --validate-only)\n";
        print_usage(argv[0]);
        return std::nullopt;
    }

    return args;
}

// ============================================================================
// File writing
// ============================================================================

bool write_file(const fs::path& path, const std::string& content) {
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

// ============================================================================
// Main pipeline
// ============================================================================

int main(int argc, char* argv[]) {
    int exit_code = 1;
    auto args_opt = parse_args(argc, argv, exit_code);
    if (!args_opt) return exit_code;
    auto& args = *args_opt;

    // Set log level
    if (args.verbose) {
        bgen::Logger::set_level(bgen::LogLevel::Verbose);
    }

    // ========================================================================
    // Step 1: Build AST
    // ========================================================================

    bgen::Logger::info("parsing " + args.input);
    auto build_result = bgen::model::build_protocol(args.input);
    if (!build_result) {
        for (const auto& e : build_result.error()) {
            std::cerr << e.format() << "\n";
        }
        return 1;
    }

    auto& protocol = *build_result;

    // Report parse warnings
    for (const auto& [msg, wloc] : protocol.warnings) {
        bgen::Logger::warn(wloc.to_string() + ": " + msg);
    }

    bgen::Logger::info("protocol '" + protocol.name + "' v" + protocol.version);
    bgen::Logger::info(std::to_string(protocol.types.size()) + " types, "
              + std::to_string(protocol.structs.size()) + " structs, "
              + std::to_string(protocol.messages.size()) + " messages, "
              + std::to_string(protocol.constants.size()) + " constants");

    // ========================================================================
    // Step 2: Resolve types
    // ========================================================================

    bgen::Logger::info("resolving types...");
    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    if (!resolve_result) {
        for (const auto& e : resolve_result.error()) {
            std::cerr << e.format() << "\n";
        }
        return 1;
    }
    auto& index = *resolve_result;

    // ========================================================================
    // Step 3: Validate
    // ========================================================================

    bgen::Logger::info("validating...");
    auto validate_result = bgen::analyzer::validate(protocol, index);
    if (!validate_result) {
        for (const auto& e : validate_result.error()) {
            std::cerr << e.format() << "\n";
        }
        return 1;
    }

    if (args.validate_only) {
        bgen::Logger::info("validation passed");
        return 0;
    }

    if (args.dump_ast) {
        bgen::model::dump_protocol(protocol, std::cout);
        return 0;
    }

    // ========================================================================
    // Step 4: Compute wire sizes
    // ========================================================================

    bgen::Logger::info("computing wire sizes...");
    auto sizes = bgen::analyzer::compute_wire_sizes(protocol, index);

    // ========================================================================
    // Step 5: Analyze sessions
    // ========================================================================

    bgen::Logger::info("analyzing sessions...");
    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);

    for (const auto& s : sessions) {
        bgen::Logger::info("entry-point '" + s.entry_point_name + "' -> "
                  + std::to_string(s.leaf_types.size()) + " leaf types");
    }

    // ========================================================================
    // Step 6: Generate code
    // ========================================================================

    // Namespace priority: CLI override > defaults namespace > protocol name
    std::string ns;
    if (!args.ns.empty()) {
        ns = args.ns;
    } else if (protocol.defaults.namespace_) {
        ns = *protocol.defaults.namespace_;
    } else {
        ns = protocol.name;
    }
    // Convert hyphens to underscores in namespace
    for (char& c : ns) {
        if (c == '-') c = '_';
    }

    bgen::Logger::info("generating code in namespace '" + ns + "'...");

    // Create output directory
    try {
        fs::create_directories(args.output);
    } catch (const std::exception& e) {
        bgen::Logger::error("cannot create output directory '" + args.output + "': " + std::string(e.what()));
        return 2;
    }

    auto output_dir = fs::path(args.output);

    // Generate and write files
    bool ok = true;

    auto constants_code = bgen::codegen::generate_constants(protocol, index, ns);
    ok &= write_file(output_dir / "constants.hpp", constants_code);

    auto types_code = bgen::codegen::generate_types(protocol, ns);
    ok &= write_file(output_dir / "types.hpp", types_code);

    auto structs_code = bgen::codegen::generate_structs(protocol, index, sizes, sessions, ns);
    ok &= write_file(output_dir / "structs.hpp", structs_code);

    auto messages_code = bgen::codegen::generate_messages(protocol, index, sizes, sessions, ns);
    ok &= write_file(output_dir / "messages.hpp", messages_code);

    auto sessions_code = bgen::codegen::generate_sessions(protocol, index, sessions, ns);
    ok &= write_file(output_dir / "sessions.hpp", sessions_code);

    auto protocol_code = bgen::codegen::generate_protocol(protocol, sessions, ns);
    ok &= write_file(output_dir / "protocol.hpp", protocol_code);

    auto umbrella_code = bgen::codegen::generate_umbrella(protocol.name);
    ok &= write_file(output_dir / (protocol.name + ".hpp"), umbrella_code);

    if (!ok) {
        bgen::Logger::error("I/O errors occurred");
        return 3;
    }

    bgen::Logger::info("generated 7 files in " + args.output);
    return 0;
}
