// SPDX-License-Identifier: MIT
// Bgen tests - JSON serialization/deserialization roundtrip verification
//
// These tests verify that generated to_json/from_json functions correctly
// serialize struct/message data to JSON and deserialize it back, preserving
// all field values through the roundtrip.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

#include "all_types/json.hpp"
#include "struct_features/json.hpp"
#include "arrays_choices/json.hpp"
#include "boundary_types/json.hpp"

// ============================================================================
// Section: Primitive types JSON roundtrip (all_types fixture)
// ============================================================================

TEST_CASE("JSON roundtrip: unsigned integers", "[json][primitives]") {
    all_types::AllTypesMessage msg;
    msg.mutable_u8() = 0x42;
    msg.mutable_u16() = 0x1234;
    msg.mutable_u32() = 0xDEADBEEF;
    msg.mutable_u64() = 0x0102030405060708ULL;

    nlohmann::json j = msg;
    auto decoded = j.get<all_types::AllTypesMessage>();

    CHECK(decoded.u8() == 0x42);
    CHECK(decoded.u16() == 0x1234);
    CHECK(decoded.u32() == 0xDEADBEEF);
    CHECK(decoded.u64() == 0x0102030405060708ULL);
}

TEST_CASE("JSON roundtrip: signed integers", "[json][primitives]") {
    all_types::AllTypesMessage msg;
    msg.mutable_i8() = -42;
    msg.mutable_i16() = -1000;
    msg.mutable_i32() = -100000;

    nlohmann::json j = msg;
    auto decoded = j.get<all_types::AllTypesMessage>();

    CHECK(decoded.i8() == -42);
    CHECK(decoded.i16() == -1000);
    CHECK(decoded.i32() == -100000);
}

TEST_CASE("JSON roundtrip: floating point", "[json][primitives]") {
    all_types::AllTypesMessage msg;
    msg.mutable_f32() = 3.14f;
    msg.mutable_f64() = 2.718281828;

    nlohmann::json j = msg;
    auto decoded = j.get<all_types::AllTypesMessage>();

    CHECK_THAT(decoded.f32(), Catch::Matchers::WithinRel(3.14f, 1e-5f));
    CHECK_THAT(decoded.f64(), Catch::Matchers::WithinRel(2.718281828, 1e-9));
}

TEST_CASE("JSON roundtrip: boolean flag", "[json][primitives]") {
    all_types::AllTypesMessage msg;
    msg.mutable_flag() = true;

    nlohmann::json j = msg;
    CHECK(j["flag"] == true);
    auto decoded = j.get<all_types::AllTypesMessage>();
    CHECK(decoded.flag() == true);
}

TEST_CASE("JSON roundtrip: string types", "[json][strings]") {
    all_types::AllTypesMessage msg;
    msg.mutable_ascii().set_value("hello");
    msg.mutable_utf8().set_value("world");

    nlohmann::json j = msg;
    CHECK(j["ascii"] == "hello");
    CHECK(j["utf8"] == "world");

    auto decoded = j.get<all_types::AllTypesMessage>();
    CHECK(decoded.ascii().value() == "hello");
    CHECK(decoded.utf8().value() == "world");
}

TEST_CASE("JSON roundtrip: enum type", "[json][enum]") {
    all_types::AllTypesMessage msg;
    msg.mutable_color() = all_types::color_enum::green;

    nlohmann::json j = msg;
    // Enums serialize as their underlying integer value
    CHECK(j["color"] == 2);

    auto decoded = j.get<all_types::AllTypesMessage>();
    CHECK(decoded.color() == all_types::color_enum::green);
}

TEST_CASE("JSON roundtrip: scaled type", "[json][scaled]") {
    all_types::AllTypesMessage msg;
    all_types::scaled_temp temp;
    temp.set_value(25.5);
    msg.mutable_temp() = temp;

    nlohmann::json j = msg;
    auto decoded = j.get<all_types::AllTypesMessage>();
    CHECK_THAT(decoded.temp().value(), Catch::Matchers::WithinAbs(25.5, 0.1));
}

