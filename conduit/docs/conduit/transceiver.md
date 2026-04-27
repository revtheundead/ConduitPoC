# Transceiver

[Back to index](index.md)

The `Transceiver` is the main orchestrator. It wires protocol sessions to transports, manages peers, dispatches decoded messages to typed handlers, and encodes outgoing messages -- all without exposing raw bytes to user code.

The Transceiver API is available in C++, Java (via JNI or Panama FFI), and Python (via ctypes). All three provide the same capabilities: peer management, typed message handlers, state/error callbacks, statistics, and message logging.

```cpp
#include <conduit/transceiver/transceiver.hpp>

namespace conduit::transceiver {
class Transceiver;
}
```

```java
import io.conduit.Transceiver;
import io.conduit.TransportConfig;
import io.conduit.ConduitNative;
```

```python
from conduit import Transceiver, TcpClientConfig, UdpConfig
# or for async usage:
from conduit import AsyncTransceiver
```

`Transceiver` is default-constructible (no config required for imperative peer setup). In Java it implements `AutoCloseable`; in Python it supports the context manager protocol (`with` statement).

## Architecture

```
                        ┌─────────────────────────────┐
  Network / Serial ──►  │        Transport            │
                        │  (TCP, UDP, Serial)         │
                        └────────────┬────────────────┘
                                     │ on_data_received
                        ┌────────────▼────────────────┐
                        │      StreamFramer           │  (stream transports only)
                        └────────────┬────────────────┘
                                     │ complete frames
                        ┌────────────▼────────────────┐
                        │        ISession             │
                        │  decode_frame() → messages  │
                        └────────────┬────────────────┘
                                     │ InboundMessage
                        ┌────────────▼────────────────┐
                        │     BoundedQueue            │  dispatch queue
                        └────────────┬────────────────┘
                                     │ pop()
                        ┌────────────▼────────────────┐
                        │     Worker Thread(s)        │
                        │  HandlerRegistry::dispatch  │
                        └────────────┬────────────────┘
                                     │
                        ┌────────────▼────────────────┐
                        │   Your Handler Callback     │
                        │  on<Heartbeat>([](auto& m)) │
                        └─────────────────────────────┘
```

## Peer Management

Peers represent communication endpoints. Each peer has a transport, a session, and (for stream transports) a framer. Add peers before calling `start()`.

### PeerId

`PeerId` is an opaque identifier for a peer. It is returned by `add_peer()` and used throughout the API to target specific peers.

| Method | Returns | Description |
|--------|---------|-------------|
| `PeerId()` | -- | Default-constructs an invalid peer (id = 0) |
| `value()` | `uint32_t` | Raw numeric ID |
| `valid()` | `bool` | True if the peer ID is valid (non-zero) |
| `operator bool()` | `bool` | Same as `valid()` |
| `to_string()` | `std::string` | Formats as `"Peer(N)"` |
| `operator<<` | `ostream&` | Stream output as `Peer(N)` |

`PeerId` supports `==`, `<=>`, `std::format`, and `std::hash` (usable as a map key).

There are two ways to add peers: through [`TransceiverConfig::add_peer()`](configuration.md) (declarative, config-driven) or through the `Transceiver::add_peer()` methods below (imperative, for custom transports). The config-driven approach is recommended for most use cases.

### add_peer (single-peer transport)

```cpp
[[nodiscard]] Result<PeerId> add_peer(
    std::string name,
    std::unique_ptr<traits::ISession> session,
    std::shared_ptr<transport::ITransport> transport);
```

For TCP client, UDP (single-peer), and serial transports.

### add_peer (multi-peer transport)

```cpp
[[nodiscard]] Result<PeerId> add_peer(
    std::string name,
    SessionFactory session_factory,
    std::shared_ptr<transport::ITransport> transport);
```

For TCP server and UDP (multi-peer). Each connecting client gets a session instance from the factory.

### Java and Python Peer Management

In Java and Python, peer setup follows the same pattern: register a session, add a peer with a name and transport config, then start. The `Transceiver` handles framing and codec dispatch identically to C++.

```java
// Java — register session, add peer, start
try (Transceiver tx = new Transceiver()) {
    tx.registerSession("asterix", new AsterixDataBlockSession());
    int peerId = tx.addPeer("server", "asterix",
            TransportConfig.tcpClient("10.0.0.1:5000"));
    tx.start();
    // ...
}
```

