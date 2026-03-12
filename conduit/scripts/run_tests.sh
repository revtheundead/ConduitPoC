#!/usr/bin/env bash
# ============================================================================
# Conduit — Test Runner
#
# Runs all available test suites: C++ (Catch2), Java (JUnit 5), Python (pytest).
#
# Usage:
#   ./scripts/run_tests.sh              Run tests (expects build/ directory)
#   ./scripts/run_tests.sh BUILD_DIR    Run tests using a custom build directory
#
# Java and Python tests are non-fatal — failures are reported but do not
# prevent subsequent test suites from running.
# ============================================================================

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_DIR/build}"

# ============================================================================
# Helpers
# ============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

step()  { echo -e "\n${GREEN}==>${NC} $1"; }
warn()  { echo -e "${YELLOW}Warning:${NC} $1"; }
fail()  { echo -e "${RED}FAIL:${NC} $1"; }

run_suite() {
    local name="$1"
    shift
    if "$@"; then
        PASSED=$((PASSED + 1))
        echo -e "  ${GREEN}PASS${NC}: $name"
    else
        FAILED=$((FAILED + 1))
        fail "$name"
    fi
}

# ============================================================================
# C++ Tests (Catch2)
# ============================================================================

step "C++ Tests"

if [ -x "$BUILD_DIR/tests/conduit_tests" ]; then
    run_suite "conduit_tests" "$BUILD_DIR/tests/conduit_tests"
else
    warn "conduit_tests not found at $BUILD_DIR/tests/conduit_tests"
fi

if [ -x "$BUILD_DIR/bgen/tests/bgen_tests" ]; then
    run_suite "bgen_tests" "$BUILD_DIR/bgen/tests/bgen_tests"
else
    warn "bgen_tests not found"
fi

if [ -x "$BUILD_DIR/bgen/tests/bgen_python_tests" ]; then
    run_suite "bgen_python_tests" "$BUILD_DIR/bgen/tests/bgen_python_tests"
fi

if [ -x "$BUILD_DIR/bgen/tests/bgen_java_tests" ]; then
    run_suite "bgen_java_tests" "$BUILD_DIR/bgen/tests/bgen_java_tests"
fi

# ============================================================================
# Java JUnit Tests
# ============================================================================

step "Java JUnit Tests"

JUNIT_JAR="$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar"
JAVA_TEST_CLASSES="$BUILD_DIR/java-test-classes"
JAVA_JAR="$BUILD_DIR/conduit-java-0.1.0.jar"

if [ -f "$JUNIT_JAR" ] && [ -d "$JAVA_TEST_CLASSES" ] && [ -f "$JAVA_JAR" ]; then
    if command -v java &>/dev/null; then
        run_suite "Java JUnit (standalone)" java -jar "$JUNIT_JAR" \
            --class-path "${JAVA_TEST_CLASSES}:${JAVA_JAR}" \
            --scan-class-path "$JAVA_TEST_CLASSES" \
            --include-classname "^Test.*" \
            --exclude-classname ".*Transceiver.*" \
            --exclude-classname ".*CodecCabi.*"
    else
        warn "java not found — skipping Java JUnit tests"
    fi
else
    echo "  Java JUnit tests not available (build with CONDUIT_BUILD_JAVA_JAR=ON)"
fi

# ============================================================================
# Python pytest Tests
# ============================================================================

step "Python pytest Tests"

PYTEST_WHEEL_DIR="$PROJECT_DIR/third_party/pytest"
PYTHON_TESTS="$PROJECT_DIR/tests/python"

if [ -d "$PYTHON_TESTS" ]; then
    if command -v python3 &>/dev/null; then
        PYTHON_CMD="python3"
    elif command -v python &>/dev/null; then
        PYTHON_CMD="python"
    else
        PYTHON_CMD=""
    fi

    if [ -n "$PYTHON_CMD" ]; then
        # Install pytest from vendored wheels if available
        if [ -d "$PYTEST_WHEEL_DIR" ]; then
            $PYTHON_CMD -m pip install --no-index --find-links "$PYTEST_WHEEL_DIR" \
                pytest 2>/dev/null || true
        fi

        if $PYTHON_CMD -c "import pytest" 2>/dev/null; then
            run_suite "Python pytest (standalone)" $PYTHON_CMD -m pytest "$PYTHON_TESTS" -x -q \
                --ignore="$PYTHON_TESTS/test_codec_cabi.py" \
                --ignore="$PYTHON_TESTS/test_transceiver_cabi.py"
        else
            warn "pytest not available — skipping Python tests"
        fi
    else
        warn "python not found — skipping Python tests"
    fi
else
    echo "  Python tests directory not found"
fi

# ============================================================================
# Summary
# ============================================================================

echo ""
echo "============================================================================"
echo -e "  Test suites passed: ${GREEN}${PASSED}${NC}    failed: ${RED}${FAILED}${NC}"
echo "============================================================================"

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
