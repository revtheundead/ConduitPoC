cmake_minimum_required(VERSION 3.20)
project(conduit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Options
# ============================================================================

option(CONDUIT_BUILD_BGEN            "Build bgen code generator" ON)
option(CONDUIT_BUILD_TESTS           "Build unit tests"          ON)
option(CONDUIT_BUILD_EXAMPLES        "Build examples"            OFF)
option(CONDUIT_BUILD_BENCHMARKS      "Build benchmarks"          OFF)
option(CONDUIT_BUILD_FUZZ            "Build fuzz testing harnesses (requires Clang)" OFF)
option(CONDUIT_ENABLE_SANITIZERS     "Enable address and undefined-behavior sanitizers" OFF)
option(CONDUIT_ENABLE_COVERAGE       "Enable code coverage instrumentation (GCC/Clang)" OFF)
option(CONDUIT_BUILD_CODEC_CABI      "Build codec-only C ABI shared library"    OFF)
option(CONDUIT_BUILD_CABI            "Build full Transceiver C ABI shared lib"  OFF)
option(CONDUIT_BUILD_PYTHON_BINDINGS "Build Python bindings (requires CABI)"    OFF)
option(CONDUIT_BUILD_JAVA_BINDINGS   "Build Java bindings (requires CABI)"      OFF)
option(CONDUIT_BUILD_JNI             "Build JNI shared libraries for Java 11+"  OFF)

# ============================================================================
# Compiler warnings
# ============================================================================

add_library(conduit_warnings INTERFACE)

if(MSVC)
    target_compile_options(conduit_warnings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
    )
else()
    target_compile_options(conduit_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Werror=return-type -Wsign-conversion -Wdouble-promotion
    )
    # GCC-specific warnings
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(conduit_warnings INTERFACE
            -Wlogical-op
        )
    endif()
endif()

# ============================================================================
# Sanitizers (ASAN + UBSAN)
# ============================================================================

if(CONDUIT_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ============================================================================
# Code coverage (GCC/Clang only)
# ============================================================================

if(CONDUIT_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate)
    endif()
endif()

# ============================================================================
# Third-party dependencies
# ============================================================================

set(CONDUIT_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# --- pugixml (header + source, used by bgen) ---
# Vendored in third_party/pugixml; no network needed.
if(CONDUIT_BUILD_BGEN AND NOT TARGET pugixml::pugixml)
    if(NOT EXISTS "${CONDUIT_THIRD_PARTY_DIR}/pugixml/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/pugixml not found. "
            "Please place pugixml sources in ${CONDUIT_THIRD_PARTY_DIR}/pugixml/")
    endif()
    set(PUGIXML_INSTALL OFF CACHE INTERNAL "")
    set(PUGIXML_BUILD_TESTS OFF CACHE INTERNAL "")
    add_subdirectory(
        "${CONDUIT_THIRD_PARTY_DIR}/pugixml"
        "${CMAKE_BINARY_DIR}/_deps/pugixml"
    )
endif()

# --- nlohmann/json (header-only, used by generated C++ JSON code) ---
# Vendored in third_party/nlohmann; no network needed.
if(NOT TARGET nlohmann_json::nlohmann_json)
    if(NOT EXISTS "${CONDUIT_THIRD_PARTY_DIR}/nlohmann/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/nlohmann not found. "
            "Please place nlohmann/json in ${CONDUIT_THIRD_PARTY_DIR}/nlohmann/")
    endif()
    add_subdirectory(
        "${CONDUIT_THIRD_PARTY_DIR}/nlohmann"
        "${CMAKE_BINARY_DIR}/_deps/nlohmann"
    )
endif()

# --- Catch2 (compiled as part of the test build) ---
# Vendored in third_party/Catch2; no network needed.
if((CONDUIT_BUILD_TESTS OR CONDUIT_BUILD_BENCHMARKS) AND NOT TARGET Catch2::Catch2WithMain)
    if(NOT EXISTS "${CONDUIT_THIRD_PARTY_DIR}/Catch2/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/Catch2 not found. "
            "Please place Catch2 v3 sources in ${CONDUIT_THIRD_PARTY_DIR}/Catch2/")
    endif()
    set(CATCH_INSTALL_DOCS OFF CACHE INTERNAL "")
    set(CATCH_INSTALL_EXTRAS OFF CACHE INTERNAL "")
    add_subdirectory(
        "${CONDUIT_THIRD_PARTY_DIR}/Catch2"
        "${CMAKE_BINARY_DIR}/_deps/Catch2"
    )
    list(APPEND CMAKE_MODULE_PATH "${CONDUIT_THIRD_PARTY_DIR}/Catch2/extras")
endif()

# ============================================================================
# conduit_codec — pure computation, no OS deps (except threading for StreamFramer mutex)
# ============================================================================

set(CONDUIT_CODEC_SOURCES
    src/core/error.cpp
    src/io/bit_reader.cpp
    src/io/bit_writer.cpp
    src/logging/logger.cpp
    src/transceiver/stream_framer.cpp
)

add_library(conduit_codec STATIC ${CONDUIT_CODEC_SOURCES})

target_include_directories(conduit_codec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(conduit_codec PUBLIC cxx_std_23)
target_link_libraries(conduit_codec PRIVATE conduit_warnings)

# When building shared CABI libraries, static libs must be -fPIC
if(CONDUIT_BUILD_CODEC_CABI OR CONDUIT_BUILD_CABI)
    set_target_properties(conduit_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(conduit_codec PUBLIC Threads::Threads)
endif()

# ============================================================================
# conduit — full library (depends on conduit_codec, adds transport + threading)
# ============================================================================

set(CONDUIT_TRANSCEIVER_SOURCES
    src/transceiver/peer.cpp
    src/transceiver/handler.cpp
    src/transceiver/message_log.cpp
    src/transceiver/transceiver.cpp
    src/transceiver/transport/tcp_client.cpp
    src/transceiver/transport/tcp_server.cpp
    src/transceiver/transport/udp.cpp
    src/transceiver/transport/serial_common.cpp
)

if(WIN32)
    list(APPEND CONDUIT_TRANSCEIVER_SOURCES
        src/transceiver/transport/socket_ops_win32.cpp
        src/transceiver/transport/serial_win32.cpp
    )
else()
    list(APPEND CONDUIT_TRANSCEIVER_SOURCES
        src/transceiver/transport/socket_ops_posix.cpp
        src/transceiver/transport/serial_posix.cpp
        src/transceiver/transport/poll_set.cpp
    )
endif()

add_library(conduit STATIC ${CONDUIT_TRANSCEIVER_SOURCES})

target_include_directories(conduit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(conduit PUBLIC cxx_std_23)
target_link_libraries(conduit PUBLIC conduit_codec)
target_link_libraries(conduit PRIVATE conduit_warnings)

if(CONDUIT_BUILD_CABI)
    set_target_properties(conduit PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(WIN32)
    target_link_libraries(conduit PUBLIC ws2_32)
endif()

# ============================================================================
# C ABI shared libraries (optional)
# ============================================================================

if(CONDUIT_BUILD_CODEC_CABI)
    add_library(conduit_codec_cabi SHARED src/cabi/conduit_codec_cabi.cpp)
    target_link_libraries(conduit_codec_cabi PRIVATE conduit_codec conduit_warnings)
    target_include_directories(conduit_codec_cabi PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    set_target_properties(conduit_codec_cabi PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

if(CONDUIT_BUILD_CABI)
    add_library(conduit_cabi SHARED src/cabi/conduit_cabi.cpp)
    target_link_libraries(conduit_cabi PRIVATE conduit conduit_warnings)
    target_include_directories(conduit_cabi PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    set_target_properties(conduit_cabi PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

# ============================================================================
# JNI shared libraries (Java 11+, optional)
# ============================================================================

if(CONDUIT_BUILD_JNI)
    find_package(JNI REQUIRED)

    if(CONDUIT_BUILD_CODEC_CABI)
        add_library(conduit_codec_jni SHARED src/jni/conduit_codec_jni.cpp)
        target_link_libraries(conduit_codec_jni PRIVATE
            conduit_codec_cabi conduit_warnings ${JNI_LIBRARIES})
        target_include_directories(conduit_codec_jni PRIVATE
            ${JNI_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        )
        set_target_properties(conduit_codec_jni PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
    endif()

    if(CONDUIT_BUILD_CABI)
        add_library(conduit_jni SHARED src/jni/conduit_jni.cpp)
        target_link_libraries(conduit_jni PRIVATE
            conduit_cabi conduit_warnings ${JNI_LIBRARIES})
        target_include_directories(conduit_jni PRIVATE
            ${JNI_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        )
        set_target_properties(conduit_jni PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
    endif()
endif()

# ============================================================================
# Subdirectories
# ============================================================================

if(CONDUIT_BUILD_BGEN)
    add_subdirectory(bgen)
endif()

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CONDUIT_BUILD_EXAMPLES)
    if(NOT CONDUIT_BUILD_BGEN)
        message(FATAL_ERROR "CONDUIT_BUILD_EXAMPLES requires CONDUIT_BUILD_BGEN=ON")
    endif()
    add_subdirectory(examples)
endif()

if(CONDUIT_BUILD_BENCHMARKS)
    if(NOT CONDUIT_BUILD_BGEN)
        message(FATAL_ERROR "CONDUIT_BUILD_BENCHMARKS requires CONDUIT_BUILD_BGEN=ON")
    endif()
    add_subdirectory(benchmarks)
endif()

# ============================================================================
# Code coverage target
# ============================================================================

include(cmake/Coverage.cmake)

# ============================================================================
# Install rules
# ============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS conduit conduit_codec conduit_warnings
    EXPORT conduit-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/conduit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(CONDUIT_BUILD_BGEN)
    install(TARGETS bgen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES cmake/BgenGenerate.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
    )
endif()

# CMake package config for find_package(conduit)
install(EXPORT conduit-targets
    FILE conduit-targets.cmake
    NAMESPACE conduit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/conduit-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/conduit-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/conduit-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/conduit-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/conduit-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/conduit
)
