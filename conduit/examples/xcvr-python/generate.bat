@echo off
REM ============================================================================
REM xcvr-python — Generate Python code from BMDL definitions
REM
REM Usage:
REM   generate.bat                           Use bgen from ..\..\build\bgen\Release\bgen.exe
REM   set BGEN=C:\path\to\bgen.exe && generate.bat   Use a specific bgen binary
REM ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if "%BGEN%"=="" (
    set "BGEN=%SCRIPT_DIR%..\..\build\bgen\Release\bgen.exe"
)

if not exist "%BGEN%" (
    echo bgen not found at %BGEN% — set BGEN env var or build first 1>&2
    exit /b 1
)

for %%D in (asterix asterix-alt) do (
    set "INPUT=%SCRIPT_DIR%%%D\%%D.bmdl.xml"
    set "OUTPUT=%SCRIPT_DIR%%%D\generated"
    if exist "!INPUT!" (
        if not exist "!OUTPUT!" mkdir "!OUTPUT!"
        echo Generating Python code: %%D -^> !OUTPUT!
        "%BGEN%" --input "!INPUT!" --output "!OUTPUT!" --language python
        if errorlevel 1 (
            echo WARNING: bgen failed for %%D 1>&2
        )
    ) else (
        echo Skipping %%D: !INPUT! not found 1>&2
    )
)

echo Done.
