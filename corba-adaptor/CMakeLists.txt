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

# Conduit — required for codec sessions and the TCP transceiver.
set(CONDUIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../conduit")
if(NOT EXISTS "${CONDUIT_DIR}/include")
    message(FATAL_ERROR
        "corba-adaptor: Conduit include dir not found at ${CONDUIT_DIR}/include. "
        "The adaptor requires Conduit for both codec sessions and the TCP "
        "transceiver — build the conduit project first.")
endif()

if(NOT TARGET conduit)
    if(EXISTS "${CONDUIT_DIR}/CMakeLists.txt")
        add_subdirectory("${CONDUIT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_conduit_build"
                         EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "corba-adaptor: Could not locate Conduit build targets. "
            "Expected a 'conduit' target or a buildable conduit tree at "
            "${CONDUIT_DIR}.")
    endif()
endif()

# Verify the target name matches what we'll link against — conduit's build
# could rename or split this target in the future, and we'd rather fail at
# configure time than with a confusing linker error.
if(NOT TARGET conduit)
    message(FATAL_ERROR
        "corba-adaptor: expected a 'conduit' CMake target to be defined after "
        "adding the conduit subdirectory at ${CONDUIT_DIR}, but none was found. "
        "Check conduit's top-level CMakeLists.txt for the correct target name "
        "and update corba-adaptor/CMakeLists.txt to match.")
endif()

message(STATUS "Conduit include: ${CONDUIT_DIR}/include")

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

include("${CONDUIT_DIR}/cmake/BgenGenerate.cmake")

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

# Fail fast if bgen_generate didn't actually produce the targets we're about
# to link against — e.g. if its signature changes and TARGET stops meaning
# "create a library with this name".
foreach(_required_target conduit tcp_peer_codec corba_peer_codec)
    if(NOT TARGET ${_required_target})
        message(FATAL_ERROR
            "corba-adaptor: required CMake target '${_required_target}' is not "
            "defined. The adaptor library cannot be built without it.")
    endif()
endforeach()

# ============================================================================
# Main adaptor library
# ============================================================================

add_library(corba_adaptor_lib STATIC
    src/tcp_peer.cpp
    src/corba_peer.cpp
    src/command_servant.cpp
    src/naming.cpp
    src/adaptor.cpp
)

target_include_directories(corba_adaptor_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${IDL_OUTPUT_DIR}"
    "${CONDUIT_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(corba_adaptor_lib PUBLIC
    corba_adaptor_idl
    tcp_peer_codec
    corba_peer_codec
    conduit
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
