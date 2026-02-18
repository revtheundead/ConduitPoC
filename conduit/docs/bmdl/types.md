# Type System

[Back to index](index.md)

BMDL's type system builds custom types from a small set of primitive bases. Types defined in `<types>` blocks are reusable across fields, arrays, and other constructs.

## Primitive Bases

| Primitive | Description | Size Control |
|-----------|-------------|--------------|
| `uint` | Unsigned integer | `bits` (1-64) |
| `int` | Signed integer (two's complement) | `bits` (1-64) |
| `float` | IEEE 754 floating point | `bits` (32 or 64 only) |
| `bool` | Boolean (always 1 bit) | -- |
| `bytes` | Raw byte sequence | `length` (on types); fields also support `length-from`, `length-prefix`, `length="*"` |
| `string` | Character sequence | `length`, `terminated` (on types); fields also support `length-from`, `length-prefix`, `length="*"` |

## Custom Type Definitions

```xml
<types>
  <!-- Simple aliases -->
  <type name="uint8" base="uint" bits="8"/>
  <type name="uint16" base="uint" bits="16"/>
  <type name="int32" base="int" bits="32"/>
  <type name="float32" base="float" bits="32"/>
  <type name="float64" base="float" bits="64"/>
</types>
```

### Type Attributes

| Attribute | Description | Valid On |
|-----------|-------------|----------|
| `name` | Type name (required) | All types |
| `base` | Primitive base (defaults to `uint` if omitted) | All types |
| `bits` | Bit width | `uint`, `int`, `float` |
| `length` | Byte length (fixed or `"*"`) | `bytes`, `string` |
| `endian` | Byte order (`big`, `little`) | `uint`, `int`, `float` |
| `format` | Display format (`decimal`, `hex`, `octal`, `binary`); defaults to `decimal` | `uint`, `int` |
| `wire-encoding` | Wire encoding (`cb2`, `bnr`, `bnr-s`, `bcd`, `bcd-s`) | `uint`, `int` |
| `encoding` | String character encoding | `string` |
| `padding` | String padding | `string` |
| `trim` | String trim side | `string` |
| `terminated` | String terminator | `string` |
| `max-length` | Max length for terminated strings | `string` |
| `char-bits` | Bits per character (1-8) | `string` |

## Enumerations

Enums define named values for an integer type:

```xml
<type name="message-type" base="uint" bits="8">
  <enum>
    <value id="0" name="unknown"/>
    <value id="1" name="heartbeat"/>
    <value id="2" name="position"/>
    <value id="3" name="status"/>
  </enum>
</type>
```

**Validation rules:**
- IDs are decimal or `0x`-prefixed hexadecimal
- IDs may have gaps (sparse enums are valid, e.g., 0, 1, 5, 10)
- IDs must be non-negative and unique within the enum
- Names must be unique within the enum
- When `id` is omitted, it auto-increments from 0 (or from the previous value's ID + 1)
- Decoding/encoding an unknown enum value (not matching any ID) produces an error

## Bit Flags

Flags define named bit positions within an integer type:

```xml
<type name="track-flags" base="uint" bits="8">
  <flags>
    <flag bit="0" name="valid"/>
    <flag bit="1" name="simulated"/>
    <flag bit="2" name="test-target"/>
    <flag bit="7" name="emergency"/>
  </flags>
</type>
```

**Validation rules:**
- Bit positions must be within range 0 to `bits - 1`
- Bit positions must be unique within the flags block
- Flag names must be unique within the flags block

## Scaled Types

Scaled types apply a linear transformation between wire (raw) and application (decoded) values:

```
decoded = (raw * scale) + offset
```

```xml
<!-- Scale only (offset defaults to 0) -->
<type name="wgs84" base="int" bits="32">
  <scale>8.381903171539307e-8</scale>
  <unit>degrees</unit>
</type>

<!-- Scale + offset -->
<type name="temperature-c" base="uint" bits="8">
  <scale>0.5</scale>
  <offset>-40</offset>
  <unit>celsius</unit>
</type>
```

- When only `<scale>` is present, offset defaults to 0
- When only `<offset>` is present, scale defaults to 1
- `<unit>` is documentation only (does not affect wire format)
- `<scale>`, `<offset>`, and `<unit>` are only valid on numeric types (`int`, `uint`, `float`)

Scaled types expose both a raw (wire) value and a decoded (application) value. The conversion is applied transparently during encode/decode.

## Constrained Types

Types can carry [constraints](constraints.md) that are inherited by all fields using the type:

```xml
<type name="protocol-version" base="uint" bits="8">
  <constraint min="1" max="10"/>
</type>
```

Fields using a constrained type inherit the constraint. A field can narrow the range but cannot relax it. See [Constraints](constraints.md) for full details.

## Attribute Inheritance

Types propagate their attributes to fields that reference them:

| Attribute | Inheritance |
|-----------|-------------|
| `endian` | Type → Field (field can override) |
| `format` | Type → Field (field can override) |
| `wire-encoding` | Type → Field (field can override) |
| `encoding` | Type → Field (field can override) |
| `padding` | Type → Field (field can override) |
| `trim` | Type → Field (field can override) |
| `scale` / `offset` | Type → Field (field **cannot** redefine if type already has them) |
| `constraint` | Type → Field (field can narrow, not relax) |

## Mutual Exclusivity

A type may contain **at most one** of:
- `<enum>` (enumeration values)
- `<flags>` (bit flags)
- `<scale>` / `<offset>` (linear scaling)

These are mutually exclusive. The same rule applies to inline type definitions on `<field>` elements.

## Best Practices

- Define reusable types for values that appear in multiple messages (e.g., `Altitude`, `Heading`, `CallSign`) rather than repeating `bits`/`scale` inline on each field.
- Prefer `<scale>` + `<offset>` on types over manual conversion in application code -- it keeps the conversion close to the wire format definition.
- For enums without a zero-valued member, use `default` on fields to set a valid starting value, otherwise the default-constructed value (0) may be invalid for the enum.

## Common Pitfalls

- `float` must be exactly 32 or 64 bits -- no other widths are valid.
- Enum, flags, and scale are mutually exclusive on a single type -- you can't have a scaled enum.
- A field referencing a type that already has `<scale>` or `<offset>` cannot redefine them. The generator reports a parse error if both the type and the field specify `<scale>` or `<offset>`.
- Explicitly specifying `wire-encoding="cb2"` on `int` or `wire-encoding="bnr"` on `uint` is a no-op (these are the defaults) but is accepted without error.
