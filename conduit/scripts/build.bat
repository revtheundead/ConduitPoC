@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: Conduit — Build Script (Windows)
::
:: Usage:
::   scripts\build.bat              Configure + build (Debug)
::   scripts\build.bat --release    Build everything (Release): enables
::                                  examples, benchmarks, cabi, jni, java
::   scripts\build.bat --debug      Build (Debug, no optimizations)
::   scripts\build.bat --clean      Wipe build dir, then configure + build
::   scripts\build.bat --cabi       Build CABI shared libraries
::   scripts\build.bat --jni        Build JNI shared libraries (implies --cabi)
::   scripts\build.bat --java       Build Java bindings via Maven (implies --jni)
::   scripts\build.bat --sanitize   Enable address + undefined-behavior sanitizers
::   scripts\build.bat --third-party Build only third-party dependencies
::   scripts\build.bat --test       Run all tests after build
::
:: Flags may be combined freely, e.g.:
::   scripts\build.bat --debug --jni --test
:: ============================================================================

:: Navigate to project root (parent of scripts\)
cd /d "%~dp0.."

set "BUILD_TYPE=Debug"
set "CLEAN=0"
set "THIRD_PARTY_ONLY=0"
set "BUILD_ALL=0"
set "RUN_TESTS=0"
set "BUILD_CABI=0"
set "BUILD_JNI=0"
set "BUILD_JAVA=0"
set "ENABLE_SANITIZERS=0"
set "BUILD_DIR=build"

:: Auto-detect number of CPU cores for parallel builds
set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"

:: ============================================================================
:: Parse arguments
:: ============================================================================

:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--clean"        ( set "CLEAN=1"             & shift & goto :parse_args )
if /i "%~1"=="--release"      ( set "BUILD_TYPE=Release"  & set "BUILD_ALL=1" & set "BUILD_CABI=1" & set "BUILD_JNI=1" & set "BUILD_JAVA=1" & shift & goto :parse_args )
if /i "%~1"=="--debug"        ( set "BUILD_TYPE=Debug"    & shift & goto :parse_args )
if /i "%~1"=="--third-party"  ( set "THIRD_PARTY_ONLY=1"  & shift & goto :parse_args )
if /i "%~1"=="--test"         ( set "RUN_TESTS=1"         & shift & goto :parse_args )
if /i "%~1"=="--cabi"         ( set "BUILD_CABI=1"        & shift & goto :parse_args )
if /i "%~1"=="--jni"          ( set "BUILD_CABI=1"        & set "BUILD_JNI=1" & shift & goto :parse_args )
if /i "%~1"=="--java"         ( set "BUILD_CABI=1"        & set "BUILD_JNI=1" & set "BUILD_JAVA=1" & shift & goto :parse_args )
if /i "%~1"=="--sanitize"     ( set "ENABLE_SANITIZERS=1" & shift & goto :parse_args )
echo Unknown argument: %~1
echo Usage: %~nx0 [--release] [--debug] [--clean] [--cabi] [--jni] [--java] [--sanitize] [--third-party] [--test]
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

:: Detect generator.
:: Use HAS_GENERATOR flag to avoid the "if defined VAR" pitfall with empty
:: string variables — in CMD, a variable set to "" is still "defined".
set "GENERATOR="
set "HAS_GENERATOR=0"

where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "HAS_GENERATOR=1"
    echo   ninja ... ok ^(using Ninja generator^)
    goto :generator_done
)

:: Clang: prefer Ninja if available, otherwise let CMake pick a generator.
where clang++ >nul 2>&1
if not errorlevel 1 (
    echo   clang++ ... ok
    if "%HAS_GENERATOR%"=="0" (
        echo   ^(no Ninja found; CMake will select a generator^)
    )
    goto :generator_done
)

:: MSVC cl.exe: CMake will auto-select the installed Visual Studio generator.
where cl >nul 2>&1
if not errorlevel 1 (
    echo   cl.exe ... ok ^(CMake will select Visual Studio generator^)
    goto :generator_done
)

:: vswhere fallback — Visual Studio installed but not on PATH
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

:: JNI/Java tool checks
if "%BUILD_JNI%"=="1" (
    where java >nul 2>&1
    if errorlevel 1 (
        echo Warning: java not found -- JNI build may fail if JAVA_HOME is not set.
    ) else (
        echo   java ... ok
    )
)

