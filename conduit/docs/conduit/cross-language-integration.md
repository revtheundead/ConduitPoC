# Cross-Language Integration Strategies

[Back to index](index.md)

Conduit is a C++ library, but real-world deployments often require integration with other languages, tools, and technologies. This document outlines strategies for cross-language integration without sacrificing UX convenience.

The strategies are ordered by impact and build on each other. The recommended approach is to combine Strategies 1-3 for maximum coverage.

---

## Strategy 1: Multi-Language bgen Code Generation Backends

**Impact: Highest.** This is the single most leveraged investment.

bgen already has a clean 6-stage pipeline. Stages 1-5 (parse, resolve, validate, wire-size computation, session analysis) are language-agnostic — only stage 6 emits C++. Adding parallel code generation backends for Python, Rust, Go, C#, Java, TypeScript, etc. means each target language gets **native, idiomatic message types** generated from the same BMDL source.

### What each backend would produce

- **Message structs** with typed accessors that feel native to the target language
- **encode/decode functions** that operate on the language's native byte types (`bytes`, `[]byte`, `ByteBuffer`, etc.)
- **Wire-compatible serialization** — a Python sender and C++ receiver interoperate because both are generated from the same BMDL schema

### Architecture

Add a `--language` flag to bgen, or a plugin system where `bgen --plugin python` loads a language-specific emitter. The plugin receives the resolved `Protocol` AST + `TypeIndex` + `WireSizeInfo` + `SessionInfo` and produces language-specific files.

This mirrors the approach of Protocol Buffers (`protoc` with plugins) and Cap'n Proto, applied to Conduit's binary protocol domain.

### Why this works

BMDL already captures everything needed for code generation in any language:
- Field types, bit widths, endianness
- Wire encodings (BCD, BNR, sign-magnitude, EBCDIC)
- Framing (sync patterns, length fields, payload dispatch)
- Presence bitmaps, FX extension chains
- Constraints and validation rules

The schema is the single source of truth. Every integration point gets typed, generated code rather than ad-hoc byte manipulation.

---

## Strategy 2: C ABI Wrapper Layer (extern "C" Flat API)

**Impact: High.** The universal FFI escape hatch.

Build a thin C-linkage shared library around the `Transceiver` class that flattens the API to opaque handles and C-compatible callbacks:

```c
// Lifecycle
conduit_transceiver_t* conduit_create(const conduit_config_t* cfg);
int conduit_start(conduit_transceiver_t* xcvr);
void conduit_stop(conduit_transceiver_t* xcvr);
void conduit_destroy(conduit_transceiver_t* xcvr);

// Peers
conduit_peer_id conduit_add_peer(conduit_transceiver_t* xcvr,
                                  const char* name,
                                  const char* session_type,
                                  const conduit_transport_cfg_t* transport);

// Messaging
int conduit_send(conduit_transceiver_t* xcvr,
                 conduit_peer_id peer,
                 uint64_t type_id,
                 const uint8_t* data, size_t len);

void conduit_on_message(conduit_transceiver_t* xcvr,
                        uint64_t type_id,
                        conduit_msg_callback_t callback,
                        void* user_data);
```

This enables bindings from every major language:
- **Python**: ctypes, cffi, or as the native layer under nanobind
- **Rust**: bindgen-generated bindings
- **Go**: cgo
- **Java**: JNI, JNA, or Project Panama
- **C#**: P/Invoke
- **Node.js**: ffi-napi or N-API
- **Ruby**: fiddle

### Combining with Strategy 1

The C ABI provides transport + session management (the complex, stateful part), while language-native generated types (from Strategy 1) handle encode/decode (the type-rich part). The result: high-level typed APIs in every language that use the battle-tested C++ transport stack underneath.

At the FFI boundary, messages are passed as raw byte spans. The C++ side's `ISession::decode_frame` and `ISession::encode_wrap` already work with `std::span<const uint8_t>`, so this maps naturally.

---

## Strategy 3: pybind11/nanobind for First-Class Python Support

**Impact: High for the most common integration target.**

Python is the dominant language for signal processing, protocol testing, data analysis, and rapid prototyping — all core Conduit use cases. Use **nanobind** (lighter, faster than pybind11) to expose:

- `Transceiver` as a Python class with context manager support
- Generated message types as Python dataclasses or attrs classes (via Strategy 1)
- Handler registration with Python callables
- Optional `asyncio` integration for non-blocking receive

### Target UX

```python
from my_protocol import Heartbeat, SensorReading, create_session
from conduit import Transceiver, UdpConfig

with Transceiver() as t:
    t.add_peer("rx", create_session, UdpConfig(bind="0.0.0.0:5000"))

    @t.on(Heartbeat)
    def handle_hb(msg):
        print(f"seq={msg.sequence}, status={msg.status}")

    t.start()
    t.wait()
```

