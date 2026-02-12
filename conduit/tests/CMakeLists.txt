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
    test_udp_transport.cpp
    test_tcp_transport_extended.cpp
    test_logger.cpp
    test_string_encoding.cpp
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

    list(APPEND CONDUIT_TEST_SOURCES
        test_transceiver_config.cpp
        test_integration_session_protocol.cpp
        test_integration_sentry_link.cpp
        test_stream_framer_generated.cpp
        test_integration_direction.cpp
    )

    list(APPEND CONDUIT_TEST_LIBS
        test_session_protocol
        test_sentry_link
        test_direction_qualified
    )
endif()

add_executable(conduit_tests ${CONDUIT_TEST_SOURCES})
target_link_libraries(conduit_tests PRIVATE ${CONDUIT_TEST_LIBS})
catch_discover_tests(conduit_tests)