if "%BUILD_JAVA%"=="1" (
    where mvn >nul 2>&1
    if not errorlevel 1 (
        echo   mvn ... ok
    ) else (
        where gradle >nul 2>&1
        if not errorlevel 1 (
            echo   gradle ... ok
        ) else (
            where javac >nul 2>&1
            if not errorlevel 1 (
                echo   javac ... ok
            ) else (
                echo Warning: No Java build tool found ^(mvn, gradle, or javac^) -- Java JAR may not build
            )
        )
    )
)

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

    if "%HAS_GENERATOR%"=="1" (
        cmake -B "%BUILD_DIR%" -G "!GENERATOR!" ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
            -DCONDUIT_BUILD_BGEN=OFF -DCONDUIT_BUILD_TESTS=ON ^
            -DCONDUIT_BUILD_EXAMPLES=OFF -DCONDUIT_BUILD_BENCHMARKS=OFF
    ) else (
        cmake -B "%BUILD_DIR%" ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
            -DCONDUIT_BUILD_BGEN=OFF -DCONDUIT_BUILD_TESTS=ON ^
            -DCONDUIT_BUILD_EXAMPLES=OFF -DCONDUIT_BUILD_BENCHMARKS=OFF
    )
    if errorlevel 1 (
        echo Error: CMake configure failed.
        exit /b 1
    )

    cmake --build "%BUILD_DIR%" --config !BUILD_TYPE! -j !JOBS! --target Catch2 Catch2WithMain
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
:: Assemble CMake flags
:: ============================================================================

set "FLAG_EXAMPLES=-DCONDUIT_BUILD_EXAMPLES=OFF"
set "FLAG_BENCHMARKS=-DCONDUIT_BUILD_BENCHMARKS=OFF"
if "%BUILD_ALL%"=="1" (
    set "FLAG_EXAMPLES=-DCONDUIT_BUILD_EXAMPLES=ON"
    set "FLAG_BENCHMARKS=-DCONDUIT_BUILD_BENCHMARKS=ON"
)

set "FLAG_CABI=-DCONDUIT_BUILD_CABI=OFF -DCONDUIT_BUILD_CODEC_CABI=OFF"
if "%BUILD_CABI%"=="1" (
    set "FLAG_CABI=-DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_CODEC_CABI=ON"
)

set "FLAG_JNI=-DCONDUIT_BUILD_JNI=OFF"
if "%BUILD_JNI%"=="1" (
    set "FLAG_JNI=-DCONDUIT_BUILD_JNI=ON"
)

set "FLAG_JAVA_JAR=-DCONDUIT_BUILD_JAVA_JAR=OFF"
if "%BUILD_JAVA%"=="1" (
    set "FLAG_JAVA_JAR=-DCONDUIT_BUILD_JAVA_JAR=ON"
)

set "FLAG_SANITIZE=-DCONDUIT_ENABLE_SANITIZERS=OFF"
if "%ENABLE_SANITIZERS%"=="1" (
    set "FLAG_SANITIZE=-DCONDUIT_ENABLE_SANITIZERS=ON"
)

:: ============================================================================
:: Determine if (re)configuration is needed
:: ============================================================================

set "NEEDS_CONFIGURE=0"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    set "NEEDS_CONFIGURE=1"
    goto :do_configure_check_done
)

:: Read cached option values
set "CACHED_TYPE="
set "CACHED_EXAMPLES="
set "CACHED_BENCHMARKS="
set "CACHED_CABI="
set "CACHED_JNI="
set "CACHED_JAVA_JAR="
set "CACHED_SANITIZE="

for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CMAKE_BUILD_TYPE"') do set "CACHED_TYPE=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_EXAMPLES"') do set "CACHED_EXAMPLES=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_BENCHMARKS"') do set "CACHED_BENCHMARKS=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_CABI:"') do set "CACHED_CABI=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_JNI"') do set "CACHED_JNI=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_BUILD_JAVA_JAR"') do set "CACHED_JAVA_JAR=%%a"
for /f "tokens=2 delims==" %%a in ('cmake -L -N "%BUILD_DIR%" 2^>nul ^| findstr "CONDUIT_ENABLE_SANITIZERS"') do set "CACHED_SANITIZE=%%a"

:: Compute desired values
set "WANT_EXAMPLES=OFF" & set "WANT_BENCHMARKS=OFF"
if "%BUILD_ALL%"=="1" ( set "WANT_EXAMPLES=ON" & set "WANT_BENCHMARKS=ON" )
set "WANT_CABI=OFF"
if "%BUILD_CABI%"=="1" set "WANT_CABI=ON"
set "WANT_JNI=OFF"
if "%BUILD_JNI%"=="1" set "WANT_JNI=ON"
set "WANT_JAVA_JAR=OFF"
if "%BUILD_JAVA%"=="1" set "WANT_JAVA_JAR=ON"
set "WANT_SANITIZE=OFF"
if "%ENABLE_SANITIZERS%"=="1" set "WANT_SANITIZE=ON"

