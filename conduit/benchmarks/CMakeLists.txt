# ============================================================================
# Conduit benchmarks
#
# C++ benchmarks (Catch2):
#   cmake -DCONDUIT_BUILD_BENCHMARKS=ON -DCONDUIT_BUILD_BGEN=ON
#
# Python benchmarks (ctypes → C ABI):
#   cmake -DCONDUIT_BUILD_PYTHON_BENCHMARKS=ON -DCONDUIT_BUILD_BGEN=ON \
#         -DCONDUIT_BUILD_CODEC_CABI=ON
#
# Java benchmarks (JNI / Panama → C ABI):
#   cmake -DCONDUIT_BUILD_JAVA_BENCHMARKS=ON -DCONDUIT_BUILD_BGEN=ON \
#         -DCONDUIT_BUILD_CODEC_CABI=ON -DCONDUIT_BUILD_JAVA_JAR=ON
# ============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/BgenGenerate.cmake)

# Generate C++ protocol code for benchmarks (always needed for C++ benchmarks
# and for the CABI bench library that Python/Java depend on)
bgen_generate(
    TARGET    bench_asterix
    INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/asterix.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/asterix
    NAMESPACE asterix
)

bgen_generate(
    TARGET    bench_sentry_link
    INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/sentry_link.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/sentry_link
    NAMESPACE sentry_link
)

bgen_generate(
    TARGET    bench_stress_protocol
    INPUT     ${CMAKE_CURRENT_SOURCE_DIR}/stress_protocol.bmdl.xml
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/stress
    NAMESPACE stress
)

add_executable(conduit_benchmarks
    bench_codec.cpp
    bench_throughput.cpp
    bench_memory.cpp
    bench_stress_codec.cpp
    bench_stress_transport.cpp
)

target_link_libraries(conduit_benchmarks PRIVATE
    conduit
    conduit_warnings
    bench_asterix
    bench_sentry_link
    bench_stress_protocol
    Catch2::Catch2WithMain
)

# Place benchmark binary under build/benchmarks/ instead of the global
# build/lib/.  Per-config overrides prevent MSVC multi-config generators
# from appending a config subdirectory.
set_target_properties(conduit_benchmarks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
)
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set_target_properties(conduit_benchmarks PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/benchmarks"
    )
endforeach()

# ============================================================================
# Benchmark CABI shared library (shared by Python and Java benchmarks)
#
# Bundles the codec C ABI with stress_frame and sentry_link sessions
# registered, so Python/Java benchmarks can use conduit_session_create().
# ============================================================================

