// SPDX-License-Identifier: MIT
// Bgen tests - Wire size computation

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/wire_sizer.hpp"
#include <filesystem>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

// Helper: build + resolve + compute wire sizes for a fixture
struct SizedProtocol {
    bgen::model::Protocol protocol;
    bgen::analyzer::TypeIndex index;
    bgen::analyzer::WireSizeInfo sizes;
};

static std::optional<SizedProtocol> size_fixture(const std::string& fixture_name) {
    auto build_result = bgen::model::build_protocol(fixture_path(fixture_name));
    if (!build_result) return std::nullopt;

    auto resolve_result = bgen::analyzer::resolve_types(*build_result);
    if (!resolve_result) return std::nullopt;

    auto sizes = bgen::analyzer::compute_wire_sizes(*build_result, *resolve_result);
    return SizedProtocol{std::move(*build_result), std::move(*resolve_result), std::move(sizes)};
}

// ============================================================================
// Primitive type sizes
// ============================================================================

TEST_CASE("Primitive type sizes", "[wire_sizer]") {
    auto sp = size_fixture("all_types.bmdl.xml");
    REQUIRE(sp.has_value());

    CHECK(sp->sizes.get("uint8") == 1);
    CHECK(sp->sizes.get("uint16") == 2);
    CHECK(sp->sizes.get("uint32") == 4);
    CHECK(sp->sizes.get("uint64") == 8);
    CHECK(sp->sizes.get("int8") == 1);
    CHECK(sp->sizes.get("int16") == 2);
    CHECK(sp->sizes.get("int32") == 4);
    CHECK(sp->sizes.get("float32") == 4);
    CHECK(sp->sizes.get("float64") == 8);
    CHECK(sp->sizes.get("bool8") == 1);
}

// ============================================================================
// String type sizes
// ============================================================================

TEST_CASE("String type fixed size", "[wire_sizer]") {
    auto sp = size_fixture("string_features.bmdl.xml");
    REQUIRE(sp.has_value());

    // name-str has length=20 -> 20 bytes
    auto name_size = sp->sizes.get("name-str");
    CHECK(name_size == 20);
}

TEST_CASE("Packed char-bits string", "[wire_sizer]") {
    auto sp = size_fixture("string_features.bmdl.xml");
    REQUIRE(sp.has_value());

    // packed-str: length=8, char-bits=6 -> (8*6+7)/8 = 6 bytes
    auto packed_size = sp->sizes.get("packed-str");
    CHECK(packed_size == 6);
}

// ============================================================================
// Struct sizes
// ============================================================================

TEST_CASE("Simple struct size", "[wire_sizer]") {
    auto sp = size_fixture("session_protocol.bmdl.xml");
    REQUIRE(sp.has_value());

    // PingBody: timestamp(uint32=4) -> 4 bytes
    CHECK(sp->sizes.get("PingBody") == 4);

    // DataBody: channel(uint8=1) + payload_a(uint32=4) + payload_b(uint32=4) = 9
    CHECK(sp->sizes.get("DataBody") == 9);

    // AckBody: acked-seq(uint16=2) -> 2 bytes
    CHECK(sp->sizes.get("AckBody") == 2);
}

TEST_CASE("Struct with reserved", "[wire_sizer]") {
    auto sp = size_fixture("struct_features.bmdl.xml");
    REQUIRE(sp.has_value());

    // ConstrainedMessage: magic(16) + version(8) + value(16) + reserved(8) + GpsCoord(64) = 112 bits = 14 bytes
    CHECK(sp->sizes.get("ConstrainedMessage") == 14);
}

TEST_CASE("Message with inline struct", "[wire_sizer]") {
    auto sp = size_fixture("inline_struct.bmdl.xml");
    REQUIRE(sp.has_value());

    // Header: sync(16) + seq(16) + length(16) = 48 bits = 6 bytes
    CHECK(sp->sizes.get("Header") == 6);
}

// ============================================================================
// Dynamic sizes
// ============================================================================

TEST_CASE("Dynamic sizes return nullopt", "[wire_sizer]") {
    auto sp = size_fixture("bitmap_fx.bmdl.xml");
    REQUIRE(sp.has_value());

    // Bitmap struct is always dynamic
    CHECK_FALSE(sp->sizes.get("BitmapItems").has_value());
}

TEST_CASE("FX block is dynamic", "[wire_sizer]") {
    auto sp = size_fixture("fx_block.bmdl.xml");
    REQUIRE(sp.has_value());

    // FxMessage has an FX block, so it's dynamic
    CHECK_FALSE(sp->sizes.get("FxMessage").has_value());
}

TEST_CASE("Choice makes message dynamic", "[wire_sizer]") {
    auto sp = size_fixture("choice_protocol.bmdl.xml");
    REQUIRE(sp.has_value());

    // Frame has a choice, so it's dynamic
    CHECK_FALSE(sp->sizes.get("Frame").has_value());
}

// ============================================================================
// Array sizes
// ============================================================================

TEST_CASE("Fixed count array", "[wire_sizer]") {
    auto sp = size_fixture("arrays_choices.bmdl.xml");
    REQUIRE(sp.has_value());

    // FixedArrayMsg: 3 Points, each Point = x(uint16=2) + y(uint16=2) = 4 bytes -> 12 bytes
    CHECK(sp->sizes.get("FixedArrayMsg") == 12);
}

TEST_CASE("Count-from array is dynamic", "[wire_sizer]") {
    auto sp = size_fixture("arrays_choices.bmdl.xml");
    REQUIRE(sp.has_value());

    // CountFromArrayMsg has count-from, so it's dynamic
    CHECK_FALSE(sp->sizes.get("CountFromArrayMsg").has_value());
}

// ============================================================================
// Nested struct references
// ============================================================================

TEST_CASE("Nested struct references resolve size", "[wire_sizer]") {
    auto sp = size_fixture("struct_features.bmdl.xml");
    REQUIRE(sp.has_value());

    // GpsCoord: latitude(uint32=4) + longitude(uint32=4) = 8 bytes
    CHECK(sp->sizes.get("GpsCoord") == 8);
}

// ============================================================================
// Optional field makes struct dynamic
// ============================================================================

TEST_CASE("Optional field makes struct dynamic", "[wire_sizer]") {
    auto sp = size_fixture("struct_features.bmdl.xml");
    REQUIRE(sp.has_value());

    // ConditionalMessage has present-when field, so it's dynamic
    CHECK_FALSE(sp->sizes.get("ConditionalMessage").has_value());
}
