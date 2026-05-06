# Building & Testing

This page is about *building* Conduit itself.  If you're writing a
project that wants to *consume* a built Conduit -- including how to
link against the libraries and call the bgen code generator from
your CMake -- see
[Integrating Conduit into Your Project](integration.md).

## Requirements

- **C++23** compiler -- required for the conduit runtime and bgen itself
  - **GCC** 12+ (Linux)
  - **Clang** 19+ (Linux, macOS, Windows)
  - **MSVC** 19.30+ / Visual Studio 2022 (Windows)
- CMake 3.20+ (build system)
- **Ninja** -- required when building with Clang on Windows; recommended on all platforms
- **Java** (JDK 8+ for JNI, JDK 21+ for Panama FFI) -- optional, for Java bindings and examples
  - **Maven** 3.6+ -- required for building the `conduit-java` JAR and the Java 11 example (`mvn`)
  - **Gradle** 7+ -- required for the Java 11 and Java 21 Gradle-based examples (`gradle` or the included wrapper)
  - `javac` and `java` must be on `PATH` (provided by the JDK)
- **Python** 3.7+ -- optional, for Python backend

Vendored build-time dependencies (Catch2, pugixml, nlohmann/json) are included in `third_party/` -- no network access is required to build.

### Runtime dependencies

Conduit's shared libraries (`conduit_cabi`, `conduit_jni`, etc.) are compiled C++ and require the platform's C++ runtime to be present on the target machine:

| Platform | Toolchain | Runtime dependency |
|----------|-----------|-------------------|
| **Windows** | MSVC | [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) (x64). Required on machines without Visual Studio. |
| **Windows** | Clang / LLVM MinGW | None -- the CMake build statically links libc++ and libunwind. Ensure `conduit/lib/` is on `PATH` so DLLs can find each other (see [Native library path](#native-library-path-conduitlib)). |
| **Linux** | GCC | `libstdc++6` -- usually pre-installed. Install via `apt install libstdc++6` or `yum install libstdc++` if missing. |
| **Linux** | Clang | `libc++` -- install via `apt install libc++1` if missing |
| **macOS** | Any | None -- system libc++ is always present |

The Java and Python bindings detect common missing-dependency errors at load time and report actionable messages (e.g. pointing to the MSVC redistributable download URL).

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
| `--release` | Release build; enables examples, benchmarks (C++, Python, Java), CABI, JNI, Java |
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
| `CONDUIT_BUILD_BENCHMARKS` | OFF | Build C++ performance benchmarks |
| `CONDUIT_BUILD_PYTHON_BENCHMARKS` | OFF | Build Python codec benchmarks (requires CABI + bgen) |
| `CONDUIT_BUILD_JAVA_BENCHMARKS` | OFF | Build Java codec benchmarks (requires CABI + bgen + JAR) |
| `CONDUIT_BUILD_CABI` | OFF | Build full Transceiver C ABI shared library (`libconduit_cabi`) |
| `CONDUIT_BUILD_CODEC_CABI` | OFF | Build codec-only C ABI shared library (`libconduit_codec_cabi`) — exposes only `conduit_session_*` / `conduit_decode_frame` / `conduit_encode_*` / `conduit_framer_*` for protocols that don't need the transceiver. Smaller binary; can be loaded on hosts with no networking. |
| `CONDUIT_BUILD_JNI` | OFF | Build JNI shared libraries (Java 11+) |
| `CONDUIT_BUILD_JAVA_JAR` | OFF | Build conduit-java JAR and compile Java tests |
| `CONDUIT_ENABLE_SANITIZERS` | OFF | Enable AddressSanitizer + UBSan |
| `CONDUIT_ENABLE_COVERAGE` | OFF | Enable code coverage instrumentation (GCC/Clang) |

## Native library path (`conduit/lib/`)

The CMake build places shared libraries (`libconduit_cabi.so`, `libconduit_jni.so`, etc.) into `conduit/lib/`. Java and Python examples must be able to find these at runtime.

### Java

The JVM needs `-Djava.library.path` pointing to `conduit/lib/` so it can locate the JNI or CABI shared libraries. The Maven POM files and Gradle build files set this automatically when you use the provided run targets (e.g. `mvn exec:java` or `gradle runDummyPeer`). If you run the JAR directly, pass it yourself:

```bash
java -Djava.library.path=$(pwd)/conduit/lib -jar myapp.jar
```

Alternatively, add `conduit/lib/` to `LD_LIBRARY_PATH` (Linux), `DYLD_LIBRARY_PATH` (macOS), or `PATH` (Windows):

```bash
# Linux
export LD_LIBRARY_PATH=$(pwd)/conduit/lib:$LD_LIBRARY_PATH

# macOS
export DYLD_LIBRARY_PATH=$(pwd)/conduit/lib:$DYLD_LIBRARY_PATH

# Windows (PowerShell)
$env:PATH = "$(Get-Location)\conduit\lib;$env:PATH"
```

### Python

The Python bindings locate the CABI library via the `CONDUIT_CABI_LIB` environment variable, which must point to the exact `.so` / `.dylib` / `.dll` file:

```bash
# Linux
export CONDUIT_CABI_LIB=$(pwd)/conduit/lib/libconduit_cabi.so

# macOS
export CONDUIT_CABI_LIB=$(pwd)/conduit/lib/libconduit_cabi.dylib

# Windows
set CONDUIT_CABI_LIB=%cd%\conduit\lib\conduit_cabi.dll
```

If the CABI library depends on other shared libraries in the same directory, you may also need `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` / `PATH` as shown above.

## Java build tools

The Java examples use both Maven and Gradle. Below are the essential commands.

### Maven (conduit-java JAR + xcvr-java11 example)

```bash
# Install conduit-java to local Maven repo (required before building Java examples)
mvn install -q -f conduit/bindings/java/pom.xml

# Build the Java 11 example
mvn package -q -f conduit/examples/xcvr-java11/pom.xml -Dskip.bgen=true \
    -Dconduit.build.dir=$(pwd)/build

# Run the Java 11 example (server mode)
mvn exec:java -f conduit/examples/xcvr-java11/pom.xml -PrunDummyPeer \
    -Dconduit.build.dir=$(pwd)/build \
    -Dexec.appArgs="server --port 5000 --interval-ms 500"
```

### Gradle (xcvr-java11 and xcvr-java21 examples)

Both Java examples include Gradle build files as an alternative to Maven.

```bash
# Build the Java 11 example
cd conduit/examples/xcvr-java11
gradle build

# Build the Java 21 example
cd conduit/examples/xcvr-java21
gradle build

# Run code generation (requires bgen to be built first)
gradle generateCode

# Run the dummy peer
gradle runDummyPeer -DappArgs="server --port 5000 --interval-ms 500"
```

### Verifying your Java toolchain

```bash
javac --version   # JDK compiler — should report 11+ (or 21+ for Panama FFI)
java  --version   # JVM runtime
mvn   --version   # Maven (3.6+)
gradle --version  # Gradle (7+)
```

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
