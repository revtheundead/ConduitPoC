#!/bin/bash
# SENTRIX Third-Party Setup Script (Bash)
# Downloads, builds, and installs all dependencies for offline/fast builds
#
# Usage:
#   ./setup.sh           # Download, build, and install
#   ./setup.sh download  # Download only (no build)
#   ./setup.sh build     # Build only (assumes already downloaded)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"

do_download=false
do_build=false

case "${1:-all}" in
    download) do_download=true ;;
    build)    do_build=true ;;
    all|"")   do_download=true; do_build=true ;;
    *)        echo "Usage: $0 [download|build|all]"; exit 1 ;;
esac

# ============================================================================
# Download Dependencies
# ============================================================================

if $do_download; then
    echo "=== Downloading dependencies ==="
    cd "$SCRIPT_DIR"

    # pugixml
    if [ ! -f "pugixml/CMakeLists.txt" ]; then
        echo "Downloading pugixml v1.15..."
        curl -L https://github.com/zeux/pugixml/releases/download/v1.15/pugixml-1.15.tar.gz | tar -xz
        rm -rf pugixml
        mv pugixml-* pugixml
    else
        echo "pugixml already present"
    fi

    # Catch2
    if [ ! -f "Catch2/CMakeLists.txt" ]; then
        echo "Downloading Catch2 v3.5.0..."
        curl -L https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz | tar -xz
        rm -rf Catch2
        mv Catch2-* Catch2
    else
        echo "Catch2 already present"
    fi

    echo "Download complete!"
fi

# ============================================================================
# Build Dependencies
# ============================================================================

if $do_build; then
    echo "=== Building dependencies ($BUILD_TYPE) ==="
    cd "$SCRIPT_DIR"

    INSTALL_DIR="$SCRIPT_DIR/install"

    # Configure
    echo "Configuring..."
    cmake -B build -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

    # Build
    echo "Building..."
    cmake --build build --config "$BUILD_TYPE" --parallel

    # Install
    echo "Installing to $INSTALL_DIR..."
    cmake --install build --config "$BUILD_TYPE"

    # Verify installation
    echo ""
    echo "=== Verifying installation ==="

    if [ -f "$INSTALL_DIR/lib/cmake/pugixml/pugixml-config.cmake" ]; then
        echo "✓ pugixml installed successfully"
    else
        echo "✗ WARNING: pugixml not found"
    fi

    echo ""
    echo "Note: Catch2 is built from source by the test target (not pre-built)"
    if [ -d "$SCRIPT_DIR/Catch2" ]; then
        echo "✓ Catch2 sources available for offline builds"
    else
        echo "! Catch2 sources not found - tests will use FetchContent"
    fi

    echo ""
    echo "Build complete!"
    echo ""
    echo "To use pre-built dependencies, configure the main project with:"
    echo "  cmake -B build -DSENTRIX_THIRD_PARTY_PREFIX=third_party/install"
fi
