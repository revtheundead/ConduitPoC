# ============================================================================
# Fuzz testing harnesses -- requires Clang with libFuzzer
#
# Prerequisites: CONDUIT_BUILD_BGEN=ON and CONDUIT_BUILD_TESTS=ON
# (fuzz harnesses reuse bgen_lib and gen_* fixture targets from bgen/tests)
#
# Enable with:
#   cmake -DCONDUIT_BUILD_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ ..
#
# Run a harness:
#   ./fuzz_decode corpus_dir/ -max_total_time=300
#   ./fuzz_bgen_parse corpus_dir/ -max_total_time=300
# ============================================================================

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Fuzz targets require Clang with libFuzzer support. "
        "Set -DCMAKE_CXX_COMPILER=clang++ or disable -DCONDUIT_BUILD_FUZZ=OFF.")
endif()

# ============================================================================
# Harness 1: fuzz_decode -- feed random bytes to generated decode paths
# ============================================================================

add_executable(fuzz_decode fuzz_decode.cpp)
target_link_libraries(fuzz_decode PRIVATE conduit conduit_warnings)
target_compile_options(fuzz_decode PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_decode PRIVATE -fsanitize=fuzzer,address)

# Generated headers live under bgen/tests build dir (same fixtures as bgen tests)
target_include_directories(fuzz_decode PRIVATE
    ${CMAKE_BINARY_DIR}/bgen/tests/generated
)

# Ensure generated headers exist before building the fuzz harness
add_dependencies(fuzz_decode
    gen_all_types
    gen_asterix
    gen_sentry_link
    gen_session_protocol
)

# ============================================================================
# Harness 2: fuzz_bgen_parse -- feed random bytes as XML to BMDL parser
# ============================================================================

add_executable(fuzz_bgen_parse fuzz_bgen_parse.cpp)
target_link_libraries(fuzz_bgen_parse PRIVATE bgen_lib conduit_warnings)
target_compile_options(fuzz_bgen_parse PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_bgen_parse PRIVATE -fsanitize=fuzzer,address)
