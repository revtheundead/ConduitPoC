@echo off
REM Windows wrapper for bgen_to_cpp11.py
setlocal
set SCRIPT_DIR=%~dp0
if not defined PYTHON set PYTHON=python
"%PYTHON%" "%SCRIPT_DIR%bgen_to_cpp11.py" %*
endlocal
