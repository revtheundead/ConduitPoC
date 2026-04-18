# SPDX-License-Identifier: MIT
# VerifySharedLibDeps.cmake — Post-build script to verify a shared library
# has no unresolvable runtime dependencies.
#
# Usage (as a CMake -P script):
#   cmake -DLIB_FILE=<path> [-DOBJDUMP=<path>] -P VerifySharedLibDeps.cmake
#
# On Linux:  runs ldd and checks for "not found" entries.
# On MinGW:  runs objdump -p and checks for MinGW runtime DLLs that indicate
#            the C++ runtime was not statically linked.

if(NOT EXISTS "${LIB_FILE}")
    message(FATAL_ERROR "VerifySharedLibDeps: library not found: ${LIB_FILE}")
endif()

if(WIN32 OR MINGW_VERIFY)
    # MinGW / Windows: check that the DLL does not reference MinGW runtime
    # DLLs (libc++, libunwind, libstdc++, libgcc, libwinpthread).
    if(NOT OBJDUMP)
        message(WARNING "VerifySharedLibDeps: OBJDUMP not set, skipping ${LIB_FILE}")
        return()
    endif()
    execute_process(
        COMMAND "${OBJDUMP}" -p "${LIB_FILE}"
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(WARNING "VerifySharedLibDeps: objdump failed for ${LIB_FILE}: ${_err}")
        return()
    endif()
    set(_BAD_DEPS "")
    foreach(_dll
        libc++.dll
        libunwind.dll
        "libstdc++-6.dll"
        libgcc_s_seh-1.dll
        libgcc_s_dw2-1.dll
        libwinpthread-1.dll
    )
        string(FIND "${_output}" "${_dll}" _pos)
        if(NOT _pos EQUAL -1)
            list(APPEND _BAD_DEPS "${_dll}")
        endif()
    endforeach()
    if(_BAD_DEPS)
        string(REPLACE ";" ", " _bad_str "${_BAD_DEPS}")
        message(FATAL_ERROR
            "VerifySharedLibDeps: ${LIB_FILE} depends on MinGW runtime DLLs "
            "that may not be present on end-user systems: ${_bad_str}\n"
            "Ensure CMAKE_SHARED_LINKER_FLAGS includes "
            "-static-libstdc++ -static-libgcc")
    endif()
    get_filename_component(_name "${LIB_FILE}" NAME)
    message(STATUS "VerifySharedLibDeps: ${_name} — no MinGW runtime dependencies found")
else()
    # Unix: use ldd to check for unresolved dependencies.
    find_program(_LDD ldd)
    if(NOT _LDD)
        return()
    endif()
    execute_process(
        COMMAND "${_LDD}" "${LIB_FILE}"
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(WARNING "VerifySharedLibDeps: ldd failed for ${LIB_FILE}: ${_err}")
        return()
    endif()
    string(FIND "${_output}" "not found" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "VerifySharedLibDeps: ${LIB_FILE} has unresolved dependencies:\n${_output}")
    endif()
    get_filename_component(_name "${LIB_FILE}" NAME)
    message(STATUS "VerifySharedLibDeps: ${_name} — all dependencies resolved")
endif()
