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

## Sending Messages

```cpp
// Send to a specific peer
VoidResult r = xcvr.send<Heartbeat>(peer_id, msg);

// Send to sole peer (convenience — requires exactly 1 peer)
VoidResult r = xcvr.send<Heartbeat>(msg);
```

`send()` encodes the message through the peer's session (`encode_wrap`) and transmits the resulting bytes via the transport.

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
2. `send_impl` calls `session.encode_wrap(T::TYPE_ID, std::any(msg))`
3. The session wraps the leaf message into a complete frame and encodes it
4. The resulting bytes are sent via `transport.send(peer, bytes)`

## Common Pitfalls

> **Pitfall:** `DropPolicy::Block` on the dispatch queue causes the transport I/O thread to stall when the queue is full, blocking all receive processing. Prefer `DropOldest` or `DropNewest`.

> **Pitfall:** Slow handlers block worker threads. If you have one worker thread (default) and a handler takes 5 seconds, no other messages are dispatched during that time. Increase `WorkerConfig::thread_count` for throughput, or offload heavy processing to a separate thread.

> **Pitfall:** `handler_timeout` is warning-only. It does not interrupt or cancel the handler -- it only logs a warning when a handler exceeds the timeout.

## See Also

- [Configuration](configuration.md) -- `TransceiverConfig`, `QueueConfig`, `WorkerConfig`, and config-driven `add_peer()`
- [Message Handlers](handlers.md) -- Full handler reference: dispatch priority, builder, `ErasedHandler`
- [Transports](transports.md) -- TCP, UDP, and Serial transport configs
- [Stream Framing](stream-framing.md) -- How `StreamFramer` reassembles bytes into frames
- [Sessions & Generated Code](sessions-and-codegen.md) -- `ISession` interface and session factories
- [BoundedQueue](bounded-queue.md) -- The dispatch queue between I/O and worker threads
- [Error Handling](error-handling.md) -- `Result<T>` and `VoidResult` returned by `start()`, `send()`, `add_peer()`
