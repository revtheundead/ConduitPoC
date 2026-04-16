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

REM Use PowerShell for reliable find-and-replace across files
set "PS=powershell -NoProfile -Command"

REM NOTE: conduit/CMakeLists.txt is intentionally NOT updated here — it reads
REM the version dynamically from the VERSION file via file(READ ...) at CMake
REM configure time, so no hardcoded version string exists to patch.

REM Java pom.xml (bindings) — first <version> tag
%PS% "$f='%PROJECT_ROOT%\conduit\bindings\java\pom.xml'; $c=Get-Content $f -Raw; $c=$c -replace '(?<=<artifactId>conduit-java</artifactId>\s*\n\s*)<version>[^<]*</version>','<version>!VERSION!</version>'; Set-Content $f $c -Encoding UTF8 -NoNewline"

REM Java build.gradle (xcvr-java11)
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java11\build.gradle') -replace \"version = '[^']*'\",\"version = '!VERSION!'\" | Set-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java11\build.gradle' -Encoding UTF8"

REM Java pom.xml (xcvr-java11) — all version tags with semver
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java11\pom.xml') -replace '<version>\d+\.\d+\.\d+</version>','<version>!VERSION!</version>' | Set-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java11\pom.xml' -Encoding UTF8"

REM Java pom.xml (xcvr-java21) — all version tags with semver
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java21\pom.xml') -replace '<version>\d+\.\d+\.\d+</version>','<version>!VERSION!</version>' | Set-Content '%PROJECT_ROOT%\conduit\examples\xcvr-java21\pom.xml' -Encoding UTF8"

REM Python pyproject.toml
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\bindings\python\pyproject.toml') -replace '^version = \"[^\"]*\"','version = \"!VERSION!\"' | Set-Content '%PROJECT_ROOT%\conduit\bindings\python\pyproject.toml' -Encoding UTF8"

REM Python __init__.py
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\bindings\python\conduit\__init__.py') -replace '__version__ = \"[^\"]*\"','__version__ = \"!VERSION!\"' | Set-Content '%PROJECT_ROOT%\conduit\bindings\python\conduit\__init__.py' -Encoding UTF8"

REM C ABI
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\src\cabi\conduit_cabi.cpp') -replace 'return \"[0-9]+\.[0-9]+\.[0-9]+\"','return \"!VERSION!\"' | Set-Content '%PROJECT_ROOT%\conduit\src\cabi\conduit_cabi.cpp' -Encoding UTF8"

REM package_fat_jar.sh
%PS% "(Get-Content '%PROJECT_ROOT%\conduit\scripts\package_fat_jar.sh') -replace '^VERSION=\"[^\"]*\"','VERSION=\"!VERSION!\"' | Set-Content '%PROJECT_ROOT%\conduit\scripts\package_fat_jar.sh' -Encoding UTF8"

REM package_fat_jar.bat
if exist "%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat" (
    %PS% "(Get-Content '%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat') -replace '^set VERSION=.*','set VERSION=!VERSION!' | Set-Content '%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat' -Encoding UTF8"
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
echo   conduit\scripts\package_fat_jar.sh
if exist "%PROJECT_ROOT%\conduit\scripts\package_fat_jar.bat" echo   conduit\scripts\package_fat_jar.bat
echo.
echo Done. VERSION = !VERSION!

endlocal
