# NIIS Transceiver Documentation

NIIS Transceiver is a three-part system for working with binary protocols. **BMDL** is an XML language for defining binary message formats. **bgen** is a code generator that reads BMDL definitions and produces type-safe code in **C++**, **Java**, or **Python**. **conduit** is the C++20 runtime library that provides error handling, bit-level I/O, network transports, and a transceiver orchestrator -- connecting generated protocol code to real-world communication. Java and Python users can access the full transceiver through native bindings (JNI/Panama for Java, ctypes for Python) or use the generated codec code standalone.

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
                my_protocol::create_frame_session,
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

- **C++20** compiler (GCC 12+, Clang 15+, MSVC 19.30+) -- required for the conduit runtime and bgen itself
- CMake 3.20+ (build system)
- **Java** (JDK 8+ for JNI, JDK 19+ for Panama FFI) -- optional, for Java backend
- **Python** 3.7+ -- optional, for Python backend
- No external runtime dependencies (header-only generated code, conduit is a static library)

## Documentation

| Section | Description |
|---------|-------------|
| [BMDL Language Reference](bmdl/index.md) | XML language for defining binary message formats |
| [bgen Code Generator](bgen/index.md) | Reads BMDL, validates, generates C++, Java, or Python code |
| [conduit Runtime Library](conduit/index.md) | Error handling, bit I/O, transports, transceiver |
| [Benchmarks & Performance](conduit/performance.md) | Codec latency, throughput, memory footprint, and scaling characteristics |
| [Limitations & Known Issues](conduit/limitations.md) | Feature parity across backends, known bugs, and general constraints |
