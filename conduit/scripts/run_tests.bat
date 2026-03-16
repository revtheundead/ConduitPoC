@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: Conduit — Test Runner (Windows)
::
:: Runs all available test suites: C++ (Catch2), Java (JUnit 5), Python (pytest).
::
:: Usage:
::   scripts\run_tests.bat              Run tests (expects build\ directory)
::   scripts\run_tests.bat BUILD_DIR    Run tests using a custom build directory
::
:: Java and Python tests are non-fatal — failures are reported but do not
:: prevent subsequent test suites from running.
:: ============================================================================

:: Resolve BUILD_DIR to absolute path before cd (argument is relative to CWD)
set "BUILD_DIR=build"
if not "%~1"=="" set "BUILD_DIR=%~1"
pushd "!BUILD_DIR!" 2>nul && (
    set "BUILD_DIR=!CD!"
    popd
)

cd /d "%~dp0.."

:: Ensure DLL dependencies in lib\ are findable (JNI, Python ctypes, etc.)
set "PATH=%CD%\lib;%PATH%"

set "PASSED=0"
set "FAILED=0"

:: ============================================================================
:: C++ Tests (Catch2)
:: ============================================================================

echo.
echo ==^> C++ Tests

:: Detect test binary path: multi-config (MSVC) vs single-config (Ninja)
set "TEST_PREFIX=%BUILD_DIR%\tests"
set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests"
if exist "%BUILD_DIR%\tests\Release\conduit_tests.exe" (
    set "TEST_PREFIX=%BUILD_DIR%\tests\Release"
    set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests\Release"
)
if exist "%BUILD_DIR%\tests\Debug\conduit_tests.exe" (
    set "TEST_PREFIX=%BUILD_DIR%\tests\Debug"
    set "BGEN_TEST_PREFIX=%BUILD_DIR%\bgen\tests\Debug"
)

if exist "!TEST_PREFIX!\conduit_tests.exe" (
    echo   Running conduit_tests...
    "!TEST_PREFIX!\conduit_tests.exe"
    if errorlevel 1 (
        echo   FAIL: conduit_tests
        set /a FAILED+=1
    ) else (
        echo   PASS: conduit_tests
        set /a PASSED+=1
    )
) else (
    echo   Warning: conduit_tests not found at !TEST_PREFIX!\conduit_tests.exe
)

if exist "!BGEN_TEST_PREFIX!\bgen_tests.exe" (
    echo   Running bgen_tests...
    "!BGEN_TEST_PREFIX!\bgen_tests.exe"
    if errorlevel 1 (
        echo   FAIL: bgen_tests
        set /a FAILED+=1
    ) else (
        echo   PASS: bgen_tests
        set /a PASSED+=1
    )
) else (
    echo   Warning: bgen_tests not found
)

if exist "!BGEN_TEST_PREFIX!\bgen_python_tests.exe" (
    echo   Running bgen_python_tests...
    "!BGEN_TEST_PREFIX!\bgen_python_tests.exe"
    if errorlevel 1 (
        echo   FAIL: bgen_python_tests
        set /a FAILED+=1
    ) else (
        echo   PASS: bgen_python_tests
        set /a PASSED+=1
    )
)

if exist "!BGEN_TEST_PREFIX!\bgen_java_tests.exe" (
    echo   Running bgen_java_tests...
    "!BGEN_TEST_PREFIX!\bgen_java_tests.exe"
    if errorlevel 1 (
        echo   FAIL: bgen_java_tests
        set /a FAILED+=1
    ) else (
        echo   PASS: bgen_java_tests
        set /a PASSED+=1
    )
)

:: ============================================================================
:: Java JUnit Tests
:: ============================================================================

echo.
echo ==^> Java JUnit Tests

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
                echo   Running Java JUnit tests...
                :: Conditionally exclude CABI/JNI tests based on native test library presence
                set "JUNIT_EXCLUDES="
                if not exist "%~dp0..\lib\conduit_cabi_test.dll" (
                    if not exist "!BUILD_DIR!\tests\conduit_cabi_test.dll" (
                        set "JUNIT_EXCLUDES=--exclude-classname TestTransceiverCabi --exclude-classname .*CodecCabi.*"
                    )
                )
                if not exist "%~dp0..\lib\conduit_jni_test.dll" (
                    if not exist "!BUILD_DIR!\tests\conduit_jni_test.dll" (
                        set "JUNIT_EXCLUDES=!JUNIT_EXCLUDES! --exclude-classname TestTransceiverJni --exclude-classname TestXcvrScenarios --exclude-classname TestTransceiverAdvanced"
                    )
                )
                :: Panama FFI tests require --enable-preview on JDK 21+
                set "JAVA_JVM_FLAGS="
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
                    echo   FAIL: Java JUnit tests
                    set /a FAILED+=1
                ) else (
                    echo   PASS: Java JUnit tests
                    set /a PASSED+=1
                )
            ) else (
                echo   Warning: java not found -- skipping Java JUnit tests
            )
        ) else (
            echo   Java JAR not found -- build with --java or CONDUIT_BUILD_JAVA_JAR=ON
        )
    ) else (
        echo   Java test classes not found -- build with --java or CONDUIT_BUILD_JAVA_JAR=ON
    )
) else (
    echo   JUnit JAR not found -- build with --java or CONDUIT_BUILD_JAVA_JAR=ON
)

:: ============================================================================
:: Python pytest Tests
:: ============================================================================

echo.
echo ==^> Python pytest Tests

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
            :: Exclude CABI-dependent tests if native test libraries are not available
            set "PYTEST_IGNORES="
            if not exist "%~dp0..\lib\conduit_cabi_test.dll" (
                if not exist "!BUILD_DIR!\tests\conduit_cabi_test.dll" (
                    set "PYTEST_IGNORES=--ignore="!PYTHON_TESTS!\test_codec_cabi.py" --ignore="!PYTHON_TESTS!\test_transceiver_cabi.py" --ignore="!PYTHON_TESTS!\test_xcvr_scenarios.py" --ignore="!PYTHON_TESTS!\test_async_roundtrip.py""
                )
            )
            :: Generate Python test packages from BMDL fixtures
            if exist "!BUILD_DIR!" (
                cmake --build "!BUILD_DIR!" --target pytest_generated >nul 2>&1
            )
            echo   Running Python pytest tests...
            python -m pytest "!PYTHON_TESTS!" -x -q !PYTEST_IGNORES!
            if errorlevel 1 (
                echo   FAIL: Python pytest tests
                set /a FAILED+=1
            ) else (
                echo   PASS: Python pytest tests
                set /a PASSED+=1
            )
        ) else (
            echo   Warning: pytest not available -- skipping Python tests
        )
    ) else (
        echo   Warning: python not found -- skipping Python tests
    )
) else (
    echo   Python tests directory not found
)

:: ============================================================================
:: Summary
:: ============================================================================

echo.
echo ============================================================================
echo   Test suites passed: !PASSED!    failed: !FAILED!
echo ============================================================================

if !FAILED! gtr 0 exit /b 1
exit /b 0