```python
# Python — register session, add peer, start
with Transceiver() as tx:
    tx.register_session("asterix", AsterixDataBlockSession())
    peer_id = tx.add_peer("server", "asterix",
                          TcpClientConfig("10.0.0.1:5000"))
    tx.start()
    # ...
```

### Peer Lookup

```cpp
Result<PeerId> sole_peer() const;            // exactly one peer, else error
Result<PeerId> peer(std::string_view name) const;  // lookup by name
```

```java
int peerId = tx.solePeer();              // exactly one peer, else ConduitError
int peerId = tx.peerByName("server");    // lookup by name
```

```python
peer_id = tx.sole_peer()              # exactly one peer, else ConduitError
peer_id = tx.peer_by_name("server")   # lookup by name
```

> **Pitfall:** `sole_peer()` returns an error if more than one peer exists. Use named lookup instead.

## Handler Registration

See [Message Handlers](handlers.md) for the full handler reference.

### Direct Registration

```cpp
// Per-type handler (applies to all peers)
xcvr.on<Heartbeat>([](const Heartbeat& msg) {
    std::cout << msg.sequence() << "\n";
});

// Per-type handler for a specific peer
xcvr.on<Heartbeat>(peer_id, [](const Heartbeat& msg) { ... });

// Remove handlers (returns true if a handler was found and removed)
bool removed = xcvr.remove_handler<Heartbeat>();        // global
bool removed = xcvr.remove_handler<Heartbeat>(peer_id); // per-peer
```

### Java Handler Registration

```java
// Typed handler — auto-deserializes to the message class
tx.onMessage(Heartbeat.class, (peerId, msg) -> {
    System.out.println("heartbeat seq=" + msg.sequence);
});

// Raw handler — receives byte[] payload
tx.onMessage(Heartbeat.TYPE_ID, (peerId, typeId, typeName, data) -> {
    System.out.println("raw message: " + typeName);
});

// Catch-all handler — fires for any unmatched type
tx.onAnyMessage((peerId, typeId, typeName, data) -> {
    System.out.println("unhandled: " + typeName);
});

// Remove a handler
tx.removeHandler(peerId, Heartbeat.TYPE_ID);
```

### Python Handler Registration

```python
# Typed handler — decorator style, auto-deserializes
@tx.on(Heartbeat)
def on_heartbeat(peer_id, msg):
    print(f"heartbeat seq={msg.sequence}")

# Raw handler — receives bytes payload
@tx.on(type_id=Heartbeat.TYPE_ID)
def on_heartbeat_raw(peer_id, type_id, type_name, data):
    print(f"raw message: {type_name}")

# Catch-all handler
tx.on_any(lambda peer_id, type_id, type_name, data:
    print(f"unhandled: {type_name}"))

# Remove a handler
tx.remove_handler(peer_id, Heartbeat.TYPE_ID)
```

### MessageHandler Builder

```cpp
MessageHandler handler;
handler.on<Heartbeat>([](const auto& m) { ... })
       .on<SensorData>([](const auto& m) { ... })
       .on_any([](uint64_t type_id, const std::any& payload) { ... });

xcvr.set_handler(std::move(handler));          // sole peer
xcvr.set_handler(peer_id, std::move(handler)); // specific peer
```

### Connection State Callbacks

```cpp
CallbackId id = xcvr.on_state_change([](PeerId peer, net::ConnectionState state) {
    std::cout << peer << " → " << net::to_string(state) << "\n";
});

xcvr.remove_state_change(id);
```

### Connection State Callbacks (Java / Python)

```java
// Java
int callbackId = tx.onStateChange((peerId, newState) ->
    System.out.println("peer " + peerId + " -> " + newState.name()));

tx.removeStateChange(callbackId);
```

```python
# Python
tx.on_state_change(lambda peer_id, state:
    print(f"peer {peer_id} -> {state}"))
```

### Error Callbacks

```cpp
#include <conduit/transceiver/error_event.hpp>

CallbackId id = xcvr.on_error([](const ErrorEvent& event) {
    std::cerr << event.peer_name << ": " << event.error.format() << "\n";
});

xcvr.remove_error_callback(id);
```

