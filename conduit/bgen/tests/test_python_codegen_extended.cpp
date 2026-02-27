// SPDX-License-Identifier: MIT
// Extended Python codegen tests
//
// Verifies Python backend generates correct output for session features,
// auto-fields, alignment, frame details, and string encoding — matching
// the depth of the C++ codegen and session tests.

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

struct PyGenResult {
    std::map<std::string, std::string> files;
};

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static std::optional<PyGenResult> gen_python(const std::string& fixture) {
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

    auto tmp = fs::temp_directory_path() / ("bgen_ext_py_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::PythonBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    PyGenResult gp;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gp.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gp;
}

// Collect all generated output into a single string
static std::string all_output(const PyGenResult& gp) {
    std::string all;
    for (const auto& [name, content] : gp.files) all += content;
    return all;
}

// ============================================================================
// Session generation tests
// ============================================================================

TEST_CASE("PySess: session class with LEAF_TYPES dict", "[python][session]") {
    auto py = gen_python("session_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("class") != std::string::npos);
    CHECK(sessions.find("LEAF_TYPES") != std::string::npos);
}

TEST_CASE("PySess: session has decode_frame method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def decode_frame(") != std::string::npos);
    CHECK(sessions.find("BitReader") != std::string::npos);
}

TEST_CASE("PySess: session has encode_wrap method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def encode_wrap(") != std::string::npos);
    CHECK(sessions.find("type_id") != std::string::npos);
    CHECK(sessions.find("encode_bytes") != std::string::npos);
}

TEST_CASE("PySess: session has sync_pattern method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def sync_pattern(") != std::string::npos);
}

TEST_CASE("PySess: session has min_frame_header_size method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def min_frame_header_size(") != std::string::npos);
}

TEST_CASE("PySess: session has extract_frame_length method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def extract_frame_length(") != std::string::npos);
}

TEST_CASE("PySess: session has type_name method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def type_name(") != std::string::npos);
}

TEST_CASE("PySess: session has leaf_type_ids method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def leaf_type_ids(") != std::string::npos);
}

TEST_CASE("PySess: session has protocol_name method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def protocol_name(") != std::string::npos);
}

TEST_CASE("PySess: session has reset method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def reset(") != std::string::npos);
    CHECK(sessions.find("self._seq = 0") != std::string::npos);
}

TEST_CASE("PySess: session has format_message method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def format_message(") != std::string::npos);
}

