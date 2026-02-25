# Cross-Language Integration Strategies

[Back to index](index.md)

Conduit is a C++ library, but **users should never be forced to write C++ to integrate with it.** Real-world deployments span many languages, frameworks, and infrastructure stacks. The strategies in this document exist so that teams can connect to Conduit using the tools they already know — Python, Java, Go, JavaScript, Kafka, RabbitMQ, WebSockets — and stay focused on their own applications.

The guiding principle: **bring Conduit to the user's stack, not the user to C++.**

The strategies below are ordered by impact and build on each other. The recommended approach is to combine Strategies 1-4 for maximum coverage across language ecosystems and infrastructure patterns.

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

## Recommended Combination

The strategies serve different integration profiles. Choose based on how your users need to connect:

| Priority | Strategy | What it unlocks | C++ required by user? |
|----------|----------|-----------------|-----------------------|
| 1 | Multi-language bgen backends | Native typed messages in every language | No |
| 2 | Messaging middleware (Kafka, RabbitMQ, WebSocket) | Zero-dependency integration via existing infrastructure | No |
| 3 | C ABI wrapper | Universal access to the C++ transport stack | No (but language bindings needed) |
| 4 | Python bindings (nanobind) | First-class Python UX for the most common integration | No |
| 5 | gRPC gateway | Typed streaming RPC for polyglot services | No |

Strategy 1 (bgen backends) is the keystone — it makes all other strategies better because every integration point gets typed, generated code. Strategy 4 (messaging middleware) is the accessibility layer — it makes Conduit reachable by any team regardless of their language or tooling choices.

Together, these two strategies cover the full spectrum: teams that want tight, low-latency integration get native generated code, and teams that want loose, infrastructure-level integration get messages flowing through the middleware they already operate.

---

## Key Design Principle

**Meet users where they are.** The goal is not to make every team learn Conduit — it's to make Conduit's protocol data available through whatever tools and languages a team already uses.

This means two complementary approaches:

1. **Generate, don't wrap.** For teams that want direct integration, generate native code from BMDL rather than wrapping C++ in FFI layers. Wrapping always leaks abstraction (memory management, exception handling, callback lifecycles). Generating native code gives each language an implementation that feels like it was written for that language, while the shared schema guarantees wire compatibility.

2. **Bridge, don't require.** For teams that don't want any Conduit-specific code, bridge to standard middleware (Kafka, RabbitMQ, WebSocket). The bridge is a deployment concern, not an application concern — users write normal Kafka consumers or WebSocket handlers and never interact with Conduit APIs at all.

The C ABI layer (Strategy 2) is the pragmatic fallback for the complex, stateful parts of the system (transport management, connection lifecycle, stream framing) where reimplementation would be error-prone. The generated types (Strategy 1) handle the high-surface-area part (message encode/decode) natively. And the messaging middleware bridges (Strategy 4) provide the zero-friction path for teams that just want the data in their existing pipeline.
