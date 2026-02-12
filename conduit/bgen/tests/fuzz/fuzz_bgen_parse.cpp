// SPDX-License-Identifier: MIT
// Conduit - Fuzz harness for BMDL XML parser
//
// Feeds random bytes as XML input to the BMDL parser.
// Should never crash on malformed XML.
//
// Build:
//   cmake -DCONDUIT_BUILD_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ ..
//   cmake --build . --target fuzz_bgen_parse
//
// Run:
//   ./fuzz_bgen_parse corpus_dir/ -max_total_time=300

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <filesystem>

#include "parser/xml_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // parse_bmdl_file() reads from a file path, so we write the fuzz
    // input to a temporary file and then parse it.
    auto tmp_path = std::filesystem::temp_directory_path() / "fuzz_bmdl_input.xml";
    {
        std::FILE* fp = std::fopen(tmp_path.string().c_str(), "wb");
        if (!fp) return 0;
        std::fwrite(data, 1, size, fp);
        std::fclose(fp);
    }

    // Call the parser -- all errors should be returned as XmlParseError,
    // never as crashes or exceptions.
    (void)bgen::parser::parse_bmdl_file(tmp_path.string());

    std::filesystem::remove(tmp_path);
    return 0;
}
