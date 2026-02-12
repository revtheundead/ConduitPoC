# SENTRIX Third-Party Setup Script (PowerShell)
# Downloads, builds, and installs all dependencies for offline/fast builds
#
# Usage:
#   .\setup.ps1           # Download, build, and install
#   .\setup.ps1 -Download # Download only (no build)
#   .\setup.ps1 -Build    # Build only (assumes already downloaded)

param(
    [switch]$Download,
    [switch]$Build,
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# If neither flag specified, do both
if (-not $Download -and -not $Build) {
    $Download = $true
    $Build = $true
}

# ============================================================================
# Download Dependencies
# ============================================================================

if ($Download) {
    Write-Host "=== Downloading dependencies ===" -ForegroundColor Cyan
    Push-Location $ScriptDir

    # pugixml
    if (-not (Test-Path "pugixml/CMakeLists.txt")) {
        Write-Host "Downloading pugixml v1.14..."
        Invoke-WebRequest -Uri "https://github.com/zeux/pugixml/releases/download/v1.15/pugixml-1.15.tar.gz" -OutFile "pugixml.tar.gz"
        tar -xf pugixml.tar.gz
        Remove-Item pugixml -Recurse -ErrorAction SilentlyContinue
        Rename-Item "pugixml-*" pugixml
        Remove-Item pugixml.tar.gz
    } else {
        Write-Host "pugixml already present"
    }

    # Catch2
    if (-not (Test-Path "Catch2/CMakeLists.txt")) {
        Write-Host "Downloading Catch2 v3.5.0..."
        Invoke-WebRequest -Uri "https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz" -OutFile "Catch2.tar.gz"
        tar -xf Catch2.tar.gz
        Remove-Item Catch2 -Recurse -ErrorAction SilentlyContinue
        Rename-Item "Catch2-*" Catch2
        Remove-Item Catch2.tar.gz
    } else {
        Write-Host "Catch2 already present"
    }

    Pop-Location
    Write-Host "Download complete!" -ForegroundColor Green
}

# ============================================================================
# Build Dependencies
# ============================================================================

if ($Build) {
    Write-Host "=== Building dependencies ($BuildType) ===" -ForegroundColor Cyan
    Push-Location $ScriptDir

    $InstallDir = Join-Path $ScriptDir "install"

    # Configure
    Write-Host "Configuring..."
    cmake -B build -DCMAKE_INSTALL_PREFIX="$InstallDir" -DCMAKE_BUILD_TYPE=$BuildType

    # Build
    Write-Host "Building..."
    cmake --build build --config $BuildType --parallel

    # Install
    Write-Host "Installing to $InstallDir..."
    cmake --install build --config $BuildType

    # Verify installation
    Write-Host ""
    Write-Host "=== Verifying installation ===" -ForegroundColor Cyan

    if (Test-Path "$InstallDir/lib/cmake/pugixml/pugixml-config.cmake") {
        Write-Host "[OK] pugixml installed successfully" -ForegroundColor Green
    } else {
        Write-Host "[!] WARNING: pugixml not found" -ForegroundColor Yellow
    }

    Write-Host ""
    Write-Host "Note: Catch2 is built from source by the test target (not pre-built)"
    if (Test-Path "$ScriptDir/Catch2") {
        Write-Host "[OK] Catch2 sources available for offline builds" -ForegroundColor Green
    } else {
        Write-Host "[!] Catch2 sources not found - tests will use FetchContent" -ForegroundColor Yellow
    }

    Pop-Location
    Write-Host ""
    Write-Host "Build complete!" -ForegroundColor Green
    Write-Host ""
    Write-Host "To use pre-built dependencies, configure the main project with:" -ForegroundColor Yellow
    Write-Host "  cmake -B build -DSENTRIX_THIRD_PARTY_PREFIX=third_party/install" -ForegroundColor White
}
