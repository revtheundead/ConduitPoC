// SPDX-License-Identifier: MIT
// Tests for Python and Java codegen backends
//
// Verifies that the Python and Java backends produce correct output for
// various protocol features: basic types, booleans, floats, enums, flags,
// scaled types, inline structs, arrays, choices, length-prefix strings,
// and sessions.

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

// Helper: generate Python output into a temp dir, return all file contents
struct GeneratedPython {
    std::map<std::string, std::string> files;
};

struct GeneratedJava {
    std::map<std::string, std::string> files;
};

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static std::optional<GeneratedPython> gen_python(const std::string& fixture) {
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

    auto tmp = fs::temp_directory_path() / ("bgen_test_py_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::PythonBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    GeneratedPython gp;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gp.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gp;
}

static std::optional<GeneratedJava> gen_java(const std::string& fixture) {
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

    auto tmp = fs::temp_directory_path() / ("bgen_test_java_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::JavaBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    GeneratedJava gj;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gj.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gj;
}

// ============================================================================
// Python backend tests
// ============================================================================

TEST_CASE("Python: generates all expected files", "[codegen][python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(py->files.count("__init__.py"));
    CHECK(py->files.count("bit_io.py"));
    CHECK(py->files.count("constants.py"));
    CHECK(py->files.count("types.py"));
    CHECK(py->files.count("structs.py"));
    CHECK(py->files.count("messages.py"));
    CHECK(py->files.count("sessions.py"));
    CHECK(py->files.count("protocol.py"));
}

TEST_CASE("Python: message class has decode/encode", "[codegen][python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("class SimpleMessage:") != std::string::npos);
    CHECK(msgs.find("def decode(r:") != std::string::npos);
    CHECK(msgs.find("def encode(self, w:") != std::string::npos);
    CHECK(msgs.find("decode_bytes") != std::string::npos);
    CHECK(msgs.find("encode_bytes") != std::string::npos);
}

TEST_CASE("Python: bool field uses correct encode", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Bool encode must use 'write_bits(1 if ... else 0, ...)' not 'write_u8'
    CHECK(msgs.find("write_bits(1 if self.flag else 0") != std::string::npos);
    // Bool decode should compare != 0
    CHECK(msgs.find("(r.read_u8() != 0)") != std::string::npos);
}

TEST_CASE("Python: float64 field init", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("self.f64 = 0.0") != std::string::npos);
}

TEST_CASE("Python: enum/flags types generated", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("class ColorEnum") != std::string::npos);
    CHECK(types.find("class StatusFlags") != std::string::npos);
    CHECK(types.find("RED") != std::string::npos);
    CHECK(types.find("active") != std::string::npos);
}

TEST_CASE("Python: scaled type generated", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("class ScaledTemp") != std::string::npos);
    CHECK(types.find("0.01") != std::string::npos);
    CHECK(types.find("-40") != std::string::npos);
}

TEST_CASE("Python: little-endian fields use False", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // le16 and le32 should use big_endian=False
    CHECK(msgs.find("read_u16(False)") != std::string::npos);
    CHECK(msgs.find("read_u32(False)") != std::string::npos);
}

TEST_CASE("Python: session class generated for frame-based protocol", "[codegen][python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("class FrameSession:") != std::string::npos);
    CHECK(sessions.find("LEAF_TYPES") != std::string::npos);
    CHECK(sessions.find("AlphaBody") != std::string::npos);
    CHECK(sessions.find("BetaBody") != std::string::npos);
}

TEST_CASE("Python: protocol.py has type registry", "[codegen][python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("class ProtocolDescriptor:") != std::string::npos);
    CHECK(protocol.find("TYPES") != std::string::npos);
    CHECK(protocol.find("find_by_id") != std::string::npos);
    CHECK(protocol.find("find_by_name") != std::string::npos);
}

TEST_CASE("Python: struct types used in choices get classes", "[codegen][python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    // Struct types referenced from choices should be generated
    CHECK(structs.find("class TypeABody:") != std::string::npos);
    CHECK(structs.find("class TypeBBody:") != std::string::npos);
    CHECK(structs.find("class Point:") != std::string::npos);
}

TEST_CASE("Python: constants file", "[codegen][python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& consts = py->files["constants.py"];
    CHECK(consts.find("SYNC") != std::string::npos);
    CHECK(consts.find("0xBEEF") != std::string::npos);
}

TEST_CASE("Python: bit_io contains BitReader and BitWriter", "[codegen][python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("class BitReader:") != std::string::npos);
    CHECK(bio.find("class BitWriter:") != std::string::npos);
    CHECK(bio.find("read_u8") != std::string::npos);
    CHECK(bio.find("write_u8") != std::string::npos);
    CHECK(bio.find("read_bits") != std::string::npos);
    CHECK(bio.find("write_bits") != std::string::npos);
}

// ============================================================================
// Java backend tests
// ============================================================================

TEST_CASE("Java: generates infrastructure files", "[codegen][java]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(java->files.count("BitReader.java"));
    CHECK(java->files.count("BitWriter.java"));
    CHECK(java->files.count("ConduitCodecException.java"));
    CHECK(java->files.count("Constants.java"));
    CHECK(java->files.count("Protocol.java"));
}

TEST_CASE("Java: message class has decode/encode", "[codegen][java]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["SimpleMessage.java"];
    CHECK(msg.find("public final class SimpleMessage") != std::string::npos);
    CHECK(msg.find("public static SimpleMessage decode(BitReader r)") != std::string::npos);
    CHECK(msg.find("public void encode(BitWriter w)") != std::string::npos);
    CHECK(msg.find("decodeBytes") != std::string::npos);
    CHECK(msg.find("encodeBytes") != std::string::npos);
}