```java
// Java
int callbackId = tx.onError((peerId, peerName, errorCode, errorMsg) ->
    System.err.println("[ERROR] " + peerName + ": " + errorMsg));

tx.removeErrorCallback(callbackId);
```

```python
# Python
tx.on_error(lambda peer_id, peer_name, code, msg:
    print(f"[ERROR] {peer_name}: {msg}", file=sys.stderr))
```

Error callbacks fire for decode errors, queue drops, handler exceptions/timeouts, and session factory failures. The `ErrorEvent` struct provides full context:

```cpp
struct ErrorEvent {
    PeerId peer;                    // Which peer (may be invalid for global errors)
    std::string peer_name;          // Human-readable peer name
    std::string remote_endpoint;    // "ip:port" or serial device path
    Error error;                    // The full conduit Error
};
```

`ErrorCallback` is defined as `std::function<void(const ErrorEvent&)>`.

Error callbacks are invoked synchronously from the thread that encountered the error (I/O thread for decode errors, worker thread for handler errors). Callbacks should be fast and non-blocking.

## Sending Messages

```cpp
// Send to a specific peer
VoidResult r = xcvr.send<Heartbeat>(peer_id, msg);

// Send to sole peer (convenience — requires exactly 1 peer)
VoidResult r = xcvr.send<Heartbeat>(msg);
```

`send()` encodes the message through the peer's session (`encode_wrap`) and transmits the resulting bytes via the transport.

```java
// Java — send to specific peer or sole peer
tx.send(peerId, heartbeatMsg);
tx.send(heartbeatMsg);              // sole peer convenience
```

```python
# Python — send to specific peer or sole peer
tx.send(peer_id, heartbeat_msg)
tx.send(heartbeat_msg)              # sole peer convenience
```

`send()` checks `is_receive_only()` on the session before encoding. If the message type has `direction="receive"`, the call returns an error without transmitting.

### Batch Sending

For array-payload protocols (`<payload count="*"/>`), multiple messages can be packed into a single frame:

```cpp
// Send multiple records in one frame
std::vector<Record> records = { ... };
VoidResult r = xcvr.send_batch<Record>(peer_id, std::span{records});

// Sole peer convenience
VoidResult r = xcvr.send_batch<Record>(std::span{records});
```

`send_batch()` calls `encode_batch()` on the session, which packs all messages into a single frame and transmits the resulting bytes. For sessions that do not support batch encoding (non-array payloads), `send_batch()` returns `BatchNotSupported`.

## Lifecycle

```cpp
[[nodiscard]] VoidResult start();   // start transports and worker threads
void stop();                        // stop transports, drain queue, join workers
[[nodiscard]] bool is_running() const noexcept;
```

```java
// Java — AutoCloseable, so use try-with-resources
try (Transceiver tx = new Transceiver()) {
    // ... register sessions, add peers, register handlers ...
    tx.start();
    // ...
    tx.stop();
}   // tx.close() called automatically
```

```python
# Python — context manager handles cleanup
with Transceiver() as tx:
    # ... register sessions, add peers, register handlers ...
    tx.start()
    # ...
    tx.stop()
# tx.close() called automatically

# Async variant
async with AsyncTransceiver() as tx:
    # ... same setup ...
    await tx.start()
    # ...
    await tx.stop()
```

> **Pitfall:** Add all peers and register all handlers before calling `start()`. Adding peers after `start()` is not supported.

## Query

```cpp
net::ConnectionState peer_state(PeerId peer) const;
size_t peer_count() const;
std::vector<PeerId> peer_ids() const;
const TransceiverStats& stats() const noexcept;
```

```java
// Java
Transceiver.ConnectionState state = tx.peerState(peerId);
long count = tx.peerCount();
Transceiver.StatsSnapshot s = tx.stats();
```

```python
# Python
state = tx.peer_state(peer_id)  # int: 0=Disconnected .. 4=Failed
count = tx.peer_count()
s = tx.stats()                  # Stats namedtuple
```

## TransceiverStats

Atomic counters for observability. All counters are monotonically increasing.

```cpp
#include <conduit/transceiver/transceiver_stats.hpp>

struct TransceiverStats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_dispatched{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> decode_errors{0};
    std::atomic<uint64_t> handler_errors{0};
    std::atomic<uint64_t> handler_timeouts{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> bytes_sent{0};

    Snapshot snapshot() const noexcept;  // point-in-time sample of all counters
    void reset() noexcept;               // zero all counters
};
```

