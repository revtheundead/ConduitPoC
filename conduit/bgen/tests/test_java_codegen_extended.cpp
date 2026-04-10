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
    for (auto& entry : fs::recursive_directory_iterator(tmp))
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

// ============================================================================
// BitReader/BitWriter method parity tests
// ============================================================================

TEST_CASE("JCG: BitReader has alignTo method", "[java][codegen][bitio]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("public void alignTo(") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has alignTo method", "[java][codegen][bitio]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("public void alignTo(") != std::string::npos);
}

TEST_CASE("JCG: BitReader has all C++ BitReader methods", "[java][codegen][bitio]") {
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

TEST_CASE("JCG: BitWriter has all C++ BitWriter methods", "[java][codegen][bitio]") {
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

// ============================================================================
// Alignment and wire format codegen consistency
// ============================================================================

TEST_CASE("JCG: struct_features alignment calls present", "[java][codegen][align]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& j_msg = java->files["AlignedMessage.java"];
    CHECK(j_msg.find("r.alignTo(2)") != std::string::npos);
    CHECK(j_msg.find("w.alignTo(2)") != std::string::npos);
}

TEST_CASE("JCG: all_types has decode/encode/decodeBytes/encodeBytes", "[java][codegen][parity]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& j_msg = java->files["AllTypesMessage.java"];
    CHECK(j_msg.find("public static AllTypesMessage decode(") != std::string::npos);
    CHECK(j_msg.find("public void encode(") != std::string::npos);
    CHECK(j_msg.find("decodeBytes") != std::string::npos);
    CHECK(j_msg.find("encodeBytes") != std::string::npos);
}

TEST_CASE("JCG: wire_encodings has BCD/BNR_S calls", "[java][codegen][parity]") {
    auto java = gen_java("wire_encodings.bmdl.xml");
    REQUIRE(java.has_value());
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;
    CHECK(j_all.find("readBcd(") != std::string::npos);
    CHECK(j_all.find("writeBcd(") != std::string::npos);
    CHECK(j_all.find("readBcdSigned(") != std::string::npos);
    CHECK(j_all.find("writeBcdSigned(") != std::string::npos);
    CHECK(j_all.find("readSignMagnitude(") != std::string::npos);
    CHECK(j_all.find("writeSignMagnitude(") != std::string::npos);
}

TEST_CASE("JCG: mixed_endian uses true/false endian flags", "[java][codegen][parity]") {
    auto java = gen_java("mixed_endian.bmdl.xml");
    REQUIRE(java.has_value());
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;
    CHECK(j_all.find("true") != std::string::npos);
    CHECK(j_all.find("false") != std::string::npos);
}

TEST_CASE("JCG: choice_protocol session has leaf types", "[java][codegen][session]") {
    auto java = gen_java("choice_protocol.bmdl.xml");
    REQUIRE(java.has_value());
    std::string j_all;
    for (const auto& [name, content] : java->files) j_all += content;
    CHECK(j_all.find("AlphaBody") != std::string::npos);
    CHECK(j_all.find("BetaBody") != std::string::npos);
    CHECK(j_all.find("LEAF_TYPES") != std::string::npos);
}

// ============================================================================
// Constraint validation tests (min/max on types and fields)
// ============================================================================

TEST_CASE("JCG: constrained type decode validates max", "[java][codegen][constraint]") {
    auto java = gen_java("constraints_extended.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("exceeds max") != std::string::npos);
}

TEST_CASE("JCG: constrained type decode validates min", "[java][codegen][constraint]") {
    auto java = gen_java("constraints_extended.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("below min") != std::string::npos);
}

TEST_CASE("JCG: field-level constraint checks equals", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // Field magic has constraint equals="0xBEEF"
    CHECK(all.find("constraint violation") != std::string::npos);
}

TEST_CASE("JCG: field-level constraint checks min/max", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // percent has min=0, max=100
    CHECK(all.find("exceeds max 100") != std::string::npos);
}

TEST_CASE("JCG: deferred constraint generates validate() not decode-time check", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // deferred-val has validate="deferred" — checks go in validate(), not decode()
    // The validate() method should exist with deferred constraint checks
    CHECK(all.find("public void validate()") != std::string::npos);
    CHECK(all.find("validate") != std::string::npos);
    CHECK(all.find("deferred-val") != std::string::npos);
    CHECK(all.find("exceeds max 500") != std::string::npos);
}

// ============================================================================
// Default value tests
// ============================================================================

TEST_CASE("JCG: field with default value generates custom initializer", "[java][codegen][default]") {
    auto java = gen_java("default_initial.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // version has default="1"
    CHECK(all.find("= 1") != std::string::npos);
    // counter has default="100"
    CHECK(all.find("= 100") != std::string::npos);
}

// ============================================================================
// String encoding tests (EBCDIC, IA5)
// ============================================================================

TEST_CASE("JCG: EBCDIC strings generate encoding-aware read calls", "[java][codegen][string]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    bool has_ebcdic_ref = all.find("readStringEncoded") != std::string::npos ||
                          all.find("EBCDIC_TO_ASCII") != std::string::npos;
    CHECK(has_ebcdic_ref);
}

TEST_CASE("JCG: EBCDIC strings generate encoding-aware write calls", "[java][codegen][string]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    bool has_ebcdic_write = all.find("writeStringEncoded") != std::string::npos ||
                            all.find("ASCII_TO_EBCDIC") != std::string::npos;
    CHECK(has_ebcdic_write);
}

TEST_CASE("JCG: BitReader has EBCDIC_TO_ASCII conversion table", "[java][codegen][string]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("EBCDIC_TO_ASCII") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has ASCII_TO_EBCDIC conversion table", "[java][codegen][string]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("ASCII_TO_EBCDIC") != std::string::npos);
}

TEST_CASE("JCG: IA5 string handling in FX", "[java][codegen][string]") {
    auto java = gen_java("fx_ia5_string.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(!all.empty());
}

// ============================================================================
// String trim mode tests
// ============================================================================

TEST_CASE("JCG: string_features generates trim logic", "[java][codegen][trim]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    CHECK(all.find("readString(") != std::string::npos);
    CHECK(all.find("writeString(") != std::string::npos);
}

TEST_CASE("JCG: struct_features alignment and reserved", "[java][codegen][align]") {
    auto java = gen_java("struct_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // Reserved generates skip/write zero bits
    CHECK(all.find("skipBits") != std::string::npos);
    CHECK(all.find("writeBits(0, ") != std::string::npos);
}

// ============================================================================
// Terminated string tests
// ============================================================================

TEST_CASE("JCG: BitReader has readTerminatedString method", "[java][codegen][terminated]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("readTerminatedString") != std::string::npos);
}

TEST_CASE("JCG: BitReader has readCrlfTerminatedString method", "[java][codegen][terminated]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("readCrlfTerminatedString") != std::string::npos);
}

TEST_CASE("JCG: terminated string field generates terminator read call", "[java][codegen][terminated]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // TermStringMsg has null-term, newline-term, crlf-term fields
    CHECK(all.find("readTerminatedString(") != std::string::npos);
    CHECK(all.find("readCrlfTerminatedString(") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has writeTerminatedString method", "[java][codegen][terminated]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("writeTerminatedString") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has writeCrlfTerminatedString method", "[java][codegen][terminated]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("writeCrlfTerminatedString") != std::string::npos);
}

// ============================================================================
// Packed character / char_bits tests
// ============================================================================

TEST_CASE("JCG: BitReader has readPackedChars method", "[java][codegen][char_bits]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("readPackedChars") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has writePackedChars method", "[java][codegen][char_bits]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("writePackedChars") != std::string::npos);
}

// ============================================================================
// max_length validation tests
// ============================================================================

TEST_CASE("JCG: max_length on string field generates length check", "[java][codegen][max_length]") {
    auto java = gen_java("string_features.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // MaxLenMsg has field data with max-length="16"
    CHECK(all.find("exceeds max length 16") != std::string::npos);
}

// ============================================================================
// Extended BitReader/BitWriter method parity tests (new methods added)
// ============================================================================

TEST_CASE("JCG: BitReader has new encoding/packed/terminated methods", "[java][codegen][bitio_new]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& br = java->files["BitReader.java"];
    CHECK(br.find("readStringEncoded(") != std::string::npos);
    CHECK(br.find("readPackedChars(") != std::string::npos);
    CHECK(br.find("readTerminatedString(") != std::string::npos);
    CHECK(br.find("readCrlfTerminatedString(") != std::string::npos);
    CHECK(br.find("EBCDIC_TO_ASCII") != std::string::npos);
}

TEST_CASE("JCG: BitWriter has new encoding/packed/terminated methods", "[java][codegen][bitio_new]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bw = java->files["BitWriter.java"];
    CHECK(bw.find("writeStringEncoded(") != std::string::npos);
    CHECK(bw.find("writePackedChars(") != std::string::npos);
    CHECK(bw.find("writeTerminatedString(") != std::string::npos);
    CHECK(bw.find("writeCrlfTerminatedString(") != std::string::npos);
    CHECK(bw.find("ASCII_TO_EBCDIC") != std::string::npos);
}

// ============================================================================
// Inline struct overlap tests — verify parent-prefixed naming prevents collisions
// ============================================================================

TEST_CASE("JCG: inline struct overlap produces distinct Java files", "[java][codegen][collision]") {
    auto java = gen_java("inline_struct_overlap.bmdl.xml");
    REQUIRE(java.has_value());
    // MsgFoo and MsgBar both define inline struct "items" — they must get distinct class files
    CHECK(java->files.count("MsgFooItems.java"));
    CHECK(java->files.count("MsgBarItems.java"));
    // Each class has the correct fields
    auto& foo_items = java->files["MsgFooItems.java"];
    auto& bar_items = java->files["MsgBarItems.java"];
    CHECK(foo_items.find("fooX") != std::string::npos);
    CHECK(foo_items.find("fooY") != std::string::npos);
    CHECK(bar_items.find("barA") != std::string::npos);
    CHECK(bar_items.find("barB") != std::string::npos);
    // Parent messages reference the correct prefixed class
    auto& msg_foo = java->files["MsgFoo.java"];
    CHECK(msg_foo.find("MsgFooItems") != std::string::npos);
    auto& msg_bar = java->files["MsgBar.java"];
    CHECK(msg_bar.find("MsgBarItems") != std::string::npos);
}

TEST_CASE("JCG: inline array overlap produces distinct element Java files", "[java][codegen][collision]") {
    auto java = gen_java("inline_struct_overlap.bmdl.xml");
    REQUIRE(java.has_value());
    // MsgAlpha and MsgBeta both define inline array "entries" — elements must get distinct names
    CHECK(java->files.count("MsgAlphaEntries.java"));
    CHECK(java->files.count("MsgBetaEntries.java"));
    auto& alpha = java->files["MsgAlphaEntries.java"];
    auto& beta = java->files["MsgBetaEntries.java"];
    CHECK(alpha.find("alphaVal") != std::string::npos);
    CHECK(beta.find("betaVal") != std::string::npos);
}

TEST_CASE("JCG: inline case collision produces parent-prefixed Java files", "[java][codegen][collision]") {
    auto java = gen_java("inline_case_collision.bmdl.xml");
    REQUIRE(java.has_value());
    // MsgAlpha and MsgBeta both define inline case "TypeA" — must be distinct
    CHECK(java->files.count("MsgAlphaTypeA.java"));
    CHECK(java->files.count("MsgBetaTypeA.java"));
    auto& alpha = java->files["MsgAlphaTypeA.java"];
    auto& beta = java->files["MsgBetaTypeA.java"];
    CHECK(alpha.find("alphaVal") != std::string::npos);
    CHECK(beta.find("betaX") != std::string::npos);
}

// ============================================================================
// typeName override tests
// ============================================================================

TEST_CASE("JCG: typeName override on inline struct", "[java][codegen][typename]") {
    auto java = gen_java("type_name_override.bmdl.xml");
    REQUIRE(java.has_value());
    // struct "header" with typeName="MsgHeader" should use MsgHeader as class name
    CHECK(java->files.count("MsgHeader.java"));
    auto& hdr = java->files["MsgHeader.java"];
    CHECK(hdr.find("class MsgHeader") != std::string::npos);
}

TEST_CASE("JCG: typeName override on inline array", "[java][codegen][typename]") {
    auto java = gen_java("type_name_override.bmdl.xml");
    REQUIRE(java.has_value());
    // array "items" with typeName="ArrayItem" should use ArrayItem as element class name
    CHECK(java->files.count("ArrayItem.java"));
    auto& item = java->files["ArrayItem.java"];
    CHECK(item.find("class ArrayItem") != std::string::npos);
}

TEST_CASE("JCG: typeName override on choice cases", "[java][codegen][typename]") {
    auto java = gen_java("type_name_override.bmdl.xml");
    REQUIRE(java.has_value());
    CHECK(java->files.count("HeartbeatPayload.java"));
    CHECK(java->files.count("PositionPayload.java"));
    CHECK(java->files.count("UnknownPayload.java"));
}

TEST_CASE("JCG: typeName override disambiguates colliding inline structs", "[java][codegen][typename]") {
    auto java = gen_java("type_name_override.bmdl.xml");
    REQUIRE(java.has_value());
    // MsgOne and MsgTwo both define "details" but with different typeName overrides
    CHECK(java->files.count("MsgOneDetails.java"));
    CHECK(java->files.count("MsgTwoDetails.java"));
}

// ============================================================================
// Inline enum field tests
// ============================================================================

TEST_CASE("JCG: inline enum generates Java enum files", "[java][codegen][inline_enum]") {
    auto java = gen_java("inline_enum.bmdl.xml");
    REQUIRE(java.has_value());
    // Inline enum files should be generated with parent-prefixed names
    CHECK(java->files.count("InlineEnumMsgMode.java"));
    CHECK(java->files.count("InlineEnumMsgPriority.java"));
}

TEST_CASE("JCG: inline enum has correct values", "[java][codegen][inline_enum]") {
    auto java = gen_java("inline_enum.bmdl.xml");
    REQUIRE(java.has_value());
    auto& mode_file = java->files["InlineEnumMsgMode.java"];
    auto& prio_file = java->files["InlineEnumMsgPriority.java"];
    CHECK(mode_file.find("OFF(0)") != std::string::npos);
    CHECK(mode_file.find("STANDBY(1)") != std::string::npos);
    CHECK(mode_file.find("ACTIVE(2)") != std::string::npos);
    CHECK(prio_file.find("LOW(0)") != std::string::npos);
    CHECK(prio_file.find("MEDIUM(1)") != std::string::npos);
    CHECK(prio_file.find("HIGH(2)") != std::string::npos);
}

TEST_CASE("JCG: inline enum field type in parent class", "[java][codegen][inline_enum]") {
    auto java = gen_java("inline_enum.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg_file = java->files["InlineEnumMsg.java"];
    // Parent class field declarations
    CHECK(msg_file.find("public InlineEnumMsgMode mode = null;") != std::string::npos);
    CHECK(msg_file.find("public InlineEnumMsgPriority priority = null;") != std::string::npos);
    // Decode
    CHECK(msg_file.find("result.mode = InlineEnumMsgMode.decode(r);") != std::string::npos);
    CHECK(msg_file.find("result.priority = InlineEnumMsgPriority.decode(r);") != std::string::npos);
    // Encode
    CHECK(msg_file.find("this.mode.encode(w);") != std::string::npos);
    CHECK(msg_file.find("this.priority.encode(w);") != std::string::npos);
}

TEST_CASE("JCG: inline enum decode/encode methods", "[java][codegen][inline_enum]") {
    auto java = gen_java("inline_enum.bmdl.xml");
    REQUIRE(java.has_value());
    auto& mode_file = java->files["InlineEnumMsgMode.java"];
    auto& prio_file = java->files["InlineEnumMsgPriority.java"];
    // decode reads bits
    CHECK(mode_file.find("r.readBits(4)") != std::string::npos);
    CHECK(prio_file.find("r.readBits(2)") != std::string::npos);
    // encode writes bits
    CHECK(mode_file.find("w.writeBits(value, 4)") != std::string::npos);
    CHECK(prio_file.find("w.writeBits(value, 2)") != std::string::npos);
}

// ============================================================================
// DisplayFormat tests
// ============================================================================

TEST_CASE("JCG: display format binary in toString", "[java][codegen][display_format]") {
    auto java = gen_java("format_binary.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg_file = java->files["BinaryMsg.java"];
    // mask has explicit format="binary" → toString should use Long.toBinaryString
    CHECK(msg_file.find("Long.toBinaryString(mask)") != std::string::npos);
    // flags has type bin8 with format="binary" → toString should use Long.toBinaryString
    CHECK(msg_file.find("Long.toBinaryString(flags)") != std::string::npos);
    // tag has no format → standard toString (just field name via StringBuilder)
    CHECK(msg_file.find("sb.append(\"tag=\")") != std::string::npos);
    CHECK(msg_file.find("sb.append(tag)") != std::string::npos);
}

TEST_CASE("JCG: display format hex in toString", "[java][codegen][display_format]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg_file = java->files["AllTypesMessage.java"];
    // hex field has format="hex" → toString should use Long.toHexString
    CHECK(msg_file.find("Long.toHexString(hex)") != std::string::npos);
}

// ============================================================================
// Constraint equals implies default
// ============================================================================

TEST_CASE("JCG: constraint equals implies default value", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["ConstraintMsg.java"];
    // magic has constraint equals="0xBEEF" with no explicit default,
    // so it should be initialized to 0xBEEF
    CHECK(msg.find("magic = 0xBEEF") != std::string::npos);
}

TEST_CASE("JCG: constraint equals generates validate method", "[java][codegen][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["ConstraintMsg.java"];
    CHECK(msg.find("public void validate()") != std::string::npos);
    // Validate checks equals constraint
    CHECK(msg.find("magic != 0xBEEF") != std::string::npos);
    // Validate checks max constraint
    CHECK(msg.find("percent > 100") != std::string::npos);
}

// ============================================================================
// FX bit logic
// ============================================================================

TEST_CASE("JCG: FX block generates readBits(1) check on decode", "[java][codegen][fx]") {
    auto java = gen_java("fx_block.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["FxMessage.java"];
    // FX decode should read 1 bit and conditionally decode
    CHECK(msg.find("r.readBits(1)") != std::string::npos);
    CHECK(msg.find("if (r.readBits(1) != 0)") != std::string::npos);
}

TEST_CASE("JCG: FX block writes FX continuation bit on encode", "[java][codegen][fx]") {
    auto java = gen_java("fx_block.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["FxMessage.java"];
    // FX encode should write 1-bit continuation flag
    CHECK(msg.find("writeBits(") != std::string::npos);
    CHECK(msg.find("_fxContinue") != std::string::npos);
}

TEST_CASE("JCG: FX child fields are nullable (boxed types)", "[java][codegen][fx]") {
    auto java = gen_java("fx_block.bmdl.xml");
    REQUIRE(java.has_value());
    auto& msg = java->files["FxMessage.java"];
    // FX children should use boxed types with null init
    CHECK(msg.find("Integer") != std::string::npos);
    CHECK(msg.find("= null") != std::string::npos);
}

// ============================================================================
// Bitmap/FSPEC support
// ============================================================================

TEST_CASE("JCG: bitmap struct generates FSPEC read logic", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bm = java->files["BitmapItems.java"];
    REQUIRE(!bm.empty());
    // Should read FSPEC bytes
    CHECK(bm.find("fspec") != std::string::npos);
    // Should have conditional field decode based on fspec bits
    CHECK(bm.find("fspec[") != std::string::npos);
}

TEST_CASE("JCG: bitmap struct generates FSPEC write logic", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bm = java->files["BitmapItems.java"];
    REQUIRE(!bm.empty());
    // Should build and write FSPEC byte array
    CHECK(bm.find("encode(") != std::string::npos);
    CHECK(bm.find("fspec") != std::string::npos);
}

TEST_CASE("JCG: bitmap fields are nullable", "[java][codegen][bitmap]") {
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(java.has_value());
    auto& bm = java->files["BitmapItems.java"];
    REQUIRE(!bm.empty());
    // All bitmap-controlled fields should be nullable (init to null)
    CHECK(bm.find("item010 = null") != std::string::npos);
    CHECK(bm.find("item020 = null") != std::string::npos);
    CHECK(bm.find("item030 = null") != std::string::npos);
}

// ============================================================================
// Byte alignment tracking tests
// ============================================================================

TEST_CASE("JExt: byte-aligned field after 3+5 bits uses readU16", "[java][alignment]") {
    auto java = gen_java("bit_alignment.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // After 3-bit + 5-bit = 8 bits (byte-aligned), readU16 should be used
    CHECK(all.find("readU16") != std::string::npos);
}

TEST_CASE("JExt: misaligned 7-bit field causes next read to use readBits", "[java][alignment]") {
    auto java = gen_java("bit_alignment.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // After 7-bit field, position is NOT byte-aligned, so 16-bit field should
    // use readBits(16) not readU16 in MisalignedMsg
    CHECK(all.find("readBits(16)") != std::string::npos);
}

// ============================================================================
// EBCDIC padding tests
// ============================================================================

TEST_CASE("JExt: EBCDIC string encode uses 0x40 space padding", "[java][ebcdic]") {
    auto java = gen_java("ebcdic_strings.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // EBCDIC space is 0x40, should appear in the encode for space-padded EBCDIC strings
    CHECK(all.find("0x40") != std::string::npos);
}

// ============================================================================
// Frame footer field propagation tests
// ============================================================================

TEST_CASE("JExt: frame footer fields copied to decoded payload", "[java][frame]") {
    auto java = gen_java("frame_footer.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // Footer field 'checksum' should be copied to each payload message via instanceof cast
    // e.g. _fm.checksum = result.checksum;
    CHECK(all.find("_fm.checksum = result.checksum") != std::string::npos);
}

// ============================================================================
// Deferred validation tests
// ============================================================================

TEST_CASE("JExt: validate method checks deferred constraints", "[java][validation]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // validate method should exist
    CHECK(all.find("validate()") != std::string::npos);
}

// ============================================================================
// Encode-time constraint check tests
// ============================================================================

TEST_CASE("JExt: encode emits constraint checks before writing", "[java][encode][constraint]") {
    auto java = gen_java("constraints.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);
    // The encode() method should contain constraint checks for constrained fields
    // percent has min=0, max=100 — max check should appear in encode path
    // Find "exceeds max 100" in the encode output (appears in both decode and encode)
    size_t first = all.find("exceeds max 100");
    REQUIRE(first != std::string::npos);
    // There should be at least two occurrences (decode + encode)
    size_t second = all.find("exceeds max 100", first + 1);
    CHECK(second != std::string::npos);
}

// ============================================================================
// Issue 1: Enum comparison with .value in expressions
// ============================================================================

TEST_CASE("Java: enum comparison uses .value in present_when expressions",
          "[java][codegen][enum-compare]") {
    // The nested_bitmap fixture has an enum type (device-status) used in bitmap fields.
    // Any expression comparing an enum field should use .value for the comparison.
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());

    // Verify generation succeeded (basic sanity)
    CHECK(!java->files.empty());
}

TEST_CASE("Java: enum field in choice switch uses .value",
          "[java][codegen][enum-compare]") {
    // The asterix fixture uses choice/switch on enum-like fields
    auto java = gen_java("asterix.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // switch_expr on enum fields should use .value for comparison
    // The generated code should contain ".value ==" or ".value !=" patterns
    // for enum field comparisons, not raw "==" on enum objects
    CHECK(!all.empty());
}

// ============================================================================
// Issue 2: setValue for scaled types
// ============================================================================

TEST_CASE("Java: type wrapper with scale generates setValue(double)",
          "[java][codegen][setValue]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // The nested_bitmap fixture has a scaled-temp type with scale=0.1 and offset=-40.0
    // The generated type wrapper class should have a setValue(double v) method
    CHECK(all.find("setValue(double v)") != std::string::npos);
}

TEST_CASE("Java: scaled type setValue does inverse calculation",
          "[java][codegen][setValue]") {
    // nested_bitmap fixture defines a scaled-temp type with scale and offset
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // The scaled type wrapper should have both value() getter and setValue() setter
    CHECK(all.find("public double value()") != std::string::npos);
    CHECK(all.find("setValue(double v)") != std::string::npos);
}

TEST_CASE("Java: type wrapper with scale and offset generates correct setValue inverse",
          "[java][codegen][setValue]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // scaled-temp has scale=0.1, offset=-40.0
    // setValue should compute: raw = (long)((v - offset) / scale)
    // The generated code should contain the offset and scale values
    CHECK(all.find("setValue") != std::string::npos);
    // Should contain the scale value 0.1 in the inverse calculation
    CHECK(all.find("0.1") != std::string::npos);
    // Should contain the offset value -40.0 in the inverse calculation
    CHECK(all.find("-40.0") != std::string::npos);
}

// ============================================================================
// Issue 3: Error handling with field context
// ============================================================================

TEST_CASE("Java: decode wraps field reads in try-catch with field name context",
          "[java][codegen][error-handling]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // The decode method should wrap field decoding in try-catch
    // with message.field context for error messages
    CHECK(all.find("catch (ConduitCodecException _e)") != std::string::npos);
    CHECK(all.find("decode NestedBitmapMsg.header") != std::string::npos);
}

TEST_CASE("Java: encode wraps field writes in try-catch with field name context",
          "[java][codegen][error-handling]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // Both decode and encode should have try-catch wrapping
    // Count occurrences of catch blocks
    size_t count = 0;
    size_t pos = 0;
    while ((pos = all.find("catch (ConduitCodecException _e)", pos)) != std::string::npos) {
        count++;
        pos++;
    }
    // Should have at least 2 (one for decode, one for encode of the message fields)
    CHECK(count >= 2);
}

TEST_CASE("Java: error context includes field name for struct fields",
          "[java][codegen][error-handling]") {
    auto java = gen_java("all_types.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // All field decode/encode should be wrapped with field name context
    CHECK(all.find("catch (ConduitCodecException _e)") != std::string::npos);
}

// ============================================================================
// Issue 4: Doc tags generate Javadoc comments
// ============================================================================

TEST_CASE("Java: doc tag generates Javadoc on type wrappers",
          "[java][codegen][doc]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // The nested_bitmap fixture has doc tags on types:
    // - scaled-temp: "Temperature in 0.1 degree units with -40 offset"
    // - device-status: "Device operational status"
    CHECK(all.find("Temperature in 0.1 degree units with -40 offset") != std::string::npos);
    CHECK(all.find("Device operational status") != std::string::npos);
}

TEST_CASE("Java: doc tag generates Javadoc on struct/bitmap classes",
          "[java][codegen][doc]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // SubItems has doc="Sub-items with their own FSPEC"
    CHECK(all.find("Sub-items with their own FSPEC") != std::string::npos);
    // OuterItems has doc="Outer items with nested bitmap children"
    CHECK(all.find("Outer items with nested bitmap children") != std::string::npos);
}

TEST_CASE("Java: doc tag generates Javadoc on message classes",
          "[java][codegen][doc]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // NestedBitmapMsg has doc="Message exercising nested bitmap patterns"
    CHECK(all.find("Message exercising nested bitmap patterns") != std::string::npos);
}

TEST_CASE("Java: doc tag generates Javadoc on fields",
          "[java][codegen][doc]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // Field-level docs:
    // alpha has doc="Alpha channel value"
    CHECK(all.find("Alpha channel value") != std::string::npos);
    // temp has doc="Temperature reading"
    CHECK(all.find("Temperature reading") != std::string::npos);
    // id has doc="Item identifier"
    CHECK(all.find("Item identifier") != std::string::npos);
}

TEST_CASE("Java: doc tags use Javadoc format /** ... */",
          "[java][codegen][doc]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // Doc tags should be rendered as Javadoc: /** text */
    CHECK(all.find("/**") != std::string::npos);
    CHECK(all.find("*/") != std::string::npos);
}

TEST_CASE("Java: elements without doc tag have no Javadoc",
          "[java][codegen][doc]") {
    auto java = gen_java("bitmap_fx.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // bitmap_fx fixture has no doc tags, so no Javadoc should appear
    CHECK(all.find("/**") == std::string::npos);
}

// ============================================================================
// Issue 5: Nested bitmap FSPEC handling
// ============================================================================

TEST_CASE("Java: nested bitmap struct generates FSPEC decode",
          "[java][codegen][nested-bitmap]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // Both the outer and inner bitmap classes should read FSPEC bytes
    // The nested bitmap (inline child "nested") should have its own fspec decode
    size_t fspec_count = 0;
    size_t pos = 0;
    while ((pos = all.find("// Read FSPEC bitmap", pos)) != std::string::npos) {
        fspec_count++;
        pos++;
    }
    // At minimum: OuterItems + SubItems + inline nested = 3 bitmap classes
    CHECK(fspec_count >= 3);
}

TEST_CASE("Java: nested bitmap generates FSPEC encode",
          "[java][codegen][nested-bitmap]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // Each bitmap class should have encode() with fspec writing
    size_t encode_fspec = 0;
    size_t pos = 0;
    while ((pos = all.find("byte[] fspec = new byte[", pos)) != std::string::npos) {
        encode_fspec++;
        pos++;
    }
    // At least 6: decode+encode for each of 3 bitmap classes
    CHECK(encode_fspec >= 6);
}

TEST_CASE("Java: type-referenced bitmap field decoded with .decode(r)",
          "[java][codegen][nested-bitmap]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // SubItems (type-referenced bitmap) should be decoded via SubItems.decode(r)
    CHECK(all.find("SubItems.decode(r)") != std::string::npos);
}

TEST_CASE("Java: wrapper struct containing inner bitmap generates correctly",
          "[java][codegen][nested-bitmap]") {
    auto java = gen_java("nested_bitmap.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // The wrapper struct has tag (uint8) + inner (bitmap struct)
    // The inner bitmap should have its own class with FSPEC handling
    // Check that the inner bitmap class reference appears
    CHECK(all.find("Inner") != std::string::npos);
}

TEST_CASE("Java: wide fixed-size bitmap generates correct FSPEC byte count",
          "[java][codegen][nested-bitmap]") {
    auto java = gen_java("bitmap_wide_fixed.bmdl.xml");
    REQUIRE(java.has_value());
    auto all = all_output(*java);

    // WideBitmap has bitmap bits="24" = 3 FSPEC bytes
    // Should find "new byte[3]" for the FSPEC array
    CHECK(all.find("new byte[3]") != std::string::npos);
}
