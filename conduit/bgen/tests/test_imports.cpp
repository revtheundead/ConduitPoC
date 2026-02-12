// SPDX-License-Identifier: MIT
// Bgen tests - Import system

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/validator.hpp"
#include "../src/analyzer/session_analyzer.hpp"
#include <filesystem>

namespace fs = std::filesystem;

static std::string import_fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / "imports" / name).string();
}

// ============================================================================
// Basic import functionality
// ============================================================================

TEST_CASE("Basic import merges types", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("importing_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // SharedHeader from types_lib should be available with its fields
    bool found_shared_header = false;
    for (const auto& s : protocol.structs) {
        if (s.name == "SharedHeader") {
            found_shared_header = true;
            // SharedHeader has 2 fields: id (uint16) and flags (uint8)
            CHECK(s.children.size() == 2);
            break;
        }
    }
    CHECK(found_shared_header);
}

TEST_CASE("Imported constants available", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("importing_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // LIB_VERSION and MAX_SIZE from types_lib should be merged with correct values
    bool found_lib_version = false;
    bool found_max_size = false;
    for (const auto& c : protocol.constants) {
        if (c.name == "LIB_VERSION") {
            found_lib_version = true;
            CHECK(c.value == "42");
        }
        if (c.name == "MAX_SIZE") {
            found_max_size = true;
            CHECK(c.value == "1024");
        }
    }
    CHECK(found_lib_version);
    CHECK(found_max_size);
}

TEST_CASE("Transitive import resolution", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("transitive_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // BaseStruct from transitive_base (two levels deep) should be available
    bool found_base = false;
    bool found_mid = false;
    for (const auto& s : protocol.structs) {
        if (s.name == "BaseStruct") {
            found_base = true;
            // BaseStruct has at least 1 field (base-val), proving full import
            CHECK(!s.children.empty());
        }
        if (s.name == "MidStruct") found_mid = true;
    }
    CHECK(found_base);
    CHECK(found_mid);
}

TEST_CASE("Diamond import deduplication", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("diamond_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // SharedData should appear exactly once despite diamond import
    int shared_count = 0;
    for (const auto& s : protocol.structs) {
        if (s.name == "SharedData") shared_count++;
    }
    CHECK(shared_count == 1);

    // Both branches should be present
    bool found_a = false;
    bool found_b = false;
    for (const auto& s : protocol.structs) {
        if (s.name == "BranchA") found_a = true;
        if (s.name == "BranchB") found_b = true;
    }
    CHECK(found_a);
    CHECK(found_b);
}

TEST_CASE("Missing import file rejected", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("missing_import.bmdl.xml"));
    CHECK_FALSE(build_result.has_value());
}

TEST_CASE("No protocol wrapper rejected", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("no_protocol.bmdl.xml"));
    CHECK_FALSE(build_result.has_value());
}

TEST_CASE("Imported protocol validates", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("importing_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Imported types resolve", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("diamond_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    // SharedData from imported library should be in the type index
    CHECK(index.structs.count("SharedData") == 1);
    CHECK(index.structs.count("BranchA") == 1);
    CHECK(index.structs.count("BranchB") == 1);
}

TEST_CASE("Imported type usable in codegen", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("importing_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    // Imported types from types_lib should be in the type index
    CHECK(index.structs.count("SharedHeader") == 1);
    CHECK(index.types.count("uint16") == 1);
}

// ============================================================================
// Flexible BMDL include system tests
// ============================================================================

TEST_CASE("Messages-only library merges", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("minimal_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    bool found = false;
    for (const auto& m : protocol.messages) {
        if (m.name == "LibMessage") { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("Types-only library merges", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("cross_ref_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    bool found = false;
    for (const auto& s : protocol.structs) {
        if (s.name == "TypesOnlyStruct") { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("Constants-only library merges", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("minimal_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    bool found_a = false;
    bool found_b = false;
    for (const auto& c : protocol.constants) {
        if (c.name == "CONST_A") found_a = true;
        if (c.name == "CONST_B") found_b = true;
    }
    CHECK(found_a);
    CHECK(found_b);
}

TEST_CASE("Minimal protocol with only imports", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("minimal_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    CHECK(protocol.name == "minimal-test");
    CHECK(protocol.version == "1.0");
    // All definitions come from imports
    CHECK(!protocol.structs.empty());
    CHECK(!protocol.messages.empty());
    CHECK(!protocol.constants.empty());
}

TEST_CASE("Protocol defaults propagate to library types", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("defaults_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // sensor_val type should get Little endian from protocol defaults
    bool found_sensor_val = false;
    for (const auto& td : protocol.types) {
        if (td.name == "sensor_val") {
            found_sensor_val = true;
            CHECK(td.endian == bgen::model::Endian::Little);
            CHECK_FALSE(td.endian_explicit);
            break;
        }
    }
    CHECK(found_sensor_val);

    // SensorData struct fields should get Little endian
    bool found_sensor_data = false;
    for (const auto& sd : protocol.structs) {
        if (sd.name == "SensorData") {
            found_sensor_data = true;
            for (const auto& child : sd.children) {
                if (auto* field = std::get_if<bgen::model::Field>(&child)) {
                    CHECK(field->endian == bgen::model::Endian::Little);
                }
            }
            break;
        }
    }
    CHECK(found_sensor_data);

    // Validate that the merged protocol passes the full pipeline
    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto validate_result = bgen::analyzer::validate(protocol, *resolve_result);
    CHECK(validate_result.has_value());
}

TEST_CASE("Explicit attributes preserved despite protocol defaults", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("defaults_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // big_val type should keep Big endian despite protocol default of Little
    bool found_big_val = false;
    for (const auto& td : protocol.types) {
        if (td.name == "big_val") {
            found_big_val = true;
            CHECK(td.endian == bgen::model::Endian::Big);
            CHECK(td.endian_explicit);
            break;
        }
    }
    CHECK(found_big_val);

    // ExplicitStruct fields: "data" has explicit endian="big" and keeps it;
    // "val" has no explicit endian so it inherits the protocol default (Little)
    bool found_explicit_struct = false;
    bool found_data = false;
    bool found_val = false;
    for (const auto& sd : protocol.structs) {
        if (sd.name == "ExplicitStruct") {
            found_explicit_struct = true;
            for (const auto& child : sd.children) {
                if (auto* field = std::get_if<bgen::model::Field>(&child)) {
                    if (field->name == "data") {
                        found_data = true;
                        CHECK(field->endian == bgen::model::Endian::Big);
                        CHECK(field->endian_explicit);
                    }
                    if (field->name == "val") {
                        found_val = true;
                        // val has no explicit endian, so protocol default (Little) applies
                        CHECK(field->endian == bgen::model::Endian::Little);
                        CHECK_FALSE(field->endian_explicit);
                    }
                }
            }
            break;
        }
    }
    CHECK(found_explicit_struct);
    CHECK(found_data);
    CHECK(found_val);
}

TEST_CASE("Entry-point in library file discovered", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("entry_point_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    bool found = false;
    for (const auto& m : protocol.messages) {
        if (m.name == "LibEntryMsg") {
            CHECK(m.is_entry_point);
            found = true;
            break;
        }
    }
    CHECK(found);

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);
    CHECK(!sessions.empty());
}

TEST_CASE("Cross-library type references resolve", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("cross_ref_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    CHECK(index.structs.count("TypesOnlyStruct") == 1);

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Empty library produces warning", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("empty_import_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    bool found_warning = false;
    for (const auto& w : protocol.warnings) {
        if (w.first.find("contains no definitions") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    CHECK(found_warning);
}

TEST_CASE("No entry-point warning for multi-file protocol", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("minimal_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    // minimal_protocol imports libraries with messages but no entry-point
    CHECK(!protocol.messages.empty());
    bool found_warning = false;
    for (const auto& w : protocol.warnings) {
        if (w.first.find("no message with role=\"entry-point\" found") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    CHECK(found_warning);
}

TEST_CASE("Full pipeline with library entry-point", "[imports]") {
    auto build_result = bgen::model::build_protocol(import_fixture_path("entry_point_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto sessions = bgen::analyzer::analyze_sessions(protocol, index);
    REQUIRE(!sessions.empty());
    CHECK(sessions[0].entry_point_name == "LibEntryMsg");
}
