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

// start() returns VoidResult — check for error before relying on the
// transceiver running.  See conduit/error-handling.md for the full pattern.
auto result = xcvr.start();
if (!result) {
    std::cerr << "start failed: " << result.error().format_short() << "\n";
}
```

For Java and Python quick-start examples, see the [Quick Start](conduit/quick-start.md) tutorial.

## Requirements

- **C++23** compiler: GCC 12+ (Linux), Clang 19+ (Linux/macOS/Windows), MSVC 19.30+ (Windows)
- CMake 3.20+, Ninja (required for Clang on Windows; recommended elsewhere)
- **Java** (JDK 8+ for JNI, JDK 21+ for Panama FFI) -- optional
- **Python** 3.7+ -- optional
- No external runtime dependencies. Vendored build-time dependencies (Catch2, pugixml, nlohmann/json) are included in `third_party/`.

## Building from Source

See [Building & Testing](building.md) for complete instructions, including compiler selection (`--clang`, `--gcc`, `--msvc`), CMake options, and test runner usage.

Quick start:

```bash
# Linux / macOS
./conduit/scripts/build.sh                          # Debug build
./conduit/scripts/build.sh --release --test          # Release build + run tests
./conduit/scripts/build.sh --release --clang --test  # Same, using Clang

# Windows
scripts\build.bat --release --test                   # Release build + run tests (MSVC)
scripts\build.bat --release --clang --test           # Same, using Clang + Ninja
```

## Documentation

| Section | Description |
|---------|-------------|
| [Building & Testing](building.md) | Build scripts, compiler selection, CMake options, running tests |
| [Integrating Conduit into Your Project](integration.md) | Library variants, release tarball layout, `find_package(conduit)` + `bgen_generate()`, Java JAR, Python `CONDUIT_CABI_LIB` |
| [BMDL Language Reference](bmdl/index.md) | XML language for defining binary message formats |
| [bgen Code Generator](bgen/index.md) | Reads BMDL, validates, generates C++, Java, or Python code |
| [conduit Runtime Library](conduit/index.md) | Error handling, bit I/O, transports, transceiver |
| [Examples](examples.md) | Complete ASTERIX transceiver apps in C++, Java 11, Java 21, and Python |
| [Benchmarks & Performance](conduit/performance.md) | Codec latency, throughput, memory footprint, and scaling characteristics |
| [Limitations & Design Boundaries](conduit/limitations.md) | Scope boundaries, design decisions, and backend architectural differences |
