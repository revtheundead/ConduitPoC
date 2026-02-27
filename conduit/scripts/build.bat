@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: Conduit — Build Script (Windows)
::
:: Usage:
::   scripts\build.bat              Configure + build (Debug)
::   scripts\build.bat --release    Build everything (Release)
::   scripts\build.bat --debug      Build (Debug, no optimizations)
::   scripts\build.bat --clean      Wipe build dir, then configure + build
::   scripts\build.bat --third-party Build only third-party dependencies
::   scripts\build.bat --test       Run all tests after build
:: ============================================================================

:: Navigate to project root (parent of scripts\)
cd /d "%~dp0.."

set "BUILD_TYPE=Debug"
set "CLEAN=0"
set "THIRD_PARTY_ONLY=0"
set "BUILD_ALL=0"
set "RUN_TESTS=0"
set "BUILD_DIR=build"

:: Auto-detect number of CPU cores for parallel builds
set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"

:: ============================================================================
:: Parse arguments
:: ============================================================================

:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--clean"       ( set "CLEAN=1" & shift & goto :parse_args )
if /i "%~1"=="--release"     ( set "BUILD_TYPE=Release" & set "BUILD_ALL=1" & shift & goto :parse_args )
if /i "%~1"=="--debug"       ( set "BUILD_TYPE=Debug" & shift & goto :parse_args )
if /i "%~1"=="--third-party" ( set "THIRD_PARTY_ONLY=1" & shift & goto :parse_args )
if /i "%~1"=="--test"        ( set "RUN_TESTS=1" & shift & goto :parse_args )
echo Unknown argument: %~1
echo Usage: %~nx0 [--release] [--debug] [--clean] [--third-party] [--test]
exit /b 1
:done_args

:: ============================================================================
:: Verify third-party dependencies
:: ============================================================================

echo.
echo ==^> Checking third-party dependencies

if not exist "third_party\Catch2\CMakeLists.txt" (
    echo Error: third_party\Catch2 not found. Please place Catch2 v3 sources in third_party\Catch2\
    exit /b 1
)

if not exist "third_party\pugixml\CMakeLists.txt" (
    echo Error: third_party\pugixml not found. Please place pugixml sources in third_party\pugixml\
    exit /b 1
)

echo   Catch2 ... ok
echo   pugixml ... ok

:: ============================================================================
:: Check tools
:: ============================================================================

echo.
echo ==^> Checking build tools

where cmake >nul 2>&1
if errorlevel 1 (
    echo Error: cmake not found. Please install CMake 3.20+.
    exit /b 1
)

for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /r "cmake version"') do (
    echo   cmake %%v ... ok
)

:: Detect generator — prefer Ninja, fall back to Visual Studio
set "GENERATOR="
where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    echo   ninja ... ok (using Ninja generator)
    goto :generator_done
)

:: Look for Visual Studio (cl.exe on PATH or VS environment)
where cl >nul 2>&1
if not errorlevel 1 (
    echo   cl.exe ... ok (using Visual Studio generator)
    goto :generator_done
)

:: Try to find vswhere and detect VS
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "!VSWHERE!" (
    for /f "tokens=*" %%i in ('"!VSWHERE!" -latest -property installationPath 2^>nul') do (
        echo   Visual Studio found at %%i
    )
    goto :generator_done
)

echo Error: No build tool found. Please install Visual Studio, Ninja, or ensure cl.exe is on PATH.
exit /b 1

:generator_done

:: ============================================================================
:: Clean
:: ============================================================================

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo.
        echo ==^> Cleaning build directory
        rmdir /s /q "%BUILD_DIR%"
    )
)

:: ============================================================================
:: Third-party only
:: ============================================================================

if "%THIRD_PARTY_ONLY%"=="1" (
    echo.
    echo ==^> Building third-party dependencies only

    if defined GENERATOR (
        cmake -B "%BUILD_DIR%" -G "!GENERATOR!" -DCONDUIT_BUILD_BGEN=OFF -DCONDUIT_BUILD_TESTS=ON -DCONDUIT_BUILD_EXAMPLES=OFF -DCONDUIT_BUILD_BENCHMARKS=OFF
    ) else (
        cmake -B "%BUILD_DIR%" -DCONDUIT_BUILD_BGEN=OFF -DCONDUIT_BUILD_TESTS=ON -DCONDUIT_BUILD_EXAMPLES=OFF -DCONDUIT_BUILD_BENCHMARKS=OFF
    )

    if errorlevel 1 (
        echo Error: CMake configure failed.
        exit /b 1
    )

    cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -j %JOBS% --target Catch2 Catch2WithMain
    if errorlevel 1 (
        echo Error: Third-party build failed.
        exit /b 1
    )

    echo.
    echo ==^> Third-party build succeeded
    echo.
    echo Done.
    exit /b 0
)

