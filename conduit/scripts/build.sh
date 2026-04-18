#!/usr/bin/env bash
# ============================================================================
# Conduit — Build Script
#
# Usage:
#   ./scripts/build.sh              Configure + build (Debug)
#   ./scripts/build.sh --release    Build everything (Release): enables
#                                   examples, benchmarks, cabi, jni, java
#   ./scripts/build.sh --debug      Build (Debug, no optimizations)
#   ./scripts/build.sh --clean      Wipe build dir, then configure + build
#   ./scripts/build.sh --cabi       Build CABI shared libraries
#   ./scripts/build.sh --jni        Build JNI shared libraries (implies --cabi)
#   ./scripts/build.sh --java       Build Java JAR + JNI + CABI (implies --jni --cabi)
#   ./scripts/build.sh --sanitize   Enable address + undefined-behavior sanitizers
#   ./scripts/build.sh --online     Use online PyPI packages (default: offline third_party/)
#   ./scripts/build.sh --third-party Build only third-party dependencies
#   ./scripts/build.sh --test       Run all tests after build
#   ./scripts/build.sh --clang      Use Clang (clang / clang++)
#   ./scripts/build.sh --gcc        Use GCC (gcc / g++)
#
# Flags may be combined freely, e.g.:
#   ./scripts/build.sh --debug --jni --test
#   ./scripts/build.sh --clang --release
# ============================================================================

set -euo pipefail

# Navigate to project root (parent of scripts/)
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

BUILD_TYPE="Debug"
CLEAN=false
THIRD_PARTY_ONLY=false
BUILD_ALL=false
RUN_TESTS=false
BUILD_CABI=false
BUILD_JNI=false
BUILD_JAVA=false
BUILD_PYTHON_BENCH=false
BUILD_JAVA_BENCH=false
ENABLE_SANITIZERS=false
USE_COMPILER=""
PIP_ONLINE=false
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ============================================================================
# Parse arguments
# ============================================================================

for arg in "$@"; do
    case "$arg" in
        --clean)       CLEAN=true ;;
        --release)     BUILD_TYPE="Release"; BUILD_ALL=true
                       BUILD_CABI=true; BUILD_JNI=true; BUILD_JAVA=true
                       BUILD_PYTHON_BENCH=true; BUILD_JAVA_BENCH=true ;;
        --debug)       BUILD_TYPE="Debug" ;;
        --third-party) THIRD_PARTY_ONLY=true ;;
        --test)        RUN_TESTS=true ;;
        --cabi)        BUILD_CABI=true ;;
        --jni)         BUILD_CABI=true; BUILD_JNI=true ;;
        --java)        BUILD_CABI=true; BUILD_JNI=true; BUILD_JAVA=true ;;
        --sanitize)    ENABLE_SANITIZERS=true ;;
        --online)      PIP_ONLINE=true ;;
        --clang)       USE_COMPILER="clang" ;;
        --gcc)         USE_COMPILER="gcc" ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--release] [--debug] [--clean] [--cabi] [--jni] [--java] [--sanitize] [--online] [--clang] [--gcc] [--third-party] [--test]"
            exit 1 ;;
    esac
done

# ============================================================================
# Helpers
# ============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

step() { echo -e "\n${GREEN}==>${NC} $1"; }
warn() { echo -e "${YELLOW}Warning:${NC} $1"; }
fail() { echo -e "${RED}Error:${NC} $1"; exit 1; }

# ============================================================================
# Verify third-party dependencies
# ============================================================================

step "Checking third-party dependencies"

if [ ! -f "third_party/Catch2/CMakeLists.txt" ]; then
    fail "third_party/Catch2 not found. Please place Catch2 v3 sources in third_party/Catch2/"
fi

if [ ! -f "third_party/pugixml/CMakeLists.txt" ]; then
    fail "third_party/pugixml not found. Please place pugixml sources in third_party/pugixml/"
fi

echo "  Catch2 ... ok"
echo "  pugixml ... ok"

# ============================================================================
# Check tools
# ============================================================================

step "Checking build tools"

if ! command -v cmake &>/dev/null; then
    fail "cmake not found. Please install CMake 3.20+."
fi

CMAKE_VERSION="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
echo "  cmake $CMAKE_VERSION ... ok"

