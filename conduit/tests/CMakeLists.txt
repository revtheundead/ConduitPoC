# ============================================================================
# Conduit tests
#
# Catch2 is compiled from vendored sources in third_party/Catch2 —
# the root CMakeLists.txt adds it before reaching this file.
# ============================================================================

include(CTest)
include(Catch)

# ============================================================================
# conduit_tests — all unit + integration tests in a single executable
# ============================================================================

set(CONDUIT_TEST_SOURCES
    test_listener.cpp
    test_error.cpp
    test_endian.cpp
    test_bit_reader.cpp
    test_bit_writer.cpp
    test_bounded_queue.cpp
    test_stream_framer.cpp
    test_handler_registry.cpp
    test_transceiver.cpp
    test_tcp_transport.cpp
    test_serial_config.cpp
    test_message_handler.cpp
    test_serial_config_extended.cpp
    test_transceiver_multi_peer.cpp
    test_transceiver_errors.cpp
    test_udp_transport.cpp
    test_udp_multicast.cpp
    test_tcp_transport_extended.cpp
    test_logger.cpp
    test_string_encoding.cpp
    test_message_log.cpp
)

set(CONDUIT_TEST_LIBS
    conduit
    conduit_warnings
    Catch2::Catch2WithMain
)

# Integration tests using bgen-generated code
if(CONDUIT_BUILD_BGEN)
    include(${CMAKE_SOURCE_DIR}/cmake/BgenGenerate.cmake)

    bgen_generate(
        TARGET    test_session_protocol
        INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/session_protocol.bmdl.xml
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/session_protocol
        NAMESPACE session_test
    )

    bgen_generate(
        TARGET    test_sentry_link
        INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/sentry_link.bmdl.xml
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/sentry_link
        NAMESPACE sentry_link
    )

    bgen_generate(
        TARGET    test_direction_qualified
        INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/direction_qualified.bmdl.xml
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/direction_qualified
        NAMESPACE direction_qualified
    )

    # Also generate choice_protocol here so it is available to both
    # conduit_codec_cabi_test (CONDUIT_BUILD_CODEC_CABI) and
    # conduit_cabi_test (CONDUIT_BUILD_CABI), either of which may be
    # enabled independently.
    bgen_generate(
        TARGET    test_choice_protocol
        INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/choice_protocol.bmdl.xml
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/choice_protocol
        NAMESPACE choice_test
    )

    list(APPEND CONDUIT_TEST_SOURCES
        test_transceiver_config.cpp
        test_integration_sentry_link.cpp
        test_stream_framer_generated.cpp
        test_integration_direction.cpp
    )

    list(APPEND CONDUIT_TEST_LIBS
        test_session_protocol
        test_sentry_link
        test_direction_qualified
    )

    # ========================================================================
    # Python pytest generated packages
    #
    # Generate Python modules from the same BMDL fixtures used by the Python
    # test suite under tests/python/generated/.  A convenience target
    # "pytest_generated" triggers all of them; build.sh / run_tests.sh can
    # build this target before running pytest.
    # ========================================================================

    set(_PYTEST_GEN_DIR "${CMAKE_SOURCE_DIR}/tests/python/generated")

    set(_PYTEST_SCHEMAS
        all_types
        annotation_scope
        arrays_choices
        asterix
        auto_count
        auto_sequence
        auto_struct_length
        bit_alignment
        bitmap_advanced
        bitmap_fx
        bitmap_wide_fixed
        boundary_types
        bytes_numeric
        choice_protocol
        constants_everywhere
        constraint_tighten
        constraints
        constraints_extended
        default_initial
        direction_qualified
        ebcdic_strings
        enum_arrays
        expr_features
        field_scale
        format_binary
        frame_array
        frame_basic
        frame_config
        frame_count
        frame_direction
        frame_footer
        frame_length_arith
        frame_length_offset
        frame_payload_length
        frame_payload_length_from
        frame_timestamp
        fx_advanced
        fx_block
        fx_choice
        fx_ia5_string
        fx_string
        hex_default
        inline_case_collision
        inline_enum
        inline_field_types
        inline_struct
        inline_struct_overlap
        length_arith
        minimal
        mixed_endian
        msg_config
        msg_config_inline
        nested_bitmap
        non_overlap_ranges
        optional_constrained
        outer_scope
        present_when_complex
        send_only_leaf
        sentry_link
        session_protocol
        signed_length
        stress_large
        string_features
        string_prefix_incl
        struct_features
        type_name_override
        uint64_max_default
        wire_encodings
    )

    set(_PYTEST_GEN_TARGETS "")
    foreach(_schema ${_PYTEST_SCHEMAS})
        set(_target "pytest_${_schema}")
        bgen_generate(
            TARGET     ${_target}
            INPUT      ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/${_schema}.bmdl.xml
            OUTPUT_DIR ${_PYTEST_GEN_DIR}/${_schema}
            NAMESPACE  ${_schema}
            LANGUAGE   python
        )
        list(APPEND _PYTEST_GEN_TARGETS ${_target}_generate)
    endforeach()

    add_custom_target(pytest_generated DEPENDS ${_PYTEST_GEN_TARGETS})
