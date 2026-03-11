# ============================================================================
# Conduit — Examples
#
# Built only when CONDUIT_BUILD_EXAMPLES=ON.
# Requires CONDUIT_BUILD_BGEN=ON (bgen must be available to generate code).
# ============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/BgenGenerate.cmake)

# ----------------------------------------------------------------------------
# Generate protocol code from BMDL fixture files
# ----------------------------------------------------------------------------

bgen_generate(
    TARGET    sentry_link_generated
    INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/sentry_link.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/sentry_link
)

# ----------------------------------------------------------------------------
# SentryLink example — TCP server + client
# ----------------------------------------------------------------------------

add_executable(sentry_link_example sentry_link_example.cpp)

target_link_libraries(sentry_link_example
    PRIVATE conduit conduit_warnings sentry_link_generated
)

target_include_directories(sentry_link_example
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Place example binaries under build/examples/ instead of the global
# build/lib/.  Per-config overrides prevent MSVC multi-config generators
# from appending a config subdirectory.
set_target_properties(sentry_link_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set_target_properties(sentry_link_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/examples"
    )
endforeach()

# ----------------------------------------------------------------------------
# Generate ASTERIX protocol code
# ----------------------------------------------------------------------------

bgen_generate(
    TARGET    asterix_generated
    INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/asterix.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/asterix
)

# ----------------------------------------------------------------------------
# ASTERIX example — UDP sender + receiver
# ----------------------------------------------------------------------------

add_executable(asterix_example asterix_example.cpp)

target_link_libraries(asterix_example
    PRIVATE conduit conduit_warnings asterix_generated
)

target_include_directories(asterix_example
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

set_target_properties(asterix_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set_target_properties(asterix_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/examples"
    )
endforeach()
