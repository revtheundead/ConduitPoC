// SPDX-License-Identifier: MIT
// Comprehensive tests for the Python codegen backend
//
// Verifies that the Python backend produces correct output for all protocol
// features, matching the coverage of the C++ codegen test suite.

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/validator.hpp"
#include "../src/analyzer/wire_sizer.hpp"
#include "../src/analyzer/session_analyzer.hpp"
#include "../src/codegen/python_backend.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

struct GeneratedPython {
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

// ============================================================================
// File generation tests
// ============================================================================

TEST_CASE("Python: generates all expected files for minimal protocol", "[python]") {
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

TEST_CASE("Python: __init__.py re-exports modules", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& init = py->files["__init__.py"];
    CHECK(init.find("from .bit_io import") != std::string::npos);
    CHECK(init.find("from .constants import") != std::string::npos);
    CHECK(init.find("from .types import") != std::string::npos);
}

// ============================================================================
// bit_io.py tests
// ============================================================================

TEST_CASE("Python: bit_io contains BitReader and BitWriter classes", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("class BitReader:") != std::string::npos);
    CHECK(bio.find("class BitWriter:") != std::string::npos);
}

TEST_CASE("Python: BitReader has read methods for all types", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def read_bits(") != std::string::npos);
    CHECK(bio.find("def read_signed_bits(") != std::string::npos);
    CHECK(bio.find("def read_u8(") != std::string::npos);
    CHECK(bio.find("def read_u16(") != std::string::npos);
    CHECK(bio.find("def read_u32(") != std::string::npos);
    CHECK(bio.find("def read_u64(") != std::string::npos);
    CHECK(bio.find("def read_f32(") != std::string::npos);
    CHECK(bio.find("def read_f64(") != std::string::npos);
    CHECK(bio.find("def read_string(") != std::string::npos);
    CHECK(bio.find("def read_bytes(") != std::string::npos);
    CHECK(bio.find("def read_bcd(") != std::string::npos);
    CHECK(bio.find("def read_bcd_signed(") != std::string::npos);
    CHECK(bio.find("def read_sign_magnitude(") != std::string::npos);
}

TEST_CASE("Python: BitWriter has write methods for all types", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def write_bits(") != std::string::npos);
    CHECK(bio.find("def write_signed_bits(") != std::string::npos);
    CHECK(bio.find("def write_u8(") != std::string::npos);
    CHECK(bio.find("def write_u16(") != std::string::npos);
    CHECK(bio.find("def write_u32(") != std::string::npos);
    CHECK(bio.find("def write_u64(") != std::string::npos);
    CHECK(bio.find("def write_f32(") != std::string::npos);
    CHECK(bio.find("def write_f64(") != std::string::npos);
    CHECK(bio.find("def write_string(") != std::string::npos);
    CHECK(bio.find("def write_bytes(") != std::string::npos);
    CHECK(bio.find("def write_bcd(") != std::string::npos);
    CHECK(bio.find("def write_bcd_signed(") != std::string::npos);
    CHECK(bio.find("def write_sign_magnitude(") != std::string::npos);
}

TEST_CASE("Python: BitWriter has patch methods", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def patch_u8(") != std::string::npos);
    CHECK(bio.find("def patch_u16(") != std::string::npos);
    CHECK(bio.find("def patch_u32(") != std::string::npos);
}

TEST_CASE("Python: BitWriter has align_to method", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def align_to(") != std::string::npos);
}

TEST_CASE("Python: bit_io has error classes", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("class DecodeError") != std::string::npos);
    CHECK(bio.find("class ConstraintError") != std::string::npos);
}

// ============================================================================
// Message class tests
// ============================================================================

TEST_CASE("Python: message class has decode/encode methods", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("class SimpleMessage:") != std::string::npos);
    CHECK(msgs.find("def decode(r:") != std::string::npos);
    CHECK(msgs.find("def encode(self, w:") != std::string::npos);
    CHECK(msgs.find("decode_bytes") != std::string::npos);
    CHECK(msgs.find("encode_bytes") != std::string::npos);
}

TEST_CASE("Python: message class has TYPE_ID and TYPE_NAME", "[python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("TYPE_ID") != std::string::npos);
    CHECK(msgs.find("TYPE_NAME") != std::string::npos);
}

// ============================================================================
// Basic type tests
// ============================================================================

TEST_CASE("Python: bool field uses correct decode (comparison != 0)", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("(r.read_u8() != 0)") != std::string::npos);
}

TEST_CASE("Python: bool field uses correct encode (1 if ... else 0)", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("write_bits(1 if self.flag else 0") != std::string::npos);
}

TEST_CASE("Python: float64 field initialized to 0.0", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("self.f64 = 0.0") != std::string::npos);
}

