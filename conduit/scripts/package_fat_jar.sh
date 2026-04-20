#!/usr/bin/env bash
# ============================================================================
# package_fat_jar.sh -- Assemble a fat JAR with bundled native libraries
# ============================================================================
#
# This script copies pre-built JNI native libraries into the Maven resource
# directory and builds the conduit-java JAR.  The resulting JAR contains
# native libs for every platform provided, so Java users need only a single
# dependency.
#
# Usage:
#   ./package_fat_jar.sh [--native-dir <dir>] [--output <path>]
#
# Options:
#   --native-dir <dir>   Directory containing platform subdirectories with
#                         native libs.  Expected structure:
#                           <dir>/linux-x86_64/libconduit_jni.so
#                           <dir>/linux-x86_64/libconduit_codec_jni.so
#                           <dir>/macos-aarch64/libconduit_jni.dylib
#                           <dir>/windows-x86_64/conduit_jni.dll
#                           ...
#                         If omitted, the script copies from the local build
#                         output (conduit/lib/) for the current platform only.
#
#   --output <path>      Where to place the final JAR.  Defaults to
#                         conduit/lib/conduit-java-fat-<version>.jar
#
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BINDINGS_DIR="$PROJECT_ROOT/conduit/bindings/java"
RESOURCES_DIR="$BINDINGS_DIR/src/main/resources/native"
VERSION="1.0.9"

NATIVE_DIR=""
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --native-dir) NATIVE_DIR="$2"; shift 2 ;;
        --output)     OUTPUT="$2"; shift 2 ;;
        *)            echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$OUTPUT" ]; then
    OUTPUT="$PROJECT_ROOT/conduit/lib/conduit-java-fat-${VERSION}.jar"
fi

# Clean previous native resources
rm -rf "$RESOURCES_DIR"

if [ -n "$NATIVE_DIR" ]; then
    # Copy all platform native libs from the provided directory
    echo "Bundling native libraries from: $NATIVE_DIR"
    for platform_dir in "$NATIVE_DIR"/*/; do
        platform="$(basename "$platform_dir")"
        mkdir -p "$RESOURCES_DIR/$platform"
        cp "$platform_dir"/* "$RESOURCES_DIR/$platform/" 2>/dev/null || true
        echo "  $platform: $(ls "$RESOURCES_DIR/$platform/" 2>/dev/null | tr '\n' ' ')"
    done
else
    # Single-platform: copy from local build output
    echo "No --native-dir specified; bundling current platform only"
    OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
    ARCH="$(uname -m)"

    case "$OS" in
        linux)  OS_NAME="linux" ;;
        darwin) OS_NAME="macos" ;;
        *)      OS_NAME="$OS" ;;
    esac

    case "$ARCH" in
        x86_64|amd64) ARCH_NAME="x86_64" ;;
        aarch64|arm64) ARCH_NAME="aarch64" ;;
        *)            ARCH_NAME="$ARCH" ;;
    esac

    PLATFORM="${OS_NAME}-${ARCH_NAME}"
    mkdir -p "$RESOURCES_DIR/$PLATFORM"

    LIB_DIR="$PROJECT_ROOT/conduit/lib"
    if [ "$OS_NAME" = "linux" ]; then
        cp "$LIB_DIR"/libconduit_jni.so "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_codec_jni.so "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_cabi.so "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_codec_cabi.so "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
    elif [ "$OS_NAME" = "macos" ]; then
        cp "$LIB_DIR"/libconduit_jni.dylib "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_codec_jni.dylib "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_cabi.dylib "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
        cp "$LIB_DIR"/libconduit_codec_cabi.dylib "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null || true
    fi

    echo "  $PLATFORM: $(ls "$RESOURCES_DIR/$PLATFORM/" 2>/dev/null | tr '\n' ' ')"
fi

# Build the JAR
echo ""
echo "Building fat JAR..."
mvn package -B -q -f "$BINDINGS_DIR/pom.xml"

# Copy to output location
SRC_JAR="$BINDINGS_DIR/target/conduit-java-${VERSION}.jar"
if [ ! -f "$SRC_JAR" ]; then
    echo "ERROR: Maven did not produce $SRC_JAR" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"
cp "$SRC_JAR" "$OUTPUT"

echo ""
echo "Fat JAR: $OUTPUT"
echo "Contents:"
jar tf "$OUTPUT" | grep "^native/" || echo "  (no native libs bundled)"
echo ""
echo "Size: $(du -h "$OUTPUT" | cut -f1)"

# Clean up: remove native resources so dev builds aren't affected
rm -rf "$RESOURCES_DIR"
