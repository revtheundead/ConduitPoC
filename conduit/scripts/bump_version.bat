@echo off
REM ============================================================================
REM bump_version.bat -- Update version strings across all project files
REM ============================================================================
REM
REM Thin wrapper around bump_version.ps1. The real logic lives in the PowerShell
REM script so that file I/O is UTF-8-safe (no BOM, no ANSI-codepage mojibake)
REM and the find/replace patterns are not mangled by batch<->PowerShell quoting.
REM
REM Usage:
REM   bump_version.bat              -- apply version from VERSION file
REM   bump_version.bat 0.2.0        -- set VERSION file to 0.2.0, then apply
REM ============================================================================

setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bump_version.ps1" %*
exit /b %ERRORLEVEL%
