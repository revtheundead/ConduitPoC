# Cross-Language Integration Strategies

[Back to index](index.md)

Conduit is a C++ library, but **users should never be forced to write C++ to integrate with it.** Real-world deployments span many languages, frameworks, and infrastructure stacks. The strategies in this document exist so that teams can connect to Conduit using the tools they already know — Python, Java, Go, JavaScript, Kafka, RabbitMQ, WebSockets — and stay focused on their own applications.

The guiding principle: **bring Conduit to the user's stack, not the user to C++.**

> **Current implementation status:** Several strategies described below are already implemented or in progress:
>
> - **Strategy 1 (bgen backends):** Java and Python code generation backends are implemented and shipping. bgen supports `--language java` and `--language python` in addition to the default C++ backend. All three produce wire-compatible output from the same BMDL schema. See the [bgen documentation](../bgen/index.md) for usage.
> - **Strategy 2 (C ABI / FFI bindings):** JNI bindings (Java 8--17) and Panama FFI bindings (Java 21+) exist at `conduit/bindings/java/`. Python ctypes bindings exist at `conduit/bindings/python/`.
> - **Strategies 3--8:** Not yet implemented. These remain as architectural recommendations for future development.
>
> For known feature gaps between backends, see [Limitations & Known Issues](limitations.md).

The strategies below are ordered by impact and build on each other. The recommended approach is to combine Strategies 1-4 for maximum coverage across language ecosystems and infrastructure patterns.

---

## Strategy 1: Multi-Language bgen Code Generation Backends

**Impact: Highest.** This is the single most leveraged investment.

bgen already has a clean 6-stage pipeline. Stages 1-5 (parse, resolve, validate, wire-size computation, session analysis) are language-agnostic — only stage 6 differs per backend. **Java and Python backends are already implemented** (see [bgen docs](../bgen/index.md)). Adding additional backends for Rust, Go, C#, TypeScript, etc. would follow the same pattern, giving each target language **native, idiomatic message types** generated from the same BMDL source.

### What each backend would produce

- **Message structs** with typed accessors that feel native to the target language
- **encode/decode functions** that operate on the language's native byte types (`bytes`, `[]byte`, `ByteBuffer`, etc.)
- **Wire-compatible serialization** — a Python sender and C++ receiver interoperate because both are generated from the same BMDL schema

### Architecture

bgen's `--language` flag selects the code generation backend (`cpp`, `java`, or `python`). The backend receives the resolved `Protocol` AST + `TypeIndex` + `WireSizeInfo` + `SessionInfo` and produces language-specific files. Adding new language backends follows the same pattern.

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

## Strategy 4: Messaging Middleware Integration (Kafka, RabbitMQ, WebSocket)

**Impact: High. The zero-C++ path for teams using standard infrastructure.**

Strategies 1-3 give other languages access to Conduit's internals. Strategy 4 does something fundamentally different: it lets users integrate with Conduit **without writing any Conduit-specific code at all.** A team running a Java microservice that already consumes from Kafka, or a web dashboard that already speaks WebSocket, can receive Conduit protocol data through the infrastructure they already operate — no new libraries, no FFI, no generated code on their side.

This is the strategy that most directly serves the goal of letting users focus on their own applications.

### How it works

Conduit runs a **bridge process** (sidecar, gateway, or embedded adapter) that sits between the raw protocol transport (TCP, UDP, serial) and a standard messaging system. The bridge handles decode/encode using the generated session and republishes structured messages onto the middleware. User applications subscribe using their existing client libraries.

### 4a. WebSocket Gateway

A Conduit sidecar decodes incoming protocol frames and publishes them as JSON over a WebSocket server. This unlocks:

- **Browser applications** — dashboards, monitoring UIs, debug tools connect directly with the browser's native `WebSocket` API
- **Any language with a WebSocket client** — which is effectively all of them
- **Real-time streaming** — WebSocket's persistent connection maps naturally to Conduit's continuous message flow

The `ISession::format_message()` virtual method already provides string formatting — extend this to structured JSON output, or generate JSON serializers as another bgen backend.

Users write code like this (JavaScript example):

```javascript
const ws = new WebSocket("ws://localhost:9100/messages");
ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.type === "SensorReading") {
        updateDashboard(msg.fields);
    }
};
```

No Conduit library. No generated code. Just a WebSocket and JSON — tools every web developer already has.

### 4b. Kafka Bridge

A Conduit adapter publishes decoded messages to Kafka topics (one topic per message type, or a configurable mapping). Downstream consumers use their standard Kafka client in whatever language they prefer:

