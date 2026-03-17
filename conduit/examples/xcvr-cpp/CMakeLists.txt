# ============================================================================
# conduit-example-xcvr-cpp — ASTERIX transceiver example (C++)
#
# Built as part of conduit examples (CONDUIT_BUILD_EXAMPLES=ON).
# Assumes conduit targets are already available from the parent build.
# ============================================================================

# Common compile options
function(xcvr_compile_opts target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /permissive- /Zc:__cplusplus /Zc:preprocessor /bigobj)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(MINGW)
            target_compile_options(${target} PRIVATE -Wa,-mbig-obj)
        endif()
    endif()
endfunction()

# Helper: place example binaries in-place (in the example source directory).
# Per-config overrides prevent MSVC multi-config generators from appending
# a config subdirectory.
function(xcvr_set_output_dir target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endforeach()
endfunction()

# ----------------------------------------------------------------------------
# poc_app — TCP client using asterix (client perspective)
# ----------------------------------------------------------------------------

add_executable(poc_app src/poc_app.cpp)
target_link_libraries(poc_app PRIVATE conduit)
target_include_directories(poc_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
xcvr_compile_opts(poc_app)
xcvr_set_output_dir(poc_app)

# ----------------------------------------------------------------------------
# dummy_peer — TCP server (asterix-alt) or client (asterix)
# ----------------------------------------------------------------------------

add_executable(dummy_peer src/dummy_peer.cpp)
target_link_libraries(dummy_peer PRIVATE conduit)
target_include_directories(dummy_peer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix-alt/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
xcvr_compile_opts(dummy_peer)
xcvr_set_output_dir(dummy_peer)