# Detect generator
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
    echo "  ninja ... ok (using Ninja generator)"
elif command -v make &>/dev/null; then
    GENERATOR="Unix Makefiles"
    echo "  make ... ok (using Unix Makefiles generator)"
else
    fail "No build tool found. Please install ninja-build or make."
fi

# Compiler selection (printed right after generator, before JNI/Java checks)
CMAKE_COMPILER_FLAGS=()

if [ "$USE_COMPILER" = "clang" ]; then
    if ! command -v clang++ &>/dev/null; then
        fail "clang++ not found on PATH. Install LLVM/Clang or check your PATH."
    fi
    # Check clang version (minimum 19 required for full C++23 support)
    CLANG_VERSION=$(clang++ --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    CLANG_MAJOR=${CLANG_VERSION%%.*}
    if [ -z "$CLANG_MAJOR" ]; then
        fail "Could not determine clang++ version."
    fi
    if [ "$CLANG_MAJOR" -lt 19 ] 2>/dev/null; then
        fail "Clang $CLANG_VERSION is too old. Minimum required version is 19. Please upgrade LLVM/Clang: https://releases.llvm.org/"
    fi
    export CXX=clang++
    CMAKE_COMPILER_FLAGS+=(-DCMAKE_CXX_COMPILER=clang++)
    echo "  clang++ $CLANG_VERSION ... ok ($(clang++ --version | head -1))"
elif [ "$USE_COMPILER" = "gcc" ]; then
    if ! command -v g++ &>/dev/null; then
        fail "g++ not found on PATH. Install GCC or check your PATH."
    fi
    export CXX=g++
    CMAKE_COMPILER_FLAGS+=(-DCMAKE_CXX_COMPILER=g++)
    echo "  g++ ... ok ($(g++ --version | head -1))"
fi

# JNI/Java toolchain checks
if [ "$BUILD_JNI" = true ]; then
    if ! command -v java &>/dev/null; then
        warn "java not found — JNI build may fail if JAVA_HOME is not set"
    else
        echo "  java $(java -version 2>&1 | head -1 | grep -oE '[0-9]+\.[0-9]+[^ ]*' | head -1) ... ok"
    fi
fi

if [ "$BUILD_JAVA" = true ]; then
    if command -v mvn &>/dev/null; then
        echo "  mvn $(mvn --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1) ... ok"
    elif command -v gradle &>/dev/null; then
        echo "  gradle ... ok"
    elif command -v javac &>/dev/null; then
        echo "  javac ... ok"
    else
        warn "No Java build tool found (mvn, gradle, or javac) — Java JAR may not build"
    fi
fi

# ============================================================================
# Clean
# ============================================================================

if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    step "Cleaning build directory"
    rm -rf "$BUILD_DIR"
fi

# ============================================================================
# Third-party only
# ============================================================================

if [ "$THIRD_PARTY_ONLY" = true ]; then
    step "Building third-party dependencies only"
    cmake -B "$BUILD_DIR" \
        -G "$GENERATOR" \
        "${CMAKE_COMPILER_FLAGS[@]}" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCONDUIT_BUILD_BGEN=OFF \
        -DCONDUIT_BUILD_TESTS=ON \
        -DCONDUIT_BUILD_EXAMPLES=OFF \
        -DCONDUIT_BUILD_BENCHMARKS=OFF

    cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS" --target Catch2 Catch2WithMain
    step "Third-party build succeeded"
    echo ""
    echo -e "${GREEN}Done.${NC}"
    exit 0
fi

# ============================================================================
# Configure
# ============================================================================

CMAKE_FLAGS=(
    -DCONDUIT_BUILD_BGEN=ON
    -DCONDUIT_BUILD_TESTS=ON
)

if [ "$BUILD_ALL" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_EXAMPLES=ON -DCONDUIT_BUILD_BENCHMARKS=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_EXAMPLES=OFF -DCONDUIT_BUILD_BENCHMARKS=OFF)
fi

if [ "$BUILD_CABI" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_CODEC_CABI=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_CABI=OFF -DCONDUIT_BUILD_CODEC_CABI=OFF)
fi

if [ "$BUILD_JNI" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JNI=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JNI=OFF)
fi

if [ "$BUILD_JAVA" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JAVA_JAR=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JAVA_JAR=OFF)
fi

if [ "$BUILD_PYTHON_BENCH" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_PYTHON_BENCHMARKS=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_PYTHON_BENCHMARKS=OFF)
fi

if [ "$BUILD_JAVA_BENCH" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JAVA_BENCHMARKS=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_BUILD_JAVA_BENCHMARKS=OFF)
fi

if [ "$ENABLE_SANITIZERS" = true ]; then
    CMAKE_FLAGS+=(-DCONDUIT_ENABLE_SANITIZERS=ON)
else
    CMAKE_FLAGS+=(-DCONDUIT_ENABLE_SANITIZERS=OFF)
fi

# Determine whether (re)configuration is needed
NEEDS_CONFIGURE=false
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    NEEDS_CONFIGURE=true
elif [ "CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ] || \
     [ "tests/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ] || \
     [ "bgen/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
    NEEDS_CONFIGURE=true
else
    _cache() { cmake -L -N "$BUILD_DIR" 2>/dev/null | grep "^$1" | cut -d= -f2; }
    CACHED_TYPE=$(_cache CMAKE_BUILD_TYPE)
    CACHED_EXAMPLES=$(_cache CONDUIT_BUILD_EXAMPLES)
    CACHED_BENCHMARKS=$(_cache CONDUIT_BUILD_BENCHMARKS)
    CACHED_CABI=$(_cache CONDUIT_BUILD_CABI)
    CACHED_JNI=$(_cache CONDUIT_BUILD_JNI)
    CACHED_JAVA_JAR=$(_cache CONDUIT_BUILD_JAVA_JAR)
    CACHED_PY_BENCH=$(_cache CONDUIT_BUILD_PYTHON_BENCHMARKS)
    CACHED_JAVA_BENCH=$(_cache CONDUIT_BUILD_JAVA_BENCHMARKS)
    CACHED_SANITIZE=$(_cache CONDUIT_ENABLE_SANITIZERS)

    WANT_EXAMPLES="OFF"; WANT_BENCHMARKS="OFF"
    [ "$BUILD_ALL" = true ] && { WANT_EXAMPLES="ON"; WANT_BENCHMARKS="ON"; }
    WANT_CABI="OFF"; WANT_JNI="OFF"
    [ "$BUILD_CABI" = true ] && WANT_CABI="ON"
    [ "$BUILD_JNI"  = true ] && WANT_JNI="ON"
    WANT_JAVA_JAR="OFF"
    [ "$BUILD_JAVA" = true ] && WANT_JAVA_JAR="ON"
    WANT_PY_BENCH="OFF"
    [ "$BUILD_PYTHON_BENCH" = true ] && WANT_PY_BENCH="ON"
    WANT_JAVA_BENCH="OFF"
    [ "$BUILD_JAVA_BENCH" = true ] && WANT_JAVA_BENCH="ON"
    WANT_SANITIZE="OFF"
    [ "$ENABLE_SANITIZERS" = true ] && WANT_SANITIZE="ON"

    for pair in \
        "$CACHED_TYPE:$BUILD_TYPE" \
        "$CACHED_EXAMPLES:$WANT_EXAMPLES" \
        "$CACHED_BENCHMARKS:$WANT_BENCHMARKS" \
        "$CACHED_CABI:$WANT_CABI" \
        "$CACHED_JNI:$WANT_JNI" \
        "$CACHED_JAVA_JAR:$WANT_JAVA_JAR" \
        "$CACHED_PY_BENCH:$WANT_PY_BENCH" \
        "$CACHED_JAVA_BENCH:$WANT_JAVA_BENCH" \
        "$CACHED_SANITIZE:$WANT_SANITIZE"
    do
        cached="${pair%%:*}"; want="${pair##*:}"
        if [ "$cached" != "$want" ]; then NEEDS_CONFIGURE=true; break; fi
    done
fi

if [ "$NEEDS_CONFIGURE" = true ]; then
    step "Configuring ($BUILD_TYPE)"
    cmake -B "$BUILD_DIR" \
        -G "$GENERATOR" \
        "${CMAKE_COMPILER_FLAGS[@]}" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        "${CMAKE_FLAGS[@]}"
else
    echo ""
    echo "  Build already configured. Use --clean to reconfigure."
fi

# ============================================================================
# Build
# ============================================================================

step "Building ($BUILD_TYPE, $JOBS jobs)"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$JOBS"
step "Build succeeded"

# ============================================================================
# Build examples (only with --release or relevant flags)
# ============================================================================

if [ "$BUILD_ALL" = true ]; then
    # Install conduit-java JAR to local Maven repo (examples depend on it)
    if command -v mvn &>/dev/null && [ -f "bindings/java/pom.xml" ]; then
        step "Installing conduit-java to local Maven repo"
        mvn install -q -f "bindings/java/pom.xml" \
            || warn "conduit-java install failed (non-fatal)"
    fi

    # xcvr-java11
    if command -v mvn &>/dev/null && [ -f "examples/xcvr-java11/pom.xml" ]; then
        step "Building xcvr-java11 example (Maven)"
        mvn package -q -f "examples/xcvr-java11/pom.xml" \
            "-Dconduit.build.dir=$BUILD_DIR" \
            || warn "xcvr-java11 build failed (non-fatal)"
    fi

    # xcvr-java21
    if command -v mvn &>/dev/null && [ -f "examples/xcvr-java21/pom.xml" ]; then
        step "Building xcvr-java21 example (Maven)"
        mvn package -q -f "examples/xcvr-java21/pom.xml" \
            "-Dconduit.build.dir=$BUILD_DIR" \
            || warn "xcvr-java21 build failed (non-fatal)"
    fi

    # xcvr-python
    # Generate Python code from BMDL before pip install
    if [ -x "examples/xcvr-python/generate.sh" ]; then
        step "Generating Python code for xcvr-python example"
        BGEN="$BUILD_DIR/bgen/bgen" "examples/xcvr-python/generate.sh" \
            || warn "xcvr-python code generation failed (non-fatal)"
    fi
    _pip_cmd=""
    if command -v pip3 &>/dev/null; then _pip_cmd="pip3"
    elif command -v pip &>/dev/null; then _pip_cmd="pip"
    fi
    if [ -n "$_pip_cmd" ] && [ -f "examples/xcvr-python/pyproject.toml" ]; then
        step "Installing xcvr-python example (pip)"
        SETUPTOOLS_WHEEL_DIR="$PROJECT_DIR/third_party/setuptools"

        # Pre-install setuptools and wheel from vendored wheels (needed for
        # PEP 517 builds in offline / air-gapped environments).
        if [ -d "$SETUPTOOLS_WHEEL_DIR" ]; then
            $_pip_cmd install --no-index --find-links "$SETUPTOOLS_WHEEL_DIR" \
                setuptools wheel 2>/dev/null \
            || $_pip_cmd install --no-index --find-links "$SETUPTOOLS_WHEEL_DIR" \
                --user setuptools wheel 2>/dev/null \
            || $_pip_cmd install --no-index --find-links "$SETUPTOOLS_WHEEL_DIR" \
                --break-system-packages setuptools wheel 2>/dev/null \
            || true
        fi

        _pip_installed=false
        if [ "$PIP_ONLINE" != true ]; then
            # Offline-first: try installing from third_party without network
            $_pip_cmd install --quiet --no-build-isolation \
                "examples/xcvr-python/" 2>/dev/null \
                && _pip_installed=true
        fi
        if [ "$_pip_installed" = false ]; then
            # Fallback: online install (or explicit --online)
            $_pip_cmd install --quiet "examples/xcvr-python/" \
                || warn "xcvr-python install failed (non-fatal)"
        fi
    fi
fi

# ============================================================================
# Test (only with --test)
# ============================================================================

if [ "$RUN_TESTS" = true ]; then
    TEST_FAILURES=0

    step "Running conduit tests"
    "$BUILD_DIR/tests/conduit_tests" || { warn "conduit_tests failed"; TEST_FAILURES=$((TEST_FAILURES + 1)); }

    step "Running bgen tests"
    "$BUILD_DIR/bgen/tests/bgen_tests" || { warn "bgen_tests failed"; TEST_FAILURES=$((TEST_FAILURES + 1)); }

    if [ -x "$BUILD_DIR/bgen/tests/bgen_python_tests" ]; then
        step "Running bgen Python backend tests"
        "$BUILD_DIR/bgen/tests/bgen_python_tests" || { warn "bgen_python_tests failed"; TEST_FAILURES=$((TEST_FAILURES + 1)); }
    fi

    if [ -x "$BUILD_DIR/bgen/tests/bgen_java_tests" ]; then
        step "Running bgen Java backend tests"
        "$BUILD_DIR/bgen/tests/bgen_java_tests" || { warn "bgen_java_tests failed"; TEST_FAILURES=$((TEST_FAILURES + 1)); }
    fi

    # --- Java JUnit tests (non-fatal) ---
    JUNIT_JAR=""
    if [ -f "$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar" ]; then
        JUNIT_JAR="$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar"
    fi
    JAVA_TEST_CLASSES="$BUILD_DIR/tests/java-test-classes"
    _version=$(tr -d '[:space:]' < "$PROJECT_DIR/../VERSION")
    JAVA_JAR="$PROJECT_DIR/lib/conduit-java-${_version}.jar"
    if [ -n "$JUNIT_JAR" ] && [ -d "$JAVA_TEST_CLASSES" ] && [ -f "$JAVA_JAR" ]; then
        if command -v java &>/dev/null; then
            step "Running Java JUnit tests"
            JUNIT_EXCLUDES=()
            JAVA_JVM_FLAGS=()
            if [ "$BUILD_CABI" != true ]; then
                JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverCabi"
                                 --exclude-classname ".*CodecCabi.*")
            fi
            if [ "$BUILD_JNI" != true ] || [ "$BUILD_CABI" != true ]; then
                JUNIT_EXCLUDES+=(--exclude-classname "TestTransceiverJni"
                                 --exclude-classname "TestXcvrScenarios"
                                 --exclude-classname "TestTransceiverAdvanced")
            fi
            # Panama FFI tests require --enable-preview on JDK 21+
            _java_major="$(java -version 2>&1 | head -1 | grep -oE '[0-9]+' | head -1)"
            if [ -n "$_java_major" ] && [ "$_java_major" -ge 21 ] 2>/dev/null; then
                JAVA_JVM_FLAGS+=(--enable-preview --enable-native-access=ALL-UNNAMED)
            fi
            java "-Djava.library.path=$PROJECT_DIR/lib" \
                "${JAVA_JVM_FLAGS[@]}" \
                -jar "$JUNIT_JAR" \
                execute \
                --class-path "${JAVA_TEST_CLASSES}:${JAVA_JAR}" \
                --scan-class-path "$JAVA_TEST_CLASSES" \
                --include-classname "^Test.*" \
                --details flat \
                "${JUNIT_EXCLUDES[@]}" \
                || warn "Java JUnit tests failed (non-fatal)"
        else
            warn "java not found — skipping Java JUnit tests"
        fi
    else
        echo "  Java JUnit tests not available (build with --java or CONDUIT_BUILD_JAVA_JAR=ON)"
    fi

    # --- Python pytest tests (non-fatal) ---
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
                # Generate Python packages from BMDL fixtures
                step "Generating Python test packages"
                cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target pytest_generated -j "$JOBS" \
                    || warn "Failed to generate Python test packages"

                step "Running Python pytest tests"
                PYTEST_IGNORES=()
                if [ "$BUILD_CABI" != true ]; then
                    PYTEST_IGNORES+=(--ignore="$PYTHON_TESTS/test_codec_cabi.py"
                                     --ignore="$PYTHON_TESTS/test_transceiver_cabi.py"
                                     --ignore="$PYTHON_TESTS/test_xcvr_scenarios.py"
                                     --ignore="$PYTHON_TESTS/test_async_roundtrip.py")
                fi
                $PYTHON_CMD -m pytest "$PYTHON_TESTS" -x -q \
                    "${PYTEST_IGNORES[@]}" \
                    || warn "Python tests failed (non-fatal)"
            else
                warn "pytest not available — skipping Python tests"
            fi
        else
            warn "Python 3.11+ not found — skipping Python tests"
        fi
    fi

    step "Test run complete"
    if [ "$TEST_FAILURES" -gt 0 ]; then
        fail "$TEST_FAILURES test suite(s) failed"
    fi
fi

echo ""
echo -e "${GREEN}Done.${NC}"