- **Java/Kotlin**: Spring Kafka, standard Kafka client
- **Python**: confluent-kafka, aiokafka
- **Go**: sarama, confluent-kafka-go
- **Node.js**: kafkajs
- **.NET**: Confluent.Kafka

Kafka's durability, partitioning, and consumer group semantics add capabilities that Conduit's real-time transport layer doesn't natively provide: replay, fan-out to multiple independent consumers, backpressure via consumer lag, and audit trails.

```python
# Python consumer — no Conduit library involved
from confluent_kafka import Consumer

consumer = Consumer({'bootstrap.servers': 'localhost:9092', 'group.id': 'analytics'})
consumer.subscribe(['conduit.SensorReading'])

for msg in consumer:
    reading = json.loads(msg.value())
    store_in_timeseries_db(reading)
```

The user's application is pure Kafka. Conduit is invisible.

### 4c. RabbitMQ Bridge

Similar to Kafka, but for teams whose infrastructure is built around RabbitMQ / AMQP. Conduit publishes decoded messages to RabbitMQ exchanges; users consume from queues using their existing AMQP client libraries.

RabbitMQ's routing model (exchanges, binding keys, queues) provides flexible message filtering — users can subscribe to specific message types or field-based routing patterns without any Conduit-side changes:

- **Fanout exchanges** for broadcasting all messages to all consumers
- **Topic exchanges** for pattern-based routing (e.g., `sensor.*.temperature`)
- **Direct exchanges** for exact message-type routing

```java
// Java consumer — standard RabbitMQ client, no Conduit dependency
channel.basicConsume("conduit.heartbeats", true, (tag, delivery) -> {
    var heartbeat = objectMapper.readValue(delivery.getBody(), Heartbeat.class);
    monitor.recordHeartbeat(heartbeat);
}, tag -> {});
```

### Why messaging middleware matters

These integrations are not just "another transport." They change the integration model:

| Aspect | Direct Conduit integration (Strategies 1-3) | Middleware integration (Strategy 4) |
|--------|----------------------------------------------|--------------------------------------|
| User's dependency | Conduit library or generated code | Standard middleware client only |
| Languages supported | Languages with bgen backends or FFI | Any language with a Kafka/RabbitMQ/WS client |
| Learning curve | BMDL + Conduit API | Zero — users already know their middleware |
| Deployment coupling | Linked or co-deployed with Conduit | Fully decoupled; communicate over network |
| Additional capabilities | Direct, low-latency | Replay, fan-out, persistence, backpressure |

For many teams, Strategy 4 is the only strategy they need. They don't want to learn Conduit's API, generate code, or manage FFI bindings. They want structured protocol data to show up in their Kafka topic or WebSocket endpoint, and they'll handle the rest with the tools they already use.

### Bridge architecture

The bridge itself is a C++ process that uses Conduit's `Transceiver` internally:

```
┌──────────────┐         ┌──────────────────────┐         ┌──────────────┐
│   Protocol   │  TCP/   │    Conduit Bridge     │  Kafka/ │    User's    │
│   Endpoint   │──UDP/───│  Transceiver + Session│──AMQP/──│  Application │
│  (hardware,  │ Serial  │  decode → JSON/Avro   │  WS     │  (any lang)  │
│   simulator) │         │  encode ← JSON/Avro   │         │              │
└──────────────┘         └──────────────────────┘         └──────────────┘
```

The bridge is the only component that touches C++ and Conduit APIs. Everything to its right is standard middleware that users already know how to operate.

### Bidirectional support

The bridge is not receive-only. Users can also **send** messages back through the middleware:

- Publish a JSON message to a Kafka topic or RabbitMQ queue designated for outbound traffic
- The bridge consumes it, encodes it using the generated session, and transmits it over the protocol transport

This makes the integration fully bidirectional — users can monitor **and** control protocol endpoints entirely through their middleware of choice.

---

## Strategy 5: gRPC Gateway

**Impact: Medium-high. Best for teams that want typed, streaming RPC contracts.**

Generate `.proto` files from BMDL (another bgen backend), then run a Conduit-to-gRPC bridge. This gives every gRPC-supported language typed stubs with streaming support. gRPC's bidirectional streaming maps well to Conduit's send/receive model.

Unlike the middleware approach (Strategy 4), gRPC provides a strongly-typed contract at the integration boundary, which some teams prefer over JSON-over-Kafka/WS.

---

## Strategy 6: WebAssembly (WASM) Compilation

**Impact: Medium. Best for browser tools and sandboxed environments.**

