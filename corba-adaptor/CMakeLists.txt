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
# Options
# ============================================================================

option(ADAPTOR_USE_BGEN_CODECS
    "Build with bgen-generated codecs (requires conduit + bgen)" OFF)

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

# Conduit (for codec integration — session traits and codec types)
set(CONDUIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../conduit")
if(EXISTS "${CONDUIT_DIR}/include")
    message(STATUS "Conduit include: ${CONDUIT_DIR}/include")
else()
    message(WARNING "Conduit include dir not found at ${CONDUIT_DIR}/include — "
                    "codec integration will fail to compile")
endif()

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

# Suppress warnings in generated code
if(NOT MSVC)
    target_compile_options(corba_adaptor_idl PRIVATE -w)
endif()

# ============================================================================
# bgen code generation (optional — enable with ADAPTOR_USE_BGEN_CODECS=ON)
# ============================================================================
#
# When enabled, bgen compiles the BMDL protocol definitions into C++
# session classes that the codec bridge uses for encode/decode.
#
# Requires the conduit project to be built first (bgen executable).
# ============================================================================

if(ADAPTOR_USE_BGEN_CODECS)
    # Import conduit's bgen_generate() function
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
endif()

# ============================================================================
# Main adaptor library
# ============================================================================

add_library(corba_adaptor_lib STATIC
    src/tcp_peer.cpp
    src/corba_peer.cpp
    src/supplier.cpp
    src/command_servant.cpp
    src/adaptor.cpp
    src/codec/codec_bridge.cpp
    src/codec/session_factory.cpp
)

target_include_directories(corba_adaptor_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${IDL_OUTPUT_DIR}"
    "${CONDUIT_DIR}/include"
)

if(ADAPTOR_USE_BGEN_CODECS)
    target_include_directories(corba_adaptor_lib PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(corba_adaptor_lib PUBLIC
        tcp_peer_codec
        corba_peer_codec
    )
    target_compile_definitions(corba_adaptor_lib PUBLIC ADAPTOR_HAS_BGEN_CODECS=1)
endif()

target_link_libraries(corba_adaptor_lib PUBLIC
    corba_adaptor_idl
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
