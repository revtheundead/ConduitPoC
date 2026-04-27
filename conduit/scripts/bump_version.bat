@echo off
REM ============================================================================
REM bump_version.bat -- Update version strings across all project files
REM ============================================================================
REM
REM Reads the version from the VERSION file at the repository root and patches
REM every file that contains a hardcoded version string.
REM
REM Usage:
REM   bump_version.bat              -- apply version from VERSION file
REM   bump_version.bat 0.2.0        -- set VERSION file to 0.2.0, then apply
REM
REM ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "VERSION_FILE=%PROJECT_ROOT%\VERSION"

if not "%~1"=="" (
    echo %~1> "%VERSION_FILE%"
)

if not exist "%VERSION_FILE%" (
    echo ERROR: VERSION file not found at %VERSION_FILE% >&2
    exit /b 1
)

set /p VERSION=<"%VERSION_FILE%"
REM Trim trailing whitespace
for /f "tokens=* delims= " %%a in ("!VERSION!") do set "VERSION=%%a"

if "!VERSION!"=="" (
    echo ERROR: VERSION file is empty >&2
    exit /b 1
)

echo Setting version to: !VERSION!

REM Use PowerShell for reliable find-and-replace across files.
REM IMPORTANT: Use [IO.File]::WriteAllText() instead of Set-Content -Encoding UTF8
REM to avoid writing a UTF-8 BOM (Windows PowerShell 5 adds BOM with -Encoding UTF8).
set "PS=powershell -NoProfile -Command"

REM NOTE: conduit/CMakeLists.txt is intentionally NOT updated here — it reads
REM the version dynamically from the VERSION file via file(READ ...) at CMake
REM configure time, so no hardcoded version string exists to patch.

REM Java pom.xml (bindings) — first <version> tag after conduit-java artifactId
%PS% "$f='%PROJECT_ROOT%\conduit\bindings\java\pom.xml'; $c=Get-Content $f -Raw; $c=$c -replace '(?<=<artifactId>conduit-java</artifactId>\s*\r?\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; [IO.File]::WriteAllText($f,$c)"

REM Java build.gradle (xcvr-java11) — project version + dependency version
%PS% "$f='%PROJECT_ROOT%\conduit\examples\xcvr-java11\build.gradle'; $c=Get-Content $f -Raw; $c=$c -replace \"version = '[^']*'\",\"version = '!VERSION!'\"; $c=$c -replace 'conduit-java:[^'']*''','conduit-java:!VERSION!'''; [IO.File]::WriteAllText($f,$c)"

REM Java pom.xml (xcvr-java11) — project version + conduit-java dependency only
%PS% "$f='%PROJECT_ROOT%\conduit\examples\xcvr-java11\pom.xml'; $c=Get-Content $f -Raw; $c=$c -replace '(?<=<artifactId>conduit-example-xcvr-java11</artifactId>\s*\r?\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; $c=$c -replace '(?<=<artifactId>conduit-java</artifactId>\s*\r?\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; [IO.File]::WriteAllText($f,$c)"

REM Java pom.xml (xcvr-java21) — project version + conduit-java dependency only
%PS% "$f='%PROJECT_ROOT%\conduit\examples\xcvr-java21\pom.xml'; $c=Get-Content $f -Raw; $c=$c -replace '(?<=<artifactId>conduit-example-xcvr-java21</artifactId>\s*\r?\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; $c=$c -replace '(?<=<artifactId>conduit-java</artifactId>\s*\r?\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; [IO.File]::WriteAllText($f,$c)"

REM Python pyproject.toml
%PS% "$f='%PROJECT_ROOT%\conduit\bindings\python\pyproject.toml'; $c=Get-Content $f -Raw; $c=$c -replace '(?m)^version = \""[^\""]*\""','version = \""!VERSION!\""'; [IO.File]::WriteAllText($f,$c)"

REM Python __init__.py
%PS% "$f='%PROJECT_ROOT%\conduit\bindings\python\conduit\__init__.py'; $c=Get-Content $f -Raw; $c=$c -replace '__version__ = \""[^\""]*\""','__version__ = \""!VERSION!\""'; [IO.File]::WriteAllText($f,$c)"

REM Full C ABI
%PS% "$f='%PROJECT_ROOT%\conduit\src\cabi\conduit_cabi.cpp'; $c=Get-Content $f -Raw; $c=$c -replace 'return \""[0-9]+\.[0-9]+\.[0-9]+\""','return \""!VERSION!\""'; [IO.File]::WriteAllText($f,$c)"

REM Codec-only C ABI (conduit_codec_version)
%PS% "$f='%PROJECT_ROOT%\conduit\src\cabi\conduit_codec_cabi.cpp'; $c=Get-Content $f -Raw; $c=$c -replace 'return \""[0-9]+\.[0-9]+\.[0-9]+\""','return \""!VERSION!\""'; [IO.File]::WriteAllText($f,$c)"

REM package_fat_jar.sh
%PS% "$f='%PROJECT_ROOT%\conduit\scripts\package_fat_jar.sh'; $c=Get-Content $f -Raw; $c=$c -replace '(?m)^VERSION=\""[^\""]*\""','VERSION=\""!VERSION!\""'; [IO.File]::WriteAllText($f,$c)"

REM package_fat_jar.bat
if exist "%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat" (
    %PS% "$f='%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat'; $c=Get-Content $f -Raw; $c=$c -replace '(?m)^set VERSION=.*','set VERSION=!VERSION!'; [IO.File]::WriteAllText($f,$c)"
)

echo.
echo Updated files:
echo   conduit\bindings\java\pom.xml
echo   conduit\examples\xcvr-java11\build.gradle
echo   conduit\examples\xcvr-java11\pom.xml
echo   conduit\examples\xcvr-java21\pom.xml
echo   conduit\bindings\python\pyproject.toml
echo   conduit\bindings\python\conduit\__init__.py
echo   conduit\src\cabi\conduit_cabi.cpp
echo   conduit\src\cabi\conduit_codec_cabi.cpp
echo   conduit\scripts\package_fat_jar.sh
if exist "%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat" echo   conduit\scripts\package_fat_jar.bat
echo.
echo Done. VERSION = !VERSION!

endlocal
