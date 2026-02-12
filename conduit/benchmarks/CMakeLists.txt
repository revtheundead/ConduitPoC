# ============================================================================
# Conduit benchmarks
#
# Separate executable using Catch2 BENCHMARK macros to measure encode/decode
# throughput and memory overhead for asterix and sentry_link protocols.
#
# Enable with: cmake -DCONDUIT_BUILD_BENCHMARKS=ON -DCONDUIT_BUILD_BGEN=ON
# ============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/BgenGenerate.cmake)

# Generate protocol code for benchmarks
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

add_executable(conduit_benchmarks
    bench_codec.cpp
    bench_throughput.cpp
    bench_memory.cpp
)

target_link_libraries(conduit_benchmarks PRIVATE
    conduit
    conduit_warnings
    bench_asterix
    bench_sentry_link
    Catch2::Catch2WithMain
)
