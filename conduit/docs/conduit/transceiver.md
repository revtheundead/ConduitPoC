# Transceiver

[Back to index](index.md)

The `Transceiver` is the main orchestrator. It wires protocol sessions to transports, manages peers, dispatches decoded messages to typed handlers, and encodes outgoing messages -- all without exposing raw bytes to user code.

```cpp
#include <conduit/transceiver/transceiver.hpp>

namespace conduit::transceiver {
class Transceiver;
}
```

`Transceiver` is default-constructible (no config required for imperative peer setup), non-copyable, and non-movable.

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

### Peer Lookup

```cpp
Result<PeerId> sole_peer() const;            // exactly one peer, else error
Result<PeerId> peer(std::string_view name) const;  // lookup by name
```

> **Pitfall:** `sole_peer()` returns `MultiplePeers` error if more than one peer exists. Use `peer("name")` for named lookup.

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

### Error Callbacks

```cpp
#include <conduit/transceiver/error_event.hpp>

CallbackId id = xcvr.on_error([](const ErrorEvent& event) {
    std::cerr << event.peer_name << ": " << event.error.format() << "\n";
});

xcvr.remove_error_callback(id);
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

`send()` checks `is_receive_only()` on the session before encoding. If the message type has `direction="receive"`, the call returns a `DirectionViolation` error without transmitting.

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

> **Pitfall:** Add all peers and register all handlers before calling `start()`. Adding peers after `start()` is not supported.

## Query

```cpp
net::ConnectionState peer_state(PeerId peer) const;
size_t peer_count() const;
std::vector<PeerId> peer_ids() const;
const TransceiverStats& stats() const noexcept;
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

    Snapshot snapshot() const noexcept;  // consistent read of all counters
    void reset() noexcept;              // zero all counters
};
```

The `Snapshot` struct mirrors the same fields as plain (non-atomic) `uint64_t` values:

```cpp
auto s = xcvr.stats().snapshot();
std::cout << "received: " << s.messages_received
          << " dropped: " << s.messages_dropped << "\n";
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
