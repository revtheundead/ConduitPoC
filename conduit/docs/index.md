# NIIS Transceiver Documentation

NIIS Transceiver is a three-part system for working with binary protocols in C++20. **BMDL** is an XML language for defining binary message formats. **bgen** is a code generator that reads BMDL definitions and produces type-safe C++ headers. **conduit** is the runtime library that provides error handling, bit-level I/O, network transports, and a transceiver orchestrator -- connecting generated protocol code to real-world communication.

## How They Fit Together

```
                    ┌─────────────────┐
  Protocol spec ──► │  BMDL XML file  │
                    └────────┬────────┘
                             │ bgen
                    ┌────────▼────────┐
                    │ Generated C++   │  types.hpp, structs.hpp,
                    │ header files    │  messages.hpp, sessions.hpp, ...
                    └────────┬────────┘
                             │ #include
                    ┌────────▼────────┐
                    │  Your C++ App   │  link against conduit
                    │  + conduit lib  │
                    └─────────────────┘
```

1. **Define** your protocol in a `.bmdl.xml` file
2. **Generate** C++ code: `bgen --input protocol.bmdl.xml --output generated/`
3. **Use** the generated code with conduit to send and receive typed messages

## Quick Example

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

## Requirements

- **C++20** compiler (GCC 12+, Clang 15+, MSVC 19.30+)
- CMake 3.20+ (build system)
- No external runtime dependencies (header-only generated code, conduit is a static library)

## Documentation

| Section | Description |
|---------|-------------|
| [BMDL Language Reference](bmdl/index.md) | XML language for defining binary message formats |
| [bgen Code Generator](bgen/index.md) | Reads BMDL, validates, generates C++ header files |
| [conduit Runtime Library](conduit/index.md) | Error handling, bit I/O, transports, transceiver |
