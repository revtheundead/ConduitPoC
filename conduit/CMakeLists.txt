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
option(CONDUIT_BUILD_JAVA_JAR        "Build conduit-java JAR and compile Java tests" OFF)
option(CONDUIT_BUILD_JAVA_PANAMA     "Include Panama FFI sources in JAR (requires JDK 21+)" ON)
option(CONDUIT_BUILD_JAVA_JNI_SOURCES "Include JNI binding sources in JAR" ON)

# ============================================================================
# Compiler warnings
# ============================================================================

add_library(conduit_warnings INTERFACE)

if(MSVC)
    target_compile_options(conduit_warnings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
    )
    # Prevent windows.h from defining min/max macros (conflicts with std::min/std::max)
    # and reduce windows.h include bloat.
    target_compile_definitions(conduit_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
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
    # MinGW: COFF object format has a 65536 section limit; heavy template
    # instantiation (especially with C++23) can exceed this in debug builds.
    if(MINGW)
        target_compile_options(conduit_warnings INTERFACE -Wa,-mbig-obj)
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
# Java JAR build (optional)
# ============================================================================

if(CONDUIT_BUILD_JAVA_JAR)
    find_program(JAVAC_EXECUTABLE javac)
    find_program(JAR_EXECUTABLE jar)
    if(NOT JAVAC_EXECUTABLE OR NOT JAR_EXECUTABLE)
        message(WARNING
            "CONDUIT_BUILD_JAVA_JAR is ON but javac/jar not found on PATH. "
            "Skipping Java JAR build. Install a JDK and ensure javac and jar are in PATH.")
    else()
        # Detect JDK version to decide whether Panama sources can be compiled
        execute_process(
            COMMAND ${JAVAC_EXECUTABLE} -version
            OUTPUT_VARIABLE _javac_version_out
            ERROR_VARIABLE _javac_version_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "[0-9]+" _java_major "${_javac_version_out}")
        message(STATUS "Detected JDK major version: ${_java_major}")

        set(_java_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/bindings/java/src/main/java")
        set(_java_classes_dir "${CMAKE_CURRENT_BINARY_DIR}/java-classes")
        set(_java_jar "${CMAKE_CURRENT_BINARY_DIR}/conduit-java-0.1.0.jar")

        # Collect Java sources — always include the core (non-Panama, non-JNI) files
        file(GLOB _java_core_sources "${_java_src_dir}/io/conduit/*.java")
        set(_java_sources ${_java_core_sources})

        # Panama FFI sources (require JDK 21+)
        set(_panama_files
            "CabiBindings\\.java$"
            "CodecBindings\\.java$"
            "PanamaNativeBinding\\.java$"
            "PanamaCodecBinding\\.java$"
        )

        # JNI binding sources
        set(_jni_files
            "JniNativeBinding\\.java$"
            "JniCodecBinding\\.java$"
        )

        # Determine compiler release level
        set(_javac_flags -d "${_java_classes_dir}")
        if(CONDUIT_BUILD_JAVA_PANAMA AND _java_major GREATER_EQUAL 21)
            list(APPEND _javac_flags --release 21 --enable-preview)
            message(STATUS "Java JAR: including Panama FFI sources (JDK ${_java_major})")
        else()
            list(APPEND _javac_flags --release 11)
            # Exclude Panama-specific files that require JDK 21+
            foreach(_pat ${_panama_files})
                list(FILTER _java_sources EXCLUDE REGEX "${_pat}")
            endforeach()
            if(NOT CONDUIT_BUILD_JAVA_PANAMA)
                message(STATUS "Java JAR: Panama FFI sources excluded (CONDUIT_BUILD_JAVA_PANAMA=OFF)")
            elseif(_java_major LESS 21)
                message(STATUS "Java JAR: Panama sources requested but JDK ${_java_major} < 21; excluding Panama files")
            endif()
        endif()

        # Exclude JNI sources if not requested
        if(NOT CONDUIT_BUILD_JAVA_JNI_SOURCES)
            foreach(_pat ${_jni_files})
                list(FILTER _java_sources EXCLUDE REGEX "${_pat}")
            endforeach()
            message(STATUS "Java JAR: JNI binding sources excluded by CONDUIT_BUILD_JAVA_JNI_SOURCES=OFF")
        endif()

        add_custom_command(
            OUTPUT "${_java_jar}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_java_classes_dir}"
            COMMAND ${JAVAC_EXECUTABLE} ${_javac_flags} ${_java_sources}
            COMMAND ${JAR_EXECUTABLE} cf "${_java_jar}" -C "${_java_classes_dir}" .
            DEPENDS ${_java_sources}
            COMMENT "Building conduit-java-0.1.0.jar"
            VERBATIM
        )
        add_custom_target(conduit_java_jar ALL DEPENDS "${_java_jar}")

        # ---- Java test compilation (requires bgen + fixtures) ----
        if(CONDUIT_BUILD_BGEN AND CONDUIT_BUILD_TESTS)
            set(_java_test_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/java-test-generated")
            set(_java_test_classes_dir "${CMAKE_CURRENT_BINARY_DIR}/java-test-classes")
            set(_java_test_fixtures
                struct_features
                constraints
                field_scale
            )

            # Generate Java code from each test fixture
            set(_java_test_gen_stamps "")
            foreach(_fix ${_java_test_fixtures})
                set(_fix_input "${CMAKE_CURRENT_SOURCE_DIR}/bgen/tests/fixtures/${_fix}.bmdl.xml")
                set(_stamp "${_java_test_gen_dir}/${_fix}/.stamp")
                add_custom_command(
                    OUTPUT "${_stamp}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_java_test_gen_dir}/${_fix}"
                    COMMAND $<TARGET_FILE:bgen> --input "${_fix_input}"
                        --output "${_java_test_gen_dir}/${_fix}" --language java
                    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
                    DEPENDS bgen "${_fix_input}"
                    COMMENT "bgen: generating Java test code for ${_fix}"
                    VERBATIM
                )
                list(APPEND _java_test_gen_stamps "${_stamp}")
            endforeach()

            add_custom_target(java_test_codegen DEPENDS ${_java_test_gen_stamps})

            # Collect generated Java sources at build time using a cmake -P script
            # (avoids the POSIX-only 'find' command for cross-platform support)
            set(_java_test_compile_script "${CMAKE_CURRENT_BINARY_DIR}/java_test_compile.cmake")
            file(WRITE "${_java_test_compile_script}" "
file(GLOB_RECURSE _src \"${_java_test_gen_dir}/*.java\")
list(JOIN _src \"\\n\" _src_list)
file(WRITE \"${_java_test_classes_dir}/sources.txt\" \"\${_src_list}\")
execute_process(
    COMMAND ${JAVAC_EXECUTABLE} --release 11
        -cp \"${_java_jar}\"
        -d \"${_java_test_classes_dir}\"
        \"@${_java_test_classes_dir}/sources.txt\"
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR \"Java test compilation failed\")
endif()
")

            add_custom_command(
                OUTPUT "${_java_test_classes_dir}/.compiled"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_java_test_classes_dir}"
                COMMAND ${CMAKE_COMMAND} -P "${_java_test_compile_script}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_java_test_classes_dir}/.compiled"
                DEPENDS conduit_java_jar java_test_codegen
                COMMENT "Compiling Java test classes"
                VERBATIM
            )
            add_custom_target(java_test_compile DEPENDS "${_java_test_classes_dir}/.compiled")
        endif()
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
