# Conduit

Conduit is a three-part system for working with binary protocols:

- **BMDL** -- An XML language for defining binary message formats (bit-packed fields, FSPEC bitmaps, FX extension chains, discriminated unions, and more)
- **bgen** -- A code generator that reads BMDL definitions and produces wire-compatible encode/decode code in C++, Java, or Python
- **conduit** -- A C++20 runtime library providing network transports (TCP, UDP, Serial), stream framing, and a transceiver orchestrator that connects generated protocol code to real-world I/O

All three language backends produce wire-compatible output from the same BMDL schema -- a Java sender and a C++ receiver interoperate seamlessly.

## Quick Start

```bash
# 1. Build
cmake -B build -S conduit -DCONDUIT_BUILD_EXAMPLES=ON -DCONDUIT_BUILD_BGEN=ON
cmake --build build

# 2. Define a protocol (see conduit/docs/bmdl/ for the full language reference)
#    Example protocols are in conduit/examples/

# 3. Generate code
./build/bin/bgen --input my-protocol.bmdl.xml --output generated/ --language cpp

# 4. Run the ASTERIX example
./conduit/examples/xcvr-cpp/dummy_peer server --port 5000 --interval-ms 500
# In another terminal:
./conduit/examples/xcvr-cpp/poc_app 127.0.0.1 5000 --interval-ms 500
```

## Requirements

- **C++20** compiler (GCC 12+, Clang 15+, MSVC 19.30+)
- CMake 3.20+
- **Java** (JDK 8+ for JNI, JDK 19+ for Panama FFI) -- optional
- **Python** 3.7+ -- optional
- No external runtime dependencies

## Documentation

Full documentation is at [`conduit/docs/index.md`](conduit/docs/index.md), covering:

- [BMDL Language Reference](conduit/docs/bmdl/index.md) -- Protocol definition language
- [bgen Code Generator](conduit/docs/bgen/index.md) -- Code generation for C++, Java, Python
- [conduit Runtime Library](conduit/docs/conduit/index.md) -- Transports, transceiver, error handling, bit I/O
- [Examples](conduit/docs/examples.md) -- Complete ASTERIX transceiver apps in 4 languages
- [Benchmarks](conduit/docs/conduit/performance.md) -- Sub-microsecond codec latency, 700K+ msgs/s end-to-end

## Project Structure

```
conduit/
├── docs/              Documentation (BMDL, bgen, conduit runtime)
├── include/           C++ headers for the conduit runtime library
├── src/               C++ source for conduit runtime + bgen
├── bindings/
│   ├── java/          JNI and Panama FFI bindings
│   └── python/        ctypes bindings
├── examples/
│   ├── xcvr-cpp/      C++ ASTERIX transceiver example
│   ├── xcvr-java11/   Java 11 (JNI) example
│   ├── xcvr-java21/   Java 21 (Panama FFI) example
│   └── xcvr-python/   Python (ctypes) example
├── tests/             C++ test suite (Catch2)
└── third_party/       Vendored test dependencies (JUnit, pytest)
```

## License

See the LICENSE file for details.