`snapshot()` reads each counter atomically (`memory_order_relaxed`), but the
sample as a whole is *not* mutually consistent — counters can advance between
the individual loads. For most observability use cases (dashboards, periodic
log lines, end-of-test summaries) this is fine; if you need a strict snapshot
relationship between two counters, take two snapshots and compare deltas.

The `Snapshot` struct mirrors the same fields as plain (non-atomic) `uint64_t` values:

```cpp
auto s = xcvr.stats().snapshot();
std::cout << "received: " << s.messages_received
          << " dropped: " << s.messages_dropped << "\n";
```

```java
// Java — StatsSnapshot with accessor methods
Transceiver.StatsSnapshot s = tx.stats();
System.out.println("received: " + s.messagesReceived()
                 + " dropped: " + s.messagesDropped());
tx.statsReset();  // zero all counters
```

```python
# Python — Stats namedtuple
s = tx.stats()
print(f"received: {s.messages_received} dropped: {s.messages_dropped}")
tx.stats_reset()  # zero all counters
```

## Data Flow: Receive Path

1. Transport I/O thread receives bytes via `on_data_received`
2. For stream transports, `StreamFramer` buffers and extracts complete frames
3. Session's `decode_frame()` decodes each frame into `DecodedMessage` values
4. Each decoded message is wrapped as `InboundMessage` and pushed to the dispatch queue
5. Worker thread pops from the queue and calls `HandlerRegistry::dispatch()`
6. The matching handler callback is invoked with the typed message

## Data Flow: Send Path

1. User calls `xcvr.send<T>(peer, msg)`
2. `send_impl` checks `session.is_receive_only(T::TYPE_ID)` -- returns `DirectionViolation` if true
3. `send_impl` calls `session.encode_wrap(T::TYPE_ID, std::any(msg))`
4. The session wraps the leaf message into a complete frame and encodes it
5. The resulting bytes are sent via `transport.send(peer, bytes)`

## Message Logging

The transceiver has built-in message logging that captures both sent and received messages with full wire values. This is especially useful for frame-based protocols where frame fields (like category, length) are populated during encode -- the log shows the actual encoded values, not the pre-encode state.

### Configuration

Enable logging by setting `enabled = true` on `MessageLogConfig`. The examples below show common configurations:

**Single file for everything** -- simplest setup, good for debugging:

```cpp
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::Combined;
config.message_log.output = MessageLogOutput::File;
config.message_log.directory = "./logs";
config.message_log.prefix = "myapp";
// Creates: ./logs/myapp_messages.log
```

**Separate sent/received files** -- keeps send and receive traffic apart:

```cpp
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::SeparateDirection;
config.message_log.output = MessageLogOutput::File;
config.message_log.directory = "./logs";
config.message_log.prefix = "myapp";
// Creates: ./logs/myapp_sent.log, ./logs/myapp_received.log
```

**Per-peer files** -- useful with multiple peers:

```cpp
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::PerPeer;
config.message_log.directory = "./logs";
config.message_log.prefix = "myapp";
// Creates: ./logs/myapp_radar1.log, ./logs/myapp_radar2.log, ...
```

**Per-peer + per-direction** -- maximum granularity:

```cpp
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::PerPeerDirection;
config.message_log.directory = "./logs";
config.message_log.prefix = "myapp";
// Creates: ./logs/myapp_radar1_sent.log, ./logs/myapp_radar1_received.log, ...
```

**Custom filenames per direction** -- full naming control:

```cpp
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::SeparateDirection;
config.message_log.directory = "./logs";
config.message_log.sent_filename = "outbound.log";
config.message_log.received_filename = "inbound.log";
// Creates: ./logs/outbound.log, ./logs/inbound.log
```

**Stdout only** -- for development/debugging, no files created:

```cpp
config.message_log.enabled = true;
config.message_log.output = MessageLogOutput::Stdout;
config.message_log.include_message_content = false; // headers only, less noise
```

### Message Logging (Java)

```java
Transceiver.MessageLogConfig logCfg = new Transceiver.MessageLogConfig();
logCfg.enabled = true;
logCfg.mode = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
logCfg.output = Transceiver.MessageLogOutput.FILE;
logCfg.directory = "./logs";
logCfg.prefix = "myapp";
logCfg.includeMessageContent = true;
tx.setMessageLogConfig(logCfg);
```