Compile the Conduit codec layer (generated structs + `BitReader`/`BitWriter`) to WASM using Emscripten or `wasi-sdk`. This enables:

- **Browser integration** for protocol monitoring dashboards, debug tools, message inspectors
- **Node.js integration** without native addon compilation
- **Sandboxed execution** via WASI runtimes (Wasmtime, Wasmer)

The bit-level I/O, encoding logic, and decode/encode paths are pure computation with no OS dependencies — they compile cleanly to WASM.

### Practical approach

Compile just the codec layer to WASM, not the full transceiver. The host environment handles transport (JavaScript `WebSocket`, Node.js `net`, etc.) and feeds byte buffers into the WASM module for encode/decode. This plays well with Strategy 1 (WASM becomes another bgen codegen target).

---

## Strategy 7: Shared Memory IPC for Same-Machine High-Performance

**Impact: Niche but critical for latency-sensitive deployments.**

For same-machine cross-language scenarios (e.g., C++ ingestion feeding a Python analytics pipeline):

- Shared memory ring buffers (POSIX `shm_open` / Windows named shared memory)
- Conduit writes decoded message bytes into the ring buffer
- Consumer language reads with near-zero-copy overhead

Conduit's existing `BoundedQueue` with configurable drop policies (`DropOldest`, `DropNewest`, `Block`) provides the design pattern — the same semantics apply to cross-process shared memory queues.

---

## Strategy 8: Native Reimplementation from BMDL

**Impact: Medium. For environments that prohibit native dependencies entirely.**

Instead of wrapping the C++ stack, provide transport-only libraries in each language that reimplement the framing logic:

- Read sync pattern, extract frame length, buffer until complete frame
- The `StreamFramer` algorithm is simple (~100 lines in any language)
- Combined with Strategy 1 (generated native codecs), this yields fully native implementations with zero C++ dependency

### When to use

Restricted deployment environments (embedded Python, locked-down containers), or when the target language's ecosystem prefers owning its full dependency chain (e.g., pure-Rust crates, pure-Go modules).

---

## Making Conduit Natively Accessible Without Networking

The strategies above focus on how users in other languages reach Conduit data. This section addresses a different question: **how to make the Conduit library itself — its codec, message types, framing, and session logic — run directly as native code in other languages, with no networking layer present at all.**

This matters for use cases that have nothing to do with live transport: parsing captured protocol data from files, building test harnesses that construct and validate messages, embedding protocol logic in data pipelines, or integrating Conduit's encode/decode into applications that handle their own I/O.

### Architectural prerequisite: split `conduit-codec` from `conduit-transceiver`

Conduit's internal layering already separates pure computation from platform-bound I/O:

```
conduit-codec  (pure computation, no OS deps)
├── BitReader / BitWriter       — bit-level I/O on byte spans
├── Endian utilities            — byte-order conversion
├── Error / Result<T>           — error propagation
├── Codec traits                — Encodable / Decodable / Message concepts
├── Generated message types     — structs with encode_bytes() / decode_bytes()
├── Generated sessions          — ISession implementations (decode_frame, encode_wrap)
└── StreamFramer                — stateful frame extraction from byte streams

conduit-transceiver  (platform-bound, threading + networking)
├── ITransport + implementations (TCP, UDP, Serial)
├── Transceiver orchestrator
├── BoundedQueue
├── HandlerRegistry
└── Worker thread pool
```

Making this split explicit — two separate build targets, `conduit-codec` and `conduit-transceiver` — is the foundation for native accessibility. Everything below depends on `conduit-codec` being independently compilable and linkable.

### Suggestion 1: Codec-only C ABI (`libconduit_codec`)

Expose the codec layer through a minimal C-linkage shared library that handles encode, decode, and introspection without any transport or threading:

```c
// Session lifecycle
conduit_session_t* conduit_session_create(const char* session_type);
void conduit_session_destroy(conduit_session_t* session);

// Decode: raw bytes → structured messages
int conduit_decode_frame(conduit_session_t* session,
                         const uint8_t* data, size_t len,
                         conduit_decoded_msg_t** out_msgs, size_t* out_count);

// Encode: structured fields → raw bytes
int conduit_encode_message(conduit_session_t* session,
                           uint64_t type_id,
                           const uint8_t* fields, size_t fields_len,
                           uint8_t** out_data, size_t* out_len);

// Introspection
const char* conduit_type_name(conduit_session_t* session, uint64_t type_id);
int conduit_format_message(conduit_session_t* session,
                           const uint8_t* data, size_t len,
                           char* buf, size_t buf_len);

// Stream framing (for users doing their own I/O)
conduit_framer_t* conduit_framer_create(conduit_session_t* session);
int conduit_framer_feed(conduit_framer_t* framer,
                        const uint8_t* data, size_t len,
                        conduit_frame_t** out_frames, size_t* out_count);
```

