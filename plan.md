# Plan: Bridge the Conduit UX Gap Between C++ and Java/Python

## Goal
Make using Conduit in Java and Python feel identical to C++. Users should only
deal with **typed message objects** — never raw bytes, type IDs, or manual
serialization. Error handling, statistics, and observability should also match.

## Current State

### What C++ delivers
```cpp
// Send — typed, zero boilerplate
PingBody msg; msg.timestamp = 42;
transceiver.send<PingBody>(msg);

// Receive — typed callback, auto-deserialized
transceiver.on<PingBody>([](const PingBody& m) {
    std::cout << m.timestamp << std::endl;
});

// Stats, error callbacks, state changes — all available
auto snap = transceiver.stats().snapshot();
transceiver.on_error([](const ErrorEvent& e) { ... });
```

### What Java/Python currently deliver
- `send()` is a **stub** that always returns an error
- Message callbacks receive **nullptr/empty bytes** (raw bytes lost in dispatch)
- No statistics API
- Error/state callbacks work but are the only functional advanced feature

## Architecture

```
┌──────────────────────────────────────────┐
│  Layer 4: Python/Java Typed API          │  Users live here
│  t.send(peer, msg)                       │
│  @t.on(PingBody) def handle(peer, msg)   │
├──────────────────────────────────────────┤
│  Layer 3: Python/Java Bindings           │  Auto-marshal via bgen classes
│  Auto-encode on send, auto-decode on recv│
├──────────────────────────────────────────┤
│  Layer 2: C ABI                          │  Raw bytes cross FFI boundary
│  conduit_send(xcvr, peer, tid, data, len)│
│  conduit_stats(xcvr, &snapshot)          │
├──────────────────────────────────────────┤
│  Layer 1: C++ Transceiver                │  New raw-bytes entry points
│  send_raw(peer, type_id, bytes)          │
│  Raw bytes flow through dispatch         │
└──────────────────────────────────────────┘
```

---

## Layer 1: C++ Transceiver — Raw-Bytes Paths

### 1a. `send_raw()` method
Add to Transceiver a raw-bytes send that wraps payload in `std::any(vector<uint8_t>)` and
calls `send_impl()`. The generated `encode_wrap()` already handles the `vector<uint8_t>`
fallback path.

### 1b. Extended dispatch with raw bytes
Add a raw-aware catch-all signature and dispatch overload so raw bytes flow through to
the C ABI callback layer. Modify worker_loop to pass `DecodedMessage::raw` alongside payload.

---

## Layer 2: C ABI — Complete the Implementation

### 2a. Fix `conduit_send()` — call `send_raw()` instead of returning stub error
### 2b. Fix message callbacks — deliver raw bytes via the new raw catch-all
### 2c. Add stats API — `conduit_stats()` and `conduit_stats_reset()`

---

## Layer 3: Python Bindings — Typed Message API

### 3a. Typed send: `t.send(peer_id, msg)` auto-serializes via `msg.encode_bytes()`
### 3b. Typed on: `@t.on(PingBody)` auto-deserializes via `PingBody.decode_bytes(data)`
### 3c. Stats: `t.stats()` returns a `Stats` namedtuple
### 3d. Keep backward-compatible raw API

---

## Layer 4: Java Bindings — Typed Message API

### 4a. Typed send via reflection on TYPE_ID and encodeBytes()
### 4b. Typed onMessage via Class<T> with auto-decode
### 4c. Stats: `t.stats()` returns StatsSnapshot
### 4d. Keep backward-compatible raw API

---

## Tests

Comprehensive happy-path and error-path tests for both Python and Java.
