# Message Handlers

[Back to index](index.md)

Handlers are callbacks that fire when a decoded message arrives. Conduit provides two registration styles: direct `on<T>()` calls on the `Transceiver`, and the `MessageHandler` builder for fluent multi-type setup.

## Direct Registration

```cpp
#include <conduit/transceiver/transceiver.hpp>

// Global handler (fires for any peer)
xcvr.on<Heartbeat>([](const Heartbeat& msg) {
    std::cout << "heartbeat seq=" << msg.sequence() << "\n";
});

// Per-peer handler (overrides global for this peer)
xcvr.on<Heartbeat>(peer_id, [](const Heartbeat& msg) {
    std::cout << "peer-specific heartbeat\n";
});

// Remove handlers
xcvr.remove_handler<Heartbeat>();           // remove global
xcvr.remove_handler<Heartbeat>(peer_id);    // remove per-peer
```

### Java

```java
// Typed handler — auto-deserializes message
tx.onMessage(Heartbeat.class, (peerId, msg) -> {
    System.out.println("heartbeat seq=" + msg.sequence);
});

// Raw handler — receives byte[] payload
tx.onMessage(Heartbeat.TYPE_ID, (peerId, typeId, typeName, data) -> {
    System.out.println("raw: " + typeName + " (" + data.length + " bytes)");
});

// Catch-all
tx.onAnyMessage((peerId, typeId, typeName, data) -> {
    System.out.println("unhandled: " + typeName);
});

// Remove
tx.removeHandler(peerId, Heartbeat.TYPE_ID);
```

### Python

```python
# Typed handler — decorator style
@tx.on(Heartbeat)
def on_heartbeat(peer_id, msg):
    print(f"heartbeat seq={msg.sequence}")

# Raw handler by type ID
@tx.on(type_id=Heartbeat.TYPE_ID)
def on_heartbeat_raw(peer_id, type_id, type_name, data):
    print(f"raw: {type_name} ({len(data)} bytes)")

# Catch-all
tx.on_any(lambda peer_id, type_id, type_name, data:
    print(f"unhandled: {type_name}"))

# Remove
tx.remove_handler(peer_id, Heartbeat.TYPE_ID)
```

### Python Async Handlers

```python
# AsyncTransceiver supports async handler functions
@tx.on(Heartbeat)
async def on_heartbeat(peer_id, msg):
    await process_heartbeat(msg)

# Async message stream — alternative to callback-based handlers
async for peer_id, msg in tx.messages(Heartbeat):
    await process_heartbeat(msg)
```

## MessageHandler Builder

```cpp
#include <conduit/transceiver/message_handler.hpp>

MessageHandler handler;
handler
    .on<Heartbeat>([](const Heartbeat& msg) {
        // typed handler for Heartbeat
    })
    .on<SensorData>([](const SensorData& msg) {
        // typed handler for SensorData
    })
    .on_group({AlertBody::TYPE_ID, WarningBody::TYPE_ID},
              [](uint64_t type_id, const std::any& payload) {
        // group handler — receives raw std::any
    })
    .on_any([](uint64_t type_id, const std::any& payload) {
        // catch-all for unmatched types
    });

// Install for sole peer
xcvr.set_handler(std::move(handler));

// Install for a specific peer
xcvr.set_handler(peer_id, std::move(handler));
```

### Builder Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `on<T>()` | `MessageHandler& on(std::function<void(const T&)>)` | Register typed handler |
| `on_group()` | `MessageHandler& on_group(initializer_list<uint64_t>, std::function<void(uint64_t, const std::any&)>)` | Register handler for a set of type IDs |
| `on_any()` | `MessageHandler& on_any(std::function<void(uint64_t, const std::any&)>)` | Register catch-all handler |

## Dispatch Priority

When a message arrives, the handler registry checks in this order:

1. **Per-peer typed handler** -- registered via `on<T>(peer_id, cb)` or `set_handler(peer_id, handler)`
2. **Global typed handler** -- registered via `on<T>(cb)` or `set_handler(handler)`
3. **Per-peer catch-all** -- from `on_any()` on a per-peer `MessageHandler`
4. **Global catch-all** -- from `on_any()` on a global `MessageHandler`

The first match wins. If no handler matches, the message is silently discarded (with `DispatchResult::NotFound`).

> **Note:** `on_group()` registers individual typed handlers for each type ID in the group. These handlers share the same dispatch priority as `on<T>()` handlers (steps 1-2 above). If you register both `on<T>()` and `on_group()` for the same type ID, the last one installed wins.

### Raw Catch-All (FFI Forwarding)

Independent of the typed dispatch chain, `HandlerRegistry::set_raw_catch_all()`
installs a single global callback that fires **for every dispatched message**
with the raw frame bytes alongside the typed payload:

```cpp
using RawCatchAllFn = std::function<void(PeerId, uint64_t /*type_id*/,
                                         const std::any& /*payload*/,
                                         std::span<const uint8_t> /*raw*/)>;
xcvr.handlers().set_raw_catch_all(my_raw_callback);
```

