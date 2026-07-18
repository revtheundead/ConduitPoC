# ============================================================================
# bump_version.ps1 -- Update version strings across all project files (Windows)
# ============================================================================
#
# PowerShell port of bump_version.sh. Reads the version from the VERSION file at
# the repository root (or sets it from the first argument), then patches every
# file that contains a hardcoded version string.
#
# All file I/O is done as UTF-8 WITHOUT a BOM. This is deliberate: Windows
# PowerShell 5's Get-Content defaults to the system ANSI code page (which
# corrupts UTF-8 source files -- e.g. em dashes become mojibake) and
# Set-Content -Encoding UTF8 writes a BOM (which breaks tools that expect a
# clean XML/UTF-8 stream). Reading and writing explicitly here avoids both.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File bump_version.ps1            # from VERSION
#   powershell -ExecutionPolicy Bypass -File bump_version.ps1 0.2.0      # set, then apply
# ============================================================================

param([string]$NewVersion)

$ErrorActionPreference = 'Stop'

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$VersionFile = Join-Path $ProjectRoot 'VERSION'

if ($NewVersion) {
    [System.IO.File]::WriteAllText($VersionFile, "$NewVersion`n", $Utf8NoBom)
}

if (-not (Test-Path -LiteralPath $VersionFile)) {
    Write-Error "VERSION file not found at $VersionFile"; exit 1
}

$Version = ((Get-Content -LiteralPath $VersionFile -Raw -Encoding UTF8) -replace '\s', '')
if (-not $Version) { Write-Error "VERSION file is empty"; exit 1 }

Write-Host "Setting version to: $Version"

# Read a file as UTF-8, apply a transform, and write it back as UTF-8 (no BOM).
function Edit-File {
    param([string]$RelPath, [scriptblock]$Transform)
    $full = Join-Path $ProjectRoot $RelPath
    $c = Get-Content -LiteralPath $full -Raw -Encoding UTF8
    $c = & $Transform $c
    [System.IO.File]::WriteAllText($full, $c, $Utf8NoBom)
}

# Replace only the FIRST <version>...</version> -- the project's own version
# (dependency <version> tags come later and are handled explicitly).
function Set-ProjectVersion {
    param([string]$c)
    ([regex]'<version>[^<]*</version>').Replace($c, "<version>$Version</version>", 1)
}

# Replace the <version> immediately following the conduit-java <artifactId>
# (the dependency pin), without touching any other version.
function Set-ConduitDep {
    param([string]$c)
    $c -replace '(<artifactId>conduit-java</artifactId>\s*)<version>[^<]*</version>', "`${1}<version>$Version</version>"
}

# NOTE: conduit/CMakeLists.txt is intentionally NOT updated here -- it reads the
# version dynamically from the VERSION file at CMake configure time.

# Java pom.xml (bindings) -- project version
Edit-File 'conduit\bindings\java\pom.xml' { param($c) Set-ProjectVersion $c }

# Java build.gradle (xcvr-java11) -- project version + dependency version
Edit-File 'conduit\examples\xcvr-java11\build.gradle' {
    param($c)
    $c = $c -replace "version = '[^']*'", "version = '$Version'"
    $c -replace "conduit-java:[^']*'", "conduit-java:$Version'"
}

# Java pom.xml (xcvr-java11 / xcvr-java21) -- project version + conduit-java dep
Edit-File 'conduit\examples\xcvr-java11\pom.xml' { param($c) Set-ConduitDep (Set-ProjectVersion $c) }
Edit-File 'conduit\examples\xcvr-java21\pom.xml' { param($c) Set-ConduitDep (Set-ProjectVersion $c) }

# Python pyproject.toml (bindings) -- version = "..."
Edit-File 'conduit\bindings\python\pyproject.toml' {
    param($c) $c -replace '(?m)^version = "[^"]*"', "version = `"$Version`""
}

# Python __init__.py -- __version__ = "..."
Edit-File 'conduit\bindings\python\conduit\__init__.py' {
    param($c) $c -replace '__version__ = "[^"]*"', "__version__ = `"$Version`""
}

# Python xcvr-python example -- project version + conduit dependency pin
Edit-File 'conduit\examples\xcvr-python\pyproject.toml' {
    param($c)
    $c = $c -replace '(?m)^version = "[^"]*"', "version = `"$Version`""
    $c -replace '"conduit>=[^"]*"', "`"conduit>=$Version`""
}

# Full C ABI + codec-only C ABI -- return "X.Y.Z"
Edit-File 'conduit\src\cabi\conduit_cabi.cpp' {
    param($c) $c -replace 'return "[0-9]+\.[0-9]+\.[0-9]+"', "return `"$Version`""
}
Edit-File 'conduit\src\cabi\conduit_codec_cabi.cpp' {
    param($c) $c -replace 'return "[0-9]+\.[0-9]+\.[0-9]+"', "return `"$Version`""
}

# package_fat_jar.sh / .bat -- VERSION assignment
Edit-File 'conduit\scripts\package_fat_jar.sh' {
    param($c) $c -replace '(?m)^VERSION="[^"]*"', "VERSION=`"$Version`""
}
Edit-File 'conduit\scripts\package_fat_jar.bat' {
    param($c) $c -replace '(?m)^set VERSION=.*', "set VERSION=$Version"
}

Write-Host ""
Write-Host "Updated files:"
Write-Host "  conduit\bindings\java\pom.xml"
Write-Host "  conduit\examples\xcvr-java11\build.gradle"
Write-Host "  conduit\examples\xcvr-java11\pom.xml"
Write-Host "  conduit\examples\xcvr-java21\pom.xml"
Write-Host "  conduit\bindings\python\pyproject.toml"
Write-Host "  conduit\bindings\python\conduit\__init__.py"
Write-Host "  conduit\examples\xcvr-python\pyproject.toml"
Write-Host "  conduit\src\cabi\conduit_cabi.cpp"
Write-Host "  conduit\src\cabi\conduit_codec_cabi.cpp"
Write-Host "  conduit\scripts\package_fat_jar.sh"
Write-Host "  conduit\scripts\package_fat_jar.bat"
Write-Host ""
Write-Host "Done. VERSION = $Version"
