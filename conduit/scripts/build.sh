#!/usr/bin/env bash
# ============================================================================
# Conduit — Build Script
#
# Usage:
#   ./scripts/build.sh              # Configure + build (Debug)
#   ./scripts/build.sh --release    # Build everything (Release)
#   ./scripts/build.sh --debug      # Build (Debug, no optimizations)
#   ./scripts/build.sh --clean      # Wipe build dir, then configure + build
#   ./scripts/build.sh --third-party # Build only third-party dependencies
#   ./scripts/build.sh --test       # Run all tests after build
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
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ============================================================================
# Parse arguments
# ============================================================================

for arg in "$@"; do
    case "$arg" in
        --clean)       CLEAN=true ;;
        --release)     BUILD_TYPE="Release"; BUILD_ALL=true ;;
        --debug)       BUILD_TYPE="Debug" ;;
        --third-party) THIRD_PARTY_ONLY=true ;;
        --test)        RUN_TESTS=true ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--release] [--debug] [--clean] [--third-party] [--test]"
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
    CMAKE_FLAGS+=(
        -DCONDUIT_BUILD_EXAMPLES=ON
        -DCONDUIT_BUILD_BENCHMARKS=ON
    )
else
    CMAKE_FLAGS+=(
        -DCONDUIT_BUILD_EXAMPLES=OFF
        -DCONDUIT_BUILD_BENCHMARKS=OFF
    )
fi

NEEDS_CONFIGURE=false
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    NEEDS_CONFIGURE=true
elif [ "CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ] || \
     [ "tests/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ] || \
     [ "bgen/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
    NEEDS_CONFIGURE=true
else
    # Reconfigure if build type or options changed from cached values
    CACHED_TYPE=$(cmake -L -N "$BUILD_DIR" 2>/dev/null | grep 'CMAKE_BUILD_TYPE' | cut -d= -f2)
    CACHED_EXAMPLES=$(cmake -L -N "$BUILD_DIR" 2>/dev/null | grep 'CONDUIT_BUILD_EXAMPLES' | cut -d= -f2)
    CACHED_BENCHMARKS=$(cmake -L -N "$BUILD_DIR" 2>/dev/null | grep 'CONDUIT_BUILD_BENCHMARKS' | cut -d= -f2)

    if [ "$CACHED_TYPE" != "$BUILD_TYPE" ]; then
        NEEDS_CONFIGURE=true
    fi
    if [ "$BUILD_ALL" = true ]; then
        if [ "$CACHED_EXAMPLES" != "ON" ] || [ "$CACHED_BENCHMARKS" != "ON" ]; then
            NEEDS_CONFIGURE=true
        fi
    else
        if [ "$CACHED_EXAMPLES" != "OFF" ] || [ "$CACHED_BENCHMARKS" != "OFF" ]; then
            NEEDS_CONFIGURE=true
        fi
    fi
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

    step "All tests passed"
fi

echo ""
echo -e "${GREEN}Done.${NC}"
