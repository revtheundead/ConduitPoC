cmake_minimum_required(VERSION 3.10)
project(corba-adaptor VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Strict C++11
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

# -----------------------------------------------------------------------------
# Dependencies: Threads, ACE/TAO 5.4, cpp11-compat
# -----------------------------------------------------------------------------

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Require ACE-TAO 5.4 exactly. ACE and TAO historically shared a version
# number; newer versions are not ABI-compatible with the adaptor.
find_package(TAO 5.4 EXACT REQUIRED)

message(STATUS "ACE include:   ${ACE_INCLUDE_DIRS}")
message(STATUS "TAO include:   ${TAO_INCLUDE_DIRS}")
message(STATUS "TAO idl:       ${TAO_IDL_COMPILER}")
message(STATUS "TAO version:   ${TAO_VERSION}")

# cpp11-compat: polyfills + bgen11 runtime for the translated codec headers.
if(NOT TARGET cpp11::compat)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cpp11-compat/CMakeLists.txt")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp11-compat cpp11-compat)
    else()
        find_package(cpp11_compat REQUIRED)
    endif()
endif()

# -----------------------------------------------------------------------------
# Warnings
# -----------------------------------------------------------------------------

add_library(adaptor_warnings INTERFACE)
if(MSVC)
    target_compile_options(adaptor_warnings INTERFACE /W4)
else()
    target_compile_options(adaptor_warnings INTERFACE
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

# -----------------------------------------------------------------------------
# IDL compilation
# -----------------------------------------------------------------------------

set(IDL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/idl-generated")

tao_idl_generate(
    IDL_FILE    "${CMAKE_CURRENT_SOURCE_DIR}/idl/CorbaAdaptor.idl"
    OUTPUT_DIR  "${IDL_OUTPUT_DIR}"
)

add_library(corba_adaptor_idl STATIC
    ${TAO_IDL_GENERATED_SOURCES}
)
target_include_directories(corba_adaptor_idl PUBLIC "${IDL_OUTPUT_DIR}")
target_link_libraries(corba_adaptor_idl PUBLIC
    TAO::PortableServer
    TAO::AnyTypeCode
    TAO::TAO
    ACE::ACE
)
if(TARGET TAO::CosNaming)
    target_link_libraries(corba_adaptor_idl PUBLIC TAO::CosNaming)
endif()
if(NOT MSVC)
    target_compile_options(corba_adaptor_idl PRIVATE -w)
endif()

# -----------------------------------------------------------------------------
# Committed bgen-generated (C++11 translated) codec headers
# -----------------------------------------------------------------------------

add_subdirectory(generated/tcp-peer)
add_subdirectory(generated/corba-peer)

# -----------------------------------------------------------------------------
# Optional: regenerate codec via bgen + bgen-cpp11 converter
# -----------------------------------------------------------------------------

option(CORBA_ADAPTOR_REGENERATE_BGEN
    "Add a custom target that regenerates codec/*.hpp via bgen + bgen-cpp11" OFF)
if(CORBA_ADAPTOR_REGENERATE_BGEN)
    find_program(BGEN_EXECUTABLE bgen)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(NOT BGEN_EXECUTABLE)
        message(WARNING "bgen not found; regenerate_bgen will fail at build time")
    endif()
    set(_bgen_cpp11
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/bgen-cpp11/bgen_to_cpp11.py")
    add_custom_target(regenerate_bgen
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/tcp-peer"
        COMMAND ${BGEN_EXECUTABLE}
                --input  "${CMAKE_CURRENT_SOURCE_DIR}/protocols/tcp-peer/tcp_peer.bmdl.xml"
                --output "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/tcp-peer"
                --namespace tcp_peer
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/corba-peer"
        COMMAND ${BGEN_EXECUTABLE}
                --input  "${CMAKE_CURRENT_SOURCE_DIR}/protocols/corba-peer/corba_peer.bmdl.xml"
                --output "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/corba-peer"
                --namespace corba_peer
        COMMAND ${Python3_EXECUTABLE} "${_bgen_cpp11}"
                --input  "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/tcp-peer"
                --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/tcp-peer"
        COMMAND ${Python3_EXECUTABLE} "${_bgen_cpp11}"
                --input  "${CMAKE_CURRENT_BINARY_DIR}/_bgen-raw/corba-peer"
                --output "${CMAKE_CURRENT_SOURCE_DIR}/generated/corba-peer"
        COMMENT "Regenerating bgen codec and translating to C++11"
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
# Main adaptor library
# -----------------------------------------------------------------------------

add_library(corba_adaptor_lib STATIC
    src/tcp_peer.cpp
    src/ace_tcp_client.cpp
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
)

target_link_libraries(corba_adaptor_lib PUBLIC
    corba_adaptor_idl
    tcp_peer_codec
    corba_peer_codec
    cpp11::compat
    adaptor_warnings
    Threads::Threads
)

# -----------------------------------------------------------------------------
# Executable
# -----------------------------------------------------------------------------

add_executable(corba-adaptor src/main.cpp)
target_link_libraries(corba-adaptor PRIVATE
    corba_adaptor_lib
    adaptor_warnings
)
