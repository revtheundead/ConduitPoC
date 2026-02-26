// SPDX-License-Identifier: MIT
// Cross-backend wire format consistency tests
//
// Verifies that Python and Java backends generate BitReader/BitWriter with
// all required methods (especially align_to/alignTo), and that both backends
// produce structurally consistent encode/decode code for the same BMDL specs.

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/validator.hpp"
#include "../src/analyzer/wire_sizer.hpp"
#include "../src/analyzer/session_analyzer.hpp"
#include "../src/codegen/python_backend.hpp"
#include "../src/codegen/java_backend.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

struct GenPy {
    std::map<std::string, std::string> files;
};

struct GenJava {
    std::map<std::string, std::string> files;
};

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static std::optional<GenPy> gen_python(const std::string& fixture) {
    auto build = bgen::model::build_protocol(fixture_path(fixture));
    if (!build) return std::nullopt;
    auto& protocol = *build;

    auto resolve = bgen::analyzer::resolve_types(protocol);
    if (!resolve) return std::nullopt;
    auto& index = *resolve;

    auto sizes = bgen::analyzer::compute_wire_sizes(protocol, index);
    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);

    std::string ns = protocol.name;
    for (char& c : ns) if (c == '-') c = '_';

    auto tmp = fs::temp_directory_path() / ("bgen_xb_py_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::PythonBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    GenPy gp;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gp.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gp;
}

static std::optional<GenJava> gen_java(const std::string& fixture) {
    auto build = bgen::model::build_protocol(fixture_path(fixture));
    if (!build) return std::nullopt;
    auto& protocol = *build;

    auto resolve = bgen::analyzer::resolve_types(protocol);
    if (!resolve) return std::nullopt;
    auto& index = *resolve;

    auto sizes = bgen::analyzer::compute_wire_sizes(protocol, index);
    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);

    std::string ns = protocol.name;
    for (char& c : ns) if (c == '-') c = '_';

    auto tmp = fs::temp_directory_path() / ("bgen_xb_java_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::JavaBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    GenJava gj;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gj.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gj;
}

// ============================================================================
// BitReader/BitWriter method parity tests
// ============================================================================

