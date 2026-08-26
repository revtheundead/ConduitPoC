# Session Code Generation

[Back to index](index.md)

bgen generates session classes that handle frame-based message dispatch. The C++ backend produces `sessions.hpp` with classes implementing `conduit::traits::ISession`. The Java backend produces session `.java` files, and the Python backend produces `sessions.py`. All three backends provide the same session functionality: decode frames into typed messages, encode typed messages into framed wire bytes, and manage auto fields (sequence counters, timestamps, config).

This page primarily documents the C++ output. Java and Python equivalents are summarized at the end.

## Session Class

When a `<frame>` is present, bgen generates a session class based on the frame:

```
class SimpleFrameSession : public conduit::traits::ISession { ... };

inline std::unique_ptr<conduit::traits::ISession> create_simple_frame_session();
```

The session class name is formed by `to_cpp_type_name(frame_name) + "Session"`. Since `to_cpp_type_name` only replaces hyphens with underscores (preserving original casing), a BMDL frame named `Frame` produces `FrameSession`, while `data-frame` produces `data_frameSession`.

The factory function name is `"create_" + to_lower_snake_case(frame_name) + "_session"`. `to_lower_snake_case()` converts both hyphens and PascalCase boundaries to underscores and lowercases everything, so:

| BMDL frame name | Factory function |
|------------------|-------------------|
| `Frame` | `create_frame_session()` |
| `MyFrame` | `create_my_frame_session()` |
| `data-frame` | `create_data_frame_session()` |
| `HTTPParser` | `create_httpparser_session()` (consecutive uppercase letters are *not* split) |

### Config Parameter

If the frame has `auto="config(key)"` fields, the session class contains a nested `Config` struct and requires it in the constructor:

```cpp
SimpleFrameSession::Config config;
config.system_id = 42;
auto session = create_simple_frame_session(config);
```

If there are no `auto="config(key)"` fields, no `Config` struct is generated, and the constructor/factory take no parameters.

## ISession Interface Methods

### decode_frame

```cpp
[[nodiscard]] conduit::Result<std::vector<conduit::traits::DecodedMessage>>
decode_frame(std::span<const uint8_t> data) override;
```

Calls `FrameClass::decode_bytes(data)` to decode the entire frame, then uses `std::visit` on the payload variant to create `DecodedMessage` structs. Returns a vector of `DecodedMessage` structs, each containing:

- `type_id` (`uint64_t`) -- FNV-1a hash of the leaf type name
- `type_name` (`std::string_view`) -- The BMDL name (e.g., `"Heartbeat"`)
- `payload` (`std::any`) -- The decoded leaf value, holding the concrete C++ type (use `std::any_cast` to extract)

The `DecodedMessage` struct also has a `raw` (`std::vector<uint8_t>`) field containing a copy of the raw encoded bytes for the frame. The generated `decode_frame` populates this automatically from the input data.

For `<payload count="*"/>` frames, `decode_frame` iterates over the frame's payload vector and emits one `DecodedMessage` per payload element.

Direction-constrained types (`direction="send"`) generate a log warning when decoded.

### encode_wrap

```cpp
[[nodiscard]] conduit::Result<conduit::traits::EncodeResult>
encode_wrap(uint64_t type_id, const std::any& payload) override;
```