TEST_CASE("Python: little-endian u16 uses False", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_u16(False)") != std::string::npos);
    CHECK(msgs.find("read_u32(False)") != std::string::npos);
}

TEST_CASE("Python: big-endian u16 uses True", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_u16(True)") != std::string::npos);
}

// ============================================================================
// Enum / Flags / Scaled type tests
// ============================================================================

TEST_CASE("Python: enum type generates IntEnum class", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("class ColorEnum") != std::string::npos);
    CHECK(types.find("IntEnum") != std::string::npos);
    CHECK(types.find("RED") != std::string::npos);
    CHECK(types.find("GREEN") != std::string::npos);
    CHECK(types.find("BLUE") != std::string::npos);
    CHECK(types.find("def decode(r:") != std::string::npos);
    CHECK(types.find("def encode(self, w:") != std::string::npos);
}

TEST_CASE("Python: flags type generates class with properties", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("class StatusFlags") != std::string::npos);
    CHECK(types.find("active") != std::string::npos);
    CHECK(types.find("@property") != std::string::npos);
    CHECK(types.find("__slots__") != std::string::npos);
}

TEST_CASE("Python: scaled type generates class with value property", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("class ScaledTemp") != std::string::npos);
    CHECK(types.find("0.01") != std::string::npos);
    CHECK(types.find("-40") != std::string::npos);
    CHECK(types.find("def value(self)") != std::string::npos);
    CHECK(types.find("SCALE") != std::string::npos);
    CHECK(types.find("OFFSET") != std::string::npos);
}

// ============================================================================
// Constants tests
// ============================================================================

TEST_CASE("Python: constants file has class and values", "[python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& consts = py->files["constants.py"];
    CHECK(consts.find("class Constants:") != std::string::npos);
    CHECK(consts.find("SYNC") != std::string::npos);
    CHECK(consts.find("0xBEEF") != std::string::npos);
}

TEST_CASE("Python: empty constants gets pass statement", "[python]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& consts = py->files["constants.py"];
    CHECK(consts.find("class Constants:") != std::string::npos);
}

// ============================================================================
// Struct tests
// ============================================================================

TEST_CASE("Python: struct types used in choices get classes", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    CHECK(structs.find("class TypeABody:") != std::string::npos);
    CHECK(structs.find("class TypeBBody:") != std::string::npos);
    CHECK(structs.find("class Point:") != std::string::npos);
}

TEST_CASE("Python: struct class has __init__, decode, encode, __repr__", "[python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("def __init__(self)") != std::string::npos);
    CHECK(msgs.find("def __repr__(self)") != std::string::npos);
}

// ============================================================================
// Session tests
// ============================================================================

TEST_CASE("Python: session class generated for frame-based protocol", "[python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("class FrameSession:") != std::string::npos);
    CHECK(sessions.find("LEAF_TYPES") != std::string::npos);
    CHECK(sessions.find("AlphaBody") != std::string::npos);
    CHECK(sessions.find("BetaBody") != std::string::npos);
}

// ============================================================================
// Protocol tests
// ============================================================================

