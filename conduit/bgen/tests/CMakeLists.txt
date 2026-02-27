# ============================================================================
# bgen unit tests
#
# Tests for the BMDL parser, validator, session analyzer, codegen output,
# and generated code roundtrip / integration tests.
# Uses Catch2 v3 from vendored third_party/Catch2.
# ============================================================================

include(CTest)
include(Catch)

# Build bgen source files as a library for test linking.
# Uses BGEN_LIB_SOURCES defined in parent bgen/CMakeLists.txt.
add_library(bgen_lib STATIC ${BGEN_LIB_SOURCES})

target_compile_features(bgen_lib PUBLIC cxx_std_23)
target_link_libraries(bgen_lib PUBLIC pugixml::pugixml conduit_warnings)
target_include_directories(bgen_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# ============================================================================
# Build-time code generation for roundtrip / session integration tests
# ============================================================================

macro(bgen_generate_fixture FIXTURE_NAME)
    set(FIXTURE_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${FIXTURE_NAME}.bmdl.xml")
    set(FIXTURE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIXTURE_NAME}")

    add_custom_command(
        OUTPUT "${FIXTURE_OUTPUT_DIR}/constants.hpp"
               "${FIXTURE_OUTPUT_DIR}/types.hpp"
               "${FIXTURE_OUTPUT_DIR}/structs.hpp"
               "${FIXTURE_OUTPUT_DIR}/messages.hpp"
               "${FIXTURE_OUTPUT_DIR}/sessions.hpp"
               "${FIXTURE_OUTPUT_DIR}/protocol.hpp"
               "${FIXTURE_OUTPUT_DIR}/json.hpp"
        COMMAND bgen --input "${FIXTURE_INPUT}" --output "${FIXTURE_OUTPUT_DIR}"
        DEPENDS bgen "${FIXTURE_INPUT}"
        COMMENT "Generating code from ${FIXTURE_NAME}.bmdl.xml"
    )

    add_custom_target(gen_${FIXTURE_NAME}
        DEPENDS "${FIXTURE_OUTPUT_DIR}/constants.hpp"
                "${FIXTURE_OUTPUT_DIR}/types.hpp"
                "${FIXTURE_OUTPUT_DIR}/structs.hpp"
                "${FIXTURE_OUTPUT_DIR}/messages.hpp"
                "${FIXTURE_OUTPUT_DIR}/sessions.hpp"
                "${FIXTURE_OUTPUT_DIR}/protocol.hpp"
                "${FIXTURE_OUTPUT_DIR}/json.hpp"
    )
endmacro()

bgen_generate_fixture(all_types)
bgen_generate_fixture(struct_features)
bgen_generate_fixture(bitmap_fx)
bgen_generate_fixture(arrays_choices)
bgen_generate_fixture(session_protocol)
bgen_generate_fixture(inline_struct)
bgen_generate_fixture(choice_protocol)
bgen_generate_fixture(asterix)
bgen_generate_fixture(sentry_link)
bgen_generate_fixture(string_features)
bgen_generate_fixture(boundary_types)
bgen_generate_fixture(constraints)
bgen_generate_fixture(field_scale)
bgen_generate_fixture(expr_features)
bgen_generate_fixture(mixed_endian)
bgen_generate_fixture(wire_encodings)
bgen_generate_fixture(fx_block)
bgen_generate_fixture(fx_string)
bgen_generate_fixture(auto_sequence)
bgen_generate_fixture(default_initial)
bgen_generate_fixture(string_prefix_incl)
bgen_generate_fixture(bitmap_advanced)
bgen_generate_fixture(fx_advanced)
bgen_generate_fixture(fx_choice)
bgen_generate_fixture(enum_arrays)
bgen_generate_fixture(signed_length)
bgen_generate_fixture(send_only_leaf)
bgen_generate_fixture(non_overlap_ranges)
bgen_generate_fixture(stress_large)
bgen_generate_fixture(constraint_tighten)
bgen_generate_fixture(direction_qualified)
bgen_generate_fixture(frame_basic)
bgen_generate_fixture(frame_config)
bgen_generate_fixture(frame_footer)
bgen_generate_fixture(frame_direction)
bgen_generate_fixture(frame_array)
bgen_generate_fixture(fx_ia5_string)
bgen_generate_fixture(frame_length_offset)
bgen_generate_fixture(outer_scope)
bgen_generate_fixture(present_when_complex)
bgen_generate_fixture(frame_timestamp)
bgen_generate_fixture(ebcdic_strings)
bgen_generate_fixture(inline_case_collision)
bgen_generate_fixture(bitmap_wide_fixed)
bgen_generate_fixture(optional_constrained)
bgen_generate_fixture(inline_field_types)
bgen_generate_fixture(auto_count)
bgen_generate_fixture(auto_struct_length)
bgen_generate_fixture(frame_count)
bgen_generate_fixture(frame_payload_length)
bgen_generate_fixture(constraints_extended)
bgen_generate_fixture(length_arith)
bgen_generate_fixture(frame_length_arith)
bgen_generate_fixture(inline_enum)
bgen_generate_fixture(frame_payload_length_from)
bgen_generate_fixture(format_binary)
bgen_generate_fixture(msg_config)
bgen_generate_fixture(msg_config_inline)
bgen_generate_fixture(constants_everywhere)
bgen_generate_fixture(bytes_numeric)
bgen_generate_fixture(type_name_override)

# ============================================================================
# Single test executable combining parser/validator and generated code tests
# ============================================================================

add_executable(bgen_tests
    # Parser, validator, analyzer, codegen tests
    test_auto_expr_parser.cpp
    test_parser.cpp
    test_validator.cpp
    test_session_analyzer.cpp
    test_codegen_output.cpp
    test_imports.cpp
    test_wire_sizer.cpp
    # Generated code roundtrip / integration tests
    test_generated_roundtrip.cpp
    test_generated_session.cpp
    test_generated_protocols.cpp
    test_string_roundtrip.cpp
    test_string_overflow.cpp
    test_boundary_roundtrip.cpp
    test_constraints.cpp
    test_wire_format.cpp
    test_error_paths.cpp
    test_field_scale_roundtrip.cpp
    test_expr_roundtrip.cpp
    test_mixed_endian_roundtrip.cpp
    test_wire_encoding_roundtrip.cpp
    test_fx_roundtrip.cpp
    test_fx_edge_cases.cpp
    test_misc_roundtrip.cpp
    test_bitmap_roundtrip.cpp
    test_asterix_roundtrip.cpp
    test_expr_edge_cases.cpp
    test_stress_roundtrip.cpp
    test_constraint_tighten.cpp
    test_to_string.cpp
    test_frame_roundtrip.cpp
    test_outer_scope_roundtrip.cpp
    test_frame_length_offset_roundtrip.cpp
    test_present_when_complex.cpp
    test_frame_timestamp.cpp
    test_ebcdic_roundtrip.cpp
    test_inline_case_collision.cpp
    test_inline_field_types.cpp
    test_auto_count.cpp
    test_auto_struct_length.cpp
    test_constraints_extended.cpp
    test_frame_count.cpp
    test_frame_payload_length.cpp
    test_frame_payload_length_from.cpp
    test_format_binary.cpp
    test_length_arith.cpp
    test_frame_length_arith.cpp
    test_inline_enum.cpp
    test_msg_config.cpp
    test_constants_everywhere.cpp
    test_bytes_numeric.cpp
    test_string_encoding_edge_cases.cpp
    test_type_name_override.cpp
    # JSON serialization roundtrip tests
    test_json_roundtrip.cpp
)

add_dependencies(bgen_tests
    gen_all_types gen_struct_features gen_bitmap_fx
    gen_arrays_choices gen_session_protocol
    gen_inline_struct gen_choice_protocol
    gen_asterix gen_sentry_link
    gen_string_features gen_boundary_types gen_constraints
    gen_field_scale gen_expr_features gen_mixed_endian
    gen_wire_encodings
    gen_fx_block gen_fx_string
    gen_auto_sequence
    gen_default_initial gen_string_prefix_incl
    gen_bitmap_advanced gen_fx_advanced
    gen_fx_choice gen_enum_arrays gen_signed_length
    gen_send_only_leaf gen_non_overlap_ranges
    gen_stress_large gen_constraint_tighten
    gen_direction_qualified
    gen_frame_basic
    gen_frame_config
    gen_frame_footer
    gen_frame_direction
    gen_frame_array
    gen_fx_ia5_string
    gen_frame_length_offset
    gen_outer_scope
    gen_present_when_complex
    gen_frame_timestamp
    gen_ebcdic_strings
    gen_inline_case_collision
    gen_bitmap_wide_fixed
    gen_optional_constrained
    gen_inline_field_types
    gen_auto_count
    gen_auto_struct_length
    gen_frame_count
    gen_frame_payload_length
    gen_frame_payload_length_from
    gen_format_binary
    gen_constraints_extended
    gen_length_arith
    gen_frame_length_arith
    gen_inline_enum
    gen_msg_config
    gen_msg_config_inline
    gen_constants_everywhere
    gen_bytes_numeric
    gen_type_name_override
)

target_link_libraries(bgen_tests PRIVATE
    bgen_lib
    conduit
    conduit_warnings
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)

# Define paths to test fixtures and protocol files
target_compile_definitions(bgen_tests PRIVATE
    BGEN_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_include_directories(bgen_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

catch_discover_tests(bgen_tests)

# ============================================================================
# Python backend tests (separate executable)
# ============================================================================

add_executable(bgen_python_tests
    test_python_backend.cpp
    test_python_codegen_extended.cpp
)

target_link_libraries(bgen_python_tests PRIVATE
    bgen_lib
    conduit_warnings
    Catch2::Catch2WithMain
)

target_compile_definitions(bgen_python_tests PRIVATE
    BGEN_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

catch_discover_tests(bgen_python_tests)

# ============================================================================
# Java backend tests (separate executable)
# ============================================================================

add_executable(bgen_java_tests
    test_java_backend.cpp
    test_java_codegen_extended.cpp
)

target_link_libraries(bgen_java_tests PRIVATE
    bgen_lib
    conduit_warnings
    Catch2::Catch2WithMain
)

target_compile_definitions(bgen_java_tests PRIVATE
    BGEN_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

catch_discover_tests(bgen_java_tests)

# bgen fuzz
if(CONDUIT_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()