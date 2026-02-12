# Session Code Generation

[Back to index](index.md)

bgen generates `sessions.hpp` containing session classes that implement `conduit::traits::ISession`. There are two code paths:

## Frame-based Sessions (v2)

When a `<frame>` is present, bgen generates a session class based on the frame:

```
class SimpleFrameSession : public conduit::traits::ISession { ... };

inline std::unique_ptr<conduit::traits::ISession> create_simple_frame_session();
```

The session class name is formed by `to_cpp_type_name(frame_name) + "Session"`. The factory function name uses `to_lower_snake_case`.

### decode_frame (v2)

The v2 session calls `FrameClass::decode_bytes(data)` to decode the entire frame, then uses `std::visit` on the payload variant to create `DecodedMessage` structs for each message type. This is much simpler than the v1 trie walk because the frame handles all dispatch internally.

### encode_wrap (v2)

Switches on `type_id`, `std::any_cast`s the payload to the correct message type, calls `FrameClass::wrap(msg)` to create the frame, sets any `auto="config(key)"` fields from the Config struct, sets any `auto="increment"` fields, and calls `frame.encode_bytes()`.

### Config Parameter

If the frame has `auto="config(key)"` fields, the session class contains a nested `Config` struct and requires it in the constructor:

```cpp
SimpleFrameSession::Config config;
config.system_id = 42;
auto session = create_simple_frame_session(config);
```

If there are no `auto="config(key)"` fields, no `Config` struct is generated, and the constructor/factory take no parameters.

## Entry-point Sessions (v1)

For each entry-point message, bgen generates:

```
class FrameSession : public conduit::traits::ISession { ... };

inline std::unique_ptr<conduit::traits::ISession> create_frame_session();
```

The session class name is formed by `to_cpp_type_name(entry_point_name) + "Session"`. Since `to_cpp_type_name` only replaces hyphens with underscores (preserving original casing), a BMDL entry-point named `Frame` produces `FrameSession`, while `data-frame` produces `data_frameSession`. The factory function name uses `to_lower_snake_case`, so `Frame` becomes `create_frame_session()` and `MyFrame` becomes `create_my_frame_session()`.

## ISession Interface Methods

### decode_frame

```cpp
[[nodiscard]] conduit::Result<std::vector<conduit::traits::DecodedMessage>>
decode_frame(std::span<const uint8_t> data) override;
```

Decodes a raw byte buffer into the entry-point type, then walks the decoded structure to extract all leaf types. Returns a vector of `DecodedMessage` structs, each containing:

- `type_id` (`uint64_t`) -- FNV-1a hash of the leaf type name
- `type_name` (`std::string_view`) -- The BMDL name (e.g., `"Cat048Record"`)
- `payload` (`std::any`) -- The decoded leaf value, holding the concrete C++ type (use `std::any_cast` to extract)

The `DecodedMessage` struct also has a `raw` (`std::vector<uint8_t>`) field for raw bytes forwarding, but the generated `decode_frame` does not populate it -- that is left to the application or transceiver layer.

The walk logic uses a trie structure to efficiently share common access path prefixes. For each leaf type, the session navigates through struct fields, choice variants (`std::holds_alternative` + `std::get`), and array iterations to reach the leaf value.

#### Array Dispatch

When the entry-point structure reaches arrays inside inline `<case>` blocks, the `dispatch` attribute on `<array>` controls the leaf type and delivery behavior.

**Batch (default):** With `dispatch="batch"` or no `dispatch` attribute, the case wrapper type is the leaf. `decode_frame()` emits **one `DecodedMessage`** per case match, with the wrapper holding the full array via `.items()`:

```
AsterixFrame
  └─ blocks[0]: DataBlock (cat=7, len=...)
       └─ records: cat007_downlink (dispatch="batch")
            └─ cat007_downlink wrapper  →  DecodedMessage #1
                 └─ .items() = [Record, Record, Record, Record, Record]
```

```cpp
handler.on<asterix::cat007_downlink>([](const auto& wrapper) {
    // Called once — wrapper.items() gives the full record vector
});
```

**Per-record:** With `dispatch="per-record"`, the array element type is the leaf. `decode_frame()` iterates the array and emits **one `DecodedMessage` per element**:

```
AsterixFrame
  └─ blocks[0]: DataBlock (cat=7, len=...)
       └─ records: cat007_downlink (dispatch="per-record")
            └─ items[0]: Cat007DownlinkRecord  →  DecodedMessage #1
            └─ items[1]: Cat007DownlinkRecord  →  DecodedMessage #2
            └─ items[2]: Cat007DownlinkRecord  →  DecodedMessage #3
```

