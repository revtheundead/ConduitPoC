# Type Code Generation

[Back to index](index.md)

bgen generates `types.hpp` containing C++ representations for every `<type>` in the BMDL protocol. The mapping depends on the type's attributes (enum values, flags, scale/offset, constraints, string properties).

All generated C++ type names have hyphens replaced with underscores (e.g., BMDL `status-flags` becomes C++ `status_flags`). See [Naming Conventions](naming-conventions.md) for full rules.

## Decision Logic

For each type definition, bgen checks attributes in this priority order:

1. **Enum values present** -> `enum class` + free functions
2. **Flags present** -> wrapper class with bool getters/setters
3. **Scale or offset present** -> wrapper class with `value()`/`raw()` accessors
4. **String base** -> wrapper class with string-specific encode/decode
5. **Constraint present** -> wrapper class with validating setters
6. **Otherwise** -> simple `using` alias

## Simple Alias

Types without special attributes become `using` aliases:

```xml
<type name="uint16" base="uint" bits="16"/>
```

```cpp
using uint16 = uint16_t;
```

The C++ type is the smallest standard integer type that fits the bit width:

| Bits | Unsigned | Signed |
|------|----------|--------|
| 1-8 | `uint8_t` | `int8_t` |
| 9-16 | `uint16_t` | `int16_t` |
| 17-32 | `uint32_t` | `int32_t` |
| 33-64 | `uint64_t` | `int64_t` |

Float types with bit width ≤ 32 map to `float`; widths 33-64 map to `double`. Bool types map to `bool`.

## Enum Types

Types with `<enum>` children become `enum class` types with supporting functions:

```xml
<type name="MsgType" base="uint" bits="8">
  <enum>
    <value name="Heartbeat" id="1"/>
    <value name="Data" id="2"/>
  </enum>
</type>
```

Generated:

- **`enum class MsgType : uint8_t`** -- Strongly-typed enum with underlying storage type
- **`to_string(MsgType)`** -- Returns the BMDL name as a `string_view` (e.g., `"Heartbeat"`), or `"unknown"` for invalid values
- **`decode_MsgType(BitReader&)`** -- Reads bits, validates against known values, returns `Result<MsgType>`. Returns `UnknownEnumValue` error for invalid values.
- **`encode_MsgType(MsgType, BitWriter&)`** -- Validates and writes bits. Returns `EncodeConstraintViolation` error for invalid values.

Enum value names follow the [naming conventions](naming-conventions.md) -- hyphens become underscores.

## Flags Types

Types with `<flags>` children become wrapper classes:

```xml
<type name="StatusFlags" base="uint" bits="8">
  <flags>
    <flag name="active" bit="0"/>
    <flag name="error" bit="1"/>
    <flag name="ready" bit="7"/>
  </flags>
</type>
```

Generated class with:

- **`bool active() const`** / **`void set_active(bool)`** -- Per-flag bool accessors (one pair per flag)
- **`uint8_t raw() const`** / **`void set_raw(uint8_t)`** -- Raw value access
- **`operator==`** -- Default equality
- **`VoidResult encode(BitWriter&) const`** -- Write raw bits
- **`static Result<StatusFlags> decode(BitReader&)`** -- Read raw bits

## Scaled Types

Types with `scale` and/or `offset` attributes become wrapper classes:

```xml
<type name="Temperature" base="int" bits="16" scale="0.01" offset="-50"/>
```

Generated class with:

- **`static constexpr double SCALE`** / **`static constexpr double OFFSET`** -- Constants (only those that are present)
- **`double value() const`** -- Returns `raw * SCALE + OFFSET` (formula adapts to which constants are present)
- **`int16_t raw() const`** -- Returns raw stored value (signed/unsigned and width match the type definition)
- **`void set_value(double)`** -- Sets raw from `(v - OFFSET) / SCALE`
- **`void set_raw(int16_t)`** -- Sets raw directly
- **`operator==`** -- Default equality
- **`VoidResult encode(BitWriter&) const`** -- Write raw bits
- **`static Result<Temperature> decode(BitReader&)`** -- Read raw bits