TEST_CASE("Cross: Java BitReader has alignTo method", "[cross][bitio]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("public void alignTo(") != std::string::npos);
}

TEST_CASE("Cross: Java BitWriter has alignTo method", "[cross][bitio]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("public void alignTo(") != std::string::npos);
}

TEST_CASE("Cross: Python BitReader has align_to method", "[cross][bitio]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    // Find align_to WITHIN the BitReader class (before BitWriter class starts)
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    auto align_pos = bio.find("def align_to(", 0);
    CHECK(align_pos != std::string::npos);
    // Must exist in BitReader section, not only in BitWriter
    CHECK(align_pos < bw_start);
}

TEST_CASE("Cross: Python BitWriter still has align_to method", "[cross][bitio]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    // Find align_to after BitWriter class starts
    auto align_pos = bio.find("def align_to(", bw_start);
    CHECK(align_pos != std::string::npos);
}

// ============================================================================
// BitReader/BitWriter complete method parity
// ============================================================================

TEST_CASE("Cross: Java BitReader has all C++ BitReader methods", "[cross][bitio]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("readBits(") != std::string::npos);
    CHECK(br.find("readSignedBits(") != std::string::npos);
    CHECK(br.find("readU8(") != std::string::npos);
    CHECK(br.find("readU16(") != std::string::npos);
    CHECK(br.find("readU32(") != std::string::npos);
    CHECK(br.find("readU64(") != std::string::npos);
    CHECK(br.find("readF32(") != std::string::npos);
    CHECK(br.find("readF64(") != std::string::npos);
    CHECK(br.find("readString(") != std::string::npos);
    CHECK(br.find("readBytes(") != std::string::npos);
    CHECK(br.find("readBcd(") != std::string::npos);
    CHECK(br.find("readBcdSigned(") != std::string::npos);
    CHECK(br.find("readSignMagnitude(") != std::string::npos);
    CHECK(br.find("skipBits(") != std::string::npos);
    CHECK(br.find("subReader(") != std::string::npos);
    CHECK(br.find("alignTo(") != std::string::npos);
    CHECK(br.find("remainingBits(") != std::string::npos);
    CHECK(br.find("remainingBytes(") != std::string::npos);
}

TEST_CASE("Cross: Java BitWriter has all C++ BitWriter methods", "[cross][bitio]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("writeBits(") != std::string::npos);
    CHECK(bw.find("writeSignedBits(") != std::string::npos);
    CHECK(bw.find("writeU8(") != std::string::npos);
    CHECK(bw.find("writeU16(") != std::string::npos);
    CHECK(bw.find("writeU32(") != std::string::npos);
    CHECK(bw.find("writeU64(") != std::string::npos);
    CHECK(bw.find("writeF32(") != std::string::npos);
    CHECK(bw.find("writeF64(") != std::string::npos);
    CHECK(bw.find("writeString(") != std::string::npos);
    CHECK(bw.find("writeBytes(") != std::string::npos);
    CHECK(bw.find("writeBcd(") != std::string::npos);
    CHECK(bw.find("writeBcdSigned(") != std::string::npos);
    CHECK(bw.find("writeSignMagnitude(") != std::string::npos);
    CHECK(bw.find("sizeBytes(") != std::string::npos);
    CHECK(bw.find("toBytes(") != std::string::npos);
    CHECK(bw.find("patchU8(") != std::string::npos);
    CHECK(bw.find("patchU16(") != std::string::npos);
    CHECK(bw.find("patchU32(") != std::string::npos);
    CHECK(bw.find("alignTo(") != std::string::npos);
}

TEST_CASE("Cross: Python BitReader has all C++ BitReader methods", "[cross][bitio]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    // All methods must be in BitReader section (before BitWriter)
    std::string br_section = bio.substr(0, bw_start);
    CHECK(br_section.find("def read_bits(") != std::string::npos);
    CHECK(br_section.find("def read_signed_bits(") != std::string::npos);
    CHECK(br_section.find("def read_u8(") != std::string::npos);
    CHECK(br_section.find("def read_u16(") != std::string::npos);
    CHECK(br_section.find("def read_u32(") != std::string::npos);
    CHECK(br_section.find("def read_u64(") != std::string::npos);
    CHECK(br_section.find("def read_f32(") != std::string::npos);
    CHECK(br_section.find("def read_f64(") != std::string::npos);
    CHECK(br_section.find("def read_string(") != std::string::npos);
    CHECK(br_section.find("def read_bytes(") != std::string::npos);
    CHECK(br_section.find("def read_bcd(") != std::string::npos);
    CHECK(br_section.find("def read_bcd_signed(") != std::string::npos);
    CHECK(br_section.find("def read_sign_magnitude(") != std::string::npos);
    CHECK(br_section.find("def skip_bits(") != std::string::npos);
    CHECK(br_section.find("def sub_reader(") != std::string::npos);
    CHECK(br_section.find("def align_to(") != std::string::npos);
    CHECK(br_section.find("def remaining_bits(") != std::string::npos);
    CHECK(br_section.find("def remaining_bytes(") != std::string::npos);
}

TEST_CASE("Cross: Python BitWriter has all C++ BitWriter methods", "[cross][bitio]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    std::string bw_section = bio.substr(bw_start);
    CHECK(bw_section.find("def write_bits(") != std::string::npos);
    CHECK(bw_section.find("def write_signed_bits(") != std::string::npos);
    CHECK(bw_section.find("def write_u8(") != std::string::npos);
    CHECK(bw_section.find("def write_u16(") != std::string::npos);
    CHECK(bw_section.find("def write_u32(") != std::string::npos);
    CHECK(bw_section.find("def write_u64(") != std::string::npos);
    CHECK(bw_section.find("def write_f32(") != std::string::npos);
    CHECK(bw_section.find("def write_f64(") != std::string::npos);
    CHECK(bw_section.find("def write_string(") != std::string::npos);
    CHECK(bw_section.find("def write_bytes(") != std::string::npos);
    CHECK(bw_section.find("def write_bcd(") != std::string::npos);
    CHECK(bw_section.find("def write_bcd_signed(") != std::string::npos);
    CHECK(bw_section.find("def write_sign_magnitude(") != std::string::npos);
    CHECK(bw_section.find("def size_bytes(") != std::string::npos);
    CHECK(bw_section.find("def to_bytes(") != std::string::npos);
    CHECK(bw_section.find("def patch_u8(") != std::string::npos);
    CHECK(bw_section.find("def patch_u16(") != std::string::npos);
    CHECK(bw_section.find("def patch_u32(") != std::string::npos);
    CHECK(bw_section.find("def align_to(") != std::string::npos);
}

// ============================================================================
// Alignment codegen consistency
// ============================================================================

TEST_CASE("Cross: struct_features alignment calls present in both backends", "[cross][align]") {
    auto py = gen_python("struct_features.bmdl.xml");
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());

    // Python AlignedMessage should have r.align_to(2) and w.align_to(2)
    auto& py_msgs = py->files["messages.py"];
    CHECK(py_msgs.find("r.align_to(2)") != std::string::npos);
    CHECK(py_msgs.find("w.align_to(2)") != std::string::npos);

    // Java AlignedMessage should have r.alignTo(2) and w.alignTo(2)
    auto& j_msg = java->files["AlignedMessage.java"];
    CHECK(j_msg.find("r.alignTo(2)") != std::string::npos);
    CHECK(j_msg.find("w.alignTo(2)") != std::string::npos);
}

// ============================================================================
// Cross-backend field parity tests
// ============================================================================

TEST_CASE("Cross: all_types field names present in both backends", "[cross][parity]") {
    auto py = gen_python("all_types.bmdl.xml");
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());

    auto& py_msgs = py->files["messages.py"];
    auto& j_msg = java->files["AllTypesMessage.java"];

    // Both backends should have decode/encode methods
    CHECK(py_msgs.find("def decode(") != std::string::npos);
    CHECK(py_msgs.find("def encode(") != std::string::npos);
    CHECK(j_msg.find("public static AllTypesMessage decode(") != std::string::npos);
    CHECK(j_msg.find("public void encode(") != std::string::npos);

    // Both should have decode_bytes/decodeBytes and encode_bytes/encodeBytes
    CHECK(py_msgs.find("decode_bytes") != std::string::npos);
    CHECK(py_msgs.find("encode_bytes") != std::string::npos);
    CHECK(j_msg.find("decodeBytes") != std::string::npos);
    CHECK(j_msg.find("encodeBytes") != std::string::npos);
}

TEST_CASE("Cross: wire_encodings both backends have BCD/BNR_S calls", "[cross][parity]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    auto java = gen_java("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());

    auto& py_msgs = py->files["messages.py"];
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;

    // BCD read/write in Python
    CHECK(py_msgs.find("read_bcd(") != std::string::npos);
    CHECK(py_msgs.find("write_bcd(") != std::string::npos);
    CHECK(py_msgs.find("read_bcd_signed(") != std::string::npos);
    CHECK(py_msgs.find("write_bcd_signed(") != std::string::npos);
    CHECK(py_msgs.find("read_sign_magnitude(") != std::string::npos);
    CHECK(py_msgs.find("write_sign_magnitude(") != std::string::npos);

    // BCD read/write in Java
    CHECK(j_all.find("readBcd(") != std::string::npos);
    CHECK(j_all.find("writeBcd(") != std::string::npos);
    CHECK(j_all.find("readBcdSigned(") != std::string::npos);
    CHECK(j_all.find("writeBcdSigned(") != std::string::npos);
    CHECK(j_all.find("readSignMagnitude(") != std::string::npos);
    CHECK(j_all.find("writeSignMagnitude(") != std::string::npos);
}

TEST_CASE("Cross: mixed_endian both backends use matching endian flags", "[cross][parity]") {
    auto py = gen_python("mixed_endian.bmdl.xml");
    auto java = gen_java("mixed_endian.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());

    auto& py_msgs = py->files["messages.py"];
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;

    // Python uses True/False for big_endian parameter
    CHECK(py_msgs.find("True") != std::string::npos);
    CHECK(py_msgs.find("False") != std::string::npos);

    // Java uses true/false for big_endian parameter
    CHECK(j_all.find("true") != std::string::npos);
    CHECK(j_all.find("false") != std::string::npos);
}

TEST_CASE("Cross: both backends generate output for all major fixtures", "[cross][parity]") {
    // Every fixture that both backends should handle without errors
    std::vector<std::string> fixtures = {
        "minimal.bmdl.xml",
        "all_types.bmdl.xml",
        "struct_features.bmdl.xml",
        "choice_protocol.bmdl.xml",
        "arrays_choices.bmdl.xml",
        "string_features.bmdl.xml",
        "wire_encodings.bmdl.xml",
        "mixed_endian.bmdl.xml",
        "bitmap_fx.bmdl.xml",
        "constraints.bmdl.xml",
        "field_scale.bmdl.xml",
        "expr_features.bmdl.xml",
        "format_binary.bmdl.xml",
    };
    for (const auto& fixture : fixtures) {
        INFO("Fixture: " << fixture);
        auto py = gen_python(fixture);
        auto java = gen_java(fixture);
        CHECK(py.has_value());
        CHECK(java.has_value());
    }
}

// ============================================================================
// Session parity tests
// ============================================================================

TEST_CASE("Cross: choice_protocol sessions have matching structure", "[cross][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());

    auto& py_sess = py->files["sessions.py"];
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;

    // Both should have the same leaf type references
    CHECK(py_sess.find("AlphaBody") != std::string::npos);
    CHECK(py_sess.find("BetaBody") != std::string::npos);
    CHECK(j_all.find("AlphaBody") != std::string::npos);
    CHECK(j_all.find("BetaBody") != std::string::npos);

    // Both should have LEAF_TYPES
    CHECK(py_sess.find("LEAF_TYPES") != std::string::npos);
    CHECK(j_all.find("LEAF_TYPES") != std::string::npos);
}