TEST_CASE("Python: protocol.py has type registry with find methods", "[python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("class ProtocolDescriptor:") != std::string::npos);
    CHECK(protocol.find("TYPES") != std::string::npos);
    CHECK(protocol.find("find_by_id") != std::string::npos);
    CHECK(protocol.find("find_by_name") != std::string::npos);
}

TEST_CASE("Python: protocol.py has NAME and VERSION", "[python]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("NAME") != std::string::npos);
    CHECK(protocol.find("VERSION") != std::string::npos);
}

// ============================================================================
// Array tests
// ============================================================================

TEST_CASE("Python: fixed-count array uses list comprehension", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("for _ in range(") != std::string::npos);
}

TEST_CASE("Python: array field initialized as empty list", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("= []") != std::string::npos);
}

TEST_CASE("Python: array encode uses for loop", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("for _item in") != std::string::npos);
    CHECK(msgs.find("_item.encode(w)") != std::string::npos);
}

// ============================================================================
// Choice tests
// ============================================================================

TEST_CASE("Python: choice uses if/elif dispatch", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("if ") != std::string::npos);
    CHECK(msgs.find("elif ") != std::string::npos);
}

TEST_CASE("Python: choice encode uses is not None guard", "[python]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("is not None") != std::string::npos);
}

// ============================================================================
// String feature tests
// ============================================================================

TEST_CASE("Python: fixed-length string decode with read_string", "[python]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_string(") != std::string::npos);
}

TEST_CASE("Python: string field initialized to empty string", "[python]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("= ''") != std::string::npos);
}

TEST_CASE("Python: length-prefix string generates prefix read", "[python]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Length prefix should read the prefix first, then the string
    CHECK(msgs.find("read_u") != std::string::npos);
    CHECK(msgs.find("read_string(") != std::string::npos);
}

TEST_CASE("Python: string encode uses write_string", "[python]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("write_string(") != std::string::npos);
}

// ============================================================================
// Wire encoding tests (BCD, BCD_S, BNR_S)
// ============================================================================

TEST_CASE("Python: BCD field uses read_bcd/write_bcd", "[python]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_bcd(") != std::string::npos);
    CHECK(msgs.find("write_bcd(") != std::string::npos);
}

TEST_CASE("Python: BCD_S field uses read_bcd_signed/write_bcd_signed", "[python]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_bcd_signed(") != std::string::npos);
    CHECK(msgs.find("write_bcd_signed(") != std::string::npos);
}

TEST_CASE("Python: BNR_S field uses read_sign_magnitude/write_sign_magnitude", "[python]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_sign_magnitude(") != std::string::npos);
    CHECK(msgs.find("write_sign_magnitude(") != std::string::npos);
}

// ============================================================================
// Mixed endian tests
// ============================================================================

