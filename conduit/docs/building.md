# Building & Testing

## Requirements

- **C++23** compiler -- required for the conduit runtime and bgen itself
  - **GCC** 12+ (Linux)
  - **Clang** 19+ (Linux, macOS, Windows)
  - **MSVC** 19.30+ / Visual Studio 2022 (Windows)
- CMake 3.20+ (build system)
- **Ninja** -- required when building with Clang on Windows; recommended on all platforms
- **Java** (JDK 8+ for JNI, JDK 21+ for Panama FFI) -- optional, for Java backend
- **Python** 3.7+ -- optional, for Python backend
- No external runtime dependencies (header-only generated code, conduit is a static library). Vendored build-time dependencies (Catch2, pugixml, nlohmann/json) are included in `third_party/`.

## Using the build script (recommended)

```bash
# Linux / macOS
./conduit/scripts/build.sh              # Debug build (C++ runtime + bgen)
./conduit/scripts/build.sh --release    # Release build with examples, CABI, JNI, Java
./conduit/scripts/build.sh --cabi       # Build C ABI shared libraries
./conduit/scripts/build.sh --jni        # Build JNI shared libraries (implies --cabi)
./conduit/scripts/build.sh --java       # Build Java JAR + JNI + CABI
./conduit/scripts/build.sh --test       # Run all test suites after build
./conduit/scripts/build.sh --sanitize   # Enable AddressSanitizer + UBSan
./conduit/scripts/build.sh --clean      # Wipe build directory and rebuild
./conduit/scripts/build.sh --clang      # Use Clang (clang / clang++)
./conduit/scripts/build.sh --gcc        # Use GCC (gcc / g++)

# Windows
scripts\build.bat --release             # Release build (same flags as Linux)
scripts\build.bat --clang               # Use Clang via LLVM MinGW (no VS dependency)
scripts\build.bat --clang-msvc          # Use Clang targeting MSVC STL (requires VS)
scripts\build.bat --msvc                # Use MSVC cl.exe (the default)
```

Flags can be combined freely in any order, e.g.:

```bash
./conduit/scripts/build.sh --release --clang --test
scripts\build.bat --release --clang --test
```

### Compiler selection

When both MSVC and Clang are installed on Windows (or both GCC and Clang on Linux), the build scripts default to the platform's native compiler unless overridden:

| Platform | Default | Override |
|----------|---------|----------|
| Linux / macOS | System default (usually GCC) | `--clang` or `--gcc` |
| Windows | MSVC (cl.exe / Visual Studio) | `--clang`, `--clang-msvc`, or `--msvc` |

The `--clang` flag on Windows uses an **LLVM MinGW** toolchain — a self-contained Clang distribution that includes its own libc++, LLD linker, and mingw-w64 headers, so it does **not** depend on Visual Studio headers or the MSVC linker. Set the `LLVM_MINGW_DIR` environment variable to point to your installation, or ensure the LLVM MinGW `bin/` directory is on `PATH`. Download it from [mstorsjo/llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases).

If you prefer to use stock Clang targeting the MSVC STL (which uses VS headers and `link.exe`), use `--clang-msvc` instead. This requires Visual Studio to be installed.

Both `--clang` and `--clang-msvc` require **Ninja** as the build generator (install via `choco install ninja`). On Linux, the script auto-detects Ninja (preferred) or Make regardless of compiler choice.

The scripts validate that the requested compiler is on `PATH` and fail early with a clear error if it is not found.

### Build script flags reference