This is smaller and simpler than the full transceiver C ABI (Strategy 2). It compiles to a shared library with no socket, threading, or OS dependencies beyond the C runtime. Any language that can load a `.so`/`.dll` can use it immediately.

### Suggestion 2: Pre-built language packages

Distribute `libconduit_codec` as native packages in each language's ecosystem, with thin idiomatic wrappers:

| Language | Package | Wrapper technology | What the user sees |
|----------|---------|-------------------|-------------------|
| Python | `pip install conduit-codec` | nanobind or cffi around `libconduit_codec` | Python classes with `encode()` / `decode()` methods |
| Rust | `cargo add conduit-codec` | `-sys` crate linking `libconduit_codec` + safe Rust wrapper | Rust structs implementing `TryFrom<&[u8]>` |
| Node.js | `npm install conduit-codec` | N-API native addon | JS objects with `encode()` / `decode()` |
| Java | Maven / Gradle artifact | JNI or Panama FFI | Java records with builder pattern |
| C# | NuGet `Conduit.Codec` | P/Invoke | C# structs with `Span<byte>` methods |
| Go | Go module | cgo wrapping `libconduit_codec` | Go structs with `Marshal()` / `Unmarshal()` |

Each package ships pre-compiled binaries for common platforms (Linux x86_64/ARM64, macOS x86_64/ARM64, Windows x64) via platform-specific wheels, crate features, or native addon prebuild. Users `pip install` or `cargo add` and start working immediately — no C++ toolchain required on their machine.

**Target UX (Python example):**

```python
from conduit_codec import load_session

session = load_session("asterix_cat062")

# Decode a raw frame from a pcap file
messages = session.decode(raw_bytes)
for msg in messages:
    print(msg.type_name, msg.fields)

# Encode a message for injection testing
frame = session.encode("Heartbeat", {"sequence": 42, "status": 0x01})
```

**Target UX (Rust example):**

```rust
use conduit_codec::Session;

let session = Session::new("asterix_cat062");

// Decode
let messages = session.decode_frame(&raw_bytes)?;
for msg in &messages {
    println!("{}: {:?}", msg.type_name(), msg.fields());
}

// Encode
let frame = session.encode("Heartbeat", &[("sequence", 42.into())])?;
```

No sockets. No threads. No Transceiver. Just codec operations on byte buffers.

### Suggestion 3: Embeddable scripting runtime

Instead of only letting other languages call into Conduit, let Conduit call out to them. Embed a lightweight scripting runtime (Lua, Python, or JavaScript) inside the Transceiver so that users can write message handlers in a scripting language without building a separate process or managing FFI:

```lua
-- handlers.lua — loaded by a Conduit process at startup
function on_heartbeat(msg)
    log.info("seq=%d status=%d", msg.sequence, msg.status)
    if msg.status == 0 then
        send("tx", "Alert", { reason = "heartbeat_lost" })
    end
end
```

The Conduit process loads the script, registers the functions as handlers, and dispatches decoded messages into the scripting runtime. This inverts the integration direction: the user's code runs *inside* Conduit rather than Conduit running inside the user's process.

**Lua** is the natural first choice — it's designed for embedding (tiny runtime, C API, no GIL), widely used in gamedev and networking for exactly this pattern, and adds minimal binary size. Python embedding (via `pybind11` embed mode) is an alternative for teams that need the Python ecosystem.

### Suggestion 4: Pure-native codec libraries via bgen (extends Strategy 1)

Strategy 1 describes generating message types in other languages. Taken further, bgen can generate **complete, self-contained codec libraries** that include not just message structs but also the session logic, stream framing, and bit-level I/O — all in the target language:

- **Python**: `BitReader`/`BitWriter` reimplemented in Python (or Cython for performance), plus generated message classes and a `Session` class with `decode_frame()`/`encode_wrap()`
- **Rust**: `BitReader`/`BitWriter` as a Rust crate, generated message structs with `#[derive(Encode, Decode)]`, `Session` trait implementations
- **Go**: `BitReader`/`BitWriter` as a Go package, generated structs with `encoding.BinaryMarshaler`/`BinaryUnmarshaler`
- **TypeScript**: `BitReader`/`BitWriter` for `ArrayBuffer`/`DataView`, generated interfaces with encode/decode functions

