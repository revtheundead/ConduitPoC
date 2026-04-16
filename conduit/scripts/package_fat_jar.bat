@echo off
REM ============================================================================
REM package_fat_jar.bat -- Assemble a fat JAR with bundled native libraries
REM ============================================================================
REM
REM This script copies pre-built JNI native libraries into the Maven resource
REM directory and builds the conduit-java JAR.  The resulting JAR contains
REM native libs for every platform provided, so Java users need only a single
REM dependency.
REM
REM Usage:
REM   package_fat_jar.bat [--native-dir <dir>] [--output <path>]
REM
REM Options:
REM   --native-dir <dir>   Directory containing platform subdirectories with
REM                         native libs.  Expected structure:
REM                           <dir>\linux-x86_64\libconduit_jni.so
REM                           <dir>\windows-x86_64\conduit_jni.dll
REM                           ...
REM                         If omitted, the script copies from the local build
REM                         output (conduit\lib\) for the current platform only.
REM
REM   --output <path>      Where to place the final JAR.  Defaults to
REM                         conduit\lib\conduit-java-fat-<version>.jar
REM
REM ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "BINDINGS_DIR=%PROJECT_ROOT%\conduit\bindings\java"
set "RESOURCES_DIR=%BINDINGS_DIR%\src\main\resources\native"
set VERSION=1.0.8

set "NATIVE_DIR="
set "OUTPUT="

:parse_args
if "%~1"=="" goto args_done
if "%~1"=="--native-dir" (
    set "NATIVE_DIR=%~2"
    shift
    shift
    goto parse_args
)
if "%~1"=="--output" (
    set "OUTPUT=%~2"
    shift
    shift
    goto parse_args
)
echo Unknown option: %~1 >&2
exit /b 1

:args_done

if "!OUTPUT!"=="" (
    set "OUTPUT=%PROJECT_ROOT%\conduit\lib\conduit-java-fat-!VERSION!.jar"
)

REM Clean previous native resources
if exist "!RESOURCES_DIR!" rmdir /s /q "!RESOURCES_DIR!"

if not "!NATIVE_DIR!"=="" (
    REM Copy all platform native libs from the provided directory
    echo Bundling native libraries from: !NATIVE_DIR!
    for /d %%P in ("!NATIVE_DIR!\*") do (
        set "PLATFORM=%%~nxP"
        mkdir "!RESOURCES_DIR!\!PLATFORM!" 2>nul
        copy /y "%%P\*" "!RESOURCES_DIR!\!PLATFORM!\" >nul 2>&1
        echo   !PLATFORM!
    )
) else (
    REM Single-platform: copy from local build output (Windows x86_64)
    echo No --native-dir specified; bundling current platform only
    set "PLATFORM=windows-x86_64"
    mkdir "!RESOURCES_DIR!\!PLATFORM!" 2>nul

    set "LIB_DIR=%PROJECT_ROOT%\conduit\lib"
    if exist "!LIB_DIR!\conduit_jni.dll" (
        copy /y "!LIB_DIR!\conduit_jni.dll" "!RESOURCES_DIR!\!PLATFORM!\" >nul
    )
    if exist "!LIB_DIR!\conduit_codec_jni.dll" (
        copy /y "!LIB_DIR!\conduit_codec_jni.dll" "!RESOURCES_DIR!\!PLATFORM!\" >nul
    )
    if exist "!LIB_DIR!\conduit_cabi.dll" (
        copy /y "!LIB_DIR!\conduit_cabi.dll" "!RESOURCES_DIR!\!PLATFORM!\" >nul
    )
    if exist "!LIB_DIR!\conduit_codec_cabi.dll" (
        copy /y "!LIB_DIR!\conduit_codec_cabi.dll" "!RESOURCES_DIR!\!PLATFORM!\" >nul
    )
    echo   !PLATFORM!
)

REM Build the JAR
echo.
echo Building fat JAR...
call mvn package -B -q -f "!BINDINGS_DIR!\pom.xml"
if errorlevel 1 (
    echo ERROR: Maven build failed >&2
    exit /b 1
)

REM Copy to output location
set "SRC_JAR=!BINDINGS_DIR!\target\conduit-java-!VERSION!.jar"
if not exist "!SRC_JAR!" (
    echo ERROR: Maven did not produce !SRC_JAR! >&2
    exit /b 1
)

for %%F in ("!OUTPUT!") do mkdir "%%~dpF" 2>nul
copy /y "!SRC_JAR!" "!OUTPUT!" >nul

echo.
echo Fat JAR: !OUTPUT!
echo.

REM Clean up: remove native resources so dev builds aren't affected
if exist "!RESOURCES_DIR!" rmdir /s /q "!RESOURCES_DIR!"

endlocal