if(CONDUIT_BUILD_PYTHON_BENCHMARKS OR CONDUIT_BUILD_JAVA_BENCHMARKS)
    add_library(conduit_codec_cabi_bench SHARED
        ${CMAKE_SOURCE_DIR}/src/cabi/conduit_codec_cabi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/bench_sessions_register.cpp
    )
    target_link_libraries(conduit_codec_cabi_bench PRIVATE
        conduit_codec
        conduit_warnings
        bench_sentry_link
        bench_stress_protocol
        Threads::Threads
    )
    target_include_directories(conduit_codec_cabi_bench PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    )
    target_include_directories(conduit_codec_cabi_bench PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_compile_definitions(conduit_codec_cabi_bench PRIVATE CONDUIT_CODEC_CABI_EXPORTS)
    set_target_properties(conduit_codec_cabi_bench PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if(WIN32)
        set_target_properties(conduit_codec_cabi_bench PROPERTIES PREFIX "")
    endif()
    if(CONDUIT_BUILD_CODEC_CABI OR CONDUIT_BUILD_CABI)
        set_target_properties(conduit_codec_cabi_bench PROPERTIES
            POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

# ============================================================================
# Python benchmarks
# ============================================================================

if(CONDUIT_BUILD_PYTHON_BENCHMARKS)
    find_program(_python_exe python3)
    if(NOT _python_exe)
        find_program(_python_exe python)
    endif()
    if(NOT _python_exe)
        message(WARNING "CONDUIT_BUILD_PYTHON_BENCHMARKS=ON but no python3/python found")
    else()
        # Generate Python code for stress_protocol and sentry_link
        bgen_generate(
            TARGET    bench_stress_py
            INPUT     ${CMAKE_CURRENT_SOURCE_DIR}/stress_protocol.bmdl.xml
            OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/stress_py/stress
            NAMESPACE stress
            LANGUAGE  python
        )
        bgen_generate(
            TARGET    bench_sentry_link_py
            INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/sentry_link.bmdl.xml
            OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/sentry_link_py/sentry_link
            NAMESPACE sentry_link
            LANGUAGE  python
        )

        add_custom_target(python_bench_run
            COMMAND ${CMAKE_COMMAND} -E env
                "CONDUIT_CODEC_LIB=$<TARGET_FILE:conduit_codec_cabi_bench>"
                "PYTHONPATH=${CMAKE_SOURCE_DIR}/bindings/python:${CMAKE_CURRENT_BINARY_DIR}/generated/stress_py:${CMAKE_CURRENT_BINARY_DIR}/generated/sentry_link_py"
                ${_python_exe} "${CMAKE_CURRENT_SOURCE_DIR}/bench_python.py"
            DEPENDS conduit_codec_cabi_bench bench_stress_py bench_sentry_link_py
            COMMENT "Running Python codec benchmarks"
            VERBATIM
        )
    endif()
endif()

# ============================================================================
# Java benchmarks (JNI + Panama)
# ============================================================================

if(CONDUIT_BUILD_JAVA_BENCHMARKS)
    find_program(JAVAC_EXECUTABLE javac)
    find_program(_java_exe java)

    if(NOT JAVAC_EXECUTABLE OR NOT _java_exe)
        message(WARNING "CONDUIT_BUILD_JAVA_BENCHMARKS=ON but javac/java not found")
    else()
        # Detect JDK version
        execute_process(
            COMMAND ${JAVAC_EXECUTABLE} -version
            OUTPUT_VARIABLE _javac_ver_out
            ERROR_VARIABLE _javac_ver_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "[0-9]+" _bench_java_major "${_javac_ver_out}")

        # Generate Java code for stress_protocol and sentry_link
        bgen_generate(
            TARGET    bench_stress_java
            INPUT     ${CMAKE_CURRENT_SOURCE_DIR}/stress_protocol.bmdl.xml
            OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/java_bench/stress
            NAMESPACE stress
            LANGUAGE  java
        )
        bgen_generate(
            TARGET    bench_sentry_link_java
            INPUT     ${CMAKE_SOURCE_DIR}/bgen/tests/fixtures/sentry_link.bmdl.xml
            OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/java_bench/sentry_link
            NAMESPACE sentry_link
            LANGUAGE  java
        )

        # Java JAR from main build
        set(_java_jar "${CMAKE_SOURCE_DIR}/lib/conduit-java-${PROJECT_VERSION}.jar")

        # Classpath separator
        if(WIN32)
            set(_cp_sep ";")
        else()
            set(_cp_sep ":")
        endif()

        set(_bench_java_classes "${CMAKE_CURRENT_BINARY_DIR}/java-bench-classes")

        # --- JNI benchmark ---
        if(CONDUIT_BUILD_JNI)
            # Build a JNI library for benchmarks (links against bench CABI)
            find_package(JNI REQUIRED)
            add_library(conduit_codec_jni_bench SHARED
                ${CMAKE_SOURCE_DIR}/src/jni/conduit_codec_jni.cpp
            )
            target_link_libraries(conduit_codec_jni_bench PRIVATE
                conduit_codec_cabi_bench
                conduit_warnings
                ${JNI_LIBRARIES}
            )
            target_include_directories(conduit_codec_jni_bench SYSTEM PRIVATE
                ${JNI_INCLUDE_DIRS}
            )
            target_include_directories(conduit_codec_jni_bench PRIVATE
                $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            )
            set_target_properties(conduit_codec_jni_bench PROPERTIES
                C_VISIBILITY_PRESET hidden
                CXX_VISIBILITY_PRESET hidden
                VISIBILITY_INLINES_HIDDEN ON
            )
            if(WIN32)
                set_target_properties(conduit_codec_jni_bench PROPERTIES PREFIX "")
            endif()

            # Compile JNI benchmark Java sources
            set(_jni_bench_compile_script "${CMAKE_CURRENT_BINARY_DIR}/compile_jni_bench.cmake")
            file(WRITE "${_jni_bench_compile_script}" "
file(MAKE_DIRECTORY \"${_bench_java_classes}\")
file(GLOB_RECURSE _gen_src \"${CMAKE_CURRENT_BINARY_DIR}/generated/java_bench/*.java\")
list(APPEND _all_src \${_gen_src}
    \"${CMAKE_CURRENT_SOURCE_DIR}/BenchWireData.java\"
    \"${CMAKE_CURRENT_SOURCE_DIR}/BenchJni.java\")
list(JOIN _all_src \"\\n\" _src_list)
file(WRITE \"${_bench_java_classes}/javac_args.txt\" \"-cp\\n${_java_jar}\\n-d\\n${_bench_java_classes}\\n--release\\n11\\n\${_src_list}\\n\")
execute_process(
    COMMAND ${JAVAC_EXECUTABLE} \"@${_bench_java_classes}/javac_args.txt\"
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR \"JNI benchmark compilation failed\")
endif()
")
            add_custom_command(
                OUTPUT "${_bench_java_classes}/.jni_compiled"
                COMMAND ${CMAKE_COMMAND} -P "${_jni_bench_compile_script}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_bench_java_classes}/.jni_compiled"
                DEPENDS bench_stress_java bench_sentry_link_java
                    conduit_java_jar
                COMMENT "Compiling Java JNI benchmark"
                VERBATIM
            )
            add_custom_target(jni_bench_compile DEPENDS "${_bench_java_classes}/.jni_compiled")

            add_custom_target(jni_bench_run
                COMMAND ${_java_exe}
                    "-Djava.library.path=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
                    "-Dconduit.codec.jni.path=$<TARGET_FILE:conduit_codec_jni_bench>"
                    -cp "${_bench_java_classes}${_cp_sep}${_java_jar}"
                    BenchJni
                DEPENDS jni_bench_compile conduit_codec_jni_bench
                COMMENT "Running Java JNI codec benchmark"
                VERBATIM
            )
        endif()

        # --- Panama benchmark (JDK 21+) ---
        if(CONDUIT_BUILD_JAVA_PANAMA AND _bench_java_major GREATER_EQUAL 21)
            set(_panama_bench_compile_script "${CMAKE_CURRENT_BINARY_DIR}/compile_panama_bench.cmake")
            file(WRITE "${_panama_bench_compile_script}" "
file(MAKE_DIRECTORY \"${_bench_java_classes}\")
file(GLOB_RECURSE _gen_src \"${CMAKE_CURRENT_BINARY_DIR}/generated/java_bench/*.java\")
list(APPEND _all_src \${_gen_src}
    \"${CMAKE_CURRENT_SOURCE_DIR}/BenchWireData.java\"
    \"${CMAKE_CURRENT_SOURCE_DIR}/BenchPanama.java\")
list(JOIN _all_src \"\\n\" _src_list)
file(WRITE \"${_bench_java_classes}/javac_panama_args.txt\" \"-cp\\n${_java_jar}\\n-d\\n${_bench_java_classes}\\n--release\\n21\\n--enable-preview\\n\${_src_list}\\n\")
execute_process(
    COMMAND ${JAVAC_EXECUTABLE} \"@${_bench_java_classes}/javac_panama_args.txt\"
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR \"Panama benchmark compilation failed\")
endif()
")
            add_custom_command(
                OUTPUT "${_bench_java_classes}/.panama_compiled"
                COMMAND ${CMAKE_COMMAND} -P "${_panama_bench_compile_script}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_bench_java_classes}/.panama_compiled"
                DEPENDS bench_stress_java bench_sentry_link_java
                    conduit_java_jar
                COMMENT "Compiling Java Panama benchmark"
                VERBATIM
            )
            add_custom_target(panama_bench_compile DEPENDS "${_bench_java_classes}/.panama_compiled")

            add_custom_target(panama_bench_run
                COMMAND ${_java_exe}
                    --enable-preview
                    --enable-native-access=ALL-UNNAMED
                    "-Djava.library.path=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
                    "-Dconduit.codec.path=$<TARGET_FILE:conduit_codec_cabi_bench>"
                    -cp "${_bench_java_classes}${_cp_sep}${_java_jar}"
                    BenchPanama
                DEPENDS panama_bench_compile conduit_codec_cabi_bench
                COMMENT "Running Java Panama FFI codec benchmark"
                VERBATIM
            )
        endif()
    endif()
endif()
