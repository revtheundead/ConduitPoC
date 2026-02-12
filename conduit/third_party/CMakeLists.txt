# ============================================================================
# Third-party dependencies — standalone pre-build (optional)
#
# This file is only for pre-building pugixml into a local install prefix.
# In practice the main project adds both pugixml and Catch2 directly as
# subdirectories, so running this is NOT required.
#
# Usage:
#   cd third_party
#   cmake -B build -DCMAKE_INSTALL_PREFIX=install
#   cmake --build build --config Release
#   cmake --install build --config Release
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(conduit_third_party LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install"
        CACHE PATH "Install prefix" FORCE)
endif()

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# --- pugixml ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pugixml/CMakeLists.txt")
    message(STATUS "Building pugixml")
    set(PUGIXML_INSTALL ON CACHE BOOL "" FORCE)
    set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(pugixml)
else()
    message(WARNING "pugixml/ not found")
endif()

# --- Catch2 ---
# Catch2 is compiled as part of the main build (add_subdirectory from root).
# Its full source tree should be present at Catch2/ for offline builds.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Catch2/CMakeLists.txt")
    message(STATUS "Note: Catch2/ not found — tests will fail without it")
endif()
