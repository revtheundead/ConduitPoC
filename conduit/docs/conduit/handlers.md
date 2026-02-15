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

## DispatchResult

```cpp
enum class DispatchResult {
    Handled,     // Handler found and invoked successfully
    NotFound,    // No handler registered for this type_id/peer
    Error        // Handler found but threw an exception
};
```

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