TEST_CASE("JSON roundtrip: flags type", "[json][flags]") {
    all_types::AllTypesMessage msg;
    all_types::status_flags flags;
    flags.set_raw(0x05);
    msg.mutable_status() = flags;

    nlohmann::json j = msg;
    CHECK(j["status"] == 0x05);

    auto decoded = j.get<all_types::AllTypesMessage>();
    CHECK(decoded.status().raw() == 0x05);
}

// ============================================================================
// Section: JSON string output format
// ============================================================================

TEST_CASE("JSON to_string and parse roundtrip", "[json][string]") {
    all_types::AllTypesMessage msg;
    msg.mutable_u8() = 0xFF;
    msg.mutable_u16() = 1000;

    nlohmann::json j = msg;
    std::string json_str = j.dump();

    // Parse back from string
    auto parsed = nlohmann::json::parse(json_str);
    auto decoded = parsed.get<all_types::AllTypesMessage>();

    CHECK(decoded.u8() == 0xFF);
    CHECK(decoded.u16() == 1000);
}

TEST_CASE("JSON pretty print", "[json][format]") {
    all_types::AllTypesMessage msg;
    msg.mutable_u8() = 42;

    nlohmann::json j = msg;
    std::string pretty = j.dump(2);

    // Should contain formatted JSON
    CHECK(pretty.find("\"u8\": 42") != std::string::npos);
}

// ============================================================================
// Section: Struct features JSON roundtrip
// ============================================================================

TEST_CASE("JSON roundtrip: nested struct", "[json][struct]") {
    struct_features::ConstrainedMessage msg;
    msg.mutable_magic() = 0xCAFE;
    msg.mutable_version() = 1;
    msg.mutable_value() = 42;

    // position is a nested GpsCoord struct
    msg.mutable_position().mutable_latitude() = 1000;
    msg.mutable_position().mutable_longitude() = 2000;

    nlohmann::json j = msg;
    CHECK(j["magic"] == 0xCAFE);
    CHECK(j["position"]["latitude"] == 1000);
    CHECK(j["position"]["longitude"] == 2000);

    auto decoded = j.get<struct_features::ConstrainedMessage>();
    CHECK(decoded.magic() == 0xCAFE);
    CHECK(decoded.version() == 1);
    CHECK(decoded.value() == 42);
    CHECK(decoded.position().latitude() == 1000);
    CHECK(decoded.position().longitude() == 2000);
}

TEST_CASE("JSON roundtrip: optional fields present", "[json][optional]") {
    struct_features::ConditionalMessage msg;
    msg.mutable_has_extra() = true;
    msg.mutable_base_value() = 100;
    msg.set_extra_value(200);

    nlohmann::json j = msg;
    CHECK(j.contains("extra-value"));
    CHECK(j["extra-value"] == 200);

    auto decoded = j.get<struct_features::ConditionalMessage>();
    CHECK(decoded.has_extra_value());
    CHECK(decoded.extra_value() == 200);
}

TEST_CASE("JSON roundtrip: optional fields absent", "[json][optional]") {
    struct_features::ConditionalMessage msg;
    msg.mutable_has_extra() = false;
    msg.mutable_base_value() = 50;

    nlohmann::json j = msg;
    CHECK_FALSE(j.contains("extra-value"));

    auto decoded = j.get<struct_features::ConditionalMessage>();
    CHECK_FALSE(decoded.has_extra_value());
    CHECK(decoded.base_value() == 50);
}

// ============================================================================
// Section: Array types JSON roundtrip (arrays_choices fixture)
// ============================================================================

TEST_CASE("JSON roundtrip: array field", "[json][array]") {
    arrays_choices::FixedArrayMsg msg;

    auto& pts = msg.mutable_points();
    for (int i = 0; i < 3; i++) {
        arrays_choices::Point elem;
        elem.mutable_x() = static_cast<uint16_t>(i * 10);
        elem.mutable_y() = static_cast<uint16_t>(i * 20);
        pts.push_back(elem);
    }

    nlohmann::json j = msg;
    CHECK(j["points"].is_array());
    CHECK(j["points"].size() == 3);
    CHECK(j["points"][0]["x"] == 0);
    CHECK(j["points"][0]["y"] == 0);
    CHECK(j["points"][1]["x"] == 10);
    CHECK(j["points"][1]["y"] == 20);
    CHECK(j["points"][2]["x"] == 20);
    CHECK(j["points"][2]["y"] == 40);

    auto decoded = j.get<arrays_choices::FixedArrayMsg>();
    REQUIRE(decoded.points().size() == 3);
    CHECK(decoded.points()[0].x() == 0);
    CHECK(decoded.points()[0].y() == 0);
    CHECK(decoded.points()[1].x() == 10);
    CHECK(decoded.points()[1].y() == 20);
    CHECK(decoded.points()[2].x() == 20);
    CHECK(decoded.points()[2].y() == 40);
}