TEST_CASE("Java: bool field uses correct encode (not (int)cast)", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["AllTypesMessage.java"];
    // Bool encode must use ternary, not (int)cast
    CHECK(msg.find("this.flag ? 1 : 0") != std::string::npos);
    // Must NOT contain illegal Java cast
    CHECK(msg.find("(int)this.flag") == std::string::npos);
}

TEST_CASE("Java: double field uses 0.0 init (not 0.0f)", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["AllTypesMessage.java"];
    // float should use 0.0f, double should use 0.0 (not 0.0f)
    CHECK(msg.find("public float f32 = 0.0f;") != std::string::npos);
    CHECK(msg.find("public double f64 = 0.0;") != std::string::npos);
    // Make sure f64 doesn't use 0.0f
    CHECK(msg.find("double f64 = 0.0f") == std::string::npos);
}

TEST_CASE("Java: enum type generates enum class", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    REQUIRE(java->files.count("ColorEnum.java"));
    auto& ce = java->files["ColorEnum.java"];
    CHECK(ce.find("public enum ColorEnum") != std::string::npos);
    CHECK(ce.find("RED(") != std::string::npos);
    CHECK(ce.find("GREEN(") != std::string::npos);
    CHECK(ce.find("BLUE(") != std::string::npos);
    CHECK(ce.find("decode(BitReader r)") != std::string::npos);
    CHECK(ce.find("encode(BitWriter w)") != std::string::npos);
}

TEST_CASE("Java: flags type uses long raw", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    REQUIRE(java->files.count("StatusFlags.java"));
    auto& sf = java->files["StatusFlags.java"];
    CHECK(sf.find("private long raw;") != std::string::npos);
    CHECK(sf.find("1L<<") != std::string::npos);
}

TEST_CASE("Java: scaled type generates wrapper class", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    REQUIRE(java->files.count("ScaledTemp.java"));
    auto& st = java->files["ScaledTemp.java"];
    CHECK(st.find("public final class ScaledTemp") != std::string::npos);
    CHECK(st.find("0.01") != std::string::npos);
    CHECK(st.find("double value()") != std::string::npos);
}

TEST_CASE("Java: inline choice types get separate files", "[codegen][java]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    // Inline choice case types should get separate .java files
    CHECK(java->files.count("TypeABody.java"));
    CHECK(java->files.count("TypeBBody.java"));
    CHECK(java->files.count("FallbackBody.java"));
}

TEST_CASE("Java: arrays have List type", "[codegen][java]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["FixedArrayMsg.java"];
    CHECK(msg.find("java.util.List<") != std::string::npos);
    CHECK(msg.find("new java.util.ArrayList<>()") != std::string::npos);
}

TEST_CASE("Java: protocol class has type registry", "[codegen][java]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto& prot = java->files["Protocol.java"];
    CHECK(prot.find("public final class Protocol") != std::string::npos);
    CHECK(prot.find("TypeInfo") != std::string::npos);
    CHECK(prot.find("findById") != std::string::npos);
    CHECK(prot.find("findByName") != std::string::npos);
}

TEST_CASE("Java: byte[] field toString uses Arrays.toString", "[codegen][java]") {
    auto java = gen_java("bytes_numeric.bmdl.xml");
    REQUIRE(java.has_value());
    REQUIRE(java->files.count("LargeBytesMsg.java"));
    auto& msg = java->files["LargeBytesMsg.java"];
    CHECK(msg.find("java.util.Arrays.toString(blob)") != std::string::npos);
}

TEST_CASE("Java: LE fields use false for big_endian", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["AllTypesMessage.java"];
    CHECK(msg.find("readU16(false)") != std::string::npos);
    CHECK(msg.find("readU32(false)") != std::string::npos);
}

TEST_CASE("Java: package declaration present", "[codegen][java]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["AllTypesMessage.java"];
    CHECK(msg.find("package all_types;") != std::string::npos);
}

// ============================================================================
// Cross-backend consistency tests
// ============================================================================

TEST_CASE("Both backends generate output for all fixture types", "[codegen][python][java]") {
    // Both backends should handle common fixture without errors
    auto py = gen_python("all_types.bmdl.xml");
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
    CHECK(py->files.size() == 8);  // 8 Python files
    CHECK(java->files.size() >= 8);  // Infrastructure + type + message files
}

TEST_CASE("Both backends handle choice protocol", "[codegen][python][java]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle arrays/choices", "[codegen][python][java]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle string features", "[codegen][python][java]") {
    auto py = gen_python("string_features.bmdl.xml");
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle inline field types", "[codegen][python][java]") {
    auto py = gen_python("inline_field_types.bmdl.xml");
    auto java = gen_java("inline_field_types.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle wire encodings", "[codegen][python][java]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    auto java = gen_java("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle mixed endian", "[codegen][python][java]") {
    auto py = gen_python("mixed_endian.bmdl.xml");
    auto java = gen_java("mixed_endian.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle bitmap protocol", "[codegen][python][java]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle constraints", "[codegen][python][java]") {
    auto py = gen_python("constraints.bmdl.xml");
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle field scale", "[codegen][python][java]") {
    auto py = gen_python("field_scale.bmdl.xml");
    auto java = gen_java("field_scale.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle expression features", "[codegen][python][java]") {
    auto py = gen_python("expr_features.bmdl.xml");
    auto java = gen_java("expr_features.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}

TEST_CASE("Both backends handle format binary", "[codegen][python][java]") {
    auto py = gen_python("format_binary.bmdl.xml");
    auto java = gen_java("format_binary.bmdl.xml");
    REQUIRE(py.has_value());
    REQUIRE(java.has_value());
}