| Flag | Description |
|------|-------------|
| `--release` | Release build; enables examples, benchmarks, CABI, JNI, Java |
| `--debug` | Debug build (the default) |
| `--clean` | Wipe the build directory and reconfigure from scratch |
| `--cabi` | Build C ABI shared libraries (`conduit_cabi`, `conduit_codec_cabi`) |
| `--jni` | Build JNI shared libraries (implies `--cabi`) |
| `--java` | Build Java JAR + JNI + CABI |
| `--sanitize` | Enable AddressSanitizer + UBSan |
| `--test` | Run all test suites after the build completes |
| `--third-party` | Build only vendored third-party dependencies, then stop |
| `--clang` | Use Clang via LLVM MinGW (Linux/macOS/Windows). On Windows, requires LLVM MinGW toolchain |
| `--clang-msvc` | Use Clang targeting MSVC STL (Windows only, requires VS) |
| `--gcc` | Use GCC compiler (Linux/macOS only) |
| `--msvc` | Use MSVC compiler (Windows only) |

## Using CMake directly

```bash
# Basic build (C++ runtime + bgen code generator)
cmake -B build -S conduit
cmake --build build

# With examples
cmake -B build -S conduit -DCONDUIT_BUILD_EXAMPLES=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# With cross-language bindings (CABI + JNI)
cmake -B build -S conduit -DCONDUIT_BUILD_CABI=ON -DCONDUIT_BUILD_JNI=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# Build with Clang on Linux
CXX=clang++ cmake -B build -S conduit
cmake --build build

# Build with Clang on Windows via LLVM MinGW (requires Ninja, Clang 19+)
# Point CMAKE_CXX_COMPILER at the LLVM MinGW clang++ binary
cmake -B build -S conduit -G Ninja -DCMAKE_CXX_COMPILER=C:/llvm-mingw/bin/clang++.exe
cmake --build build

# Build with stock Clang targeting MSVC STL (uses VS headers + link.exe)
cmake -B build -S conduit -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build

# Run C++ tests directly
./build/tests/conduit_tests
./build/bgen/tests/bgen_tests
```

### Key CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CONDUIT_BUILD_BGEN` | ON | Build the bgen code generator |
| `CONDUIT_BUILD_TESTS` | ON | Build unit tests |
| `CONDUIT_BUILD_EXAMPLES` | OFF | Build example applications |
| `CONDUIT_BUILD_BENCHMARKS` | OFF | Build performance benchmarks |
| `CONDUIT_BUILD_CABI` | OFF | Build full Transceiver C ABI shared library |
| `CONDUIT_BUILD_CODEC_CABI` | OFF | Build codec-only C ABI shared library |
| `CONDUIT_BUILD_JNI` | OFF | Build JNI shared libraries (Java 11+) |
| `CONDUIT_BUILD_JAVA_JAR` | OFF | Build conduit-java JAR and compile Java tests |
| `CONDUIT_ENABLE_SANITIZERS` | OFF | Enable AddressSanitizer + UBSan |
| `CONDUIT_ENABLE_COVERAGE` | OFF | Enable code coverage instrumentation (GCC/Clang) |

## Running Tests

Conduit includes test suites for C++ (Catch2), Java (JUnit 5), and Python (pytest). Both JUnit and pytest are vendored in `third_party/` for fully offline use.

### Via the build script

```bash
./scripts/build.sh --test                   # runs C++ tests + Java/Python if available
./scripts/build.sh --clang --test           # same, using Clang
scripts\build.bat --test                    # same on Windows (MSVC by default)
scripts\build.bat --clang --test            # same on Windows, using Clang + Ninja
```

### Standalone test runner scripts

```bash
# Linux / macOS
./scripts/run_tests.sh [BUILD_DIR]

# Windows
scripts\run_tests.bat [BUILD_DIR]
```

These scripts run all three test suites and report a summary. Java and Python tests are non-fatal -- failures are logged but do not prevent other suites from running.

### Vendored test dependencies

- `third_party/junit5/` -- JUnit Platform Console Standalone 1.11.4 JAR
- `third_party/pytest/` -- pytest and its dependencies as Python wheel files (iniconfig, pluggy, packaging)

These are automatically used by the test runner scripts when running offline.

## Vendored dependencies

All build-time dependencies are vendored in `third_party/` -- no network access is required:

- **Catch2** -- C++ test framework (v3)
- **pugixml** -- XML parser used by bgen
- **nlohmann/json** -- Header-only JSON library used by generated C++ code
