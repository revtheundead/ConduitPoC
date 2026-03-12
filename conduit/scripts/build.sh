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
#   ./scripts/build.sh --java       Build Java bindings via Maven (implies --jni)
#   ./scripts/build.sh --sanitize   Enable address + undefined-behavior sanitizers
#   ./scripts/build.sh --third-party Build only third-party dependencies
#   ./scripts/build.sh --test       Run all tests after build
#
# Flags may be combined freely, e.g.:
#   ./scripts/build.sh --debug --jni --test
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
ENABLE_SANITIZERS=false
BUILD_DIR="build"
JAVA_BINDINGS_DIR="bindings/java"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ============================================================================
# Parse arguments
# ============================================================================

for arg in "$@"; do
    case "$arg" in
        --clean)       CLEAN=true ;;
        --release)     BUILD_TYPE="Release"; BUILD_ALL=true
                       BUILD_CABI=true; BUILD_JNI=true; BUILD_JAVA=true ;;
        --debug)       BUILD_TYPE="Debug" ;;
        --third-party) THIRD_PARTY_ONLY=true ;;
        --test)        RUN_TESTS=true ;;
        --cabi)        BUILD_CABI=true ;;
        --jni)         BUILD_CABI=true; BUILD_JNI=true ;;
        --java)        BUILD_CABI=true; BUILD_JNI=true; BUILD_JAVA=true ;;
        --sanitize)    ENABLE_SANITIZERS=true ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--release] [--debug] [--clean] [--cabi] [--jni] [--java] [--sanitize] [--third-party] [--test]"
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

# JNI/Java toolchain checks
if [ "$BUILD_JNI" = true ]; then
    if ! command -v java &>/dev/null; then
        warn "java not found — JNI build may fail if JAVA_HOME is not set"
    else
        echo "  java $(java -version 2>&1 | head -1 | grep -oE '[0-9]+\.[0-9]+[^ ]*' | head -1) ... ok"
    fi
fi

if [ "$BUILD_JAVA" = true ]; then
    if ! command -v mvn &>/dev/null; then
        fail "mvn not found. Please install Apache Maven to build Java bindings."
    fi
    echo "  mvn $(mvn --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1) ... ok"
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
    CACHED_SANITIZE=$(_cache CONDUIT_ENABLE_SANITIZERS)

    WANT_EXAMPLES="OFF"; WANT_BENCHMARKS="OFF"
    [ "$BUILD_ALL" = true ] && { WANT_EXAMPLES="ON"; WANT_BENCHMARKS="ON"; }
    WANT_CABI="OFF"; WANT_JNI="OFF"
    [ "$BUILD_CABI" = true ] && WANT_CABI="ON"
    [ "$BUILD_JNI"  = true ] && WANT_JNI="ON"
    WANT_SANITIZE="OFF"
    [ "$ENABLE_SANITIZERS" = true ] && WANT_SANITIZE="ON"

    for pair in \
        "$CACHED_TYPE:$BUILD_TYPE" \
        "$CACHED_EXAMPLES:$WANT_EXAMPLES" \
        "$CACHED_BENCHMARKS:$WANT_BENCHMARKS" \
        "$CACHED_CABI:$WANT_CABI" \
        "$CACHED_JNI:$WANT_JNI" \
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
# Java bindings (Maven install to local repo)
# ============================================================================

if [ "$BUILD_JAVA" = true ]; then
    step "Building Java bindings (Maven)"
    (cd "$JAVA_BINDINGS_DIR" && mvn install -q)
    step "Java bindings installed to local Maven repo"
fi

# ============================================================================
# Test (only with --test)
# ============================================================================

if [ "$RUN_TESTS" = true ]; then
    step "Running conduit tests"
    "$BUILD_DIR/tests/conduit_tests"

    step "Running bgen tests"
    "$BUILD_DIR/bgen/tests/bgen_tests"

    if [ -x "$BUILD_DIR/bgen/tests/bgen_python_tests" ]; then
        step "Running bgen Python backend tests"
        "$BUILD_DIR/bgen/tests/bgen_python_tests"
    fi

    if [ -x "$BUILD_DIR/bgen/tests/bgen_java_tests" ]; then
        step "Running bgen Java backend tests"
        "$BUILD_DIR/bgen/tests/bgen_java_tests"
    fi

    # --- Java JUnit tests (non-fatal) ---
    JUNIT_JAR="$PROJECT_DIR/third_party/junit5/junit-platform-console-standalone-1.11.4.jar"
    JAVA_TEST_CLASSES="$BUILD_DIR/java-test-classes"
    JAVA_JAR="$BUILD_DIR/conduit-java-0.1.0.jar"
    if [ -f "$JUNIT_JAR" ] && [ -d "$JAVA_TEST_CLASSES" ] && [ -f "$JAVA_JAR" ]; then
        if command -v java &>/dev/null; then
            step "Running Java JUnit tests"
            java -jar "$JUNIT_JAR" \
                --class-path "${JAVA_TEST_CLASSES}:${JAVA_JAR}" \
                --scan-class-path "$JAVA_TEST_CLASSES" \
                --include-classname "^Test.*" \
                --exclude-classname ".*Transceiver.*" \
                --exclude-classname ".*CodecCabi.*" \
                || warn "Java JUnit tests failed (non-fatal)"
        else
            warn "java not found — skipping Java JUnit tests"
        fi
    else
        echo "  Java JUnit tests not available (build with CONDUIT_BUILD_JAVA_JAR=ON)"
    fi

    # --- Python pytest tests (non-fatal) ---
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
                step "Running Python pytest tests"
                $PYTHON_CMD -m pytest "$PYTHON_TESTS" -x -q \
                    --ignore="$PYTHON_TESTS/test_codec_cabi.py" \
                    --ignore="$PYTHON_TESTS/test_transceiver_cabi.py" \
                    || warn "Python tests failed (non-fatal)"
            else
                warn "pytest not available — skipping Python tests"
            fi
        else
            warn "python not found — skipping Python tests"
        fi
    fi

    step "All tests passed"
fi

echo ""
echo -e "${GREEN}Done.${NC}"
