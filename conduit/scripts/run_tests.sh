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

# Ensure shared library dependencies in lib/ are findable (JNI, Python ctypes)
export LD_LIBRARY_PATH="$PROJECT_DIR/lib:${LD_LIBRARY_PATH:-}"

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

JUNIT_JAR=""
if [ -f "$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar" ]; then
    JUNIT_JAR="$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar"
fi
JAVA_TEST_CLASSES="$BUILD_DIR/tests/java-test-classes"
JAVA_JAR="$PROJECT_DIR/lib/conduit-java-0.1.0.jar"

if [ -n "$JUNIT_JAR" ] && [ -d "$JAVA_TEST_CLASSES" ] && [ -f "$JAVA_JAR" ]; then
    if command -v java &>/dev/null; then
        # Conditionally exclude CABI/JNI tests based on whether native test libraries exist
        JUNIT_EXCLUDES=()
        _has_cabi_test_libs=false
        _has_jni_test_libs=false
        for _d in "$PROJECT_DIR/lib" "$BUILD_DIR/tests"; do
            if ls "$_d"/libconduit_cabi_test* "$_d"/conduit_cabi_test* 2>/dev/null | head -1 >/dev/null 2>&1; then
                _has_cabi_test_libs=true
            fi
            if ls "$_d"/libconduit_jni_test* "$_d"/conduit_jni_test* 2>/dev/null | head -1 >/dev/null 2>&1; then
                _has_jni_test_libs=true
            fi
        done
        if [ "$_has_cabi_test_libs" != true ]; then
            JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverCabi"
                             --exclude-classname ".*CodecCabi.*")
        fi
        if [ "$_has_jni_test_libs" != true ]; then
            JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverJni"
                             --exclude-classname "TestXcvrScenarios"
                             --exclude-classname "TestTransceiverAdvanced")
        fi
        # Panama FFI tests require --enable-preview on JDK 21+
        JAVA_JVM_FLAGS=()
        _java_major="$(java -version 2>&1 | head -1 | grep -oE '[0-9]+' | head -1)"
        if [ -n "$_java_major" ] && [ "$_java_major" -ge 21 ] 2>/dev/null; then
            JAVA_JVM_FLAGS+=(--enable-preview --enable-native-access=ALL-UNNAMED)
        fi
        run_suite "Java JUnit (standalone)" java \
            "-Djava.library.path=$PROJECT_DIR/lib" \
            "${JAVA_JVM_FLAGS[@]}" \
            -jar "$JUNIT_JAR" \
            execute \
            --class-path "${JAVA_TEST_CLASSES}:${JAVA_JAR}" \
            --scan-class-path "$JAVA_TEST_CLASSES" \
            --include-classname "^Test.*" \
            --details flat \
            "${JUNIT_EXCLUDES[@]}"
    else
        warn "java not found — skipping Java JUnit tests"
    fi
else
    echo "  Java JUnit tests not available (build with --java or CONDUIT_BUILD_JAVA_JAR=ON)"
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
                pytest 2>/dev/null \
            || $PYTHON_CMD -m pip install --no-index --find-links "$PYTEST_WHEEL_DIR" \
                --user pytest 2>/dev/null \
            || $PYTHON_CMD -m pip install --no-index --find-links "$PYTEST_WHEEL_DIR" \
                --break-system-packages pytest 2>/dev/null \
            || true
        fi

        if $PYTHON_CMD -c "import pytest" 2>/dev/null; then
            # Generate Python test packages from BMDL fixtures
            if [ -d "$BUILD_DIR" ]; then
                cmake --build "$BUILD_DIR" --target pytest_generated 2>/dev/null || true
            fi

            # Exclude CABI-dependent tests if native test libraries are not available
            PYTEST_IGNORES=()
            _has_cabi_libs=false
            for _d in "$PROJECT_DIR/lib" "$BUILD_DIR/tests"; do
                if ls "$_d"/libconduit_cabi_test* "$_d"/conduit_cabi_test* 2>/dev/null | head -1 >/dev/null 2>&1; then
                    _has_cabi_libs=true; break
                fi
            done
            if [ "$_has_cabi_libs" != true ]; then
                PYTEST_IGNORES+=(--ignore="$PYTHON_TESTS/test_codec_cabi.py"
                                 --ignore="$PYTHON_TESTS/test_transceiver_cabi.py"
                                 --ignore="$PYTHON_TESTS/test_xcvr_scenarios.py"
                                 --ignore="$PYTHON_TESTS/test_async_roundtrip.py")
            fi
            run_suite "Python pytest (standalone)" $PYTHON_CMD -m pytest "$PYTHON_TESTS" -x -q \
                "${PYTEST_IGNORES[@]}"
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
exit 0