Takes a `type_id` and an `std::any`-wrapped leaf payload, wraps it into a frame, encodes, and returns an `EncodeResult` (see [EncodeResult](#encoderesult)).

For each known leaf type:
1. Switches on `type_id`
2. Extracts the concrete type from `std::any` via `std::any_cast`
3. Calls `FrameClass::wrap(msg)` to create the frame (auto-sets the ID field)
4. Sets `auto="config(key)"` fields from the Config struct
5. Sets `auto="increment"` fields (session-stateful counter)
6. Sets `auto="timestamp"` fields (current system time in milliseconds)
7. Calls `frame.encode_bytes()`

Returns `UnknownTypeId` for unrecognized type IDs, and `InvalidArgument` if the `std::any` payload doesn't match the expected type.

Direction-constrained types (`direction="receive"`) generate a log warning when encoded.

### encode_batch (array payload)

```cpp
[[nodiscard]] conduit::Result<conduit::traits::EncodeResult>
encode_batch(uint64_t type_id, std::span<const std::any> payloads) override;
```

Only generated for frame-based sessions with `<payload count="*"/>`. Packs multiple messages of the same type into a single frame:

1. Matches on `type_id`
2. Creates a frame and sets the ID field
3. Iterates `payloads`, `std::any_cast`s each to the expected message type
4. Pushes each message into the frame's payload vector
5. Sets config, auto-increment, and auto-timestamp fields
6. Calls `frame.encode_bytes()`

Returns `InvalidArgument` if any payload fails the `std::any_cast`, or `UnknownTypeId` for unrecognized type IDs.

Non-array sessions do not override this method and inherit the default `BatchNotSupported` rejection from `ISession`.

### sync_pattern

```cpp
[[nodiscard]] std::span<const uint8_t> sync_pattern() const override;
```

Returns the byte pattern used for stream synchronization. The sync pattern is derived from the first `constraint equals` field in the frame header (e.g., a sync word constant). Returns an empty span if no sync pattern exists.

The sync bytes are stored as a `static constexpr uint8_t[]` array, written in wire order respecting the field's endianness.

### min_frame_header_size

```cpp
[[nodiscard]] size_t min_frame_header_size() const override;
```

Returns the minimum number of bytes needed to read the frame header (up to the first variable-length or optional element). This allows partial reads for framing decisions without attempting a full decode.

### extract_frame_length

```cpp
[[nodiscard]] size_t extract_frame_length(std::span<const uint8_t> header) const override;
```

Reads the frame length field directly from a partial header buffer without performing a full frame decode. The session analyzer records the bit offset and width of the length field during analysis.

When length field info is available: reads the field at its known bit offset using the appropriate read function (`read_u8`, `read_u16`, `read_u32`, `read_u64`) with the correct endianness. If the frame's length field has an offset (e.g., `auto="length - 3"`), the extracted value is adjusted by reversing the offset -- if encode writes `size - 3`, decode adds 3 back to recover the true frame length.

When only the field name is known: falls back to a full `decode()` and accessor call.

When no length field is found: returns `header.size()`.

Returns 0 if the header is too small.

### leaf_type_ids

```cpp
[[nodiscard]] std::span<const uint64_t> leaf_type_ids() const override;
```

Returns a span of all leaf type IDs in this session. The IDs are stored as a `static constexpr uint64_t[]` array with comments showing each type's BMDL name.

### type_name

```cpp
[[nodiscard]] std::string_view type_name(uint64_t type_id) const override;
```

Returns the BMDL name for a given type ID, or `"unknown"` if not found. Implemented as a `switch` statement over all known type IDs.

### is_receive_only

```cpp
[[nodiscard]] bool is_receive_only(uint64_t type_id) const override;
```

Returns `true` if the leaf type with the given `type_id` has `direction="receive"`. The `Transceiver` calls this before encoding to block sending receive-only message types (returning `DirectionViolation`). The default `ISession` implementation returns `false` for all types. Generated sessions override this when any leaf type has `direction="receive"`.

### reset

```cpp
void reset() override;
```

Resets session state. If any leaf type has auto-increment fields, this resets the sequence counter to 0. Auto-timestamp fields are unaffected by reset (they are stateless).

### format_message

```cpp
[[nodiscard]] std::string format_message(uint64_t type_id, const std::any& payload) const override;
```

Formats a decoded message payload as a human-readable string by switching on `type_id`, extracting the concrete type via `std::any_cast`, and calling its `to_string()` method. Returns an empty string for unrecognized type IDs.

### format_outbound

```cpp
[[nodiscard]] std::string format_outbound(
    uint64_t type_id, const std::any& payload,
    std::span<const std::pair<std::string, std::string>> auto_fields) const override;
```

Formats an outbound message with auto-field overrides (id, length, count, timestamp, etc.). The default implementation delegates to `format_message()`, ignoring `auto_fields`.

Frame-level auto fields are patched onto the wire during encoding and are not stored on the in-memory message, so `format_outbound` overlays their real values from `auto_fields`. Body-level auto fields (`auto="length"`/`auto="count(...)"` on message or struct members) are handled directly in the generated `to_string()`: a `count` field reports the referenced array's element count, and a `length` field reports the encoded byte length (computed by re-encoding the struct). This ensures logged messages never show these patched fields as their zero-initialized member values.

### protocol_name

```cpp
[[nodiscard]] std::string_view protocol_name() const override;
```

Returns the protocol name (the BMDL namespace). For example, `"asterix"` or `"my_protocol"`.

## EncodeResult

The `EncodeResult` struct is returned by `encode_wrap()` and `encode_batch()`:

```cpp
struct EncodeResult {
    std::vector<uint8_t> bytes;                              // Encoded frame bytes
    std::vector<std::pair<std::string, std::string>> auto_fields; // name-value pairs
};
```

- **`bytes`**: The fully encoded frame ready for transmission.
- **`auto_fields`**: Metadata about frame-level auto-managed fields that were set during encoding (e.g., id, length, count, sequence counter, timestamp values). Each entry is a `{field_name, string_value}` pair. Used by the transceiver for message logging (`format_outbound()`).

## Auto-Increment Counter

Sessions track a single sequence counter for auto-increment fields (`auto="increment"` in BMDL). When encoding via `encode_wrap`:

1. The frame is built via `wrap()`
2. Each auto-increment field is set to `sequence_counter_++ & mask`, where the mask is derived from the field's bit width
3. The counter increments after each encode

The counter type is determined by the maximum bit width across all auto-increment fields in all leaf types (using the smallest C++ unsigned integer type that fits).

A public `sequence_counter()` accessor is provided for reading the current counter value.

## Auto-Timestamp Fields

Sessions populate auto-timestamp fields (`auto="timestamp"` in BMDL) with the current time during `encode_wrap` and `encode_batch`. The value is milliseconds since Unix epoch from `std::chrono::system_clock`, masked to the field's bit width (e.g., a 32-bit timestamp wraps every ~49 days).

Unlike auto-increment, timestamp is stateless -- there is no session state to track or reset. Each encode operation reads the current time independently. When timestamp fields are present, the generated `sessions.hpp` includes `<chrono>`.

## Factory Function

```cpp
inline std::unique_ptr<conduit::traits::ISession> create_frame_session() {
    return std::make_unique<FrameSession>();
}
```

The factory function name converts the frame name from PascalCase/kebab-case to snake_case: `MyFrame` becomes `create_my_frame_session()`.

## Type ID Computation

Leaf type IDs are computed using FNV-1a (64-bit) hash of the BMDL type name string:

```
hash = 14695981039346656037 (FNV offset basis)
for each byte in name:
    hash ^= byte
    hash *= 1099511628211 (FNV prime)
```

The hash is computed at compile time (`constexpr`). During session analysis, bgen checks for hash collisions and reports an error if two leaf types in the same session produce the same type_id.

## Direction Filtering

BMDL messages can specify `direction="send"` or `direction="receive"`:

- **Send-only types**: Skipped in the frame's `decode()` switch (their ID returns `UnknownDiscriminator` on decode). Included in `encode_wrap()`. A warning is logged if the `decode_frame` path is reached.
- **Receive-only types**: Included in `decode_frame()`. A warning is logged when encoded via `encode_wrap()`.
- **Both (default)**: No warnings, fully bidirectional.

All leaf types participate in `leaf_type_ids()` and `type_name()` regardless of direction. The logger include is only added when direction-constrained types exist.

### Direction-Qualified Overlapping IDs

When two messages share the same `id` with complementary directions (`send` vs. `receive`), the frame's `decode()` switch dispatches to the `receive` variant. The `encode_wrap()` path uses the `send` variant. Both variants remain in the `PayloadVariant` type.

## Protocol Descriptor Integration

The `ProtocolDescriptor` in `protocol.hpp` provides a `create_session()` method that delegates to the first session's factory function. It also aggregates all leaf types across all sessions into a unified type registry with optional group annotations.

## Java and Python Session Generation

All three backends generate session classes with the same core methods. The wire behavior is identical.

### Session Method Comparison

| Method | C++ | Java | Python |
|--------|-----|------|--------|
| Decode frame | `decode_frame(span<uint8_t>)` -> `Result<vector<DecodedMessage>>` | `decodeFrame(byte[])` -> `List<Map>` | `decode_frame(bytes)` -> `list[dict]` |
| Encode message | `encode_wrap(type_id, any)` -> `Result<EncodeResult>` | `encodeWrap(type_id, Object)` -> `Map` | `encode_wrap(type_id, object)` -> `dict` |
| Encode batch | `encode_batch(type_id, span<any>)` -> `Result<EncodeResult>` | Not yet implemented | `encode_batch(type_id, list)` -> `dict` |
| Format message | `format_message(type_id, any)` -> `string` | `formatMessage(type_id, Object)` -> `String` | `format_message(type_id, object)` -> `str` |
| Format outbound | `format_outbound(type_id, any, auto_fields)` -> `string` | `formatOutbound(...)` -> `String` | `format_outbound(...)` -> `str` |
| Type name | `type_name(type_id)` -> `string_view` | `typeName(type_id)` -> `String` | `type_name(type_id)` -> `str` |
| Sync pattern | `sync_pattern()` -> `span<uint8_t>` | `syncPattern()` -> `byte[]` | `sync_pattern()` -> `bytes` |
| Reset | `reset()` | `reset()` | `reset()` |

### Factory Functions

| C++ | Java | Python |
|-----|------|--------|
| `create_my_frame_session()` returns `unique_ptr<ISession>` | `new MyFrameSession()` | `MyFrameSession()` |

### Known Gaps

- **Java:** `encodeBatch()` is not yet implemented for array-payload sessions

See [Limitations & Design Boundaries](../conduit/limitations.md) for scope and design decisions.

## See Also

- [Sessions & Generated Code](../conduit/sessions-and-codegen.md) -- `ISession` interface and how generated sessions connect to the runtime
- [Transceiver](../conduit/transceiver.md) -- The runtime orchestrator that uses session classes for decode/encode dispatch
- [Naming Conventions](naming-conventions.md) -- How BMDL names map to session class and factory function names
- [Limitations & Design Boundaries](../conduit/limitations.md) -- Scope boundaries and design decisions
