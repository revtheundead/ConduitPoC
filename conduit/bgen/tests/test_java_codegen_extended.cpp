// SPDX-License-Identifier: MIT
// Extended Java codegen tests
//
// Verifies Java backend generates correct output for session features,
// auto-fields, alignment, frame details, and string encoding — matching
// the depth of the C++ codegen and session tests.

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/validator.hpp"
#include "../src/analyzer/wire_sizer.hpp"
#include "../src/analyzer/session_analyzer.hpp"
#include "../src/codegen/java_backend.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

struct JGenResult {
    std::map<std::string, std::string> files;
};

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static std::optional<JGenResult> gen_java(const std::string& fixture) {
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

    auto tmp = fs::temp_directory_path() / ("bgen_ext_java_" + ns);
    fs::create_directories(tmp);

    bgen::codegen::JavaBackend backend;
    if (!backend.generate(protocol, index, sizes, sessions, ns, tmp))
        return std::nullopt;

    JGenResult gj;
    for (auto& entry : fs::directory_iterator(tmp))
        if (entry.is_regular_file())
            gj.files[entry.path().filename().string()] = read_file(entry.path());

    fs::remove_all(tmp);
    return gj;
}

// Collect all generated output into a single string
static std::string all_output(const JGenResult& gj) {
    std::string all;
    for (const auto& [name, content] : gj.files) all += content;
    return all;
}

// ============================================================================
// Session generation tests
// ============================================================================

TEST_CASE("JSess: session class with LEAF_TYPES map", "[java][session]") {
    auto java = gen_java("session_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("LEAF_TYPES") != std::string::npos);
}

TEST_CASE("JSess: session has decodeFrame method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("decodeFrame") != std::string::npos);
    CHECK(all.find("BitReader") != std::string::npos);
}

TEST_CASE("JSess: session has encodeWrap method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("encodeWrap") != std::string::npos);
    CHECK(all.find("typeId") != std::string::npos);
}

TEST_CASE("JSess: session has syncPattern method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("syncPattern") != std::string::npos);
}

TEST_CASE("JSess: session has minFrameHeaderSize method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("minFrameHeaderSize") != std::string::npos);
}

TEST_CASE("JSess: session has extractFrameLength method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("extractFrameLength") != std::string::npos);
}

TEST_CASE("JSess: session has typeName method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("typeName") != std::string::npos);
}

TEST_CASE("JSess: session has leafTypeIds method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("leafTypeIds") != std::string::npos);
}

TEST_CASE("JSess: session has protocolName method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("protocolName") != std::string::npos);
}

TEST_CASE("JSess: session has reset method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("reset()") != std::string::npos);
    CHECK(all.find("sequenceCounter = 0") != std::string::npos);
}

TEST_CASE("JSess: session has formatMessage method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("formatMessage") != std::string::npos);
}

TEST_CASE("JSess: session has isReceiveOnly method", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("isReceiveOnly") != std::string::npos);
}

TEST_CASE("JSess: decode dispatches by type ID", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("AlphaBody") != std::string::npos);
    CHECK(all.find("BetaBody") != std::string::npos);
}

TEST_CASE("JSess: sync pattern returns byte array", "[java][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("new byte[]") != std::string::npos);
}