endif()

add_executable(conduit_tests ${CONDUIT_TEST_SOURCES})
target_link_libraries(conduit_tests PRIVATE ${CONDUIT_TEST_LIBS})
catch_discover_tests(conduit_tests)

# Place test binary under build/tests/ so build scripts can find it.
# Per-config overrides prevent MSVC multi-config generators from using
# the global CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG> (build/lib/).
set_target_properties(conduit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set_target_properties(conduit_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/tests"
    )
endforeach()

# ============================================================================
# Test CABI shared libraries with registered sessions (for Python/Java tests)
# ============================================================================

if(CONDUIT_BUILD_BGEN AND CONDUIT_BUILD_CODEC_CABI)
    # Build test codec CABI with sessions registered
    add_library(conduit_codec_cabi_test SHARED
        ${CMAKE_SOURCE_DIR}/src/cabi/conduit_codec_cabi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cabi/test_sessions_register.cpp
    )
    target_link_libraries(conduit_codec_cabi_test PRIVATE
        conduit_codec
        conduit_warnings
        test_session_protocol
        test_choice_protocol
        test_sentry_link
        test_direction_qualified
    )
    target_include_directories(conduit_codec_cabi_test PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    )
    target_include_directories(conduit_codec_cabi_test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_compile_definitions(conduit_codec_cabi_test PRIVATE CONDUIT_CODEC_CABI_EXPORTS)
    if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_definitions(conduit_codec_cabi_test PRIVATE
            _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    endif()
    set_target_properties(conduit_codec_cabi_test PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if(WIN32)
        set_target_properties(conduit_codec_cabi_test PROPERTIES PREFIX "")
    endif()
endif()

if(CONDUIT_BUILD_BGEN AND CONDUIT_BUILD_CABI)
    # Build test transceiver CABI with sessions registered
    # Includes both transceiver and codec CABI sources so that
    # test_sessions_register.cpp can call conduit_register_session()
    # (codec registry) and conduit_xcvr_register_session() (xcvr registry).
    add_library(conduit_cabi_test SHARED
        ${CMAKE_SOURCE_DIR}/src/cabi/conduit_cabi.cpp
        ${CMAKE_SOURCE_DIR}/src/cabi/conduit_codec_cabi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cabi/test_sessions_register.cpp
    )
    target_link_libraries(conduit_cabi_test PRIVATE
        conduit
        conduit_codec
        conduit_warnings
        test_session_protocol
        test_choice_protocol
        test_sentry_link
        test_direction_qualified
    )
    target_include_directories(conduit_cabi_test PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    )
    target_include_directories(conduit_cabi_test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_compile_definitions(conduit_cabi_test PRIVATE CONDUIT_CABI_EXPORTS CONDUIT_CODEC_CABI_EXPORTS)
    if(MSVC OR (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_definitions(conduit_cabi_test PRIVATE
            _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    endif()
    set_target_properties(conduit_cabi_test PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if(WIN32)
        set_target_properties(conduit_cabi_test PROPERTIES PREFIX "")
    endif()
endif()

# ============================================================================
# Test JNI shared library (links against conduit_cabi_test for test sessions)
# ============================================================================

if(CONDUIT_BUILD_BGEN AND CONDUIT_BUILD_JNI AND CONDUIT_BUILD_CABI)
    find_package(JNI REQUIRED)

    add_library(conduit_jni_test SHARED
        ${CMAKE_SOURCE_DIR}/src/jni/conduit_jni.cpp
    )
    target_link_libraries(conduit_jni_test PRIVATE
        conduit_cabi_test
        conduit_warnings
        ${JNI_LIBRARIES}
    )
    target_include_directories(conduit_jni_test SYSTEM PRIVATE
        ${JNI_INCLUDE_DIRS}
    )
    target_include_directories(conduit_jni_test PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    )
    set_target_properties(conduit_jni_test PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if(WIN32)
        set_target_properties(conduit_jni_test PROPERTIES PREFIX "")
    endif()
endif()