:: ============================================================================
:: Configure
:: ============================================================================

set "EXAMPLES_FLAG=-DCONDUIT_BUILD_EXAMPLES=OFF"
set "BENCHMARKS_FLAG=-DCONDUIT_BUILD_BENCHMARKS=OFF"

if "%BUILD_ALL%"=="1" (
    set "EXAMPLES_FLAG=-DCONDUIT_BUILD_EXAMPLES=ON"
    set "BENCHMARKS_FLAG=-DCONDUIT_BUILD_BENCHMARKS=ON"
)

:: Determine if (re)configuration is needed
set "NEEDS_CONFIGURE=0"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    set "NEEDS_CONFIGURE=1"
) else (
    :: Check if cached build type or options differ from requested values
    set "CACHED_TYPE="
    set "CACHED_EXAMPLES="
    set "CACHED_BENCHMARKS="

    for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CMAKE_BUILD_TYPE"') do set "CACHED_TYPE=%%a"
    for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_EXAMPLES"') do set "CACHED_EXAMPLES=%%a"
    for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_BENCHMARKS"') do set "CACHED_BENCHMARKS=%%a"

    if not "!CACHED_TYPE!"=="%BUILD_TYPE%" set "NEEDS_CONFIGURE=1"

    if "%BUILD_ALL%"=="1" (
        if not "!CACHED_EXAMPLES!"=="ON" set "NEEDS_CONFIGURE=1"
        if not "!CACHED_BENCHMARKS!"=="ON" set "NEEDS_CONFIGURE=1"
    ) else (
        if not "!CACHED_EXAMPLES!"=="OFF" set "NEEDS_CONFIGURE=1"
        if not "!CACHED_BENCHMARKS!"=="OFF" set "NEEDS_CONFIGURE=1"
    )
)

if "%NEEDS_CONFIGURE%"=="1" (
    echo.
    echo ==^> Configuring (%BUILD_TYPE%)

    if defined GENERATOR (
        cmake -B "%BUILD_DIR%" -G "!GENERATOR!" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCONDUIT_BUILD_BGEN=ON -DCONDUIT_BUILD_TESTS=ON !EXAMPLES_FLAG! !BENCHMARKS_FLAG!
    ) else (
        cmake -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCONDUIT_BUILD_BGEN=ON -DCONDUIT_BUILD_TESTS=ON !EXAMPLES_FLAG! !BENCHMARKS_FLAG!
    )

    if errorlevel 1 (
        echo Error: CMake configure failed.
        exit /b 1
    )
) else (
    echo.
    echo   Build already configured. Use --clean to reconfigure.
)

:: ============================================================================
:: Build
:: ============================================================================

echo.
echo ==^> Building (%BUILD_TYPE%, %JOBS% jobs)

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -j %JOBS%
if errorlevel 1 (
    echo Error: Build failed.
    exit /b 1
)

echo.
echo ==^> Build succeeded

:: ============================================================================
:: Test (only with --test)
:: ============================================================================

if "%RUN_TESTS%"=="1" (
    :: Detect test binary path: multi-config (MSVC) vs single-config (Ninja)
    if exist "%BUILD_DIR%\tests\%BUILD_TYPE%\conduit_tests.exe" (
        set "TEST_PREFIX=%BUILD_DIR%\tests\%BUILD_TYPE%"
        set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests\%BUILD_TYPE%"
    ) else (
        set "TEST_PREFIX=%BUILD_DIR%\tests"
        set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests"
    )

    echo.
    echo ==^> Running conduit tests
    "!TEST_PREFIX!\conduit_tests.exe"
    if errorlevel 1 (
        echo Error: Conduit tests failed.
        exit /b 1
    )

    echo.
    echo ==^> Running bgen tests
    "!BGEN_TEST_PREFIX!\bgen_tests.exe"
    if errorlevel 1 (
        echo Error: Bgen tests failed.
        exit /b 1
    )

    if exist "!BGEN_TEST_PREFIX!\bgen_python_tests.exe" (
        echo.
        echo ==^> Running bgen Python backend tests
        "!BGEN_TEST_PREFIX!\bgen_python_tests.exe"
        if errorlevel 1 (
            echo Error: Bgen Python tests failed.
            exit /b 1
        )
    )

    if exist "!BGEN_TEST_PREFIX!\bgen_java_tests.exe" (
        echo.
        echo ==^> Running bgen Java backend tests
        "!BGEN_TEST_PREFIX!\bgen_java_tests.exe"
        if errorlevel 1 (
            echo Error: Bgen Java tests failed.
            exit /b 1
        )
    )

    echo.
    echo ==^> All tests passed
)

echo.
echo Done.
