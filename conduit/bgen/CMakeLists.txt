# ============================================================================
# bgen — BMDL code generator
#
# Reads BMDL XML files and emits C++ headers that #include conduit_runtime.
# bgen itself does NOT link conduit_runtime; it only needs pugixml.
# ============================================================================

# Shared source list (excluding main.cpp) — used by both the bgen
# executable and the bgen_lib test library in tests/CMakeLists.txt.
set(BGEN_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/expression_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/xml_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/import_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/auto_expr_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ast_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ast_dump.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analyzer/type_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analyzer/validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analyzer/wire_sizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analyzer/session_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/name_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/codegen_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_constants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_structs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_structs_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_structs_encode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_structs_decode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_structs_expr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_umbrella.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/cpp_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/python_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/java_backend.cpp
)

add_executable(bgen
    src/main.cpp
    ${BGEN_LIB_SOURCES}
)

target_compile_features(bgen PRIVATE cxx_std_23)
target_link_libraries(bgen PRIVATE pugixml::pugixml conduit_warnings)
target_compile_definitions(bgen PRIVATE
    BGEN_VERSION="${PROJECT_VERSION}"
)

target_include_directories(bgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Statically link the C/C++ runtime on Linux so the resulting binary does not
# depend on the build host's glibc / libstdc++ version.  bgen is a pure
# file-I/O CLI tool (no DNS/NSS/dlopen), so full static linking is safe.
# Skipped when sanitizers are active because -static and -fsanitize are
# mutually exclusive (sanitizer runtimes require dynamic linking).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CONDUIT_ENABLE_SANITIZERS)
    string(FIND "${CMAKE_CXX_FLAGS}${CMAKE_EXE_LINKER_FLAGS}" "sanitize" _bgen_sanitize_pos)
    if(_bgen_sanitize_pos EQUAL -1)
        target_link_options(bgen PRIVATE -static)
    endif()
endif()

# Put bgen binary under build/bgen/ for easy discovery.
# Per-config overrides are needed because the parent project sets global
# CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG> — without these, the per-config
# property would override our base RUNTIME_OUTPUT_DIRECTORY.
set_target_properties(bgen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bgen"
)
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set_target_properties(bgen PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bgen"
    )
endforeach()

# bgen tests
if(CONDUIT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
