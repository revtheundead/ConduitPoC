# BgenGenerate.cmake — invoke bgen at build time
#
# Usage:
#   include(cmake/BgenGenerate.cmake)
#
#   bgen_generate(
#       TARGET    asterix_generated
#       INPUT     ${CMAKE_SOURCE_DIR}/protocols/asterix/asterix.bmdl.xml
#       OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/asterix
#       [NAMESPACE asterix]
#   )
#
# Creates an INTERFACE library <TARGET> whose consumers automatically get the
# generated headers on their include path.  The headers are regenerated
# whenever the INPUT file or bgen itself changes.

function(bgen_generate)
    cmake_parse_arguments(BGEN "" "TARGET;INPUT;OUTPUT_DIR;NAMESPACE;PROTOCOL_NAME" "" ${ARGN})

    if(NOT BGEN_TARGET)
        message(FATAL_ERROR "bgen_generate: TARGET is required")
    endif()
    if(NOT BGEN_INPUT)
        message(FATAL_ERROR "bgen_generate: INPUT is required")
    endif()
    if(NOT BGEN_OUTPUT_DIR)
        message(FATAL_ERROR "bgen_generate: OUTPUT_DIR is required")
    endif()

    if(NOT TARGET bgen)
        message(FATAL_ERROR "bgen_generate: 'bgen' target not found. Enable CONDUIT_BUILD_BGEN=ON.")
    endif()

    get_filename_component(_input_abs "${BGEN_INPUT}" ABSOLUTE)
    get_filename_component(_output_abs "${BGEN_OUTPUT_DIR}" ABSOLUTE)

    # Build the bgen command line
    set(_cmd $<TARGET_FILE:bgen> --input "${_input_abs}" --output "${_output_abs}")
    if(BGEN_NAMESPACE)
        list(APPEND _cmd --namespace "${BGEN_NAMESPACE}")
    endif()

    # Expected output files (must match what bgen actually writes)
    set(_outputs
        "${_output_abs}/constants.hpp"
        "${_output_abs}/types.hpp"
        "${_output_abs}/structs.hpp"
        "${_output_abs}/messages.hpp"
        "${_output_abs}/sessions.hpp"
        "${_output_abs}/protocol.hpp"
    )
    # bgen also generates an umbrella header named <protocol-name>.hpp.
    # If PROTOCOL_NAME is provided, track it as an output for dependency purposes.
    if(BGEN_PROTOCOL_NAME)
        list(APPEND _outputs "${_output_abs}/${BGEN_PROTOCOL_NAME}.hpp")
    endif()

    add_custom_command(
        OUTPUT  ${_outputs}
        COMMAND ${_cmd}
        DEPENDS bgen "${_input_abs}"
        COMMENT "bgen: generating ${BGEN_TARGET} from ${BGEN_INPUT}"
        VERBATIM
    )

    # Interface library so dependents can just link to the target
    add_library(${BGEN_TARGET} INTERFACE)
    target_include_directories(${BGEN_TARGET} INTERFACE "${_output_abs}/..")
    target_sources(${BGEN_TARGET} INTERFACE ${_outputs})

    # Ensure the generation runs before anything that depends on the target
    add_custom_target(${BGEN_TARGET}_generate DEPENDS ${_outputs})
    add_dependencies(${BGEN_TARGET} ${BGEN_TARGET}_generate)
endfunction()
