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
#       [LANGUAGE  cpp|python|java]
#       [PROTOCOL_NAME asterix]
#   )
#
# LANGUAGE defaults to "cpp" for backward compatibility.
# When LANGUAGE is "python", output .py files and create a Python package target.
# When LANGUAGE is "java", output .java files and create a Java source target.

function(bgen_generate)
    cmake_parse_arguments(BGEN "" "TARGET;INPUT;OUTPUT_DIR;NAMESPACE;PROTOCOL_NAME;LANGUAGE" "" ${ARGN})

    if(NOT BGEN_TARGET)
        message(FATAL_ERROR "bgen_generate: TARGET is required")
    endif()
    if(NOT BGEN_INPUT)
        message(FATAL_ERROR "bgen_generate: INPUT is required")
    endif()
    if(NOT BGEN_OUTPUT_DIR)
        message(FATAL_ERROR "bgen_generate: OUTPUT_DIR is required")
    endif()

    # Default language is cpp
    if(NOT BGEN_LANGUAGE)
        set(BGEN_LANGUAGE "cpp")
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
    list(APPEND _cmd --language "${BGEN_LANGUAGE}")

    # Expected output files depend on the language
    if(BGEN_LANGUAGE STREQUAL "python")
        set(_outputs
            "${_output_abs}/__init__.py"
            "${_output_abs}/constants.py"
            "${_output_abs}/types.py"
            "${_output_abs}/structs.py"
            "${_output_abs}/messages.py"
            "${_output_abs}/sessions.py"
            "${_output_abs}/codec.py"
            "${_output_abs}/framer.py"
            "${_output_abs}/protocol.py"
            "${_output_abs}/errors.py"
        )
    elseif(BGEN_LANGUAGE STREQUAL "java")
        set(_outputs
            "${_output_abs}/Constants.java"
            "${_output_abs}/Types.java"
            "${_output_abs}/BitReader.java"
            "${_output_abs}/BitWriter.java"
            "${_output_abs}/StreamFramer.java"
            "${_output_abs}/ProtocolDescriptor.java"
            "${_output_abs}/DecodedMessage.java"
            "${_output_abs}/EncodeResult.java"
            "${_output_abs}/ConduitError.java"
        )
    else()
        # C++ outputs
        set(_outputs
            "${_output_abs}/constants.hpp"
            "${_output_abs}/types.hpp"
            "${_output_abs}/structs.hpp"
            "${_output_abs}/messages.hpp"
            "${_output_abs}/sessions.hpp"
            "${_output_abs}/protocol.hpp"
        )
        if(BGEN_PROTOCOL_NAME)
            list(APPEND _outputs "${_output_abs}/${BGEN_PROTOCOL_NAME}.hpp")
        endif()
    endif()

    add_custom_command(
        OUTPUT  ${_outputs}
        COMMAND ${_cmd}
        DEPENDS bgen "${_input_abs}"
        COMMENT "bgen(${BGEN_LANGUAGE}): generating ${BGEN_TARGET} from ${BGEN_INPUT}"
        VERBATIM
    )

    # Interface library so dependents can just link to the target
    add_library(${BGEN_TARGET} INTERFACE)

    if(BGEN_LANGUAGE STREQUAL "cpp")
        target_include_directories(${BGEN_TARGET} INTERFACE "${_output_abs}/..")
    endif()

    target_sources(${BGEN_TARGET} INTERFACE ${_outputs})

    # Ensure the generation runs before anything that depends on the target
    add_custom_target(${BGEN_TARGET}_generate DEPENDS ${_outputs})
    add_dependencies(${BGEN_TARGET} ${BGEN_TARGET}_generate)
endfunction()