if not "!CACHED_TYPE!"=="!BUILD_TYPE!"           set "NEEDS_CONFIGURE=1"
if not "!CACHED_EXAMPLES!"=="!WANT_EXAMPLES!"    set "NEEDS_CONFIGURE=1"
if not "!CACHED_BENCHMARKS!"=="!WANT_BENCHMARKS!" set "NEEDS_CONFIGURE=1"
if not "!CACHED_CABI!"=="!WANT_CABI!"            set "NEEDS_CONFIGURE=1"
if not "!CACHED_JNI!"=="!WANT_JNI!"             set "NEEDS_CONFIGURE=1"
if not "!CACHED_JAVA_JAR!"=="!WANT_JAVA_JAR!"   set "NEEDS_CONFIGURE=1"
if not "!CACHED_SANITIZE!"=="!WANT_SANITIZE!"    set "NEEDS_CONFIGURE=1"

:do_configure_check_done

:: ============================================================================
:: Configure
:: ============================================================================

if "%NEEDS_CONFIGURE%"=="1" (
    echo.
    echo ==^> Configuring ^(!BUILD_TYPE!^)

    if "%HAS_GENERATOR%"=="1" (
        cmake -B "%BUILD_DIR%" -G "!GENERATOR!" ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
            -DCONDUIT_BUILD_BGEN=ON -DCONDUIT_BUILD_TESTS=ON ^
            !FLAG_EXAMPLES! !FLAG_BENCHMARKS! ^
            !FLAG_CABI! !FLAG_JNI! !FLAG_JAVA_JAR! !FLAG_SANITIZE!
    ) else (
        cmake -B "%BUILD_DIR%" ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
            -DCONDUIT_BUILD_BGEN=ON -DCONDUIT_BUILD_TESTS=ON ^
            !FLAG_EXAMPLES! !FLAG_BENCHMARKS! ^
            !FLAG_CABI! !FLAG_JNI! !FLAG_JAVA_JAR! !FLAG_SANITIZE!
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
echo ==^> Building ^(!BUILD_TYPE!, !JOBS! jobs^)

cmake --build "%BUILD_DIR%" --config !BUILD_TYPE! -j !JOBS!
if errorlevel 1 (
    echo Error: Build failed.
    exit /b 1
)

echo.
echo ==^> Build succeeded

:: ============================================================================
:: Build examples (only with --release or relevant flags)
:: ============================================================================

if "%BUILD_ALL%"=="1" (
    where mvn >nul 2>&1
    if not errorlevel 1 (
        if exist "examples\xcvr-java11\pom.xml" (
            echo.
            echo ==^> Building xcvr-java11 example ^(Maven^)
            mvn package -q -f "examples\xcvr-java11\pom.xml" -Dskip.bgen=true "-Dconduit.build.dir=!BUILD_DIR!"
            if errorlevel 1 echo Warning: xcvr-java11 build failed ^(non-fatal^)
        )
        if exist "examples\xcvr-java21\pom.xml" (
            echo.
            echo ==^> Building xcvr-java21 example ^(Maven^)
            mvn package -q -f "examples\xcvr-java21\pom.xml" -Dskip.bgen=true
            if errorlevel 1 echo Warning: xcvr-java21 build failed ^(non-fatal^)
        )
    )
    where pip >nul 2>&1
    if not errorlevel 1 (
        if exist "examples\xcvr-python\pyproject.toml" (
            echo.
            echo ==^> Installing xcvr-python example ^(pip^)
            pip install --quiet "examples\xcvr-python\"
            if errorlevel 1 echo Warning: xcvr-python install failed ^(non-fatal^)
        )
    )
)

:: ============================================================================
:: Test (only with --test)
:: ============================================================================

if "%RUN_TESTS%"=="1" (
    :: Detect test binary path: multi-config (MSVC) vs single-config (Ninja)
    set "TEST_PREFIX=%BUILD_DIR%\tests"
    set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests"
    if exist "%BUILD_DIR%\tests\!BUILD_TYPE!\conduit_tests.exe" (
        set "TEST_PREFIX=%BUILD_DIR%\tests\!BUILD_TYPE!"
        set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests\!BUILD_TYPE!"
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

    :: --- Java JUnit tests (non-fatal) ---
    set "JUNIT_JAR="
    if exist "%~dp0..\third_party\junit5\junit-platform-console-standalone-1.11.4.jar" (
        set "JUNIT_JAR=%~dp0..\third_party\junit5\junit-platform-console-standalone-1.11.4.jar"
    )
    set "JAVA_TEST_CLASSES=!BUILD_DIR!\tests\java-test-classes"
    set "JAVA_JAR=%~dp0..\lib\conduit-java-0.1.0.jar"
    if defined JUNIT_JAR (
        if exist "!JAVA_TEST_CLASSES!" (
            if exist "!JAVA_JAR!" (
                where java >nul 2>&1
                if not errorlevel 1 (
                    echo.
                    echo ==^> Running Java JUnit tests
                    set "JUNIT_EXCLUDES="
                    set "JAVA_JVM_FLAGS="
                    if not "%BUILD_CABI%"=="1" (
                        set "JUNIT_EXCLUDES=--exclude-classname TestTransceiverCabi --exclude-classname .*CodecCabi.*"
                    )
                    :: JNI tests require both JNI and CABI libraries
                    set "_exclude_jni=0"
                    if not "%BUILD_JNI%"=="1" set "_exclude_jni=1"
                    if not "%BUILD_CABI%"=="1" set "_exclude_jni=1"
                    if "!_exclude_jni!"=="1" (
                        set "JUNIT_EXCLUDES=!JUNIT_EXCLUDES! --exclude-classname TestTransceiverJni --exclude-classname TestXcvrScenarios"
                    )
                    :: Panama FFI tests require --enable-preview on JDK 21+
                    for /f "tokens=3" %%v in ('java -version 2^>^&1 ^| findstr /i "version"') do (
                        set "_java_ver=%%~v"
                    )
                    for /f "delims=." %%m in ("!_java_ver!") do set "_java_major=%%m"
                    if defined _java_major (
                        if !_java_major! geq 21 (
                            set "JAVA_JVM_FLAGS=--enable-preview --enable-native-access=ALL-UNNAMED"
                        )
                    )
                    java "-Djava.library.path=%~dp0..\lib" ^
                        !JAVA_JVM_FLAGS! ^
                        -jar "!JUNIT_JAR!" ^
                        --class-path "!JAVA_TEST_CLASSES!;!JAVA_JAR!" ^
                        --scan-class-path "!JAVA_TEST_CLASSES!" ^
                        --include-classname "^Test.*" ^
                        !JUNIT_EXCLUDES!
                    if errorlevel 1 (
                        echo Warning: Java JUnit tests failed ^(non-fatal^)
                    )
                ) else (
                    echo Warning: java not found -- skipping Java JUnit tests
                )
            )
        )
    ) else (
        echo   Java JUnit tests not available ^(build with --java or CONDUIT_BUILD_JAVA_JAR=ON^)
    )

    :: --- Python pytest tests (non-fatal) ---
    set "PYTEST_WHEEL_DIR=%~dp0..\third_party\pytest"
    set "PYTHON_TESTS=%~dp0..\tests\python"
    if exist "!PYTHON_TESTS!" (
        where python >nul 2>&1
        if not errorlevel 1 (
            :: Install pytest from vendored wheels if available
            if exist "!PYTEST_WHEEL_DIR!" (
                python -m pip install --no-index --find-links "!PYTEST_WHEEL_DIR!" pytest >nul 2>&1 || (
                    python -m pip install --no-index --find-links "!PYTEST_WHEEL_DIR!" --user pytest >nul 2>&1 || (
                        python -m pip install --no-index --find-links "!PYTEST_WHEEL_DIR!" --break-system-packages pytest >nul 2>&1
                    )
                )
            )
            python -c "import pytest" >nul 2>&1
            if not errorlevel 1 (
                echo.
                echo ==^> Running Python pytest tests
                set "PYTEST_IGNORES="
                if not "%BUILD_CABI%"=="1" (
                    set "PYTEST_IGNORES=--ignore="!PYTHON_TESTS!\test_codec_cabi.py" --ignore="!PYTHON_TESTS!\test_transceiver_cabi.py" --ignore="!PYTHON_TESTS!\test_xcvr_scenarios.py""
                )
                python -m pytest "!PYTHON_TESTS!" -x -q !PYTEST_IGNORES!
                if errorlevel 1 (
                    echo Warning: Python tests failed ^(non-fatal^)
                )
            ) else (
                echo Warning: pytest not available -- skipping Python tests
            )
        ) else (
            echo Warning: python not found -- skipping Python tests
        )
    )

    echo.
    echo ==^> Test run complete
)

echo.
echo Done.