This produces zero-dependency, native-language libraries that users can `import` and use like any other package in their ecosystem. The core bit-level I/O algorithms (`BitReader` reads N bits from a byte span, `BitWriter` accumulates bits into a byte vector) are straightforward to port — the complexity is in the protocol-specific encode/decode logic, which is exactly what bgen generates.

The `StreamFramer` algorithm (scan for sync pattern, extract header length, buffer until frame complete) is ~100 lines in any language. Combined with the generated session, this yields a fully native implementation that can parse protocol streams without any C++ dependency.

### Suggestion 5: WASM as a universal codec runtime

Compile `conduit-codec` to WebAssembly (extends Strategy 6) and distribute it as a universal binary that runs in any WASM runtime:

- **Browser**: protocol decode/encode in the browser via `WebAssembly.instantiate()`
- **Node.js / Deno / Bun**: WASM module imported as a package
- **Python**: via `wasmtime-py` or `wasmer-python`
- **Rust**: via `wasmtime` or native WASM support
- **Any language with a WASM runtime**: JVM (GraalWasm), .NET (wasmtime-dotnet), Go (wazero)

WASM provides a single compiled artifact that runs everywhere, without per-platform native builds. The codec layer's pure-computation nature (no OS calls, no threads, no sockets) means it compiles cleanly to WASM with no emulation overhead.

### Choosing between native approaches

| Approach | Effort | Performance | Dependency on C++ | Best for |
|----------|--------|-------------|-------------------|----------|
| Codec C ABI + language wrappers (Suggestions 1-2) | Medium | Highest (native C++) | Runtime: links `libconduit_codec` | Performance-critical decode/encode |
| Embedded scripting (Suggestion 3) | Low | Good (C++ codec, scripted handlers) | Embedded in C++ process | Rapid prototyping, ops scripting |
| Pure-native bgen output (Suggestion 4) | High (per language) | Good (native but not C++) | None | Ecosystems that reject native deps |
| WASM codec (Suggestion 5) | Medium | Good (near-native) | None (WASM binary) | Maximum portability, browser use |

For most teams, **Suggestions 1-2** (codec C ABI with pre-built packages) deliver the best balance: users get native performance, no C++ toolchain requirement, and an idiomatic API in their language. **Suggestion 4** (pure-native bgen) is the long-term investment for language ecosystems that strongly prefer zero native dependencies (Rust, Go).

---

## Direct Transceiver Access from Other Languages

The native accessibility section above covers the **codec-only** path — encode/decode without networking. But many users need the **full Transceiver**: transport management, peer lifecycle, typed handler dispatch, connection state awareness, and bidirectional send/receive. This section describes how to make the complete Transceiver directly usable from other languages, so that users get all of Conduit's capabilities — not just the codec — without writing C++.

### Why direct Transceiver access matters

The Transceiver is the orchestrator that ties together transport I/O, session decode/encode, stream framing, handler dispatch, and worker threading into a single coherent API. When users access only the codec, they must reimplement or forgo all of that:

| Capability | Codec-only | Full Transceiver |
|------------|-----------|-----------------|
| Encode/decode messages | Yes | Yes |
| Manage TCP/UDP/Serial connections | No — user handles own I/O | Yes — built-in transports |
| Automatic stream framing | Manual — user feeds `StreamFramer` | Automatic per-peer |
| Typed handler dispatch | No — user routes messages manually | Yes — `on<T>(callback)` |
| Connection state tracking | No | Yes — `on_state_change`, `peer_state()` |
| Multi-peer management | No | Yes — per-peer handlers, named lookup |
| Worker thread pool | No | Yes — configurable concurrency |
| Back-pressure and queue management | No | Yes — `BoundedQueue` with drop policies |
| Statistics and observability | No | Yes — `stats()`, error callbacks |

For teams building long-running protocol applications — not just parsing files or running one-off tests — the Transceiver is the right integration point.

### The API surface

The Transceiver's public API is compact. Stripping away C++ template machinery, there are ~12 operations:

| Category | Operations |
|----------|-----------|
| Setup | `create`, `add_peer` |
| Lifecycle | `start`, `stop`, `is_running` |
| Messaging | `send`, `send_batch`, `on` (register handler), `remove_handler` |
| Observability | `on_state_change`, `on_error`, `peer_state`, `stats` |

At the FFI boundary, message identity shifts from compile-time types to runtime type IDs + byte spans. The C++ side already has `TYPE_ID` on every generated message and `ISession` works in terms of `std::span<const uint8_t>`, so this flattening is natural:

