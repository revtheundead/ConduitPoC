// SPDX-License-Identifier: MIT
// Bgen tests - Validator

#include <catch2/catch_test_macros.hpp>
#include "../src/model/ast_builder.hpp"
#include "../src/analyzer/type_resolver.hpp"
#include "../src/analyzer/validator.hpp"
#include <filesystem>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

TEST_CASE("Valid minimal protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("minimal.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

// ============================================================================
// Deep-inspection fixes
// ============================================================================

TEST_CASE("Constant with unresolved type_ref rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_const_type.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    CHECK_FALSE(resolve_result.has_value());
}

TEST_CASE("Recursive struct cycle detected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("recursive_struct.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    auto& errors = validate_result.error();
    bool found_cycle = false;
    for (const auto& e : errors) {
        if (e.message.find("recursive type cycle") != std::string::npos) {
            found_cycle = true;
            break;
        }
    }
    CHECK(found_cycle);
}

TEST_CASE("Constraint min/max relaxation rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("constraint_relaxation.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    // Should fail during resolution due to relaxed constraints
    REQUIRE_FALSE(resolve_result.has_value());
    auto& errors = resolve_result.error();
    bool found_min = false;
    bool found_max = false;
    for (const auto& e : errors) {
        if (e.message.find("constraint min") != std::string::npos) found_min = true;
        if (e.message.find("constraint max") != std::string::npos) found_max = true;
    }
    CHECK(found_min);
    CHECK(found_max);
}

TEST_CASE("Valid choice protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("choice_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid all_types protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("all_types.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid struct_features protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("struct_features.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid bitmap_fx protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("bitmap_fx.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid arrays_choices protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("arrays_choices.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid session_protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("session_protocol.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Forward reference in present-when rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("forward_ref.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Duplicate field names rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("duplicate_fields.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Overlapping case values rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("overlapping_cases.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Choice without switch expression rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("choice_no_switch.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Invalid constraint fixture rejects", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_constraint.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

// ============================================================================
// Valid advanced fixtures pass validation
// ============================================================================

TEST_CASE("Valid expression features protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid string features protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("string_features.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid FX block protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("fx_block.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid inline struct protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("inline_struct.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid default and initial protocol passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("default_initial.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid non-overlapping constant ranges passes validation", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("non_overlap_ranges.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

// ============================================================================
// Negative validation tests - new error cases
// ============================================================================

TEST_CASE("Auto on signed type rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Invalid auto attribute value rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_type.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Initial and default mutually exclusive rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_initial.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Char-bits out of range rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_char_bits.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("FX inside array rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_fx_context.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Remaining outside bounded container rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_remaining.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Inline struct introducing duplicate field name rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_inline_dup.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Duplicate bitmap bit position rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bitmap_dup.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Overlapping ranges with constant references rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("overlapping_ranges.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Complementary direction cases with same value accepted", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("direction_qualified.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Same-direction overlapping cases rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("overlap_same_direction.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

TEST_CASE("Both-direction overlapping with send rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("overlap_both_with_send.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK_FALSE(validate_result.has_value());
}

// ============================================================================
// Resolver edge case tests (errors surface during resolve_types)
// ============================================================================

TEST_CASE("Scale/offset conflict between field and type rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("resolve_scale_conflict.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    CHECK_FALSE(resolve_result.has_value());
}

TEST_CASE("Constraint equals conflict rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("resolve_constraint_conflict.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    CHECK_FALSE(resolve_result.has_value());
}

TEST_CASE("Cross-category name collision rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("resolve_cross_collision.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    CHECK_FALSE(resolve_result.has_value());
}

TEST_CASE("Unresolved type reference rejected", "[validator][resolver]") {
    auto build_result = bgen::model::build_protocol(fixture_path("resolve_unresolved_ref.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    CHECK_FALSE(resolve_result.has_value());
}

// ============================================================================
// Existing protocol validations
// ============================================================================

// ============================================================================
// Wire encoding validation
// ============================================================================

TEST_CASE("Valid wire encodings protocol passes validation", "[validator][wire_encoding]") {
    auto build_result = bgen::model::build_protocol(fixture_path("wire_encodings.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("BCD with non-multiple-of-4 bits rejected", "[validator][wire_encoding]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bcd_bits.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("bcd") != std::string::npos &&
            e.message.find("divisible by 4") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("BCD on signed type rejected", "[validator][wire_encoding]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bcd_signed.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("bcd") != std::string::npos &&
            e.message.find("uint") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("BNR_S on unsigned type rejected", "[validator][wire_encoding]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bnrs_unsigned.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("bnr-s") != std::string::npos &&
            e.message.find("int") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("BCD_S with invalid bit count rejected", "[validator][wire_encoding]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bcds_bits.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("bcd-s") != std::string::npos &&
            e.message.find("divisible by 4") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Existing protocol validations
// ============================================================================

TEST_CASE("Sentry-link protocol passes validation", "[validator]") {
    std::string sentry_path = (fs::path(BGEN_TEST_FIXTURES_DIR) / "sentry_link.bmdl.xml").string();
    if (!fs::exists(sentry_path)) {
        SKIP("sentry-link protocol not found");
    }

    auto build_result = bgen::model::build_protocol(sentry_path);
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

// ============================================================================
// Advanced feature validation tests
// ============================================================================

TEST_CASE("Advanced bitmap protocol passes validation", "[validator][bitmap]") {
    auto build_result = bgen::model::build_protocol(fixture_path("bitmap_advanced.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Enum arrays protocol passes validation", "[validator][array]") {
    auto build_result = bgen::model::build_protocol(fixture_path("enum_arrays.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("FX advanced protocol passes validation", "[validator][fx]") {
    auto build_result = bgen::model::build_protocol(fixture_path("fx_advanced.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

// ============================================================================
// Bug-fix and new validation tests
// ============================================================================

TEST_CASE("Inline struct cycle does not crash (B1)", "[validator][inline]") {
    // B1: struct A inlines struct B which inlines struct A — the cycle guard
    // in check_inline_duplicates should prevent infinite recursion.
    // This also triggers the recursive type cycle detector, so we expect
    // validation to fail (cycle error), but NOT crash.
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_inline_cycle.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    // The struct cycle detector should catch this cycle
    REQUIRE_FALSE(validate_result.has_value());
    bool found_cycle = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("recursive type cycle") != std::string::npos) {
            found_cycle = true;
            break;
        }
    }
    CHECK(found_cycle);
}

TEST_CASE("Length-prefix referencing unknown type rejected (B2)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_length_prefix_unknown.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("length-prefix") != std::string::npos &&
            e.message.find("unknown type") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Constraint equals with incompatible constant type rejected (B3)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_constraint_type_mismatch.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("type mismatch") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Char-bits on non-string/bytes field rejected (V1)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_char_bits_uint.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("char-bits") != std::string::npos &&
            e.message.find("string or bytes") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Auto-increment with constraint equals rejected (V3)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_constraint.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("auto-increment") != std::string::npos &&
            e.message.find("constraint equals") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Auto-config with constraint equals rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_config_constraint.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("auto=\"config\"") != std::string::npos &&
            e.message.find("constraint equals") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Auto-length with constraint equals rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_length_constraint.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("auto=\"length\"") != std::string::npos &&
            e.message.find("constraint equals") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Frame field with present-when rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_present_when.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("present-when") != std::string::npos &&
            e.message.find("frame") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Edge case validator tests (B12)
// ============================================================================

TEST_CASE("Align with non-power-of-2 rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_align.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("power of 2") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Bitmap ext bit out of range rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bitmap_ext.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("ext bit must be 0-7") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Field after star-length field rejected (P3)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_star_trailing.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("follows") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Duplicate case name in choice rejected (P4)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_dup_case_name.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("duplicate case name") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Float with non-32/64 bits rejected (P5)", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_float_bits.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("float must be 32 or 64") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("FX choice protocol passes validation", "[validator][fx]") {
    auto build_result = bgen::model::build_protocol(fixture_path("fx_choice.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Empty FX block rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("empty_fx.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("empty") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Expression safety validation tests (Phase 5B)
// ============================================================================

TEST_CASE("Division by zero in expression rejected", "[validator][expr]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_div_zero.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("division by zero") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Shift overflow in expression rejected", "[validator][expr]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_shift_overflow.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("shift") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Dispatch attribute validation tests
// ============================================================================

TEST_CASE("Mixed dispatch modes within a single case rejected", "[validator][dispatch]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_mixed_dispatch.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("same dispatch mode") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Dispatch attribute outside choice case rejected", "[validator][dispatch]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_dispatch_context.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("only valid on arrays inside inline choice cases") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Frame validation (v2)
// ============================================================================

TEST_CASE("Valid frame_basic passes validation", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_basic.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid frame_config passes validation", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_config.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Frame missing payload rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_no_payload.bmdl.xml"));
    // Parser catches missing payload, so build should fail
    CHECK_FALSE(build_result.has_value());
}

TEST_CASE("Frame with duplicate auto=id rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_dup_auto_id.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("auto=\"id\"") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Message without id when frame exists rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_message_no_id.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("must have an 'id' attribute") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Duplicate message id with same direction rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_message_dup_id.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("same id") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Frame with zero messages rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_zero_messages.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("no messages exist") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Message id exceeding field range rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_id_overflow.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("exceeds") != std::string::npos && e.message.find("id field range") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Non-scalar field in frame header rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_non_scalar.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("struct/message type") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Auto-length field exceeding 32 bits rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_length_wide.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("maximum supported is 32 bits") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Valid frame_footer passes validation", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_footer.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid frame_direction passes validation", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_direction.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Valid frame_array passes validation", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_array.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Frame-message field name collision rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_collision.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("conflicts with frame field") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Negative bits attribute rejected at parse time", "[validator][parse]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_negative_bits.bmdl.xml"));
    REQUIRE_FALSE(build_result.has_value());
    auto& errors = build_result.error();
    bool found = false;
    for (const auto& e : errors) {
        if (e.message.find("non-negative") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Constraint value exceeding field bit width rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_constraint_range.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    auto& errors = validate_result.error();
    bool found = false;
    for (const auto& e : errors) {
        if (e.message.find("exceeds") != std::string::npos &&
            e.message.find("bit range") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Constant value exceeding type bit width rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_constant_range.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    auto& errors = validate_result.error();
    bool found = false;
    for (const auto& e : errors) {
        if (e.message.find("exceeds") != std::string::npos &&
            e.message.find("bit range") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("64-bit constraint range validation does not trigger UB", "[validator]") {
    // This test verifies that the validator handles 64-bit fields without
    // undefined behavior from 1ULL<<64 or 1LL<<63 shift operations.
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_constraint_range_64bit.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    // Should pass validation without crashing (no UB)
    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("C++ keyword field name rejected as error", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_keyword_name.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    auto& errors = validate_result.error();
    bool found = false;
    for (const auto& e : errors) {
        if (e.message.find("C++ keyword") != std::string::npos &&
            e.message.find("class") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Auto field reference to nonexistent field rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_field_ref.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    auto& errors = validate_result.error();
    bool found = false;
    for (const auto& e : errors) {
        if (e.message.find("unknown field") != std::string::npos &&
            e.message.find("nonexistent") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Bitmap array with out-of-range bit rejected", "[validator][bitmap]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_bitmap_array_bit.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("bit 99 out of range") != std::string::npos &&
            e.message.find("items") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Timestamp auto field on uint passes validation", "[validator][frame][timestamp]") {
    auto build_result = bgen::model::build_protocol(fixture_path("frame_timestamp.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    CHECK(validate_result.has_value());
}

TEST_CASE("Timestamp auto field on signed int rejected", "[validator][frame][timestamp]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_auto_timestamp_signed.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("timestamp") != std::string::npos &&
            e.message.find("unsigned") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Terminated + length-prefix mutual exclusion rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_terminated_length_prefix.bmdl.xml"));
    CHECK_FALSE(build_result.has_value());
}

TEST_CASE("Frame field with terminated rejected", "[validator][frame]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_frame_terminated.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("frame") != std::string::npos &&
            e.message.find("frame fields must have fixed size") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Inline enum id exceeding field bit range rejected", "[validator]") {
    auto build_result = bgen::model::build_protocol(fixture_path("invalid_enum_range.bmdl.xml"));
    REQUIRE(build_result.has_value());
    auto& protocol = *build_result;

    auto resolve_result = bgen::analyzer::resolve_types(protocol);
    REQUIRE(resolve_result.has_value());
    auto& index = *resolve_result;

    auto validate_result = bgen::analyzer::validate(protocol, index);
    REQUIRE_FALSE(validate_result.has_value());
    bool found = false;
    for (const auto& e : validate_result.error()) {
        if (e.message.find("exceeds") != std::string::npos &&
            e.message.find("range") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}
