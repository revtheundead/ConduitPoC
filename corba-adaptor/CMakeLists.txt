cmake_minimum_required(VERSION 3.20)
project(corba-adaptor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output everything to a single directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

# ============================================================================
# Dependencies
# ============================================================================

# Threading
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ACE / TAO
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(TAO REQUIRED)

message(STATUS "ACE include: ${ACE_INCLUDE_DIRS}")
message(STATUS "TAO include: ${TAO_INCLUDE_DIRS}")
message(STATUS "TAO IDL:     ${TAO_IDL_COMPILER}")
if(TAO_VERSION)
    message(STATUS "TAO version: ${TAO_VERSION}")
endif()

# ============================================================================
# Conduit — headers, libraries, and bgen
# ============================================================================
#
# The adaptor is a separate application from Conduit.  It requires:
#   - Conduit headers       (include/conduit/)
#   - Conduit static libs   (libconduit.a, libconduit_codec.a)
#   - The bgen executable   (bin/bgen)
#
# Discovery strategy:
#   1. Try find_package(conduit) for a cmake-installed Conduit
#   2. Fall back to CONDUIT_ROOT pointing at a release tarball or
#      install prefix
#
# Usage:
#   cmake -DCONDUIT_ROOT=/path/to/conduit-0.1.0-linux-x86_64 ...
# Or (if installed via cmake --install):
#   cmake -Dconduit_ROOT=/usr/local ...
# ============================================================================

find_package(conduit QUIET CONFIG)

if(conduit_FOUND)
    message(STATUS "Conduit: found via find_package (version ${conduit_VERSION})")
else()
    # Try CONDUIT_ROOT cmake variable or environment variable
    if(NOT CONDUIT_ROOT AND DEFINED ENV{CONDUIT_ROOT})
        set(CONDUIT_ROOT "$ENV{CONDUIT_ROOT}")
    endif()

    if(NOT CONDUIT_ROOT)
        message(FATAL_ERROR
            "Conduit not found.  Set CONDUIT_ROOT to a Conduit release "
            "directory or install prefix:\n"
            "  cmake -DCONDUIT_ROOT=/path/to/conduit-release ...\n"
            "Or install Conduit so that find_package(conduit) succeeds.")
    endif()

    if(NOT EXISTS "${CONDUIT_ROOT}/include/conduit")
        message(FATAL_ERROR
            "CONDUIT_ROOT (${CONDUIT_ROOT}) does not contain include/conduit/")
    endif()

    message(STATUS "Conduit: using CONDUIT_ROOT=${CONDUIT_ROOT}")

    # Import conduit::conduit (transceiver + codec)
    add_library(conduit::conduit STATIC IMPORTED)
    find_library(_CONDUIT_LIB conduit
        PATHS "${CONDUIT_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT _CONDUIT_LIB)
        message(FATAL_ERROR "libconduit not found in ${CONDUIT_ROOT}/lib")
    endif()
    set_target_properties(conduit::conduit PROPERTIES
        IMPORTED_LOCATION "${_CONDUIT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CONDUIT_ROOT}/include"
        INTERFACE_LINK_LIBRARIES "Threads::Threads"
    )

    # Import conduit::conduit_codec
    add_library(conduit::conduit_codec STATIC IMPORTED)
    find_library(_CONDUIT_CODEC_LIB conduit_codec
        PATHS "${CONDUIT_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT _CONDUIT_CODEC_LIB)
        message(FATAL_ERROR "libconduit_codec not found in ${CONDUIT_ROOT}/lib")
    endif()
    set_target_properties(conduit::conduit_codec PROPERTIES
        IMPORTED_LOCATION "${_CONDUIT_CODEC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CONDUIT_ROOT}/include"
        INTERFACE_LINK_LIBRARIES "Threads::Threads"
    )
endif()

# bgen executable — required for code generation
find_program(BGEN_EXECUTABLE bgen
    HINTS "${CONDUIT_ROOT}/bin"
    PATHS ENV PATH
)
if(conduit_DIR AND NOT BGEN_EXECUTABLE)
    # When installed via cmake, bgen is next to the prefix root
    find_program(BGEN_EXECUTABLE bgen
        HINTS "${conduit_DIR}/../../../bin")
endif()
if(NOT BGEN_EXECUTABLE)
    message(FATAL_ERROR
        "bgen executable not found.  Ensure Conduit was built with "
        "CONDUIT_BUILD_BGEN=ON and bgen is in CONDUIT_ROOT/bin or PATH.")
endif()

# Create an imported target so BgenGenerate.cmake finds it
if(NOT TARGET bgen)
    add_executable(bgen IMPORTED GLOBAL)
    set_target_properties(bgen PROPERTIES IMPORTED_LOCATION "${BGEN_EXECUTABLE}")
endif()

message(STATUS "bgen: ${BGEN_EXECUTABLE}")

# ============================================================================
# Compiler warnings
# ============================================================================

add_library(adaptor_warnings INTERFACE)

if(MSVC)
    target_compile_options(adaptor_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(adaptor_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()

# ============================================================================
# IDL compilation
# ============================================================================

set(IDL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

tao_idl_generate(
    IDL_FILE    "${CMAKE_CURRENT_SOURCE_DIR}/idl/CorbaAdaptor.idl"
    OUTPUT_DIR  "${IDL_OUTPUT_DIR}"
)

# Library for generated IDL stubs and skeletons
add_library(corba_adaptor_idl STATIC
    ${TAO_IDL_GENERATED_SOURCES}
)

target_include_directories(corba_adaptor_idl PUBLIC
    "${IDL_OUTPUT_DIR}"
)

target_link_libraries(corba_adaptor_idl PUBLIC
    TAO::PortableServer
    TAO::AnyTypeCode
    TAO::TAO
    ACE::ACE
)

# The Naming Service client bits live in orbsvcs / CosNaming.  Different TAO
# installations expose this via different target names, so just fall back to
# linking the bare library name if the imported target isn't present.
if(TARGET TAO::CosNaming)
    target_link_libraries(corba_adaptor_idl PUBLIC TAO::CosNaming)
else()
    target_link_libraries(corba_adaptor_idl PUBLIC TAO_CosNaming)
endif()

# Suppress warnings in generated code
if(NOT MSVC)
    target_compile_options(corba_adaptor_idl PRIVATE -w)
endif()

# ============================================================================
# bgen code generation for the TCP and CORBA peer protocols
# ============================================================================

include(BgenGenerate)

bgen_generate(
    TARGET      tcp_peer_codec
    INPUT       "${CMAKE_CURRENT_SOURCE_DIR}/protocols/tcp-peer/tcp_peer.bmdl.xml"
    OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated/tcp-peer"
    NAMESPACE   tcp_peer
    PROTOCOL_NAME tcp_peer
)

bgen_generate(
    TARGET      corba_peer_codec
    INPUT       "${CMAKE_CURRENT_SOURCE_DIR}/protocols/corba-peer/corba_peer.bmdl.xml"
    OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated/corba-peer"
    NAMESPACE   corba_peer
    PROTOCOL_NAME corba_peer
)

# ============================================================================
# Main adaptor library
# ============================================================================

add_library(corba_adaptor_lib STATIC
    src/tcp_peer.cpp
    src/corba_peer.cpp
    src/command_servant.cpp
    src/naming.cpp
    src/subscription_manager.cpp
    src/supplier.cpp
    src/adaptor.cpp
)

target_include_directories(corba_adaptor_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${IDL_OUTPUT_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(corba_adaptor_lib PUBLIC
    corba_adaptor_idl
    tcp_peer_codec
    corba_peer_codec
    conduit::conduit
    conduit::conduit_codec
    adaptor_warnings
    Threads::Threads
)

# ============================================================================
# Executable
# ============================================================================

add_executable(corba-adaptor src/main.cpp)

target_link_libraries(corba-adaptor PRIVATE
    corba_adaptor_lib
    adaptor_warnings
)
