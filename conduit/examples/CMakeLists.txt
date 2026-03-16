# ============================================================================
# Conduit — Examples
#
# Built only when CONDUIT_BUILD_EXAMPLES=ON.
# xcvr-cpp uses pre-generated code and builds without bgen.
# ============================================================================

# ============================================================================
# xcvr examples (non-critical — failures do not block the main build)
# ============================================================================

# xcvr-cpp: C++ ASTERIX transceiver example
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-cpp/CMakeLists.txt")
    add_subdirectory(xcvr-cpp)
else()
    message(WARNING "xcvr-cpp example not found — skipping")
endif()

# xcvr-java11: optional Maven build target (not part of ALL)
find_program(_mvn_exe mvn)
if(_mvn_exe AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java11/pom.xml")
    add_custom_target(xcvr_java11_build
        COMMAND ${_mvn_exe} package -q
            -f "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java11/pom.xml"
            "-Dconduit.build.dir=${CMAKE_BINARY_DIR}"
        COMMENT "Building xcvr-java11 example (Maven)"
        VERBATIM
    )
else()
    if(NOT _mvn_exe)
        message(STATUS "xcvr-java11: mvn not found — skipping (optional)")
    elseif(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java11/pom.xml")
        message(WARNING "xcvr-java11: pom.xml not found — skipping")
    endif()
endif()

# xcvr-java21: optional Maven build target (not part of ALL)
if(_mvn_exe AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java21/pom.xml")
    add_custom_target(xcvr_java21_build
        COMMAND ${_mvn_exe} package -q
            -f "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java21/pom.xml"
            "-Dconduit.build.dir=${CMAKE_BINARY_DIR}"
        COMMENT "Building xcvr-java21 example (Maven)"
        VERBATIM
    )
else()
    if(NOT _mvn_exe)
        message(STATUS "xcvr-java21: mvn not found — skipping (optional)")
    elseif(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-java21/pom.xml")
        message(WARNING "xcvr-java21: pom.xml not found — skipping")
    endif()
endif()

# xcvr-python: optional pip install target (not part of ALL)
find_program(_pip_exe pip3)
if(NOT _pip_exe)
    find_program(_pip_exe pip)
endif()
if(_pip_exe AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-python/pyproject.toml")
    add_custom_target(xcvr_python_build
        COMMAND ${_pip_exe} install --quiet "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-python/"
        COMMENT "Installing xcvr-python example (pip)"
        VERBATIM
    )
else()
    if(NOT _pip_exe)
        message(STATUS "xcvr-python: pip not found — skipping (optional)")
    elseif(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xcvr-python/pyproject.toml")
        message(WARNING "xcvr-python: pyproject.toml not found — skipping")
    endif()
endif()