```
C++:   xcvr.send<Heartbeat>(peer, heartbeat_obj)
C ABI: conduit_send(xcvr, peer_id, HEARTBEAT_TYPE_ID, bytes, len)

C++:   xcvr.on<Heartbeat>([](const Heartbeat& h) { ... })
C ABI: conduit_on_message(xcvr, HEARTBEAT_TYPE_ID, callback_fn, user_data)
```

Sessions become string-named lookups instead of C++ constructors. bgen registers each generated session type under a name at library load time, and the C ABI resolves it:

```c
// User never constructs C++ objects — just describes what they want
conduit_add_peer(xcvr, "radar",
    "asterix_cat062",                         // session type (string lookup)
    CONDUIT_TRANSPORT_UDP,                    // transport kind
    &(conduit_udp_config_t){                  // transport config
        .bind_address = "0.0.0.0",
        .bind_port = 5000
    });
```

### Language-specific Transceiver wrappers

Each language gets an idiomatic wrapper that hides the C ABI. The same application — listen for Heartbeat and SensorReading on UDP port 5000 — in every target language:

#### Python

```python
from conduit import Transceiver, UdpConfig
from my_protocol import Heartbeat, SensorReading, create_session

with Transceiver() as t:
    t.add_peer("radar", create_session, UdpConfig(bind="0.0.0.0:5000"))

    @t.on(Heartbeat)
    def handle_hb(msg):
        print(f"seq={msg.sequence}, status={msg.status}")

    @t.on(SensorReading)
    def handle_sr(msg):
        db.insert(msg.sensor_id, msg.value)

    t.start()
    t.wait()
```

Context manager maps to `conduit_create` / `conduit_destroy`. The `@t.on(Heartbeat)` decorator calls `conduit_on_message` under the hood. When the C++ worker thread fires the callback, the wrapper acquires the GIL, deserializes bytes into a Python `Heartbeat` object (via bgen-generated decode), and calls the user's function.

#### Rust

```rust
use conduit::{Transceiver, UdpConfig};
use my_protocol::{Heartbeat, SensorReading, create_session};

fn main() -> Result<(), conduit::Error> {
    let mut t = Transceiver::new();
    t.add_peer("radar", create_session, UdpConfig::bind("0.0.0.0:5000"))?;

    t.on::<Heartbeat>(|msg| {
        println!("seq={}, status={}", msg.sequence, msg.status);
    });

    t.on::<SensorReading>(|msg| {
        db.insert(msg.sensor_id, msg.value);
    });

    t.start()?;
    t.wait();
    Ok(())
}
```

A `-sys` crate provides raw FFI, a safe wrapper enforces lifetime correctness (`Transceiver` owns the handle, `Drop` calls `conduit_destroy`), and closures must be `Send + 'static`. Generated `Heartbeat` and `SensorReading` are native Rust structs with `Decode`/`Encode` from bgen's Rust backend.

#### Go

```go
package main

import (
    "fmt"
    "conduit"
    "my_protocol"
)

func main() {
    t := conduit.NewTransceiver()
    defer t.Stop()

    t.AddPeer("radar", my_protocol.CreateSession, conduit.UdpConfig{
        Bind: "0.0.0.0:5000",
    })

    conduit.On[my_protocol.Heartbeat](t, func(msg my_protocol.Heartbeat) {
        fmt.Printf("seq=%d, status=%d\n", msg.Sequence, msg.Status)
    })

    conduit.On[my_protocol.SensorReading](t, func(msg my_protocol.SensorReading) {
        db.Insert(msg.SensorId, msg.Value)
    })

    t.Start()
    t.Wait()
}
```

cgo handles the FFI. C → Go callbacks require `//export` trampoline functions; the wrapper routes all callbacks through a single registered dispatcher that looks up Go closures by ID.

#### Java

```java
import com.conduit.Transceiver;
import com.conduit.UdpConfig;
import com.myprotocol.Heartbeat;
import com.myprotocol.SensorReading;
import static com.myprotocol.Sessions.createSession;

public class Main {
    public static void main(String[] args) {
        try (var t = new Transceiver()) {
            t.addPeer("radar", createSession(),
                      new UdpConfig("0.0.0.0", 5000));

            t.on(Heartbeat.class, msg ->
                System.out.printf("seq=%d, status=%d%n",
                    msg.sequence(), msg.status()));

            t.on(SensorReading.class, msg ->
                db.insert(msg.sensorId(), msg.value()));

            t.start();
            t.await();
        }
    }
}
```

