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

# Check if a shared library's runtime dependencies are all resolvable.
can_load_lib() {
    local lib="$1"
    [ -f "$lib" ] || return 1
    if command -v ldd &>/dev/null; then
        if ldd "$lib" 2>&1 | grep -q 'not found'; then
            warn "$lib has unresolved dependencies:"
            ldd "$lib" 2>&1 | grep 'not found' | sed 's/^/    /'
            return 1
        fi
    fi
    return 0
}

# Find a loadable shared library by base name across directories.
# Prints the found path to stdout; returns 1 if none found.
find_loadable_lib() {
    local base="$1"
    shift
    for _d in "$@"; do
        for _f in "$_d/lib${base}.so" "$_d/lib${base}.dylib" "$_d/${base}.dll"; do
            if can_load_lib "$_f"; then
                echo "$_f"
                return 0
            fi
        done
    done
    return 1
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
_version=$(tr -d '[:space:]' < "$PROJECT_DIR/../VERSION")
JAVA_JAR="$PROJECT_DIR/lib/conduit-java-${_version}.jar"

if [ -n "$JUNIT_JAR" ] && [ -d "$JAVA_TEST_CLASSES" ] && [ -f "$JAVA_JAR" ]; then
    if command -v java &>/dev/null; then
        # Conditionally exclude CABI/JNI tests based on whether native test
        # libraries exist AND can actually be loaded (all dependencies resolved).
        JUNIT_EXCLUDES=()
        _has_cabi_test_libs=false
        _has_jni_test_libs=false
        if find_loadable_lib conduit_cabi_test "$PROJECT_DIR/lib" "$BUILD_DIR/tests" >/dev/null; then
            _has_cabi_test_libs=true
        fi
        if find_loadable_lib conduit_jni_test "$PROJECT_DIR/lib" "$BUILD_DIR/tests" >/dev/null; then
            _has_jni_test_libs=true
        fi
        if [ "$_has_cabi_test_libs" != true ]; then
            JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverCabi"
                             --exclude-classname ".*CodecCabi.*")
        fi
        if [ "$_has_jni_test_libs" != true ]; then
            JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverJni"
                             --exclude-classname "TestXcvrScenarios"
                             --exclude-classname "TestTransceiverAdvanced"
                             --exclude-classname "TestUdpMulticast")
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
    PYTHON_CMD=""
    for _py_candidate in python3 python; do
        if command -v "$_py_candidate" &>/dev/null \
           && "$_py_candidate" -c "import sys; exit(0 if sys.version_info >= (3,11) else 1)" 2>/dev/null; then
            PYTHON_CMD="$_py_candidate"
            break
        fi
    done

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

            # Exclude CABI-dependent tests if native test libraries are missing
            # or have unresolvable dependencies.
            PYTEST_IGNORES=()
            _has_cabi_libs=false
            if find_loadable_lib conduit_cabi_test "$PROJECT_DIR/lib" "$BUILD_DIR/tests" >/dev/null; then
                _has_cabi_libs=true
            fi
            if [ "$_has_cabi_libs" != true ]; then
                PYTEST_IGNORES+=(--ignore="$PYTHON_TESTS/test_codec_cabi.py"
                                 --ignore="$PYTHON_TESTS/test_transceiver_cabi.py"
                                 --ignore="$PYTHON_TESTS/test_xcvr_scenarios.py"
                                 --ignore="$PYTHON_TESTS/test_async_roundtrip.py"
                                 --ignore="$PYTHON_TESTS/test_udp_multicast.py")
            fi
            run_suite "Python pytest (standalone)" $PYTHON_CMD -m pytest "$PYTHON_TESTS" -x -q \
                "${PYTEST_IGNORES[@]}"
        else
            warn "pytest not available — skipping Python tests"
        fi
    else
        warn "Python 3.11+ not found — skipping Python tests"
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