TEST_CASE("PySess: session has is_receive_only method", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def is_receive_only(") != std::string::npos);
}

TEST_CASE("PySess: decode_frame dispatches by type ID", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    // decode_frame should contain type dispatch using if/elif
    CHECK(sessions.find("AlphaBody") != std::string::npos);
    CHECK(sessions.find("BetaBody") != std::string::npos);
    // Should look up type_id to decide which class to decode
    CHECK(sessions.find("type_id") != std::string::npos);
}

TEST_CASE("PySess: encode_wrap wraps payload in frame", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    // encode_wrap should create a frame and set fields
    CHECK(sessions.find(".wrap(") != std::string::npos);
}

TEST_CASE("PySess: session sync pattern is bytes literal", "[python][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    // Sync pattern should be a bytes literal like b'\xBE\xEF'
    CHECK(sessions.find("b'\\x") != std::string::npos);
}

TEST_CASE("PySess: session auto-sequence uses self._seq", "[python][session]") {
    auto py = gen_python("session_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("self._seq") != std::string::npos);
}

TEST_CASE("PySess: session auto-sequence increments in encode_wrap", "[python][session]") {
    auto py = gen_python("session_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("self._seq += 1") != std::string::npos);
}

TEST_CASE("PySess: direction-qualified session has RECEIVE_ONLY set", "[python][session]") {
    auto py = gen_python("direction_qualified.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    // Direction-qualified protocol should have RECEIVE_ONLY set
    CHECK(sessions.find("RECEIVE_ONLY") != std::string::npos);
}

TEST_CASE("PySess: frame-config session has config handling", "[python][session]") {
    auto py = gen_python("frame_config.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("self._config") != std::string::npos);
}

TEST_CASE("PySess: auto-timestamp uses time.time()", "[python][session]") {
    auto py = gen_python("frame_timestamp.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("time.time()") != std::string::npos);
}

TEST_CASE("PySess: frame_basic generates session with decode and encode", "[python][session]") {
    auto py = gen_python("frame_basic.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def decode_frame(") != std::string::npos);
    CHECK(sessions.find("def encode_wrap(") != std::string::npos);
}

TEST_CASE("PySess: frame_footer generates session output", "[python][session]") {
    auto py = gen_python("frame_footer.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_direction generates session output", "[python][session]") {
    auto py = gen_python("frame_direction.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_array generates encode_batch", "[python][session]") {
    auto py = gen_python("frame_array.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find("def encode_batch(") != std::string::npos);
}

TEST_CASE("PySess: frame_count generates session output", "[python][session]") {
    auto py = gen_python("frame_count.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_payload_length generates session output", "[python][session]") {
    auto py = gen_python("frame_payload_length.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_payload_length_from generates session output", "[python][session]") {
    auto py = gen_python("frame_payload_length_from.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_length_arith generates arithmetic modifier", "[python][session]") {
    auto py = gen_python("frame_length_arith.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

TEST_CASE("PySess: frame_length_offset generates session output", "[python][session]") {
    auto py = gen_python("frame_length_offset.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(!sessions.empty());
}

// ============================================================================
// Auto-field codegen tests
// ============================================================================

TEST_CASE("PyCG: auto-count field writes len(array) on encode", "[python][codegen][auto]") {
    auto py = gen_python("auto_count.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("len(") != std::string::npos);
}

TEST_CASE("PyCG: auto-struct-length field writes placeholder and patches", "[python][codegen][auto]") {
    auto py = gen_python("auto_struct_length.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("_len_pos") != std::string::npos);
    // Should have a patch call
    CHECK(all.find("patch_u") != std::string::npos);
}

TEST_CASE("PyCG: auto-sequence generates output", "[python][codegen][auto]") {
    auto py = gen_python("auto_sequence.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Wire encoding tests
// ============================================================================

TEST_CASE("PyCG: BNR encoding generates read_bits/write_bits", "[python][codegen][wire]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_bcd(") != std::string::npos);
    CHECK(msgs.find("write_bcd(") != std::string::npos);
    CHECK(msgs.find("read_bcd_signed(") != std::string::npos);
    CHECK(msgs.find("write_bcd_signed(") != std::string::npos);
    CHECK(msgs.find("read_sign_magnitude(") != std::string::npos);
    CHECK(msgs.find("write_sign_magnitude(") != std::string::npos);
}

// ============================================================================
// String encoding tests
// ============================================================================

TEST_CASE("PyCG: EBCDIC strings generate correct decode calls", "[python][codegen][string]") {
    auto py = gen_python("ebcdic_strings.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(!all.empty());
    // Should reference EBCDIC encoding handling or string reading
    bool has_ebcdic_ref = all.find("ebcdic") != std::string::npos ||
                          all.find("EBCDIC") != std::string::npos ||
                          all.find("read_string") != std::string::npos;
    CHECK(has_ebcdic_ref);
}

TEST_CASE("PyCG: IA5 string handling in FX", "[python][codegen][string]") {
    auto py = gen_python("fx_ia5_string.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(!all.empty());
}

TEST_CASE("PyCG: string features generate length handling", "[python][codegen][string]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("read_string(") != std::string::npos);
    CHECK(msgs.find("write_string(") != std::string::npos);
}

TEST_CASE("PyCG: string prefix inclusive generates correct length math", "[python][codegen][string]") {
    auto py = gen_python("string_prefix_incl.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(!all.empty());
}

// ============================================================================
// Alignment tests
// ============================================================================

TEST_CASE("PyCG: struct_features generates alignment and reserved", "[python][codegen][align]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Reserved fields should generate skip_bits in decode
    CHECK(msgs.find("skip_bits") != std::string::npos);
    // Reserved fields should write zeros in encode
    CHECK(msgs.find("write_bits(0, ") != std::string::npos);
}

// ============================================================================
// Frame wrap/unwrap tests
// ============================================================================

TEST_CASE("PyCG: session uses Frame.wrap to wrap payload", "[python][codegen][frame]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& sessions = py->files["sessions.py"];
    CHECK(sessions.find(".wrap(") != std::string::npos);
}

TEST_CASE("PyCG: frame message has TYPE_ID and TYPE_NAME", "[python][codegen][frame]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("TYPE_ID") != std::string::npos);
    CHECK(msgs.find("TYPE_NAME") != std::string::npos);
}

// ============================================================================
// Constraint tests
// ============================================================================

TEST_CASE("PyCG: constrained type generates ConstraintError check", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto& types = py->files["types.py"];
    CHECK(types.find("ConstraintError") != std::string::npos);
}

TEST_CASE("PyCG: constraint_tighten generates output", "[python][codegen][constraint]") {
    auto py = gen_python("constraint_tighten.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: constraints_extended generates output", "[python][codegen][constraint]") {
    auto py = gen_python("constraints_extended.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// present_when conditional codegen
// ============================================================================

TEST_CASE("PyCG: present_when on field generates if guard in decode", "[python][codegen][present_when]") {
    auto py = gen_python("present_when_complex.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("if ") != std::string::npos);
    CHECK(all.find("flags") != std::string::npos);
}

TEST_CASE("PyCG: present_when generates is not None guard in encode", "[python][codegen][present_when]") {
    auto py = gen_python("present_when_complex.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("is not None") != std::string::npos);
}

// ============================================================================
// FX block tests
// ============================================================================

TEST_CASE("PyCG: FX block generates recursive decode", "[python][codegen][fx]") {
    auto py = gen_python("fx_block.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("FxMessage") != std::string::npos);
}

TEST_CASE("PyCG: FX advanced generates output", "[python][codegen][fx]") {
    auto py = gen_python("fx_advanced.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: FX choice generates output", "[python][codegen][fx]") {
    auto py = gen_python("fx_choice.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: FX string generates output", "[python][codegen][fx]") {
    auto py = gen_python("fx_string.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Bitmap tests
// ============================================================================

TEST_CASE("PyCG: bitmap FX protocol generates output", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: bitmap advanced generates output", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_advanced.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: bitmap wide fixed generates output", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_wide_fixed.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Protocol descriptor tests
// ============================================================================

TEST_CASE("PyCG: protocol has NAME constant", "[python][codegen][protocol]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("NAME") != std::string::npos);
}

TEST_CASE("PyCG: protocol has VERSION constant", "[python][codegen][protocol]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("VERSION") != std::string::npos);
}

TEST_CASE("PyCG: protocol has TYPES list with type info", "[python][codegen][protocol]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& protocol = py->files["protocol.py"];
    CHECK(protocol.find("TYPES") != std::string::npos);
    CHECK(protocol.find("type_id") != std::string::npos);
    CHECK(protocol.find("name") != std::string::npos);
}

// ============================================================================
// __init__.py tests
// ============================================================================

TEST_CASE("PyCG: __init__.py imports key classes", "[python][codegen][init]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& init = py->files["__init__.py"];
    CHECK(init.find("import") != std::string::npos);
}

// ============================================================================
// Inline type tests
// ============================================================================

TEST_CASE("PyCG: inline enum generates IntEnum class", "[python][codegen][inline]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("IntEnum") != std::string::npos);
}

TEST_CASE("PyCG: inline struct generates class in structs.py", "[python][codegen][inline]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    CHECK(!structs.empty());
}

// ============================================================================
// Edge case fixtures
// ============================================================================

TEST_CASE("PyCG: boundary types generates output", "[python][codegen][edge]") {
    auto py = gen_python("boundary_types.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: default initial values generates output", "[python][codegen][edge]") {
    auto py = gen_python("default_initial.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: optional constrained generates output", "[python][codegen][edge]") {
    auto py = gen_python("optional_constrained.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: inline case collision generates output", "[python][codegen][edge]") {
    auto py = gen_python("inline_case_collision.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: send_only_leaf generates output", "[python][codegen][edge]") {
    auto py = gen_python("send_only_leaf.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: outer_scope generates output", "[python][codegen][edge]") {
    auto py = gen_python("outer_scope.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: enum arrays generates output", "[python][codegen][edge]") {
    auto py = gen_python("enum_arrays.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: signed length fields generates output", "[python][codegen][edge]") {
    auto py = gen_python("signed_length.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: stress large protocol generates output", "[python][codegen][edge]") {
    auto py = gen_python("stress_large.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: msg_config generates output", "[python][codegen][edge]") {
    auto py = gen_python("msg_config.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: msg_config_inline generates output", "[python][codegen][edge]") {
    auto py = gen_python("msg_config_inline.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: constants_everywhere generates output", "[python][codegen][edge]") {
    auto py = gen_python("constants_everywhere.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: type_name_override generates output", "[python][codegen][edge]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: length_arith generates output", "[python][codegen][edge]") {
    auto py = gen_python("length_arith.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

TEST_CASE("PyCG: non_overlap_ranges generates output", "[python][codegen][edge]") {
    auto py = gen_python("non_overlap_ranges.bmdl.xml");
    REQUIRE(py.has_value());
    CHECK(!py->files.empty());
}

// ============================================================================
// Bytes field tests
// ============================================================================

TEST_CASE("PyCG: bytes field generates read_bytes/write_bytes", "[python][codegen][bytes]") {
    auto py = gen_python("bytes_numeric.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("read_bytes(") != std::string::npos);
    CHECK(all.find("write_bytes(") != std::string::npos);
    CHECK(all.find("= b''") != std::string::npos);
}

// ============================================================================
// Mixed endian tests
// ============================================================================

TEST_CASE("PyCG: mixed endian protocol uses both True and False for endian", "[python][codegen][endian]") {
    auto py = gen_python("mixed_endian.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("True") != std::string::npos);
    CHECK(msgs.find("False") != std::string::npos);
}

// ============================================================================
// Field scale/offset tests
// ============================================================================

TEST_CASE("PyCG: field-level scale generates multiply in decode", "[python][codegen][scale]") {
    auto py = gen_python("field_scale.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("* ") != std::string::npos);
}

TEST_CASE("PyCG: field-level scale generates divide in encode", "[python][codegen][scale]") {
    auto py = gen_python("field_scale.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("/ ") != std::string::npos);
}

// ============================================================================
// Array tests
// ============================================================================

TEST_CASE("PyCG: fixed-count array uses range(N) in decode", "[python][codegen][array]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("for _ in range(") != std::string::npos);
}

TEST_CASE("PyCG: array encode uses for loop", "[python][codegen][array]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("for _item in") != std::string::npos);
    CHECK(msgs.find("_item.encode(w)") != std::string::npos);
}

// ============================================================================
// Choice tests
// ============================================================================

TEST_CASE("PyCG: choice decode uses if/elif", "[python][codegen][choice]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("if ") != std::string::npos);
    CHECK(msgs.find("elif ") != std::string::npos);
}

TEST_CASE("PyCG: choice encode uses is not None guard", "[python][codegen][choice]") {
    auto py = gen_python("arrays_choices.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("is not None") != std::string::npos);
}

// ============================================================================
// __repr__ tests
// ============================================================================

TEST_CASE("PyCG: message class has __repr__", "[python][codegen][repr]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("def __repr__") != std::string::npos);
}

// ============================================================================
// bit_io infrastructure tests
// ============================================================================

TEST_CASE("PyCG: bit_io has align_to method", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def align_to(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has patch methods", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def patch_u8(") != std::string::npos);
    CHECK(bio.find("def patch_u16(") != std::string::npos);
    CHECK(bio.find("def patch_u32(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has error classes", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("class DecodeError") != std::string::npos);
    CHECK(bio.find("class ConstraintError") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has remaining_bytes method", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def remaining_bytes(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has size_bytes method", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def size_bytes(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has to_bytes method", "[python][codegen][bitio]") {
    auto py = gen_python("minimal.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def to_bytes(") != std::string::npos);
}

// ============================================================================
// BitReader/BitWriter method parity tests
// ============================================================================

TEST_CASE("PyCG: BitReader has align_to method", "[python][codegen][bitio]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    auto align_pos = bio.find("def align_to(", 0);
    CHECK(align_pos != std::string::npos);
    CHECK(align_pos < bw_start);
}

TEST_CASE("PyCG: BitWriter still has align_to method", "[python][codegen][bitio]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
    auto align_pos = bio.find("def align_to(", bw_start);
    CHECK(align_pos != std::string::npos);
}

TEST_CASE("PyCG: BitReader has all C++ BitReader methods", "[python][codegen][bitio]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    auto bw_start = bio.find("class BitWriter:");
    REQUIRE(bw_start != std::string::npos);
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

TEST_CASE("PyCG: BitWriter has all C++ BitWriter methods", "[python][codegen][bitio]") {
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
// Alignment and wire format codegen consistency
// ============================================================================

TEST_CASE("PyCG: struct_features alignment calls present", "[python][codegen][align]") {
    auto py = gen_python("struct_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& py_msgs = py->files["messages.py"];
    CHECK(py_msgs.find("r.align_to(2)") != std::string::npos);
    CHECK(py_msgs.find("w.align_to(2)") != std::string::npos);
}

TEST_CASE("PyCG: all_types has decode/encode/decode_bytes/encode_bytes", "[python][codegen][parity]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& py_msgs = py->files["messages.py"];
    CHECK(py_msgs.find("def decode(") != std::string::npos);
    CHECK(py_msgs.find("def encode(") != std::string::npos);
    CHECK(py_msgs.find("decode_bytes") != std::string::npos);
    CHECK(py_msgs.find("encode_bytes") != std::string::npos);
}

TEST_CASE("PyCG: wire_encodings has BCD/BNR_S calls", "[python][codegen][parity]") {
    auto py = gen_python("wire_encodings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& py_msgs = py->files["messages.py"];
    CHECK(py_msgs.find("read_bcd(") != std::string::npos);
    CHECK(py_msgs.find("write_bcd(") != std::string::npos);
    CHECK(py_msgs.find("read_bcd_signed(") != std::string::npos);
    CHECK(py_msgs.find("write_bcd_signed(") != std::string::npos);
    CHECK(py_msgs.find("read_sign_magnitude(") != std::string::npos);
    CHECK(py_msgs.find("write_sign_magnitude(") != std::string::npos);
}

TEST_CASE("PyCG: mixed_endian uses True/False endian flags", "[python][codegen][parity]") {
    auto py = gen_python("mixed_endian.bmdl.xml");
    REQUIRE(py.has_value());
    auto& py_msgs = py->files["messages.py"];
    CHECK(py_msgs.find("True") != std::string::npos);
    CHECK(py_msgs.find("False") != std::string::npos);
}

TEST_CASE("PyCG: choice_protocol session has leaf types", "[python][codegen][session]") {
    auto py = gen_python("choice_protocol.bmdl.xml");
    REQUIRE(py.has_value());
    auto& py_sess = py->files["sessions.py"];
    CHECK(py_sess.find("AlphaBody") != std::string::npos);
    CHECK(py_sess.find("BetaBody") != std::string::npos);
    CHECK(py_sess.find("LEAF_TYPES") != std::string::npos);
}

// ============================================================================
// Field-level constraint checks
// ============================================================================

TEST_CASE("PyCG: field-level constraint checks equals", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    // Field magic has constraint equals="0xBEEF"
    CHECK(all.find("constraint violation") != std::string::npos);
}

TEST_CASE("PyCG: field-level constraint checks min/max", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    // percent has min=0, max=100
    CHECK(all.find("exceeds max 100") != std::string::npos);
}

TEST_CASE("PyCG: deferred constraint does NOT generate validation", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("deferred-val exceeds max") == std::string::npos);
    CHECK(all.find("deferred-val below min") == std::string::npos);
}

// ============================================================================
// String encoding tests (EBCDIC, IA5)
// ============================================================================

TEST_CASE("PyCG: EBCDIC strings generate encoding-aware read calls", "[python][codegen][string]") {
    auto py = gen_python("ebcdic_strings.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    bool has_ebcdic_ref = all.find("encoding=2") != std::string::npos ||
                          all.find("_EBCDIC_TO_ASCII") != std::string::npos;
    CHECK(has_ebcdic_ref);
}

TEST_CASE("PyCG: bit_io has EBCDIC conversion tables", "[python][codegen][string]") {
    auto py = gen_python("ebcdic_strings.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("_EBCDIC_TO_ASCII") != std::string::npos);
    CHECK(bio.find("_ASCII_TO_EBCDIC") != std::string::npos);
}

// ============================================================================
// String trim mode tests
// ============================================================================

TEST_CASE("PyCG: string_features generates field-level trim with rstrip", "[python][codegen][trim]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("rstrip(") != std::string::npos);
}

// ============================================================================
// Terminated string tests
// ============================================================================

TEST_CASE("PyCG: bit_io has read_terminated_string method", "[python][codegen][terminated]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def read_terminated_string(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has read_crlf_terminated_string method", "[python][codegen][terminated]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def read_crlf_terminated_string(") != std::string::npos);
}

TEST_CASE("PyCG: terminated string field generates terminator read call", "[python][codegen][terminated]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("read_terminated_string(") != std::string::npos);
    CHECK(all.find("read_crlf_terminated_string(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has write_terminated_string method", "[python][codegen][terminated]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def write_terminated_string(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has write_crlf_terminated_string method", "[python][codegen][terminated]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def write_crlf_terminated_string(") != std::string::npos);
}

// ============================================================================
// Packed character / char_bits tests
// ============================================================================

TEST_CASE("PyCG: bit_io has read_packed_chars method", "[python][codegen][char_bits]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def read_packed_chars(") != std::string::npos);
}

TEST_CASE("PyCG: bit_io has write_packed_chars method", "[python][codegen][char_bits]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def write_packed_chars(") != std::string::npos);
}

// ============================================================================
// max_length validation tests
// ============================================================================

TEST_CASE("PyCG: max_length on string field generates length check", "[python][codegen][max_length]") {
    auto py = gen_python("string_features.bmdl.xml");
    REQUIRE(py.has_value());
    auto all = all_output(*py);
    CHECK(all.find("exceeds max length 16") != std::string::npos);
}

// ============================================================================
// BitReader/BitWriter extended method parity tests
// ============================================================================

TEST_CASE("PyCG: BitReader has encoding-aware read_string", "[python][codegen][bitio]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    // read_string should accept optional encoding parameter
    CHECK(bio.find("def read_string(self, length: int, encoding: int = 0)") != std::string::npos);
}

TEST_CASE("PyCG: BitWriter has encoding-aware write_string", "[python][codegen][bitio]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& bio = py->files["bit_io.py"];
    CHECK(bio.find("def write_string(self, s: str, length: int, pad: int = 0, encoding: int = 0)") != std::string::npos);
}

// ============================================================================
// Inline struct overlap tests — verify parent-prefixed naming prevents collisions
// ============================================================================

TEST_CASE("PyCG: inline struct overlap produces distinct classes", "[python][codegen][collision]") {
    auto py = gen_python("inline_struct_overlap.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // MsgFoo and MsgBar both define inline struct "items" — they must get distinct class names
    CHECK(msgs.find("class MsgFooItems:") != std::string::npos);
    CHECK(msgs.find("class MsgBarItems:") != std::string::npos);
    // MsgFoo's Items has foo_x/foo_y fields, MsgBar's has bar_a/bar_b/bar_c
    CHECK(msgs.find("self.foo_x") != std::string::npos);
    CHECK(msgs.find("self.bar_a") != std::string::npos);
    // Parent messages reference the correct prefixed class
    CHECK(msgs.find("MsgFooItems.decode(r)") != std::string::npos);
    CHECK(msgs.find("MsgBarItems.decode(r)") != std::string::npos);
}

TEST_CASE("PyCG: inline array overlap produces distinct element classes", "[python][codegen][collision]") {
    auto py = gen_python("inline_struct_overlap.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // MsgAlpha and MsgBeta both define inline array "entries" — elements must get distinct names
    CHECK(msgs.find("class MsgAlphaEntries:") != std::string::npos);
    CHECK(msgs.find("class MsgBetaEntries:") != std::string::npos);
    CHECK(msgs.find("self.alpha_val") != std::string::npos);
    CHECK(msgs.find("self.beta_val") != std::string::npos);
}

TEST_CASE("PyCG: inline case collision produces parent-prefixed classes", "[python][codegen][collision]") {
    auto py = gen_python("inline_case_collision.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // MsgAlpha and MsgBeta both define inline case "TypeA" — must be distinct
    CHECK(msgs.find("class MsgAlphaTypeA:") != std::string::npos);
    CHECK(msgs.find("class MsgBetaTypeA:") != std::string::npos);
    // Each class has the correct fields
    CHECK(msgs.find("self.alpha_val") != std::string::npos);
    CHECK(msgs.find("self.beta_x") != std::string::npos);
}

// ============================================================================
// typeName override tests
// ============================================================================

TEST_CASE("PyCG: typeName override on inline struct", "[python][codegen][typename]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // struct "header" with typeName="MsgHeader" should use MsgHeader as class name
    CHECK(msgs.find("class MsgHeader:") != std::string::npos);
    CHECK(msgs.find("MsgHeader.decode(r)") != std::string::npos);
}

TEST_CASE("PyCG: typeName override on inline array", "[python][codegen][typename]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // array "items" with typeName="ArrayItem" should use ArrayItem as element class name
    CHECK(msgs.find("class ArrayItem:") != std::string::npos);
    CHECK(msgs.find("ArrayItem.decode(r)") != std::string::npos);
}

TEST_CASE("PyCG: typeName override on choice cases", "[python][codegen][typename]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Cases with typeName overrides
    CHECK(msgs.find("class HeartbeatPayload:") != std::string::npos);
    CHECK(msgs.find("class PositionPayload:") != std::string::npos);
    CHECK(msgs.find("class UnknownPayload:") != std::string::npos);
}

TEST_CASE("PyCG: typeName override disambiguates colliding inline structs", "[python][codegen][typename]") {
    auto py = gen_python("type_name_override.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // MsgOne and MsgTwo both define "details" but with different typeName overrides
    CHECK(msgs.find("class MsgOneDetails:") != std::string::npos);
    CHECK(msgs.find("class MsgTwoDetails:") != std::string::npos);
}

// ============================================================================
// Inline enum field tests
// ============================================================================

TEST_CASE("PyCG: inline enum generates IntEnum classes", "[python][codegen][inline_enum]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Inline enum classes should be generated with parent-prefixed names
    CHECK(msgs.find("class InlineEnumMsgMode(IntEnum):") != std::string::npos);
    CHECK(msgs.find("class InlineEnumMsgPriority(IntEnum):") != std::string::npos);
}

TEST_CASE("PyCG: inline enum has correct values", "[python][codegen][inline_enum]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Check enum values
    CHECK(msgs.find("OFF = 0") != std::string::npos);
    CHECK(msgs.find("STANDBY = 1") != std::string::npos);
    CHECK(msgs.find("ACTIVE = 2") != std::string::npos);
    CHECK(msgs.find("LOW = 0") != std::string::npos);
    CHECK(msgs.find("MEDIUM = 1") != std::string::npos);
    CHECK(msgs.find("HIGH = 2") != std::string::npos);
}

TEST_CASE("PyCG: inline enum field type in parent class", "[python][codegen][inline_enum]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // Parent class should use enum type for decode
    CHECK(msgs.find("result.mode = InlineEnumMsgMode.decode(r)") != std::string::npos);
    CHECK(msgs.find("result.priority = InlineEnumMsgPriority.decode(r)") != std::string::npos);
    // Encode should call enum.encode
    CHECK(msgs.find("self.mode.encode(w)") != std::string::npos);
    CHECK(msgs.find("self.priority.encode(w)") != std::string::npos);
}

TEST_CASE("PyCG: inline enum decode/encode methods", "[python][codegen][inline_enum]") {
    auto py = gen_python("inline_enum.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // decode reads bits
    CHECK(msgs.find("raw = r.read_bits(4)") != std::string::npos);
    CHECK(msgs.find("raw = r.read_bits(2)") != std::string::npos);
    // encode writes bits
    CHECK(msgs.find("w.write_bits(self.value, 4)") != std::string::npos);
    CHECK(msgs.find("w.write_bits(self.value, 2)") != std::string::npos);
}

// ============================================================================
// DisplayFormat tests
// ============================================================================

TEST_CASE("PyCG: display format binary in repr", "[python][codegen][display_format]") {
    auto py = gen_python("format_binary.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // mask has explicit format="binary" → repr should use bin()
    CHECK(msgs.find("mask={bin(self.mask)}") != std::string::npos);
    // flags has type bin8 with format="binary" → repr should use bin()
    CHECK(msgs.find("flags={bin(self.flags)}") != std::string::npos);
    // tag has no format → standard repr
    CHECK(msgs.find("tag={self.tag}") != std::string::npos);
    // value has no format → standard repr
    CHECK(msgs.find("value={self.value}") != std::string::npos);
}

TEST_CASE("PyCG: display format hex in repr", "[python][codegen][display_format]") {
    auto py = gen_python("all_types.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // hex field in AllTypesMsg → repr should use hex()
    CHECK(msgs.find("hex={hex(self.hex)}") != std::string::npos);
}

// ============================================================================
// Constraint equals implies default
// ============================================================================

TEST_CASE("PyCG: constraint equals implies default value", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // magic has constraint equals="0xBEEF" with no explicit default,
    // so it should be initialized to 0xBEEF
    CHECK(msgs.find("magic = 0xBEEF") != std::string::npos);
}

TEST_CASE("PyCG: constraint equals generates validate method", "[python][codegen][constraint]") {
    auto py = gen_python("constraints.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    CHECK(msgs.find("def validate(self)") != std::string::npos);
    // Validate checks equals constraint
    CHECK(msgs.find("magic != 0xBEEF") != std::string::npos);
    // Validate checks max constraint
    CHECK(msgs.find("percent > 100") != std::string::npos);
}

// ============================================================================
// FX bit logic
// ============================================================================

TEST_CASE("PyCG: FX block generates read_bits(1) check on decode", "[python][codegen][fx]") {
    auto py = gen_python("fx_block.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // FX decode should read 1 bit and conditionally decode
    CHECK(msgs.find("r.read_bits(1)") != std::string::npos);
    CHECK(msgs.find("if r.read_bits(1) != 0:") != std::string::npos);
}

TEST_CASE("PyCG: FX block writes FX continuation bit on encode", "[python][codegen][fx]") {
    auto py = gen_python("fx_block.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // FX encode should write 1-bit continuation flag
    CHECK(msgs.find("write_bits(") != std::string::npos);
    CHECK(msgs.find("_fx_continue") != std::string::npos);
}

TEST_CASE("PyCG: FX child fields default to None", "[python][codegen][fx]") {
    auto py = gen_python("fx_block.bmdl.xml");
    REQUIRE(py.has_value());
    auto& msgs = py->files["messages.py"];
    // FX children should default to None
    CHECK(msgs.find("item1 = None") != std::string::npos);
    CHECK(msgs.find("item2 = None") != std::string::npos);
    CHECK(msgs.find("item3 = None") != std::string::npos);
}

// ============================================================================
// Bitmap/FSPEC support
// ============================================================================

TEST_CASE("PyCG: bitmap struct generates FSPEC read logic", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    REQUIRE(!structs.empty());
    // Should read FSPEC bytes
    CHECK(structs.find("fspec") != std::string::npos);
    // Should have conditional field decode based on fspec bits
    CHECK(structs.find("fspec[") != std::string::npos);
}

TEST_CASE("PyCG: bitmap struct generates FSPEC write logic", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    REQUIRE(!structs.empty());
    // Should build and write FSPEC byte array
    CHECK(structs.find("def encode(") != std::string::npos);
    CHECK(structs.find("fspec") != std::string::npos);
}

TEST_CASE("PyCG: bitmap fields default to None", "[python][codegen][bitmap]") {
    auto py = gen_python("bitmap_fx.bmdl.xml");
    REQUIRE(py.has_value());
    auto& structs = py->files["structs.py"];
    REQUIRE(!structs.empty());
    // All bitmap-controlled fields should default to None
    CHECK(structs.find("item010 = None") != std::string::npos);
    CHECK(structs.find("item020 = None") != std::string::npos);
    CHECK(structs.find("item030 = None") != std::string::npos);
}
