add_executable(test_cpp11_compat
    test_span.cpp
    test_optional.cpp
    test_variant.cpp
    test_any.cpp
    test_expected.cpp
    test_string_view.cpp
    test_bit_reader.cpp
    test_bit_writer.cpp
    test_main.cpp
)

target_link_libraries(test_cpp11_compat PRIVATE cpp11_compat)
target_compile_features(test_cpp11_compat PRIVATE cxx_std_11)

if(NOT MSVC)
    target_compile_options(test_cpp11_compat PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME cpp11_compat_tests COMMAND test_cpp11_compat)