It runs *before* the typed handler in step 1–4 above, and the typed dispatch
still proceeds normally afterwards. If a typed handler is found, dispatch
returns `Handled`. If only the raw catch-all fired (no typed handler
matched), dispatch still returns `Handled` because the raw catch-all has
already taken responsibility for the message.

This hook exists primarily so the C ABI / JNI / ctypes bindings can forward
raw bytes to Java / Python without going through `std::any_cast`. Most C++
applications do not need it; use `on<T>()` and `MessageHandler` instead.

## DispatchResult

```cpp
enum class DispatchResult {
    Handled,     // Handler found and invoked successfully (or raw catch-all fired)
    NotFound,    // No handler registered for this type_id/peer
    Error        // Handler found but threw an exception
};
```

`Error` is returned whenever a handler — typed, group, catch-all, or raw — throws
any exception. The exception is caught and logged via `LOG_ERRORF`; the message
is *not* retried, and dispatch continues with the next message. Specific
diagnostics:

- `std::bad_any_cast`: typed handler signature didn't match the dispatched
  payload (programmer error — wrong type registered for the type_id).
- `std::exception`: any user code throwing — the `what()` string is logged.
- Anything else: logged as "unknown exception".

## Connection State Callbacks

Track when peers connect, disconnect, or reconnect:

```cpp
CallbackId id = xcvr.on_state_change([](PeerId peer, net::ConnectionState state) {
    std::cout << peer << " → " << net::to_string(state) << "\n";
});

// Later, remove the callback
xcvr.remove_state_change(id);
```

`CallbackId` is an opaque `uint32_t`-based enum returned by `on_state_change()`.

```java
// Java — ConnectionState is an enum
int id = tx.onStateChange((peerId, newState) ->
    System.out.println(peerId + " -> " + newState.name()));
tx.removeStateChange(id);
```

```python
# Python — state is an integer (0=Disconnected .. 4=Failed)
tx.on_state_change(lambda peer_id, state:
    print(f"peer {peer_id} -> {state}"))
```

## HandlerRegistry Internals

The `HandlerRegistry` manages all handler lookups:

- **Dispatch** uses a shared lock (`std::shared_mutex`) -- multiple worker threads can dispatch concurrently
- **Registration** uses an exclusive lock -- adding/removing handlers blocks dispatch briefly
- Per-peer handlers are stored in a `(PeerId, type_id)` keyed map
- Global handlers are stored in a `type_id` keyed map

The registry is internal to the `Transceiver`. Users interact with it through `on<T>()`, `set_handler()`, and `remove_handler<T>()`.

## ErasedHandler

`ErasedHandler` is the type-erased wrapper that stores handler callbacks:

```cpp
class ErasedHandler {
public:
    // Typed — performs any_cast<const T&> internally
    template<traits::Message T>
    explicit ErasedHandler(std::function<void(const T&)> cb);

    // Raw — for group/catch-all handlers
    ErasedHandler(uint64_t type_id, std::function<void(const std::any&)> invoke);

    void invoke(const std::any& payload) const;
    uint64_t type_id() const noexcept;
};
```

This is an internal detail -- you create typed handlers via `on<T>()` and the `MessageHandler` builder, which construct `ErasedHandler` objects internally.

## Threading

- Handlers are invoked on worker threads (configured by `WorkerConfig::thread_count`)
- Multiple workers can dispatch concurrently if `thread_count > 1`
- Each message is dispatched to exactly one worker (no duplicate delivery)
- If a handler throws an exception, it is caught, logged, and `DispatchResult::Error` is returned. The exception does not propagate.

## Common Pitfalls

> **Pitfall:** Handlers should not block for extended periods. A slow handler blocks its worker thread, reducing dispatch throughput. Offload heavy processing to a separate thread.

> **Pitfall:** `handler_timeout` (in `WorkerConfig`) does not cancel or interrupt handlers -- it only logs a warning. There is no preemption.

> **Pitfall:** Per-peer handlers override global handlers for the same type. If you register both `on<T>(peer, cb1)` and `on<T>(cb2)`, peer's messages go to `cb1` and all other peers' messages go to `cb2`.

> **Pitfall:** Exceptions thrown from handlers are caught and logged, but the message is considered handled (not retried). Design handlers to handle their own errors.

> **Pitfall:** For array-payload frames (`<payload count="*"/>`), `decode_frame()` emits one `DecodedMessage` per payload element. Handlers fire once per element, not once per frame.

## See Also

- [Transceiver](transceiver.md) -- The orchestrator that owns the `HandlerRegistry` and invokes `on<T>()`, `set_handler()`
- [Configuration](configuration.md) -- `WorkerConfig::thread_count` and `handler_timeout` settings
- [Sessions & Generated Code](sessions-and-codegen.md) -- `DecodedMessage` and the `Message` concept that constrains `on<T>()`
