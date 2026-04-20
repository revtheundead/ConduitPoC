cmake_minimum_required(VERSION 3.10)
project(cpp11_compat VERSION 0.1.0 LANGUAGES CXX)

add_library(cpp11_compat INTERFACE)
add_library(cpp11::compat ALIAS cpp11_compat)

target_include_directories(cpp11_compat INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(cpp11_compat INTERFACE cxx_std_11)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS cpp11_compat EXPORT cpp11_compat-targets)
install(EXPORT cpp11_compat-targets
    FILE cpp11_compat-config.cmake
    NAMESPACE cpp11::
    DESTINATION lib/cmake/cpp11_compat
)

option(CPP11_COMPAT_BUILD_TESTS "Build cpp11-compat unit tests" OFF)
if(CPP11_COMPAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
