# Coverage.cmake — custom target for generating code coverage reports
#
# Usage:
#   cmake -DCONDUIT_ENABLE_COVERAGE=ON -B build_cov
#   cmake --build build_cov
#   cmake --build build_cov --target coverage

if(NOT CONDUIT_ENABLE_COVERAGE)
    return()
endif()

find_program(LCOV_PATH lcov)
find_program(GENHTML_PATH genhtml)

if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
        # Run tests
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        # Capture coverage data
        COMMAND ${LCOV_PATH} --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${CMAKE_BINARY_DIR}/coverage.info
            --ignore-errors mismatch
        # Remove third-party / system / build / test code from report
        COMMAND ${LCOV_PATH} --remove ${CMAKE_BINARY_DIR}/coverage.info
            '/usr/*'
            '*/third_party/*'
            '*/build*/*'
            '*/tests/*'
            '*/benchmarks/*'
            --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
            --ignore-errors unused
        # Generate HTML report
        COMMAND ${GENHTML_PATH} ${CMAKE_BINARY_DIR}/coverage_filtered.info
            --output-directory ${CMAKE_BINARY_DIR}/coverage_report
            --title "Conduit Coverage Report"
            --legend
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running tests and generating coverage report in build/coverage_report/"
        VERBATIM
    )
else()
    message(STATUS "lcov/genhtml not found — 'coverage' target unavailable")
endif()
