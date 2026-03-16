# Conduit Documentation

Conduit is a three-part system for working with binary protocols. **BMDL** is an XML language for defining binary message formats. **bgen** is a code generator that reads BMDL definitions and produces type-safe code in **C++**, **Java**, or **Python**. **conduit** is the C++23 runtime library that provides error handling, bit-level I/O, network transports, and a transceiver orchestrator -- connecting generated protocol code to real-world communication. Java and Python users can access the full transceiver through native bindings (JNI/Panama for Java, ctypes for Python) or use the generated codec code standalone.

## How They Fit Together

```
                    ┌─────────────────┐
  Protocol spec ──► │  BMDL XML file  │
                    └────────┬────────┘
                             │ bgen --language <lang>
               ┌─────────────┼─────────────┐
               ▼             ▼             ▼
       ┌──────────────┐ ┌──────────┐ ┌──────────┐
       │ C++ headers  │ │ Java     │ │ Python   │
       │ (.hpp)       │ │ classes  │ │ modules  │
       │              │ │ (.java)  │ │ (.py)    │
       └──────┬───────┘ └────┬─────┘ └────┬─────┘
              │              │             │
       ┌──────▼───────┐ ┌────▼─────┐ ┌────▼─────┐
       │ C++ App +    │ │ Java App │ │ Python   │
       │ conduit lib  │ │ + JNI    │ │ App +    │
       │              │ │ bindings │ │ ctypes   │
       └──────────────┘ └──────────┘ └──────────┘
```

1. **Define** your protocol in a `.bmdl.xml` file
2. **Generate** code: `bgen --input protocol.bmdl.xml --output generated/ --language cpp`
3. **Use** the generated code to send and receive typed messages

Replace `--language cpp` with `java` or `python` to generate code for those languages. The default is `cpp`. All three backends produce wire-compatible output from the same BMDL schema -- a Java sender and a C++ receiver interoperate seamlessly.

## Quick Example (C++)

```cpp
#include "generated/my_protocol.hpp"
#include <conduit/transceiver/transceiver.hpp>
#include <conduit/transceiver/transport/udp.hpp>

using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

// Configure and start
TransceiverConfig config;
config.add_peer("feed",
                my_protocol::create_my_frame_session,
                UdpConfig{.bind_port = 5000});
Transceiver xcvr(std::move(config));

// Register a typed handler
xcvr.on<my_protocol::Heartbeat>([](const my_protocol::Heartbeat& msg) {
    std::cout << "seq=" << msg.sequence() << "\n";
});

auto result = xcvr.start();
```

For Java and Python quick-start examples, see the [Quick Start](conduit/quick-start.md) tutorial.

## Requirements

- **C++23** compiler -- required for the conduit runtime and bgen itself
  - **GCC** 12+ (Linux)
  - **Clang** 15+ (Linux, macOS, Windows)
  - **MSVC** 19.30+ / Visual Studio 2022 (Windows)
- CMake 3.20+ (build system)
- **Ninja** -- required when building with Clang on Windows; recommended on all platforms
- **Java** (JDK 8+ for JNI, JDK 21+ for Panama FFI) -- optional, for Java backend
- **Python** 3.7+ -- optional, for Python backend
- No external runtime dependencies (header-only generated code, conduit is a static library). Vendored build-time dependencies (Catch2, pugixml, nlohmann/json) are included in `third_party/`.

## Building from Source

### Using the build script (recommended)

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
scripts\build.bat --clang               # Use Clang + Ninja (requires both on PATH)
scripts\build.bat --msvc                # Use MSVC cl.exe (the default)
```

Flags can be combined freely in any order, e.g.:

```bash
./conduit/scripts/build.sh --release --clang --test
scripts\build.bat --release --clang --test
```

#### Compiler selection

When both MSVC and Clang are installed on Windows (or both GCC and Clang on Linux), the build scripts default to the platform's native compiler unless overridden:

| Platform | Default | Override |
|----------|---------|----------|
| Linux / macOS | System default (usually GCC) | `--clang` or `--gcc` |
| Windows | MSVC (cl.exe / Visual Studio) | `--clang` or `--msvc` |

The `--clang` flag on Windows requires **Ninja** as the build generator (install via `choco install ninja`). On Linux, the script auto-detects Ninja (preferred) or Make regardless of compiler choice.

The scripts validate that the requested compiler is on `PATH` and fail early with a clear error if it is not found.

### Using CMake directly

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
CC=clang CXX=clang++ cmake -B build -S conduit
cmake --build build

# Build with Clang on Windows (requires Ninja)
cmake -B build -S conduit -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
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

### Vendored dependencies

All build-time dependencies are vendored in `third_party/` — no network access is required:

- **Catch2** — C++ test framework (v3)
- **pugixml** — XML parser used by bgen
- **nlohmann/json** — Header-only JSON library used by generated C++ code

For full build and run instructions for each language, see [Examples](examples.md).

## Documentation

| Section | Description |
|---------|-------------|
| [BMDL Language Reference](bmdl/index.md) | XML language for defining binary message formats |
| [bgen Code Generator](bgen/index.md) | Reads BMDL, validates, generates C++, Java, or Python code |
| [conduit Runtime Library](conduit/index.md) | Error handling, bit I/O, transports, transceiver |
| [Examples](examples.md) | Complete ASTERIX transceiver apps in C++, Java 11, Java 21, and Python |
| [Benchmarks & Performance](conduit/performance.md) | Codec latency, throughput, memory footprint, and scaling characteristics |
| [Limitations & Known Issues](conduit/limitations.md) | Feature parity across backends, known bugs, audit history, and general constraints |
