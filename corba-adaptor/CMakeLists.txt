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
# Main adaptor library
# ============================================================================

add_library(corba_adaptor_lib STATIC
    src/tcp_peer.cpp
    src/corba_peer.cpp
    src/supplier.cpp
    src/command_servant.cpp
    src/adaptor.cpp
)

target_include_directories(corba_adaptor_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${IDL_OUTPUT_DIR}"
)

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
