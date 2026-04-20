# cpp11-compat

Small header-only polyfill library that provides C++11 equivalents of:

- `cpp11::span<T>`, `cpp11::optional<T>`, `cpp11::variant<...>`,
  `cpp11::string_view`, `cpp11::any`, `cpp11::expected<T, E>`,
  `cpp11::visit`, `cpp11::make_unique`.
- `bgen11::io::BitReader`, `bgen11::io::BitWriter`,
  `bgen11::Result<T>`, `bgen11::Error`, `bgen11::traits::ISession` and
  related bgen runtime types.

Designed for use with bgen-generated code translated via
`tools/bgen-cpp11/bgen_to_cpp11.py`.

## Usage

```cmake
find_package(cpp11_compat REQUIRED)
target_link_libraries(your_target PRIVATE cpp11::compat)
```

Or, as a sub-project:

```cmake
add_subdirectory(cpp11-compat)
target_link_libraries(your_target PRIVATE cpp11::compat)
```

## Tests

```sh
cmake -S . -B build -DCPP11_COMPAT_BUILD_TESTS=ON -DCMAKE_CXX_STANDARD=11
cmake --build build
./build/tests/test_cpp11_compat
```