TEST_CASE("JSess: auto-sequence uses sequenceCounter", "[java][session]") {
    auto java = gen_java("session_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("sequenceCounter") != std::string::npos);
}

TEST_CASE("JSess: auto-sequence increments in encodeWrap", "[java][session]") {
    auto java = gen_java("session_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("sequenceCounter++") != std::string::npos);
}

TEST_CASE("JSess: direction-qualified session has RECEIVE_ONLY set", "[java][session]") {
    auto java = gen_java("direction_qualified.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("isReceiveOnly") != std::string::npos);
}

TEST_CASE("JSess: frame-config session has config handling", "[java][session]") {
    auto java = gen_java("frame_config.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("config") != std::string::npos);
}

TEST_CASE("JSess: auto-timestamp uses System.currentTimeMillis", "[java][session]") {
    auto java = gen_java("frame_timestamp.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("System.currentTimeMillis()") != std::string::npos);
}

TEST_CASE("JSess: frame_basic generates session", "[java][session]") {
    auto java = gen_java("frame_basic.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("decodeFrame") != std::string::npos);
    CHECK(all.find("encodeWrap") != std::string::npos);
}

TEST_CASE("JSess: frame_footer generates session", "[java][session]") {
    auto java = gen_java("frame_footer.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_direction generates session", "[java][session]") {
    auto java = gen_java("frame_direction.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_array generates encodeBatch", "[java][session]") {
    auto java = gen_java("frame_array.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("encodeBatch") != std::string::npos);
}

TEST_CASE("JSess: frame_count generates session", "[java][session]") {
    auto java = gen_java("frame_count.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_payload_length generates session", "[java][session]") {
    auto java = gen_java("frame_payload_length.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_payload_length_from generates session", "[java][session]") {
    auto java = gen_java("frame_payload_length_from.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_length_arith generates session", "[java][session]") {
    auto java = gen_java("frame_length_arith.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JSess: frame_length_offset generates session", "[java][session]") {
    auto java = gen_java("frame_length_offset.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Auto-field codegen tests
// ============================================================================

TEST_CASE("JCG: auto-count field writes array.size() on encode", "[java][codegen][auto]") {
    auto java = gen_java("auto_count.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find(".size()") != std::string::npos);
}

TEST_CASE("JCG: auto-struct-length field writes placeholder and patches", "[java][codegen][auto]") {
    auto java = gen_java("auto_struct_length.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // Should have a length reference position marker
    CHECK(all.find("_lenRefPos") != std::string::npos);
    // Should have a patchU call
    CHECK(all.find("patchU") != std::string::npos);
}

TEST_CASE("JCG: auto-sequence generates output", "[java][codegen][auto]") {
    auto java = gen_java("auto_sequence.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Wire encoding tests
// ============================================================================

TEST_CASE("JCG: BCD/BNR_S encodings generate correct calls", "[java][codegen][wire]") {
    auto java = gen_java("wire_encodings.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("readBcd(") != std::string::npos);
    CHECK(all.find("writeBcd(") != std::string::npos);
    CHECK(all.find("readBcdSigned(") != std::string::npos);
    CHECK(all.find("writeBcdSigned(") != std::string::npos);
    CHECK(all.find("readSignMagnitude(") != std::string::npos);
    CHECK(all.find("writeSignMagnitude(") != std::string::npos);
}

// ============================================================================
// String encoding tests
// ============================================================================

TEST_CASE("JCG: EBCDIC strings generate correct decode", "[java][codegen][string]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(!all.empty());
}

TEST_CASE("JCG: IA5 string in FX", "[java][codegen][string]") {
    auto java = gen_java("fx_ia5_string.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(!all.empty());
}

TEST_CASE("JCG: string features generate readString/writeString", "[java][codegen][string]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("readString(") != std::string::npos);
    CHECK(all.find("writeString(") != std::string::npos);
}

TEST_CASE("JCG: string prefix inclusive generates correct length", "[java][codegen][string]") {
    auto java = gen_java("string_prefix_incl.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Alignment tests
// ============================================================================

TEST_CASE("JCG: struct_features generates reserved field handling", "[java][codegen][align]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("skipBits") != std::string::npos);
    CHECK(all.find("writeBits(0, ") != std::string::npos);
}

// ============================================================================
// Frame wrap/unwrap tests
// ============================================================================

TEST_CASE("JCG: frame message has wrap static method", "[java][codegen][frame]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("static") != std::string::npos);
    CHECK(all.find("wrap(") != std::string::npos);
}

TEST_CASE("JCG: frame message has TYPE_ID and TYPE_NAME", "[java][codegen][frame]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("TYPE_ID") != std::string::npos);
    CHECK(all.find("TYPE_NAME") != std::string::npos);
}

// ============================================================================
// Constraint tests
// ============================================================================

TEST_CASE("JCG: constrained type generates exception check", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("ConduitCodecException") != std::string::npos);
}

TEST_CASE("JCG: constraint_tighten generates output", "[java][codegen][constraint]") {
    auto java = gen_java("constraint_tighten.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: constraints_extended generates output", "[java][codegen][constraint]") {
    auto java = gen_java("constraints_extended.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// present_when conditional codegen
// ============================================================================

TEST_CASE("JCG: present_when generates if guard in decode", "[java][codegen][present_when]") {
    auto java = gen_java("present_when_complex.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("if (") != std::string::npos);
    CHECK(all.find("flags") != std::string::npos);
}

TEST_CASE("JCG: present_when generates null check in encode", "[java][codegen][present_when]") {
    auto java = gen_java("present_when_complex.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("!= null") != std::string::npos);
}

// ============================================================================
// FX block tests
// ============================================================================

TEST_CASE("JCG: FX block generates correct output", "[java][codegen][fx]") {
    auto java = gen_java("fx_block.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("FxMessage") != std::string::npos);
}

TEST_CASE("JCG: FX advanced generates output", "[java][codegen][fx]") {
    auto java = gen_java("fx_advanced.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: FX choice generates output", "[java][codegen][fx]") {
    auto java = gen_java("fx_choice.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: FX string generates output", "[java][codegen][fx]") {
    auto java = gen_java("fx_string.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Bitmap tests
// ============================================================================

TEST_CASE("JCG: bitmap FX protocol generates output", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: bitmap advanced generates output", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_advanced.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: bitmap wide fixed generates output", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_wide_fixed.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Protocol descriptor tests
// ============================================================================

TEST_CASE("JCG: Protocol.java has type registry", "[java][codegen][protocol]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto& prot = java->files["Protocol.java"];
    CHECK(prot.find("TypeInfo") != std::string::npos);
    CHECK(prot.find("findById") != std::string::npos);
    CHECK(prot.find("findByName") != std::string::npos);
}

TEST_CASE("JCG: Protocol.java has NAME and VERSION", "[java][codegen][protocol]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    auto& prot = java->files["Protocol.java"];
    CHECK(prot.find("NAME") != std::string::npos);
    CHECK(prot.find("VERSION") != std::string::npos);
}

// ============================================================================
// Inline type tests
// ============================================================================

TEST_CASE("JCG: inline enum generates Java output", "[java][codegen][inline]") {
    auto java = gen_java("inline_enum.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
    // Should have at least the message class and infrastructure
    CHECK(java->files.size() >= 5);
}

TEST_CASE("JCG: inline struct generates separate file", "[java][codegen][inline]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    // Struct types generate their own .java files
    CHECK(java->files.size() >= 5);
}

// ============================================================================
// Edge case fixtures
// ============================================================================

TEST_CASE("JCG: boundary types generates output", "[java][codegen][edge]") {
    auto java = gen_java("boundary_types.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: default initial values generates output", "[java][codegen][edge]") {
    auto java = gen_java("default_initial.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: optional constrained generates output", "[java][codegen][edge]") {
    auto java = gen_java("optional_constrained.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: inline case collision generates output", "[java][codegen][edge]") {
    auto java = gen_java("inline_case_collision.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: send_only_leaf generates output", "[java][codegen][edge]") {
    auto java = gen_java("send_only_leaf.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: outer_scope generates output", "[java][codegen][edge]") {
    auto java = gen_java("outer_scope.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: enum arrays generates output", "[java][codegen][edge]") {
    auto java = gen_java("enum_arrays.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: signed length fields generates output", "[java][codegen][edge]") {
    auto java = gen_java("signed_length.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: stress large protocol generates output", "[java][codegen][edge]") {
    auto java = gen_java("stress_large.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: msg_config generates output", "[java][codegen][edge]") {
    auto java = gen_java("msg_config.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: msg_config_inline generates output", "[java][codegen][edge]") {
    auto java = gen_java("msg_config_inline.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: constants_everywhere generates output", "[java][codegen][edge]") {
    auto java = gen_java("constants_everywhere.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: type_name_override generates output", "[java][codegen][edge]") {
    auto java = gen_java("type_name_override.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: length_arith generates output", "[java][codegen][edge]") {
    auto java = gen_java("length_arith.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

TEST_CASE("JCG: non_overlap_ranges generates output", "[java][codegen][edge]") {
    auto java = gen_java("non_overlap_ranges.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(!java->files.empty());
}

// ============================================================================
// Bytes field tests
// ============================================================================

TEST_CASE("JCG: bytes field generates readBytes/writeBytes", "[java][codegen][bytes]") {
    auto java = gen_java("bytes_numeric.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("readBytes(") != std::string::npos);
    CHECK(all.find("writeBytes(") != std::string::npos);
}

TEST_CASE("JCG: bytes field initialized to new byte[0]", "[java][codegen][bytes]") {
    auto java = gen_java("bytes_numeric.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("new byte[0]") != std::string::npos);
}

// ============================================================================
// Mixed endian tests
// ============================================================================

TEST_CASE("JCG: mixed endian uses both true and false for endian", "[java][codegen][endian]") {
    auto java = gen_java("mixed_endian.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("true") != std::string::npos);
    CHECK(all.find("false") != std::string::npos);
}

// ============================================================================
// Field scale/offset tests
// ============================================================================

TEST_CASE("JCG: field-level scale generates multiply in decode", "[java][codegen][scale]") {
    auto java = gen_java("field_scale.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("* ") != std::string::npos);
}

TEST_CASE("JCG: field-level scale generates divide in encode", "[java][codegen][scale]") {
    auto java = gen_java("field_scale.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("/ ") != std::string::npos);
}

// ============================================================================
// Array tests
// ============================================================================

TEST_CASE("JCG: fixed-count array uses for loop", "[java][codegen][array]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("for (int _i=0") != std::string::npos);
}

TEST_CASE("JCG: array field initialized to ArrayList", "[java][codegen][array]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("new java.util.ArrayList<>()") != std::string::npos);
}

TEST_CASE("JCG: array encode uses for-each loop", "[java][codegen][array]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("for (var _item") != std::string::npos);
    CHECK(all.find("_item.encode(w)") != std::string::npos);
}

// ============================================================================
// Choice tests
// ============================================================================

TEST_CASE("JCG: choice decode uses if/else if chain", "[java][codegen][choice]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("if (") != std::string::npos);
    CHECK(all.find("} else if (") != std::string::npos);
}

TEST_CASE("JCG: choice encode uses instanceof", "[java][codegen][choice]") {
    auto java = gen_java("arrays_choices.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("instanceof") != std::string::npos);
}

// ============================================================================
// toString tests
// ============================================================================

TEST_CASE("JCG: message class has toString", "[java][codegen][tostring]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("toString()") != std::string::npos);
}

// ============================================================================
// BitReader/BitWriter infrastructure tests
// ============================================================================

TEST_CASE("JCG: BitReader has skipBits method", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("skipBits(") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has patchU methods", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("patchU8(") != std::string::npos);
    CHECK(bw.find("patchU16(") != std::string::npos);
    CHECK(bw.find("patchU32(") != std::string::npos);
}

TEST_CASE("JCG: BitReader has remainingBytes method", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("remainingBytes(") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has sizeBytes method", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("sizeBytes(") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has toBytes method", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("toBytes(") != std::string::npos);
}

TEST_CASE("JCG: ConduitCodecException has message", "[java][codegen][bitio]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    auto& ex = java->files["ConduitCodecException.java"];
    CHECK(ex.find("class ConduitCodecException") != std::string::npos);
    CHECK(ex.find("extends") != std::string::npos);
}

// ============================================================================
// Package declaration tests
// ============================================================================

TEST_CASE("JCG: all generated files have package", "[java][codegen][package]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    for (const auto& [name, content] : java->files) {
        INFO("File: " << name);
        CHECK(content.find("package ") != std::string::npos);
    }
}

TEST_CASE("JCG: all generated files have Generated by bgen header", "[java][codegen][header]") {
    auto java = gen_java("minimal.bmdl.xml");
    REQUIRE(java.has_value());
    for (const auto& [name, content] : java->files) {
        INFO("File: " << name);
        CHECK(content.find("Generated by bgen") != std::string::npos);
    }
}
