# FindTAO.cmake
# --------------
# Locate ACE/TAO (DOCGroup) installation.
#
# Searches in order:
#   1. TAO_ROOT / ACE_ROOT environment or CMake variables
#   2. Common system paths (/usr/local, /opt/ACE_TAO, etc.)
#
# Imported targets created:
#   ACE::ACE          - ACE core library
#   TAO::TAO          - TAO core library
#   TAO::PortableServer - POA support
#   TAO::CosNaming    - Naming Service (optional)
#   TAO::AnyTypeCode  - Any/TypeCode support
#
# Variables set:
#   TAO_FOUND, ACE_FOUND
#   TAO_INCLUDE_DIRS, ACE_INCLUDE_DIRS
#   TAO_LIBRARIES, ACE_LIBRARIES
#   TAO_IDL_COMPILER  - path to tao_idl
#   TAO_VERSION

include(FindPackageHandleStandardArgs)

# ---------------------------------------------------------------------------
# Resolve root directories
# ---------------------------------------------------------------------------

if(NOT ACE_ROOT)
    if(DEFINED ENV{ACE_ROOT})
        set(ACE_ROOT "$ENV{ACE_ROOT}")
    endif()
endif()

if(NOT TAO_ROOT)
    if(DEFINED ENV{TAO_ROOT})
        set(TAO_ROOT "$ENV{TAO_ROOT}")
    elseif(ACE_ROOT)
        set(TAO_ROOT "${ACE_ROOT}/TAO")
    endif()
endif()

# Common search paths
set(_TAO_SEARCH_PATHS
    ${ACE_ROOT}
    ${TAO_ROOT}
    /usr/local
    /usr
    /opt/ACE_TAO
    /opt/ace-tao
)

# ---------------------------------------------------------------------------
# Find ACE
# ---------------------------------------------------------------------------

find_path(ACE_INCLUDE_DIR
    NAMES ace/ACE.h
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES include
)