Project Panama (Foreign Function & Memory API) or JNI for the FFI. `Transceiver` implements `AutoCloseable`, callbacks dispatch through a `MethodHandle` lookup table, and generated message types are Java records with `ByteBuffer`-based decode/encode.

#### C\#

```csharp
using Conduit;
using MyProtocol;

using var t = new Transceiver();
t.AddPeer("radar", Sessions.CreateSession, new UdpConfig("0.0.0.0", 5000));

t.On<Heartbeat>(msg =>
    Console.WriteLine($"seq={msg.Sequence}, status={msg.Status}"));

t.On<SensorReading>(msg =>
    db.Insert(msg.SensorId, msg.Value));

t.Start();
t.Wait();
```

P/Invoke for the FFI, `IDisposable` for lifecycle, delegates for callbacks, generated types as `readonly record struct` with `Span<byte>` encode/decode.

#### JavaScript / TypeScript (Node.js)

```typescript
import { Transceiver, UdpConfig } from "conduit";
import { Heartbeat, SensorReading, createSession } from "./my_protocol";

const t = new Transceiver();
t.addPeer("radar", createSession, new UdpConfig({ bind: "0.0.0.0:5000" }));

t.on(Heartbeat, (msg) => {
    console.log(`seq=${msg.sequence}, status=${msg.status}`);
});

t.on(SensorReading, (msg) => {
    db.insert(msg.sensorId, msg.value);
});

await t.start();
// t.stop() on SIGINT
```

N-API native addon for the FFI. Callbacks are posted to the Node.js event loop via `napi_threadsafe_function` so they execute on the main thread, preserving Node's single-threaded model. Generated types are plain JS objects with `encode()`/`decode()` methods.

### Implementation challenges and solutions

The API shape is straightforward. Three aspects require careful per-language work:

#### 1. Callback threading

The C++ Transceiver fires handler callbacks on its worker thread pool. Every language wrapper must safely cross the thread boundary:

| Language | Mechanism |
|----------|-----------|
| Python | Acquire the GIL before invoking the user's callback |
| Java | `AttachCurrentThread` to attach the native thread to the JVM |
| Go | Route through a cgo `//export` trampoline into a goroutine |
| Rust | Require callback closures to be `Send + 'static` |
| C# | Marshal to the thread pool or a `SynchronizationContext` |
| Node.js | Post via `napi_threadsafe_function` to the event loop |

The C ABI should expose a callback mode option: fire directly on the worker thread (lowest latency) or post to a language-provided event loop (safest for single-threaded runtimes like Node.js and Python asyncio).

#### 2. Session creation without C++ constructors

In C++, users pass a `SessionFactory` (a lambda returning `std::unique_ptr<ISession>`) to `add_peer`. Foreign languages can't construct C++ objects directly. The solution is a **session registry**:

- bgen registers each generated session under a string name at library load time
- The C ABI resolves the name to an internal factory: `conduit_add_peer(xcvr, "radar", "asterix_cat062", ...)`
- Protocol schemas can be loaded as plugins — compile a `.so` per protocol, `dlopen` it, and the registry picks it up

This eliminates the need for foreign languages to manage C++ object construction or lifetime for sessions.

#### 3. Transport extensibility

The built-in transports (TCP, UDP, Serial) cover most use cases and are configured via simple structs through the C ABI. For advanced users who need a custom transport implemented in their language, the C ABI provides a reverse callback interface:

```c
conduit_custom_transport_t my_transport = {
    .start_fn    = my_start,
    .stop_fn     = my_stop,
    .send_fn     = my_send,
    .is_stream   = false,
    .is_multi_peer = false,
    .user_data   = my_context,
};
conduit_add_peer_custom(xcvr, "custom", "my_session", &my_transport);
```

This is the most complex extension point and should be deferred to a later release. Most users will use built-in transports exclusively.

### When to use direct Transceiver access vs. other approaches

| Scenario | Best approach |
|----------|--------------|
| Long-running protocol application (monitor, controller, gateway) | Direct Transceiver access |
| Parsing captured data from files or buffers | Codec-only library |
| Web dashboard or microservice consuming data | Middleware bridge (Kafka/RabbitMQ/WebSocket) |
| Unit testing message encode/decode | Codec-only library |
| Protocol simulator that sends and receives | Direct Transceiver access |
| Feeding protocol data into an existing Kafka pipeline | Middleware bridge |
| Quick scripting against live protocol traffic | Direct Transceiver access (Python or Lua embedding) |
| Browser-based protocol inspector | WASM codec + WebSocket bridge |

