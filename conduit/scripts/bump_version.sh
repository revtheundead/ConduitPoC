#!/usr/bin/env bash
# ============================================================================
# bump_version.sh — Update version strings across all project files
# ============================================================================
#
# Reads the version from the VERSION file at the repository root and patches
# every file that contains a hardcoded version string.
#
# Usage:
#   ./bump_version.sh              # apply version from VERSION file
#   ./bump_version.sh 0.2.0        # set VERSION file to 0.2.0, then apply
#
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION_FILE="$PROJECT_ROOT/VERSION"

if [ $# -ge 1 ]; then
    echo "$1" > "$VERSION_FILE"
fi

if [ ! -f "$VERSION_FILE" ]; then
    echo "ERROR: VERSION file not found at $VERSION_FILE" >&2
    exit 1
fi

VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"

if [ -z "$VERSION" ]; then
    echo "ERROR: VERSION file is empty" >&2
    exit 1
fi

echo "Setting version to: $VERSION"

# CMakeLists.txt — project(conduit VERSION X.Y.Z ...)
sed -i "s/\(project(conduit VERSION \)[^ ]*/\1${VERSION}/" \
    "$PROJECT_ROOT/conduit/CMakeLists.txt"

# Java pom.xml (bindings) — top-level <version>
sed -i "0,/<version>[^<]*<\/version>/s/<version>[^<]*<\/version>/<version>${VERSION}<\/version>/" \
    "$PROJECT_ROOT/conduit/bindings/java/pom.xml"

# Java build.gradle (xcvr-java11) — version = '...'
sed -i "s/version = '[^']*'/version = '${VERSION}'/" \
    "$PROJECT_ROOT/conduit/examples/xcvr-java11/build.gradle"

# Java pom.xml (xcvr-java21) — all <version> tags matching old pattern
sed -i "s/<version>[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*<\/version>/<version>${VERSION}<\/version>/g" \
    "$PROJECT_ROOT/conduit/examples/xcvr-java21/pom.xml"

# Python pyproject.toml — version = "..."
sed -i "s/^version = \"[^\"]*\"/version = \"${VERSION}\"/" \
    "$PROJECT_ROOT/conduit/bindings/python/pyproject.toml"

# Python __init__.py — __version__ = "..."
sed -i "s/__version__ = \"[^\"]*\"/__version__ = \"${VERSION}\"/" \
    "$PROJECT_ROOT/conduit/bindings/python/conduit/__init__.py"

# C ABI — return "X.Y.Z"
sed -i "s/return \"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"/return \"${VERSION}\"/" \
    "$PROJECT_ROOT/conduit/src/cabi/conduit_cabi.cpp"

# package_fat_jar.sh — VERSION="..."
sed -i "s/^VERSION=\"[^\"]*\"/VERSION=\"${VERSION}\"/" \
    "$PROJECT_ROOT/conduit/scripts/package_fat_jar.sh"

# package_fat_jar.bat — VERSION=...
if [ -f "$PROJECT_ROOT/conduit/scripts/package_fat_jar.bat" ]; then
    sed -i "s/^set VERSION=.*/set VERSION=${VERSION}/" \
        "$PROJECT_ROOT/conduit/scripts/package_fat_jar.bat"
fi

# GitHub Actions release.yml — CONDUIT_VERSION: "..."
sed -i "s/CONDUIT_VERSION: \"[^\"]*\"/CONDUIT_VERSION: \"${VERSION}\"/" \
    "$PROJECT_ROOT/.github/workflows/release.yml"

echo ""
echo "Updated files:"
echo "  conduit/CMakeLists.txt"
echo "  conduit/bindings/java/pom.xml"
echo "  conduit/examples/xcvr-java11/build.gradle"
echo "  conduit/examples/xcvr-java21/pom.xml"
echo "  conduit/bindings/python/pyproject.toml"
echo "  conduit/bindings/python/conduit/__init__.py"
echo "  conduit/src/cabi/conduit_cabi.cpp"
echo "  conduit/scripts/package_fat_jar.sh"
if [ -f "$PROJECT_ROOT/conduit/scripts/package_fat_jar.bat" ]; then
    echo "  conduit/scripts/package_fat_jar.bat"
fi
echo "  .github/workflows/release.yml"
echo ""
echo "Done. VERSION = $VERSION"