If the type also has a constraint, `set_value()` and `set_raw()` return `VoidResult` and validate against the constraint before assignment (returning `EncodeConstraintViolation` on failure). The decode method also validates and returns `ConstraintViolation` on failure.

## Constrained Types

Types with a `<constraint>` (but no scale, enum, or flags) become wrapper classes:

```xml
<type name="Altitude" base="uint" bits="16">
  <constraint min="0" max="50000"/>
</type>
```

Generated class with:

- **`uint16_t value() const`** / **`uint16_t raw() const`** -- Read the stored value
- **`[[nodiscard]] VoidResult set_value(uint16_t)`** -- Validates and sets; returns `EncodeConstraintViolation` on failure
- **`[[nodiscard]] VoidResult set_raw(uint16_t)`** -- Same validation
- **`operator==`** -- Default equality
- **`VoidResult encode(BitWriter&) const`** -- Write bits
- **`static Result<Altitude> decode(BitReader&)`** -- Read and validate; returns `ConstraintViolation` on failure

Constraint checks:
- `equals="V"` -- value must equal V exactly
- `min="V"` -- value must be >= V (skipped for min=0 on unsigned types to avoid compiler warnings)
- `max="V"` -- value must be <= V

## String Types

String types produce wrapper classes. The behavior depends on the string attributes:

### Fixed-Length Strings

```xml
<type name="CallSign" base="string" length="8" padding="space" trim="right"/>
```

Generated class with:

- **`static constexpr size_t WIRE_SIZE`** -- Fixed wire size in bytes
- **`std::string value() const`** / **`void set_value(const std::string&)`**
- **`operator==`** -- Default equality
- **`VoidResult encode(BitWriter&) const`** -- Writes padded to length
- **`static Result<CallSign> decode(BitReader&)`** -- Reads and trims

Encoding conversion (IA5, EBCDIC) is applied automatically during encode/decode when the type specifies a non-ASCII encoding.

### Packed Character Strings

```xml
<type name="AircraftId" base="string" length="8" char-bits="6"/>
```

Generated class with additional constants:

- **`static constexpr size_t CHAR_COUNT`** -- Number of characters
- **`static constexpr int CHAR_BITS`** -- Bits per character
- **`static constexpr size_t WIRE_SIZE`** -- Total wire size in bytes (ceil(CHAR_COUNT * CHAR_BITS / 8))

Encode packs each character into `CHAR_BITS` bits. Decode unpacks and trims.

### Terminated Strings

```xml
<type name="TextLine" base="string" terminated="newline" max-length="256"/>
```

Generated class that reads until the terminator byte is found:

- Supported terminators: `null` (0x00), `newline` (0x0A), `crlf` (0x0D 0x0A)
- `max-length` limits the scan to prevent unbounded reads
- If `max-length` is specified, `set_value()` returns `VoidResult` and checks length

## Wire Encoding Effects

Wire encoding affects how bits are read/written. Type-level wrappers always use bit-level reads/writes (not byte-optimized) since they cannot know the alignment context of the caller.

| Encoding | Read Function | Write Function |
|----------|--------------|----------------|
| Default / CB2 / BNR | `read_bits` / `read_signed_bits` | `write_bits` / `write_signed_bits` |
| BCD | `read_bcd` | `write_bcd` |
| BCD_S | `read_bcd_signed` | `write_bcd_signed` |
| BNR_S | `read_sign_magnitude` | `write_sign_magnitude` |

CB2 and BNR use the same code path as Default (standard two's complement / unsigned binary).

## Endian Handling

Endianness is recorded per-type but only affects field-level encode/decode (in structs). Type-level wrappers use bit-level operations that are endian-agnostic. The endian attribute is used by the struct emitter when selecting between byte-optimized read/write functions (e.g., `read_u16(Endian::Big)` vs `read_u16(Endian::Little)`).

## All Generated Files

Types are emitted in the order they appear in the BMDL definition, all within a single `namespace`. See [Naming Conventions](naming-conventions.md) for how BMDL names map to C++ identifiers.
