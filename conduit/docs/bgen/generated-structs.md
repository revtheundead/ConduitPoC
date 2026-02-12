# Struct & Message Code Generation

[Back to index](index.md)

bgen generates `structs.hpp` and `messages.hpp` containing C++ classes for every `<struct>` and `<message>` in the BMDL protocol. Structs and messages use the same internal emission logic, with messages adding `TYPE_ID`, convenience methods, and (for v1) `wrap()` overloads.

In v2 (frame-based protocols), `messages.hpp` also contains the Frame class with `PayloadVariant`, `wrap()` overloads, and encode/decode with auto-length backpatching. See [Frame Class](#frame-class-v2) below.

## Class Structure

Both `structs.hpp` and `messages.hpp` begin with forward declarations of all classes they define, allowing circular references between types.

Every struct/message generates a C++ class with:

- **Public section**: Accessors, `WIRE_SIZE` (if fixed), `operator==`, `encode()`, `decode()`, `to_string()`, `validate()` (if deferred constraints exist)
- **Private section**: Member variables with trailing underscores

## Member Naming

| BMDL | C++ Member | C++ Getter | C++ Setter | C++ Mutable |
|------|-----------|------------|------------|-------------|
| `my-field` | `my_field_` | `my_field()` | `set_my_field()` | `mutable_my_field()` |

See [Naming Conventions](naming-conventions.md) for the full mapping rules.

## Plain Field Accessors

For a non-optional field `status` of type `uint8_t`:

- **`const uint8_t& status() const`** -- Returns a const reference to the value
- **`void set_status(const uint8_t& v)`** -- Sets the value
- **`uint8_t& mutable_status()`** -- Returns a mutable reference (escape hatch, bypasses validation)

### Constrained Field Accessors

If a field has an immediate constraint (`validate="immediate"`, the default):

- **`[[nodiscard]] VoidResult set_status(const uint8_t& v)`** -- Validates before setting; returns `EncodeConstraintViolation` on failure

If a field has a deferred constraint (`validate="deferred"`):

- **`void set_status(const uint8_t& v)`** -- Sets without validation (checked later by `validate()`)

### String/Bytes Length Validation

If a field has a `max-length` attribute, the setter validates length:

- **`[[nodiscard]] VoidResult set_payload(const std::string& v)`** -- Returns `StringTooLong` if `v.size()` exceeds the max length

This validation is combined with any constraint validation in a single setter.

## Optional Field Accessors

Fields that are conditional (bitmap bit, `present-when`, or inside an `<fx>` block) are stored as `std::optional<T>` and get these accessors:

- **`bool has_status() const`** -- Returns whether the value is present
- **`const T& status() const`** -- Returns `.value()` (throws if not present)
- **`T& mutable_status()`** -- Returns mutable reference. If the optional is empty, it is automatically emplaced (default-constructed) first, so this never throws `bad_optional_access`.
- **`void set_status(const T& v)`** -- Sets the value (makes it present)
- **`void clear_status()`** -- Resets the optional

If the field has a `default` attribute in the BMDL, the getter returns by value using `value_or()` instead of by reference, so it returns the default when the optional is empty.

## Wire Size

If all fields have fixed sizes, the class has:

```cpp
static constexpr size_t WIRE_SIZE = 42;
```

If any field is variable-length or optional, `WIRE_SIZE` is simply absent from the class.

## Equality and Debug

- **`bool operator==(const MyStruct&) const = default`** -- Default memberwise equality
- **`std::string to_string() const`** -- Returns a human-readable string of all fields for debugging

## Encode and Decode

### encode

```cpp
conduit::VoidResult encode(conduit::io::BitWriter& w) const;
```

Writes all fields in declaration order. For each field:
1. Evaluates constraint checks (immediate constraints)
2. Writes the field value using the appropriate method (bit-level or byte-level based on alignment)
3. Handles optional fields (skips if not present, writes presence indicators)

### decode

```cpp
static conduit::Result<MyStruct> decode(conduit::io::BitReader& r);
```

Reads all fields in declaration order into a new instance. Returns the decoded struct or an error. For context-aware case types, an additional overload is generated:

```cpp
static conduit::Result<MyStruct> decode(conduit::io::BitReader& r, const MyContextStruct& ctx);
```

This overload receives entry-point context fields for resolving expressions that reference parent fields.

### validate (Deferred Constraints)

If any field has `validate="deferred"`:

```cpp
conduit::VoidResult validate() const;
```

Checks all deferred constraints and returns the first violation found (error code `ConstraintViolationDeferred`).

## Byte Alignment Tracking

bgen tracks the cumulative bit offset modulo 8 during encode/decode emission. When the offset is known to be byte-aligned (mod 8 == 0), it uses byte-optimized functions (`read_u8`, `read_u16`, `write_u16`, etc.). When not byte-aligned (e.g., after a 13-bit field), it falls back to bit-level functions (`read_bits`, `write_bits`).

BMDL uses tight packing -- there is no implicit alignment between fields. Use `<align to="1"/>` for explicit byte alignment.

## Choices

BMDL `<choice>` elements generate `std::variant`-based types:

```xml
<choice name="payload" switch="msg_type">
  <case name="Heartbeat" type="HeartbeatMsg" value="1"/>
  <case name="Data" type="DataMsg" value="2"/>
  <otherwise name="Unknown" type="RawPayload"/>
</choice>
```

Generated:

- **`using payloadVariant = std::variant<HeartbeatMsg, DataMsg, RawPayload>;`** -- Type alias (variant name = `to_cpp_type_name(choice_name) + "Variant"`)
- **`const payloadVariant& payload() const`** -- Returns the variant
- **`void set_payload(const payloadVariant& v)`** -- Sets the variant
- **`payloadVariant& mutable_payload()`** -- Mutable variant access

There are no per-case convenience accessors. Use `std::holds_alternative<T>()` and `std::get<T>()` from `<variant>` to inspect and extract specific cases.

If the choice is optional (has a `bit` or `present-when` attribute), it gets optional accessors instead (`has_payload()`, `clear_payload()`, etc.).

Inline cases (cases with `<field>` children instead of a `type` attribute) generate synthetic child classes before the parent class.

## Arrays

BMDL `<array>` elements always generate `std::vector<Element>` members, regardless of whether the count is fixed or dynamic. Arrays get the same standard accessors as any other field:

- **`const std::vector<Element>& items() const`** -- Const reference to the vector
- **`void set_items(const std::vector<Element>& v)`** -- Replace the entire vector
- **`std::vector<Element>& mutable_items()`** -- Mutable reference for direct manipulation

Use `mutable_items()` to push, pop, index, or resize the vector directly.

For fixed-count arrays, encode iterates over exactly N elements; decode uses `.reserve(N)` before a fixed-count loop.

For dynamic arrays with `count-star`, decode reads elements until the reader is exhausted.

For primitive type references (simple integer types without wrappers), the element type resolves to the underlying C++ type (e.g., `std::vector<uint16_t>`) rather than the BMDL type alias name.

Inline array elements (arrays with `<field>` children instead of a `type` attribute) generate synthetic `<ArrayName>Element` classes.

## Bitmap Structs

Structs with `presence="bitmap"` use FSPEC-based encoding. Fields are assigned to bitmap bits and are all `std::optional`:

- **Without `ext`**: A single FSPEC byte is written. All fields must fit within bits 0-7.
- **With `ext`**: Multi-byte FSPEC. The FSPEC array size is `(max_bit / 8) + 1` based on the highest bit position used across all fields (not the `bits` attribute). The extension bit position (specified by `ext` on `<bitmap>`) is set in each FSPEC byte except the last, allowing the decoder to read as many FSPEC bytes as needed.
- Bit ordering: `fspec[byte] |= (1 << bit_within_byte)` -- bit 0 is LSB of first byte
- All bitmap fields use optional accessors (`has_X()`, `set_X()`, `clear_X()`, etc.)
- String fields within bitmaps are properly trimmed after decode
- Bitmap structs may also contain inline structs and choices assigned to bitmap bits

## FX Blocks

`<fx>` blocks encode extension chains where each byte's MSB indicates whether more data follows:

- All FX fields are `std::optional<T>`
- **Flat FX** (single extent): All-or-nothing semantics. Setting any field triggers encoding of all fields with `value_or(0)` defaults
- **Nested FX**: Per-extent continuation. Each nesting level is independently optional
- Bytes fields in FX blocks use `emplace()` + `std::copy()` for `std::array` types (no iterator constructor)

## Inline Fields

Fields with `inline="true"` are flattened into the parent class -- the referenced struct's fields appear directly as members of the parent, not wrapped in a sub-object.

## Messages

Messages extend structs with additional members:

### TYPE_ID, TYPE_NAME, and ID_VALUE

```cpp
static constexpr uint64_t TYPE_ID = 0x...ULL;    // FNV-1a hash of the BMDL name
static constexpr std::string_view TYPE_NAME = "Heartbeat";
```

Leaf struct types that appear in sessions also get `TYPE_ID` and `TYPE_NAME`.

In v2 frame-based protocols, messages with an `id` attribute additionally get:

```cpp
static constexpr uint8_t ID_VALUE = 1;  // type matches the frame's auto="id" field
```

The `ID_VALUE` type matches the frame's ID field C++ type (e.g., `uint8_t`, `uint16_t`). This constant is used by `Frame::wrap()` to auto-set the ID field and by `Frame::decode()` for dispatch.

### Convenience Methods

```cpp
conduit::Result<std::vector<uint8_t>> encode_bytes() const;
static conduit::Result<MyMessage> decode_bytes(std::span<const uint8_t> data, size_t max_bytes = 0);
```

`encode_bytes()` creates a `BitWriter`, encodes, and returns the byte vector.
`decode_bytes()` creates a `BitReader` and decodes. The optional `max_bytes` parameter rejects oversized input.

### wrap() Overloads (Entry-Point Messages)

Entry-point messages get `wrap()` static methods for each reachable leaf type:

```cpp
static MyFrame wrap(const HeartbeatMsg& leaf);
static MyFrame wrap(const DataMsg& leaf);
```

Each `wrap()` overload:
1. Creates a frame instance
2. Sets all constraint fields (discriminators, fixed values) along the access path
3. Sets the leaf payload into the correct choice variant
4. Sets length fields if the choice has `length-from`
5. Returns the fully populated frame

Known-valid constant setters use `(void)` cast to suppress `[[nodiscard]]` warnings.

## Context Structs

When entry-point analysis finds context fields (concrete fields from the entry-point available to inner decode methods), a context struct is generated in `structs.hpp` before any struct classes:

```cpp
struct FrameContext {
    uint8_t msg_type{};
    uint16_t length{};
};
```

The context struct name is `to_cpp_type_name(entry_point_name) + "Context"`. Field names use `to_accessor_name` (no trailing underscore), unlike regular struct members. All fields are value-initialized with `{}`.

Every leaf type reachable from the entry-point is mapped to this context struct. Case types that need context get an additional `decode` overload:

```cpp
static Result<CaseType> decode(BitReader& r, const FrameContext& ctx);
```

Context field types are qualified with `::namespace::TypeName` when the field name would shadow the type name (avoiding GCC `-Wchanges-meaning` errors).

## Frame Class (v2)

When a `<frame>` is present, bgen generates a Frame class in `messages.hpp` (after all message classes). The frame handles wire-level transport: ID dispatch, length backpatching, and message wrapping.

### Generated Class

For a frame named `SimpleFrame` with messages `Heartbeat` (id=1) and `Status` (id=2):

```cpp
class SimpleFrame {
public:
    using PayloadVariant = std::variant<Heartbeat, Status>;
    // When <payload count="*"/>: also generates a container type for multiple payloads:
    // using PayloadContainer = std::vector<PayloadVariant>;

    // Header field accessors
    uint8_t msg_type() const;
    void set_msg_type(uint8_t v);
    uint16_t length() const;
    void set_length(uint16_t v);

    // Payload accessor
    const PayloadVariant& payload() const;
    PayloadVariant& payload();

    // Create a frame wrapping a message (auto-sets ID field)
    static SimpleFrame wrap(const Heartbeat& msg);
    static SimpleFrame wrap(const Status& msg);

    // Encode with auto-length backpatching
    VoidResult encode(BitWriter& w) const;

    // Decode with ID-based switch dispatch
    static Result<SimpleFrame> decode(BitReader& r);

    // Convenience byte-level methods
    Result<std::vector<uint8_t>> encode_bytes() const;
    static Result<SimpleFrame> decode_bytes(std::span<const uint8_t> data);
};
```

### encode() Internals

1. Writes header fields sequentially
2. For `auto="length"`: saves byte position and writes zero placeholder
3. Writes payload via `std::visit`
4. Writes footer fields (if any)
5. Backpatches the length field using `w.patch_u8()`, `w.patch_u16()`, or `w.patch_u32()` (selected based on the length field's bit width) with the computed frame size

### decode() Internals

1. Reads header fields sequentially
2. Switches on the ID field value to dispatch to the correct message's `decode()`
3. Reads footer fields (if any)
4. Returns the frame with the decoded payload variant

### wrap() Overloads

Each `wrap()` overload creates a frame, sets the ID field to the message's `ID_VALUE`, and stores the message in the payload variant. Length is computed automatically during `encode()` via backpatching. Config fields (from `auto="config(key)"`) are set by the session during `encode_wrap()`, not by `wrap()` itself.

Note: v2 messages do **not** get v1-style `wrap()` overloads on the message class itself. Only the Frame class has `wrap()` overloads. Individual messages can still be encoded/decoded standalone via `encode_bytes()` / `decode_bytes()` (without the frame envelope).

## See Also

- [Naming Conventions](naming-conventions.md) -- BMDL-to-C++ name mapping rules used by all generated code
- [Error Handling](../conduit/error-handling.md) -- `Result<T>` and `VoidResult` returned by encode/decode methods
- [Bit I/O](../conduit/bit-io.md) -- `BitReader` and `BitWriter` used by generated encode/decode
