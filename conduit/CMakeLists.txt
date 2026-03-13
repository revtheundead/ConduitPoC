cmake_minimum_required(VERSION 3.20)
project(conduit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Place all shared libraries (.dll/.so) in a single directory at the project
# root so that downstream consumers (Java apps, Python scripts, xcvr examples)
# only need one path.  Per-config overrides prevent MSVC multi-config
# generators from appending a config subdirectory — all libraries land
# directly in <project>/lib regardless of build type.
# This also ensures Windows finds dependent DLLs (e.g. conduit_cabi.dll
# needed by conduit_jni.dll) without extra PATH manipulation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_SOURCE_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_SOURCE_DIR}/lib")
endforeach()

# Threading support — required for std::mutex across all platforms.
# On glibc >= 2.34 pthreads is in libc, so FindThreads creates an empty
# Threads::Threads target (no -pthread flag).  We patch it to always
# carry -pthread so that __gthread_active_p() returns true and std::mutex
# works correctly on every toolchain (older glibc, musl, macOS, MinGW).
# On MSVC the pthread probe prints harmless "not found" messages during
# configure — these can be safely ignored; Win32 threads are used instead.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(CMAKE_USE_PTHREADS_INIT AND NOT CMAKE_THREAD_LIBS_INIT)
    set_property(TARGET Threads::Threads
        PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
    set_property(TARGET Threads::Threads
        PROPERTY INTERFACE_LINK_LIBRARIES  "-pthread")
endif()

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
    #
    # _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR: VS 2022 17.10+ changed std::mutex
    # to use a constexpr constructor with a new internal layout (_Smtx_t /
    # SRWLOCK).  When a DLL built with the new toolchain is loaded into a host
    # process (e.g. a JVM) that already has an older msvcp140.dll, the mutex
    # is initialized with the new layout but lock/unlock dispatches to the old
    # DLL which expects _Mtx_t — causing an access violation at NULL.
    # Defining this macro reverts to the classic non-constexpr constructor so
    # the mutex is always initialized through the CRT, ensuring binary
    # compatibility regardless of which msvcp140.dll version is loaded.
    # See: https://github.com/microsoft/STL/issues/4730
    target_compile_definitions(conduit_warnings INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
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

# MinGW: statically link the GCC/C++ runtime (libstdc++, libgcc, libwinpthread)
# into executables so they don't depend on MinGW's bin directory being on PATH
# at runtime.  Without this, executables fail to start when the MinGW DLLs
# cannot be found or conflict with other MinGW distributions (e.g. MSYS2).
if(MINGW)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -static")
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

target_link_libraries(conduit_codec PUBLIC Threads::Threads)

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
    target_link_libraries(conduit_codec_cabi PRIVATE conduit_codec conduit_warnings Threads::Threads)
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
    target_link_libraries(conduit_cabi PRIVATE conduit conduit_warnings Threads::Threads)
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
        # Bundle CABI source directly so the JNI lib is self-contained
        add_library(conduit_codec_jni SHARED
            src/jni/conduit_codec_jni.cpp
            src/cabi/conduit_codec_cabi.cpp
        )
        target_link_libraries(conduit_codec_jni PRIVATE
            conduit_codec conduit_warnings Threads::Threads ${JNI_LIBRARIES})
        target_include_directories(conduit_codec_jni PRIVATE
            ${JNI_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        )
        # The JNI wrapper calls CABI functions bundled in the same DLL.
        # Define the EXPORTS macro so the JNI .cpp sees the functions as
        # dllexport (matching the CABI .cpp) rather than dllimport, which
        # avoids unnecessary IAT thunks and linker warning LNK4217.
        target_compile_definitions(conduit_codec_jni PRIVATE CONDUIT_CODEC_CABI_EXPORTS)
        set_target_properties(conduit_codec_jni PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
    endif()

    if(CONDUIT_BUILD_CABI)
        # Bundle CABI source directly so the JNI lib is self-contained
        add_library(conduit_jni SHARED
            src/jni/conduit_jni.cpp
            src/cabi/conduit_cabi.cpp
        )
        target_link_libraries(conduit_jni PRIVATE
            conduit conduit_warnings Threads::Threads ${JNI_LIBRARIES})
        target_include_directories(conduit_jni PRIVATE
            ${JNI_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        )
        # The JNI wrapper calls CABI functions bundled in the same DLL.
        # Define the EXPORTS macro so the JNI .cpp sees the functions as
        # dllexport (matching the CABI .cpp) rather than dllimport, which
        # avoids unnecessary IAT thunks and linker warning LNK4217.
        target_compile_definitions(conduit_jni PRIVATE CONDUIT_CABI_EXPORTS)
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
    set(_java_bindings_dir "${CMAKE_CURRENT_SOURCE_DIR}/bindings/java")
    set(_java_src_dir "${_java_bindings_dir}/src/main/java")
    set(_java_jar "${CMAKE_SOURCE_DIR}/lib/conduit-java-0.1.0.jar")
    set(_java_jar_built FALSE)

    # Detect JDK version (needed for test compilation regardless of build strategy)
    find_program(JAVAC_EXECUTABLE javac)
    if(JAVAC_EXECUTABLE)
        execute_process(
            COMMAND ${JAVAC_EXECUTABLE} -version
            OUTPUT_VARIABLE _javac_version_out
            ERROR_VARIABLE _javac_version_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "[0-9]+" _java_major "${_javac_version_out}")
        message(STATUS "Detected JDK major version: ${_java_major}")
    else()
        set(_java_major 0)
    endif()

    # Collect Java sources for dependency tracking
    file(GLOB _java_core_sources "${_java_src_dir}/io/conduit/*.java")

    # --- Strategy 1: Maven ---
    find_program(_mvn_exe mvn)
    if(_mvn_exe AND EXISTS "${_java_bindings_dir}/pom.xml")
        message(STATUS "Java JAR: using Maven (mvn found, pom.xml exists)")
        add_custom_command(
            OUTPUT "${_java_jar}"
            COMMAND ${_mvn_exe} install -q -f "${_java_bindings_dir}/pom.xml"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${_java_bindings_dir}/target/conduit-java-0.1.0.jar"
                "${_java_jar}"
            DEPENDS ${_java_core_sources}
            COMMENT "Building conduit-java-0.1.0.jar (Maven)"
            VERBATIM
        )
        set(_java_jar_built TRUE)
    endif()

    # --- Strategy 2: Gradle ---
    if(NOT _java_jar_built)
        find_program(_gradle_exe gradle)
        if(_gradle_exe AND EXISTS "${_java_bindings_dir}/build.gradle")
            message(STATUS "Java JAR: using Gradle (gradle found, build.gradle exists)")
            add_custom_command(
                OUTPUT "${_java_jar}"
                COMMAND ${_gradle_exe} -p "${_java_bindings_dir}" jar -q
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/lib"
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${_java_bindings_dir}/build/libs/conduit-java-0.1.0.jar"
                    "${_java_jar}"
                DEPENDS ${_java_core_sources}
                COMMENT "Building conduit-java-0.1.0.jar (Gradle)"
                VERBATIM
            )
            set(_java_jar_built TRUE)
        endif()
    endif()

    # --- Strategy 3: javac/jar fallback ---
    if(NOT _java_jar_built)
        find_program(JAR_EXECUTABLE jar)
        if(NOT JAVAC_EXECUTABLE OR NOT JAR_EXECUTABLE)
            message(WARNING
                "CONDUIT_BUILD_JAVA_JAR is ON but no Java build tool found (mvn, gradle, or javac/jar). "
                "Skipping Java JAR build.")
        else()
            message(STATUS "Java JAR: using javac/jar fallback")

            set(_java_classes_dir "${CMAKE_CURRENT_BINARY_DIR}/java-classes")
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
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/lib"
                COMMAND ${JAR_EXECUTABLE} cf "${_java_jar}" -C "${_java_classes_dir}" .
                DEPENDS ${_java_sources}
                COMMENT "Building conduit-java-0.1.0.jar (javac)"
                VERBATIM
            )
            set(_java_jar_built TRUE)
        endif()
    endif()

    if(_java_jar_built)
        add_custom_target(conduit_java_jar ALL DEPENDS "${_java_jar}")

        # ---- Java test compilation (requires bgen + fixtures) ----
        if(CONDUIT_BUILD_BGEN AND CONDUIT_BUILD_TESTS)
            set(_java_test_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/tests/java-test-generated")
            set(_java_test_classes_dir "${CMAKE_CURRENT_BINARY_DIR}/tests/java-test-classes")
            set(_java_test_fixtures
                struct_features
                constraints
                field_scale
                session_protocol
                all_types
                arrays_choices
                choice_protocol
                wire_encodings
                direction_qualified
                sentry_link
                expr_features
                inline_enum
                constants_everywhere
                constraints_extended
                format_binary
                auto_count
                auto_struct_length
                bitmap_advanced
                fx_block
                fx_advanced
                fx_choice
                frame_basic
                frame_footer
                frame_direction
                frame_array
                frame_config
                bytes_numeric
                outer_scope
                string_features
                boundary_types
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

            # JUnit 5 standalone console runner for test execution
            # Referenced directly from third_party/junit5/; downloaded there if missing.
            set(_junit_version "1.11.4")
            set(_junit_jar "${CONDUIT_THIRD_PARTY_DIR}/junit5/junit-platform-console-standalone-${_junit_version}.jar")
            set(_junit_url "https://repo1.maven.org/maven2/org/junit/platform/junit-platform-console-standalone/${_junit_version}/junit-platform-console-standalone-${_junit_version}.jar")

            if(NOT EXISTS "${_junit_jar}")
                message(STATUS "Downloading JUnit Platform Console Standalone ${_junit_version}...")
                file(MAKE_DIRECTORY "${CONDUIT_THIRD_PARTY_DIR}/junit5")
                file(DOWNLOAD "${_junit_url}" "${_junit_jar}" STATUS _dl_status)
                list(GET _dl_status 0 _dl_rc)
                if(NOT _dl_rc EQUAL 0)
                    message(WARNING "Failed to download JUnit JAR (rc=${_dl_rc}). "
                        "Java JUnit tests will not be available. "
                        "Place the JAR in third_party/junit5/ or download manually from ${_junit_url}")
                endif()
            endif()

            # Java test source directory
            set(_java_test_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/java")

            # Classpath separator: semicolon on Windows, colon elsewhere
            if(WIN32)
                set(_cp_sep ";")
            else()
                set(_cp_sep ":")
            endif()

            # Collect generated Java sources at build time using a cmake -P script
            # (avoids the POSIX-only 'find' command for cross-platform support)
            set(_java_test_compile_script "${CMAKE_CURRENT_BINARY_DIR}/java_test_compile.cmake")
            if(CONDUIT_BUILD_JAVA_PANAMA AND _java_major GREATER_EQUAL 21)
                set(_java_test_release_flags "--release 21 --enable-preview")
            else()
                set(_java_test_release_flags "--release 11")
            endif()
            # Panama test files require JDK 21+; exclude them when targeting --release 11
            set(_java_test_exclude_filter "")
            if(NOT (CONDUIT_BUILD_JAVA_PANAMA AND _java_major GREATER_EQUAL 21))
                set(_java_test_exclude_filter "
# Exclude Panama FFI test files (require JDK 21+)
list(FILTER _test_src EXCLUDE REGEX \"TestCodecCabi\\\\.java$\")
list(FILTER _test_src EXCLUDE REGEX \"TestTransceiverCabi\\\\.java$\")
")
            endif()
            file(WRITE "${_java_test_compile_script}" "
file(GLOB_RECURSE _gen_src \"${_java_test_gen_dir}/*.java\")
file(GLOB _test_src \"${_java_test_src_dir}/*.java\")
${_java_test_exclude_filter}list(APPEND _all_src \${_gen_src} \${_test_src})
list(JOIN _all_src \"\\n\" _src_list)
# Write javac argfile — keeps classpath semicolons out of CMake COMMAND parsing
# (on Windows the classpath separator is semicolon which CMake treats as a list separator)
file(WRITE \"${_java_test_classes_dir}/javac_args.txt\" \"-cp\\n${_java_jar}${_cp_sep}${_junit_jar}\\n-d\\n${_java_test_classes_dir}\\n\${_src_list}\\n\")
execute_process(
    COMMAND ${JAVAC_EXECUTABLE} ${_java_test_release_flags}
        \"@${_java_test_classes_dir}/javac_args.txt\"
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
                COMMENT "Compiling Java test classes (generated + JUnit tests)"
                VERBATIM
            )
            add_custom_target(java_test_compile ALL DEPENDS "${_java_test_classes_dir}/.compiled")

            # Run target: execute JUnit tests via the console launcher
            if(EXISTS "${_junit_jar}")
                find_program(_java_exe java)
                if(_java_exe)
                    if(CONDUIT_BUILD_JAVA_PANAMA AND _java_major GREATER_EQUAL 21)
                        set(_java_test_jvm_flags --enable-preview --enable-native-access=ALL-UNNAMED)
                    else()
                        set(_java_test_jvm_flags "")
                    endif()
                    set(_java_test_excludes "")
                    if(NOT CONDUIT_BUILD_CABI)
                        list(APPEND _java_test_excludes
                            --exclude-classname "TestTransceiverCabi"
                            --exclude-classname ".*CodecCabi.*")
                    endif()
                    if(NOT CONDUIT_BUILD_JNI OR NOT CONDUIT_BUILD_CABI)
                        list(APPEND _java_test_excludes
                            --exclude-classname "TestTransceiverJni"
                            --exclude-classname "TestXcvrScenarios")
                    endif()
                    add_custom_target(java_test_run
                        COMMAND ${_java_exe}
                            "-Djava.library.path=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
                            ${_java_test_jvm_flags}
                            -jar "${_junit_jar}"
                            --class-path "${_java_test_classes_dir}${_cp_sep}${_java_jar}"
                            --scan-class-path "${_java_test_classes_dir}"
                            --include-classname "^Test.*"
                            ${_java_test_excludes}
                        DEPENDS java_test_compile
                        COMMENT "Running Java JUnit tests"
                        VERBATIM
                    )
                endif()
            endif()
        endif()
    endif()
endif()

# ============================================================================
# Subdirectories
# ============================================================================

if(CONDUIT_BUILD_TESTS)
    enable_testing()
endif()

if(CONDUIT_BUILD_BGEN)
    add_subdirectory(bgen)
endif()

if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CONDUIT_BUILD_EXAMPLES)
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