```cpp
handler.on<asterix::Cat007DownlinkRecord>([](const auto& rec) {
    // Called once per record — array boundary is lost
});
```

See [Array Dispatch](../bmdl/choices.md#array-dispatch) for the BMDL syntax and [Per-Record vs Batch Delivery](../conduit/sessions-and-codegen.md#per-record-vs-batch-delivery) for runtime details.

Direction-constrained types generate a log warning when decoded against their declared direction (e.g., decoding a send-only message).

### encode_wrap

```cpp
[[nodiscard]] conduit::Result<std::vector<uint8_t>>
encode_wrap(uint64_t type_id, const std::any& payload) override;
```

Takes a `type_id` and an `std::any`-wrapped leaf payload, wraps it into the entry-point frame structure, encodes, and returns raw bytes.

For each known leaf type:
1. Matches on `type_id`
2. Extracts the concrete type from `std::any` via `std::any_cast`
3. Calls `EntryPoint::wrap(leaf)` to build the frame
4. Sets auto-increment fields on the frame (session-stateful)
5. Encodes the frame

Returns `UnknownTypeId` for unrecognized type IDs, and `InvalidArgument` if the `std::any` payload doesn't match the expected type.

Direction-constrained types generate a log warning when encoding against their declared direction (e.g., encoding a receive-only message).

### sync_pattern

```cpp
[[nodiscard]] std::span<const uint8_t> sync_pattern() const override;
```

Returns the byte pattern used for stream synchronization. The sync pattern is derived from constraint-equals values on the leading fixed fields of the entry-point (e.g., a sync word constant). Returns an empty span if no sync pattern exists.

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

Returns a span of all leaf type IDs reachable from this entry-point. The IDs are stored as a `static constexpr uint64_t[]` array with comments showing each type's BMDL name.

### type_name

```cpp
[[nodiscard]] std::string_view type_name(uint64_t type_id) const override;
```

Returns the BMDL name for a given type ID, or `"unknown"` if not found. Implemented as a `switch` statement over all known type IDs.

### reset

```cpp
void reset() override;
```

Resets session state. If any leaf type has auto-increment fields, this resets the sequence counter to 0.

## Auto-Increment Counter

Sessions track a single sequence counter for auto-increment fields (`auto="increment"` in BMDL). When encoding via `encode_wrap`:

1. The frame is built via `wrap()`
2. Each auto-increment field is set to `sequence_counter_++ & mask`, where the mask is derived from the field's bit width
3. The counter increments after each encode

The counter type is determined by the maximum bit width across all auto-increment fields in all leaf types (using the smallest C++ unsigned integer type that fits).

A public `sequence_counter()` accessor is provided for reading the current counter value.

## Factory Function

```cpp
inline std::unique_ptr<conduit::traits::ISession> create_frame_session() {
    return std::make_unique<FrameSession>();
}
```

The factory function name converts the entry-point name from PascalCase/kebab-case to snake_case: `MyFrame` becomes `create_my_frame_session()`.

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

BMDL choice cases can specify `direction="send"` or `direction="receive"`:

- **Send-only types**: Included in `leaf_type_ids()`, `encode_wrap()`, and `decode_frame()`. A warning is logged when decoded.
- **Receive-only types**: Included in `leaf_type_ids()`, `decode_frame()`, and `encode_wrap()`. A warning is logged when encoded.
- **Both (default)**: No warnings, fully bidirectional.

All leaf types participate in all session methods regardless of direction. Direction only controls whether a warning is logged for opposite-direction usage. The logger include is only added when direction-constrained types exist.

### Direction-Qualified Overlapping Cases

When two cases share the same discriminator value with complementary directions (`send` vs. `receive`), the generated struct-level `decode()` skips the send-only case and routes to the receive variant. This happens at the choice decode level, not the session level -- the session trie then naturally finds only the receive type when walking the decoded structure.

Non-colliding send-only cases (those with a unique discriminator value) are still emitted in the decode path normally. The skip only applies when a receive or both case exists for the same resolved integer value.

Both variants remain in the C++ variant type and in `encode_wrap()`, so encoding either direction is always possible.

## Protocol Descriptor Integration

The `ProtocolDescriptor` in `protocol.hpp` provides a `create_session()` method that delegates to the first entry-point's factory function. It also aggregates all leaf types across all sessions into a unified type registry with optional group annotations.

## See Also

- [Sessions & Generated Code](../conduit/sessions-and-codegen.md) -- `ISession` interface and how generated sessions connect to the runtime
- [Transceiver](../conduit/transceiver.md) -- The runtime orchestrator that uses session classes for decode/encode dispatch
- [Naming Conventions](naming-conventions.md) -- How BMDL names map to session class and factory function names