find_library(ACE_LIBRARY
    NAMES ACE
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# ---------------------------------------------------------------------------
# Find TAO
# ---------------------------------------------------------------------------

find_path(TAO_INCLUDE_DIR
    NAMES tao/ORB.h
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES include
)

find_library(TAO_LIBRARY
    NAMES TAO
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

find_library(TAO_PORTABLE_SERVER_LIBRARY
    NAMES TAO_PortableServer
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

find_library(TAO_ANYTYPECODE_LIBRARY
    NAMES TAO_AnyTypeCode
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

find_library(TAO_COSNAMING_LIBRARY
    NAMES TAO_CosNaming
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# ---------------------------------------------------------------------------
# Find tao_idl compiler
# ---------------------------------------------------------------------------

find_program(TAO_IDL_COMPILER
    NAMES tao_idl
    PATHS ${_TAO_SEARCH_PATHS}
    PATH_SUFFIXES bin
)

# ---------------------------------------------------------------------------
# Version detection
# ---------------------------------------------------------------------------

if(TAO_INCLUDE_DIR AND EXISTS "${TAO_INCLUDE_DIR}/tao/Version.h")
    file(STRINGS "${TAO_INCLUDE_DIR}/tao/Version.h" _tao_version_line
         REGEX "#define TAO_VERSION \"[^\"]+\"")
    if(_tao_version_line MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
        set(TAO_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Standard validation
# ---------------------------------------------------------------------------

find_package_handle_standard_args(TAO
    REQUIRED_VARS
        ACE_INCLUDE_DIR ACE_LIBRARY
        TAO_INCLUDE_DIR TAO_LIBRARY
        TAO_PORTABLE_SERVER_LIBRARY
        TAO_ANYTYPECODE_LIBRARY
        TAO_IDL_COMPILER
    VERSION_VAR TAO_VERSION
)

if(TAO_FOUND)
    set(ACE_FOUND TRUE)
    set(ACE_INCLUDE_DIRS "${ACE_INCLUDE_DIR}")
    set(TAO_INCLUDE_DIRS "${TAO_INCLUDE_DIR}" "${ACE_INCLUDE_DIR}")

    # ACE::ACE
    if(NOT TARGET ACE::ACE)
        add_library(ACE::ACE UNKNOWN IMPORTED)
        set_target_properties(ACE::ACE PROPERTIES
            IMPORTED_LOCATION "${ACE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ACE_INCLUDE_DIR}"
        )
    endif()

    # TAO::TAO
    if(NOT TARGET TAO::TAO)
        add_library(TAO::TAO UNKNOWN IMPORTED)
        set_target_properties(TAO::TAO PROPERTIES
            IMPORTED_LOCATION "${TAO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${TAO_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "ACE::ACE"
        )
    endif()

    # TAO::PortableServer
    if(NOT TARGET TAO::PortableServer)
        add_library(TAO::PortableServer UNKNOWN IMPORTED)
        set_target_properties(TAO::PortableServer PROPERTIES
            IMPORTED_LOCATION "${TAO_PORTABLE_SERVER_LIBRARY}"
            INTERFACE_LINK_LIBRARIES "TAO::TAO"
        )
    endif()

    # TAO::AnyTypeCode
    if(NOT TARGET TAO::AnyTypeCode)
        add_library(TAO::AnyTypeCode UNKNOWN IMPORTED)
        set_target_properties(TAO::AnyTypeCode PROPERTIES
            IMPORTED_LOCATION "${TAO_ANYTYPECODE_LIBRARY}"
            INTERFACE_LINK_LIBRARIES "TAO::TAO"
        )
    endif()

    # TAO::CosNaming (optional)
    if(TAO_COSNAMING_LIBRARY AND NOT TARGET TAO::CosNaming)
        add_library(TAO::CosNaming UNKNOWN IMPORTED)
        set_target_properties(TAO::CosNaming PROPERTIES
            IMPORTED_LOCATION "${TAO_COSNAMING_LIBRARY}"
            INTERFACE_LINK_LIBRARIES "TAO::TAO"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# tao_idl_generate() — CMake function to compile IDL files
# ---------------------------------------------------------------------------
#
# Usage:
#   tao_idl_generate(
#       IDL_FILE    path/to/Foo.idl
#       OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/generated
#       [INCLUDE_DIRS dir1 dir2 ...]
#       [FLAGS -Wb,export_macro=FOO_EXPORT ...]
#   )
#
# Sets in parent scope:
#   TAO_IDL_GENERATED_SOURCES  — list of .cpp files
#   TAO_IDL_GENERATED_HEADERS  — list of .h files

function(tao_idl_generate)
    cmake_parse_arguments(ARG "" "IDL_FILE;OUTPUT_DIR" "INCLUDE_DIRS;FLAGS" ${ARGN})

    if(NOT ARG_IDL_FILE)
        message(FATAL_ERROR "tao_idl_generate: IDL_FILE is required")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "tao_idl_generate: OUTPUT_DIR is required")
    endif()

    get_filename_component(_idl_name "${ARG_IDL_FILE}" NAME_WE)
    get_filename_component(_idl_abs  "${ARG_IDL_FILE}" ABSOLUTE)

    # TAO IDL compiler generates these files by default
    set(_suffixes C.cpp C.h S.cpp S.h)
    set(_outputs)
    foreach(_sfx ${_suffixes})
        list(APPEND _outputs "${ARG_OUTPUT_DIR}/${_idl_name}${_sfx}")
    endforeach()

    # Build include flags
    set(_inc_flags)
    foreach(_dir ${ARG_INCLUDE_DIRS})
        list(APPEND _inc_flags "-I${_dir}")
    endforeach()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    add_custom_command(
        OUTPUT  ${_outputs}
        COMMAND ${TAO_IDL_COMPILER}
                ${_inc_flags}
                ${ARG_FLAGS}
                -o "${ARG_OUTPUT_DIR}"
                "${_idl_abs}"
        DEPENDS "${_idl_abs}"
        COMMENT "TAO IDL: compiling ${_idl_name}.idl"
        VERBATIM
    )

    set(TAO_IDL_GENERATED_SOURCES
        "${ARG_OUTPUT_DIR}/${_idl_name}C.cpp"
        "${ARG_OUTPUT_DIR}/${_idl_name}S.cpp"
        PARENT_SCOPE
    )
    set(TAO_IDL_GENERATED_HEADERS
        "${ARG_OUTPUT_DIR}/${_idl_name}C.h"
        "${ARG_OUTPUT_DIR}/${_idl_name}S.h"
        PARENT_SCOPE
    )
endfunction()