This is nearly identical to the C++ UX, which preserves the convenience goal.

---

## Strategy 4: Network-Native IPC (Sidecar / Gateway)

**Impact: Medium-high. Best for web, microservices, and loosely-coupled systems.**

Conduit already speaks TCP and UDP. Run a **Conduit sidecar process** that bridges between the real protocol transport and a simple local interface.

### 4a. JSON/MessagePack Gateway

The sidecar receives raw protocol messages, decodes them via the generated session, and re-publishes as JSON over a local WebSocket or TCP connection. Foreign-language clients connect, receive JSON, and parse with their native JSON library.

The `ISession::format_message()` virtual method already provides string formatting — extend this to structured JSON output, or generate JSON serializers as another bgen backend.

### 4b. gRPC Gateway

Generate `.proto` files from BMDL (another bgen backend), then run a Conduit-to-gRPC bridge. This gives every gRPC-supported language typed stubs with streaming support. gRPC's bidirectional streaming maps well to Conduit's send/receive model.

### When to use

When integrating with web frontends, microservice architectures, or environments where native C FFI is impractical (browsers, serverless functions, restricted containers).

---

## Strategy 5: WebAssembly (WASM) Compilation

**Impact: Medium. Best for browser tools and sandboxed environments.**

Compile the Conduit codec layer (generated structs + `BitReader`/`BitWriter`) to WASM using Emscripten or `wasi-sdk`. This enables:

- **Browser integration** for protocol monitoring dashboards, debug tools, message inspectors
- **Node.js integration** without native addon compilation
- **Sandboxed execution** via WASI runtimes (Wasmtime, Wasmer)

The bit-level I/O, encoding logic, and decode/encode paths are pure computation with no OS dependencies — they compile cleanly to WASM.

### Practical approach

Compile just the codec layer to WASM, not the full transceiver. The host environment handles transport (JavaScript `WebSocket`, Node.js `net`, etc.) and feeds byte buffers into the WASM module for encode/decode. This plays well with Strategy 1 (WASM becomes another bgen codegen target).

---

## Strategy 6: Shared Memory IPC for Same-Machine High-Performance

**Impact: Niche but critical for latency-sensitive deployments.**

For same-machine cross-language scenarios (e.g., C++ ingestion feeding a Python analytics pipeline):

- Shared memory ring buffers (POSIX `shm_open` / Windows named shared memory)
- Conduit writes decoded message bytes into the ring buffer
- Consumer language reads with near-zero-copy overhead

Conduit's existing `BoundedQueue` with configurable drop policies (`DropOldest`, `DropNewest`, `Block`) provides the design pattern — the same semantics apply to cross-process shared memory queues.

---

## Strategy 7: Native Reimplementation from BMDL

**Impact: Medium. For environments that prohibit native dependencies entirely.**

Instead of wrapping the C++ stack, provide transport-only libraries in each language that reimplement the framing logic:

- Read sync pattern, extract frame length, buffer until complete frame
- The `StreamFramer` algorithm is simple (~100 lines in any language)
- Combined with Strategy 1 (generated native codecs), this yields fully native implementations with zero C++ dependency

### When to use

Restricted deployment environments (embedded Python, locked-down containers), or when the target language's ecosystem prefers owning its full dependency chain (e.g., pure-Rust crates, pure-Go modules).

---

## Recommended Combination

The strategies are complementary, not mutually exclusive:

| Priority | Strategy | What it unlocks |
|----------|----------|-----------------|
| 1 | Multi-language bgen backends | Native typed messages in every language |
| 2 | C ABI wrapper | Universal access to the C++ transport stack |
| 3 | Python bindings (nanobind) | First-class Python UX for the most common integration |
| 4 | JSON/WebSocket gateway | Browser, web, and microservice integration |

Strategy 1 is the keystone — it makes all other strategies better because every integration point gets typed, generated code. The BMDL schema being the single source of truth is the critical architectural advantage that makes all of this feasible.

---

## Key Design Principle

Wherever possible, **generate don't wrap**. Wrapping C++ in FFI layers always leaks abstraction (memory management, exception handling, callback lifecycles). Generating native code from BMDL gives each language an implementation that feels like it was written for that language, while the shared schema guarantees wire compatibility.

The C ABI layer (Strategy 2) is the pragmatic fallback for the complex, stateful parts of the system (transport management, connection lifecycle, stream framing) where reimplementation would be error-prone. The generated types (Strategy 1) handle the high-surface-area part (message encode/decode) natively.