### Message Logging (Python)

```python
tx.set_message_log_config(
    enabled=True,
    mode=MessageLogMode.SEPARATE_DIRECTION,
    output=MessageLogOutput.FILE,
    directory="./logs",
    prefix="myapp",
    include_message_content=True,
)
```

### Log Format

```
[2026-02-15T10:24:53.486] SEND peer=clients/1 remote=192.168.1.5:54321 proto=asterix transport=tcp-server type=Cat048Record bytes=37

Cat048Record{cat=48, len=37, items=...}

```

The `remote=` field shows the remote endpoint of the connection. What it displays depends on the transport type:

- **TCP Client**: The configured server address (e.g., `remote=10.0.1.5:5000`)
- **TCP Server**: The connecting client's IP and OS-assigned ephemeral port (e.g., `remote=192.168.1.5:60630`). The ephemeral port identifies a specific TCP connection and changes on each reconnection.
- **UDP**: The configured remote address (single-peer) or the sender's address (multi-peer)
- **Serial**: The device path (e.g., `remote=COM3`)

See [Transports: Remote Endpoint in Logs](transports.md#remote-endpoint-in-logs) for more detail. The field is omitted when no endpoint information is available.

The `proto=` field is the BMDL protocol used to encode/decode messages on this peer -- it is not derived from the incoming connection. It comes from the session's `protocol_name()` (e.g., `"asterix"`, `"sentry-link"`). All messages on the same peer share the same protocol.

The metadata line and the message content are separated by blank lines for readability. The content line (via `to_string()`) only appears when `include_message_content = true`.

### File Modes

When `filename` is not set, files are named using the `prefix`:

| Mode | Files Created | Description |
|------|--------------|-------------|
| `Combined` | `{prefix}_messages.log` | All messages in one file |
| `SeparateDirection` | `{prefix}_sent.log`, `{prefix}_received.log` | Split by direction |
| `PerPeer` | `{prefix}_{peer}.log` | One file per peer |
| `PerPeerDirection` | `{prefix}_{peer}_sent.log`, `{prefix}_{peer}_received.log` | Split by peer and direction |

### Custom Filename Pattern

Set `filename` to override the default prefix-based naming. The pattern supports two placeholders:

| Placeholder | Replaced With | Example |
|-------------|---------------|---------|
| `{direction}` | `sent` or `received` | `app_{direction}.log` → `app_sent.log` |
| `{peer}` | The peer name | `log_{peer}.txt` → `log_radar1.txt` |

Both placeholders can be combined: `{peer}_{direction}.log` → `radar1_sent.log`.

When `filename` is set, the `prefix` field is ignored. The file is created in `directory`.

### Per-Direction Filename Overrides

For full control over send and receive filenames, use `sent_filename` and `received_filename`. These take priority over `filename` for their respective direction and support the `{peer}` placeholder:

```cpp
config.message_log.mode = MessageLogMode::SeparateDirection;
config.message_log.sent_filename = "outbound.log";
config.message_log.received_filename = "inbound.log";

// With per-peer-direction mode:
config.message_log.mode = MessageLogMode::PerPeerDirection;
config.message_log.sent_filename = "{peer}_out.log";
config.message_log.received_filename = "{peer}_in.log";
```

You can set only one direction; the other falls back to `filename` or prefix-based defaults.

### Output Destinations

| Output | Behavior |
|--------|----------|
| `File` | Write to log files only |
| `Stdout` | Write to stdout only |
| `Both` | Write to both log files and stdout |

### Including Raw Bytes

Set `include_raw_bytes = true` to append a hex dump of the wire bytes after
the metadata line (and before the formatted message content):

```cpp
config.message_log.enabled = true;
config.message_log.include_message_content = true;
config.message_log.include_raw_bytes = true;
```

```
[2026-02-15T10:24:53.486] SEND peer=clients/1 ... bytes=37
  hex: AA BB 00 25 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21
       22 23 24

Cat048Record{...}
```

The dump uses 32 bytes per line, two-digit uppercase hex, space-separated.
Disabled by default since the output can dwarf the metadata for large frames.

### Peer Name Sanitization

Peer names that contain path separators (`/`, `\`), `:` , or NUL are
sanitized when interpolated into a filename — those characters are replaced
with `_`. This matters for multi-peer transports (TCP server) where dynamic
peers are named `<base>/<index>` (e.g., `server/0`). Without sanitization,
`PerPeer` mode would create files inside a `server` subdirectory; with
sanitization, the file is `<prefix>_server_0.log` in `directory`.

### Performance

- **Send side**: When logging is enabled, each `send()` does one extra `decode_frame()` call on the encoded bytes to recover the actual wire values (including auto-managed frame fields). This roughly doubles the encode cost per message.
- **Receive side**: One `format_message()` call per decoded message (string formatting cost).
- **File I/O**: Mutex-protected writes. For high throughput, use `Combined` mode to minimize file handles.
- **Disabled** (`enabled = false`): Single boolean check per send/receive -- zero overhead.

### Session Requirements

The logging system uses two `ISession` virtual methods:

- `protocol_name()` -- returns the protocol name (e.g., `"asterix"`). Generated sessions override this automatically.
- `format_message(type_id, payload)` -- formats a decoded message as a string using `to_string()`. Generated sessions override this automatically.

Custom `ISession` implementations can override these for logging support. The defaults return `"unknown"` and `""` respectively.

## Python AsyncTransceiver

The `AsyncTransceiver` wraps the synchronous `Transceiver` for use with Python's `asyncio`. It bridges C++ I/O thread callbacks into the asyncio event loop via `loop.call_soon_threadsafe()`.

```python
from conduit import AsyncTransceiver, TcpClientConfig

async def main():
    async with AsyncTransceiver() as tx:
        tx.register_session("asterix", AsterixDataBlockSession())
        tx.add_peer("server", "asterix", TcpClientConfig("10.0.0.1:5000"))

        # Handlers can be async
        @tx.on(Heartbeat)
        async def on_heartbeat(peer_id, msg):
            print(f"heartbeat seq={msg.sequence}")

        # State/error callbacks work the same
        tx.on_state_change(lambda peer_id, state:
            print(f"peer {peer_id} -> {state}"))

        await tx.start()

        # Async message stream — iterate over incoming messages
        async for peer_id, msg in tx.messages(Heartbeat):
            print(f"stream: {msg.sequence}")
            break  # or continue processing

        # Async send
        await tx.send(peer_id, heartbeat_msg)

        await tx.stop()
```

Key differences from the synchronous `Transceiver`:
- `start()`, `stop()`, `send()`, and `send_raw()` are `async` methods
- Handlers registered with `@tx.on()` can be `async def` functions
- `tx.messages(MsgClass)` returns an async iterator for streaming message consumption
- Query methods (`peer_count()`, `peer_state()`, `stats()`) remain synchronous (non-blocking)

## Common Pitfalls

> **Pitfall:** `DropPolicy::Block` on the dispatch queue causes the transport I/O thread to stall when the queue is full, blocking all receive processing. Prefer `DropOldest` or `DropNewest`.

> **Pitfall:** Slow handlers block worker threads. If you have one worker thread (default) and a handler takes 5 seconds, no other messages are dispatched during that time. Increase `WorkerConfig::thread_count` for throughput, or offload heavy processing to a separate thread.

> **Pitfall:** `handler_timeout` is warning-only. It does not interrupt or cancel the handler -- it only logs a warning when a handler exceeds the timeout.

> **Pitfall:** Calling `send()` with a receive-only message type (one declared with `direction="receive"` in BMDL) returns `DirectionViolation`. The message is not transmitted. If you need to send and receive the same discriminator value, define separate direction-qualified cases.

## See Also

- [Configuration](configuration.md) -- `TransceiverConfig`, `QueueConfig`, `WorkerConfig`, and config-driven `add_peer()`
- [Message Handlers](handlers.md) -- Full handler reference: dispatch priority, builder, `ErasedHandler`
- [Transports](transports.md) -- TCP, UDP, and Serial transport configs
- [Stream Framing](stream-framing.md) -- How `StreamFramer` reassembles bytes into frames
- [Sessions & Generated Code](sessions-and-codegen.md) -- `ISession` interface and session factories
- [BoundedQueue](bounded-queue.md) -- The dispatch queue between I/O and worker threads
- [Error Handling](error-handling.md) -- `Result<T>` and `VoidResult` returned by `start()`, `send()`, `add_peer()`