The direct Transceiver path and the middleware bridge path are not competing approaches — they serve different integration profiles. The Transceiver gives users full control and awareness of the protocol transport. The bridge gives users zero-dependency access through infrastructure they already run. Both should be available; teams will gravitate toward whichever fits their deployment model.

---

## Recommended Combination

The strategies serve different integration profiles. Choose based on how your users need to connect:

| Priority | Strategy | What it unlocks | C++ required by user? | Networking required? |
|----------|----------|-----------------|-----------------------|---------------------|
| 1 | Multi-language bgen backends | Native typed messages in every language | No | No |
| 2 | Direct Transceiver wrappers (Python, Rust, Go, Java, C#, JS) | Full Conduit capabilities from any language | No | Uses built-in transports |
| 3 | Codec-only library packages (`conduit-codec`) | In-process encode/decode without transport | No | No |
| 4 | Messaging middleware (Kafka, RabbitMQ, WebSocket) | Zero-dependency integration via existing infrastructure | No | Yes (middleware) |
| 5 | gRPC gateway | Typed streaming RPC for polyglot services | No | Yes (gRPC) |

The strategies form three tiers of integration depth:

- **Full Transceiver access** (Priorities 1-2) — for teams building protocol applications that need transport management, handler dispatch, connection awareness, and bidirectional messaging. Users import a language-native Conduit package (`pip install conduit`, `cargo add conduit`) and get the same capabilities as C++ users: add peers, register typed handlers, send messages, monitor connection state. The C++ is an invisible runtime underneath idiomatic language wrappers.

- **Codec-only access** (Priority 3, plus pure-native bgen / WASM) — for teams that only need encode/decode without live transport. Parsing files, building test harnesses, embedding protocol logic in data pipelines. Lighter weight than the full Transceiver, no threading or networking pulled in.

- **Infrastructure-level access** (Priorities 4-5) — for teams that want structured protocol data delivered through middleware they already operate. Zero Conduit-specific code on the user's side. Covers dashboards, microservice architectures, and teams that prefer full deployment decoupling.

Strategy 1 (bgen backends) is the keystone — it makes all three tiers better because every integration point gets typed, generated code. The direct Transceiver wrappers (Priority 2) make the full library accessible without C++. The codec-only split (Priority 3) serves the lighter use cases. And the middleware bridges (Priority 4) provide the zero-friction path for teams that just want data in their existing pipeline.

---

## Key Design Principle

**Meet users where they are.** The goal is not to make every team learn Conduit — it's to make Conduit's protocol capabilities available through whatever tools and languages a team already uses.

This means four complementary approaches:

1. **Generate, don't wrap.** For teams that want direct integration, generate native code from BMDL rather than wrapping C++ in FFI layers. Wrapping always leaks abstraction (memory management, exception handling, callback lifecycles). Generating native code gives each language an implementation that feels like it was written for that language, while the shared schema guarantees wire compatibility.

2. **Expose the full library, not just the codec.** For teams building protocol applications in Python, Rust, Go, Java, C#, or JavaScript, provide Transceiver wrappers that deliver the complete Conduit experience — peer management, typed handlers, transport I/O, connection awareness, bidirectional send/receive — through idiomatic language APIs. The C++ is an invisible runtime. Users write `@t.on(Heartbeat)` in Python or `t.on::<Heartbeat>(|msg| ...)` in Rust and never touch a C header.

3. **Ship the library, not the toolchain.** Package both the codec and the full Transceiver as native libraries installable through each language's package manager (`pip install`, `cargo add`, `npm install`). Pre-built binaries for common platforms, thin idiomatic wrappers, zero build-time C++ dependency. Users `import` and go.

4. **Bridge, don't require.** For teams that don't want any Conduit-specific code at all, bridge to standard middleware (Kafka, RabbitMQ, WebSocket). The bridge is a deployment concern, not an application concern — users write normal Kafka consumers or WebSocket handlers and never interact with Conduit APIs.

The `conduit-codec` / `conduit-transceiver` split is the architectural foundation. The codec can be compiled, packaged, and distributed independently for lightweight use cases (file parsing, test harnesses). The full Transceiver wraps both the codec and the transport stack for teams that need live protocol communication. And the middleware bridges sit on top, providing zero-dependency access for teams that just want data in their existing pipeline. Each tier builds on the one below it, and bgen's multi-language code generation makes all three tiers better by providing typed, native message handling everywhere.
