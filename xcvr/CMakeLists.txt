cmake_minimum_required(VERSION 3.20)
project(niis-xcvr CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull in conduit library (disable tests/bgen/examples for the PoC build)
set(CONDUIT_BUILD_BGEN OFF CACHE BOOL "" FORCE)
set(CONDUIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CONDUIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONDUIT_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../conduit ${CMAKE_BINARY_DIR}/conduit)

# Common compile options
function(niis_xcvr_compile_opts target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /permissive- /Zc:__cplusplus /Zc:preprocessor)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# poc_app — TCP client using asterix (client perspective)
add_executable(poc_app src/poc_app.cpp)
target_link_libraries(poc_app PRIVATE conduit)
target_include_directories(poc_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
niis_xcvr_compile_opts(poc_app)

# dummy_peer — TCP server (asterix-alt) or client (asterix)
add_executable(dummy_peer src/dummy_peer.cpp)
target_link_libraries(dummy_peer PRIVATE conduit)
target_include_directories(dummy_peer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/asterix-alt/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
niis_xcvr_compile_opts(dummy_peer)