// ============================================================================
// Section: Partial JSON deserialization
// ============================================================================

TEST_CASE("JSON from_json: missing fields use defaults", "[json][partial]") {
    nlohmann::json j = {{"u8", 42}};

    all_types::AllTypesMessage decoded;
    from_json(j, decoded);

    CHECK(decoded.u8() == 42);
    // Other fields should retain defaults (0)
    CHECK(decoded.u16() == 0);
    CHECK(decoded.u32() == 0);
}

// ============================================================================
// Section: Type-level enum standalone JSON
// ============================================================================

TEST_CASE("JSON roundtrip: standalone enum", "[json][enum]") {
    all_types::color_enum color = all_types::color_enum::blue;

    nlohmann::json j = color;
    CHECK(j == 3);

    auto decoded = j.get<all_types::color_enum>();
    CHECK(decoded == all_types::color_enum::blue);
}

// ============================================================================
// Section: Boundary types JSON roundtrip
// ============================================================================

TEST_CASE("JSON roundtrip: boundary integer values", "[json][boundary]") {
    boundary_types::BoundaryMsg msg;
    msg.mutable_byte_val() = 255;
    msg.mutable_word() = 65535;
    msg.mutable_signed_byte() = -128;

    nlohmann::json j = msg;
    CHECK(j["byte-val"] == 255);
    CHECK(j["word"] == 65535);
    CHECK(j["signed-byte"] == -128);

    auto decoded = j.get<boundary_types::BoundaryMsg>();
    CHECK(decoded.byte_val() == 255);
    CHECK(decoded.word() == 65535);
    CHECK(decoded.signed_byte() == -128);
}

TEST_CASE("JSON roundtrip: zero values", "[json][boundary]") {
    all_types::AllTypesMessage msg;
    // All fields at default (zero) should roundtrip correctly
    nlohmann::json j = msg;
    auto decoded = j.get<all_types::AllTypesMessage>();

    CHECK(decoded.u8() == 0);
    CHECK(decoded.u16() == 0);
    CHECK(decoded.u32() == 0);
    CHECK(decoded.u64() == 0);
    CHECK(decoded.i8() == 0);
    CHECK(decoded.i16() == 0);
    CHECK(decoded.i32() == 0);
    CHECK(decoded.f32() == 0.0f);
    CHECK(decoded.f64() == 0.0);
    CHECK(decoded.flag() == false);
}

// ============================================================================
// Section: Nested struct (Point) standalone JSON
// ============================================================================

TEST_CASE("JSON roundtrip: standalone struct", "[json][struct]") {
    arrays_choices::Point pt;
    pt.mutable_x() = 100;
    pt.mutable_y() = 200;

    nlohmann::json j = pt;
    CHECK(j["x"] == 100);
    CHECK(j["y"] == 200);

    auto decoded = j.get<arrays_choices::Point>();
    CHECK(decoded.x() == 100);
    CHECK(decoded.y() == 200);
}

// ============================================================================
// Section: JSON from string roundtrip
// ============================================================================

TEST_CASE("JSON roundtrip: from JSON string", "[json][string]") {
    std::string json_str = R"({"u8":255,"u16":65535,"u32":4294967295,"u64":0,"i8":-128,"i16":-32768,"i32":0,"f32":0.0,"f64":0.0,"flag":false,"ascii":"","utf8":"","raw":0,"le16":0,"le32":0,"temp":0.0,"hex":0,"color":0,"status":0})";

    auto j = nlohmann::json::parse(json_str);
    auto msg = j.get<all_types::AllTypesMessage>();

    CHECK(msg.u8() == 255);
    CHECK(msg.u16() == 65535);
    CHECK(msg.u32() == 4294967295u);
    CHECK(msg.i8() == -128);
    CHECK(msg.i16() == -32768);
}