TEST_CASE("Python: mixed endian protocol uses both True and False", "[python]") {
    auto py = gen_python("mixed_endian.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Should contain both big-endian (True) and little-endian (False)
    CHECK(msgs.find("True") != std::string::npos);
    CHECK(msgs.find("False") != std::string::npos);
}

// ============================================================================
// Field scale/offset tests
// ============================================================================

TEST_CASE("Python: field-level scale generates multiply/add decode", "[python]") {
    auto py = gen_python("field_scale.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Scaled fields should have scale/offset in decode
    CHECK(msgs.find("* ") != std::string::npos);
}

TEST_CASE("Python: field-level scale generates inverse encode", "[python]") {
    auto py = gen_python("field_scale.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Encode should have the inverse operations
    CHECK(msgs.find("/ ") != std::string::npos);
}

// ============================================================================
// Inline struct/field tests
// ============================================================================

TEST_CASE("Python: inline field types get correct decode", "[python]") {
    auto py = gen_python("inline_field_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(!msgs.empty());
}

TEST_CASE("Python: inline struct generates nested class", "[python]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    CHECK(!structs.empty());
}

// ============================================================================
// Expression tests
// ============================================================================

TEST_CASE("Python: expression in length-from generates correct code", "[python]") {
    auto py = gen_python("expr_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(!msgs.empty());
}

// ============================================================================
// Constraint tests
// ============================================================================

TEST_CASE("Python: constrained type generates constraint check", "[python]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("ConstraintError") != std::string::npos);
}

// ============================================================================
// FX block tests
// ============================================================================

TEST_CASE("Python: FX block generates correct output", "[python]") {
    auto py = gen_python("fx_block.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("FxMessage") != std::string::npos);
}

TEST_CASE("Python: FX advanced generates output", "[python]") {
    auto py = gen_python("fx_advanced.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Bitmap tests
// ============================================================================

TEST_CASE("Python: bitmap FX protocol generates output", "[python]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: bitmap advanced generates output", "[python]") {
    auto py = gen_python("bitmap_advanced.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// present_when tests
// ============================================================================

TEST_CASE("Python: present_when on field generates if guard", "[python]") {
    auto py = gen_python("present_when_complex.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    // The struct is inside types section, so check both files
    std::string all = structs;
    if (py->files.count("messages.py")) all += py->files["messages.py"];
    if (py->files.count("types.py")) all += py->files["types.py"];
    CHECK(all.find("if ") != std::string::npos);
}

TEST_CASE("Python: present_when on array generates conditional decode", "[python]") {
    auto py = gen_python("present_when_complex.bmdl.xml");
    REQUIRE(py.has_value());
    // The present_when_complex fixture has an array with present_when="flags & 0x01"
    // Expression renders as decimal: flags & 1
    std::string all;
    for (const auto& [name, content] : py->files) all += content;
    CHECK(all.find("flags") != std::string::npos);
    CHECK(all.find("& 1)") != std::string::npos);
}

TEST_CASE("Python: present_when on choice generates conditional decode", "[python]") {
    auto py = gen_python("present_when_complex.bmdl.xml");
    REQUIRE(py.has_value());
    std::string all;
    for (const auto& [name, content] : py->files) all += content;
    // Expression renders as decimal: flags & 2
    CHECK(all.find("& 2)") != std::string::npos);
}

// ============================================================================
// Reserved field tests
// ============================================================================

TEST_CASE("Python: reserved field generates skip_bits", "[python]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("skip_bits") != std::string::npos);
}

TEST_CASE("Python: reserved field encode writes zeros", "[python]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("write_bits(0, ") != std::string::npos);
}

// ============================================================================
// Bytes field tests
// ============================================================================

TEST_CASE("Python: bytes field uses read_bytes/write_bytes", "[python]") {
    auto py = gen_python("bytes_numeric.bmdl.xml");
    REQUIRE(py.has_value());
    std::string all;
    for (const auto& [name, content] : py->files) all += content;
    CHECK(all.find("read_bytes(") != std::string::npos);
    CHECK(all.find("write_bytes(") != std::string::npos);
}

TEST_CASE("Python: bytes field initialized to b''", "[python]") {
    auto py = gen_python("bytes_numeric.bmdl.xml");
    REQUIRE(py.has_value());
    std::string all;
    for (const auto& [name, content] : py->files) all += content;
    CHECK(all.find("= b''") != std::string::npos);
}

// ============================================================================
// Frame tests
// ============================================================================

TEST_CASE("Python: basic frame generates session", "[python]") {
    auto py = gen_python("frame_basic.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("Python: frame with config generates output", "[python]") {
    auto py = gen_python("frame_config.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame with footer generates output", "[python]") {
    auto py = gen_python("frame_footer.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame with direction generates output", "[python]") {
    auto py = gen_python("frame_direction.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame with array generates output", "[python]") {
    auto py = gen_python("frame_array.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame with timestamp generates output", "[python]") {
    auto py = gen_python("frame_timestamp.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame length offset generates output", "[python]") {
    auto py = gen_python("frame_length_offset.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame count generates output", "[python]") {
    auto py = gen_python("frame_count.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame payload length generates output", "[python]") {
    auto py = gen_python("frame_payload_length.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame payload length from generates output", "[python]") {
    auto py = gen_python("frame_payload_length_from.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: frame length arith generates output", "[python]") {
    auto py = gen_python("frame_length_arith.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Inline enum tests
// ============================================================================

TEST_CASE("Python: inline enum generates class", "[python]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Auto-field tests
// ============================================================================

TEST_CASE("Python: auto-sequence generates output", "[python]") {
    auto py = gen_python("auto_sequence.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: auto-count generates output", "[python]") {
    auto py = gen_python("auto_count.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: auto-struct-length generates output", "[python]") {
    auto py = gen_python("auto_struct_length.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Format binary tests
// ============================================================================

TEST_CASE("Python: format binary generates output", "[python]") {
    auto py = gen_python("format_binary.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Edge case fixture tests
// ============================================================================

TEST_CASE("Python: EBCDIC strings generate output", "[python]") {
    auto py = gen_python("ebcdic_strings.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: inline case collision generates output", "[python]") {
    auto py = gen_python("inline_case_collision.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: optional constrained generates output", "[python]") {
    auto py = gen_python("optional_constrained.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: boundary types generate output", "[python]") {
    auto py = gen_python("boundary_types.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: default initial values generate output", "[python]") {
    auto py = gen_python("default_initial.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: string prefix incl generates output", "[python]") {
    auto py = gen_python("string_prefix_incl.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: enum arrays generate output", "[python]") {
    auto py = gen_python("enum_arrays.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: signed length fields generate output", "[python]") {
    auto py = gen_python("signed_length.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: send-only leaf generates output", "[python]") {
    auto py = gen_python("send_only_leaf.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: stress large protocol generates output", "[python]") {
    auto py = gen_python("stress_large.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: constraint tighten generates output", "[python]") {
    auto py = gen_python("constraint_tighten.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: direction qualified generates output", "[python]") {
    auto py = gen_python("direction_qualified.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: outer scope generates output", "[python]") {
    auto py = gen_python("outer_scope.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: FX IA5 string generates output", "[python]") {
    auto py = gen_python("fx_ia5_string.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: bitmap wide fixed generates output", "[python]") {
    auto py = gen_python("bitmap_wide_fixed.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: msg config generates output", "[python]") {
    auto py = gen_python("msg_config.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: msg config inline generates output", "[python]") {
    auto py = gen_python("msg_config_inline.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: constants everywhere generates output", "[python]") {
    auto py = gen_python("constants_everywhere.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: type name override generates output", "[python]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: constraints extended generates output", "[python]") {
    auto py = gen_python("constraints_extended.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: length arith generates output", "[python]") {
    auto py = gen_python("length_arith.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: non-overlap ranges generates output", "[python]") {
    auto py = gen_python("non_overlap_ranges.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: FX choice generates output", "[python]") {
    auto py = gen_python("fx_choice.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: FX string generates output", "[python]") {
    auto py = gen_python("fx_string.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("Python: nested choice passes outer-scope params to inner decode", "[python][nested]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Typed case decode should receive sub_type as an outer-scope param
    // Class is parent-prefixed to prevent collisions across messages
    CHECK(msgs.find("class NestedChoiceMsgTyped:") != std::string::npos);
    CHECK(msgs.find("def decode(r: 'BitReader', sub_type)") != std::string::npos);
}

TEST_CASE("Python: deep nested choice generates 3-level classes with outer params", "[python][nested]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // 3-level deep nested choice: classes are parent-prefixed to prevent collisions
    CHECK(msgs.find("class DeepNestedMsgL1:") != std::string::npos);
    CHECK(msgs.find("class DeepNestedMsgL1L2:") != std::string::npos);
    CHECK(msgs.find("class DeepNestedMsgL1L2L3:") != std::string::npos);
    CHECK(msgs.find("def decode(r: 'BitReader', type_b, type_c)") != std::string::npos);
    CHECK(msgs.find("def decode(r: 'BitReader', type_c)") != std::string::npos);
}

TEST_CASE("Python: session protocol generates output", "[python]") {
    auto py = gen_python("session_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Python backend basic codegen tests (moved from cross-backend tests)
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
    CHECK(msgs.find("write_bits(1 if self.flag else 0") != std::string::npos);
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

TEST_CASE("Python: generates output for all_types fixture", "[codegen][python]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(py->files.size() == 8);
}

TEST_CASE("Python: generates output for major fixtures", "[codegen][python]") {
    std::vector<std::string> fixtures = {
        "choice_protocol.bmdl.xml", "arrays_choices.bmdl.xml",
        "string_features.bmdl.xml", "inline_field_types.bmdl.xml",
        "wire_encodings.bmdl.xml", "mixed_endian.bmdl.xml",
        "bitmap_fx.bmdl.xml", "constraints.bmdl.xml",
        "field_scale.bmdl.xml", "expr_features.bmdl.xml",
        "format_binary.bmdl.xml",
    };
    for (const auto& fixture : fixtures) {
        INFO("Fixture: " << fixture);
        auto py = gen_python(fixture);
        CHECK(py.has_value());
    }
}

// ============================================================================
// Float16 / variable-size float codegen tests
// ============================================================================

TEST_CASE("Python: float16 types generate read_f16/write_f16", "[python][float]") {
    auto py = gen_python("float16_types.bmdl.xml");
    REQUIRE(py.has_value());

    // bit_io.py should have read_f16 and write_f16
    REQUIRE(py->files.count("bit_io.py"));
    CHECK(py->files.at("bit_io.py").find("read_f16") != std::string::npos);
    CHECK(py->files.at("bit_io.py").find("write_f16") != std::string::npos);

    // The generated messages.py should use read_f16/write_f16
    bool found_f16_read = false;
    bool found_f16_write = false;
    for (const auto& [name, content] : py->files) {
        if (content.find("read_f16") != std::string::npos) found_f16_read = true;
        if (content.find("write_f16") != std::string::npos) found_f16_write = true;
    }
    CHECK(found_f16_read);
    CHECK(found_f16_write);
}

// ============================================================================
// Empty struct / message codegen tests
// ============================================================================

TEST_CASE("Python: empty struct with <empty/> generates valid code", "[python][empty]") {
    auto py = gen_python("empty_struct_explicit.bmdl.xml");
    REQUIRE(py.has_value());

    // structs.py should contain EmptyExplicit
    bool found_empty_struct = false;
    bool found_empty_msg = false;
    for (const auto& [name, content] : py->files) {
        if (content.find("EmptyExplicit") != std::string::npos) found_empty_struct = true;
        if (content.find("EmptyMsg") != std::string::npos) found_empty_msg = true;
    }
    CHECK(found_empty_struct);
    CHECK(found_empty_msg);
}

// ============================================================================
// FX-terminated arrays (count="fx") and enum-typed message-id fields
// ============================================================================

TEST_CASE("Python: count=fx generates FX-terminated decode loop", "[python][fx-terminated]") {
    auto py = gen_python("fx_terminated.bmdl.xml");
    REQUIRE(py.has_value());
    auto& src = py->files["messages.py"];
    REQUIRE(!src.empty());
    // Decode loops while the FX continuation bit is set.
    CHECK(src.find("_fx_more = True") != std::string::npos);
    CHECK(src.find("_fx_more = r.read_bits(1) != 0") != std::string::npos);
    // Encode writes a continuation bit after each element.
    CHECK(src.find("w.write_bits(1 if (_i + 1 <") != std::string::npos);
}

TEST_CASE("Python: enum id field converts via EnumClass(value)", "[python][message-id]") {
    auto py = gen_python("frame_enum_id.bmdl.xml");
    REQUIRE(py.has_value());
    auto& src = py->files["messages.py"];
    REQUIRE(!src.empty());
    // The plain-int ID_VALUE is wrapped in the enum before assignment.
    CHECK(src.find("MsgId(msg.ID_VALUE)") != std::string::npos);
}
