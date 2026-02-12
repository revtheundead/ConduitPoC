# Bitmap-Controlled Structs (FSPEC)

[Back to index](index.md)

Bitmap-controlled structs use a Field Specification (FSPEC) bitmap to control which data items are present on the wire. This pattern is central to ASTERIX and similar protocols.

## Syntax

```xml
<struct name="items" presence="bitmap">
  <bitmap bits="16" ext="0"/>

  <field name="data-source" bit="7" type="DataSourceId"/>
  <field name="time-of-day" bit="6" type="uint24"/>

  <struct name="position" bit="5">
    <field name="latitude" type="wgs84"/>
    <field name="longitude" type="wgs84"/>
  </struct>

  <field name="altitude" bit="4" type="int16"/>
  <field name="track-number" bit="3" type="uint16"/>
  <field name="status" bit="2" type="uint8"/>
  <field name="mode3a" bit="1" type="uint16"/>
  <!-- bit 0 is extension indicator -->

  <!-- Second octet (bits 8-15) -->
  <field name="callsign" bit="15" type="callsign"/>
  <field name="aircraft-address" bit="14" type="uint24"/>
  <field name="flight-level" bit="13" type="int16"/>
</struct>
```

## Bitmap Configuration

The `presence` attribute on `<struct>` accepts a single value: `"bitmap"`. When `presence` is omitted, all child fields are unconditionally present (unless they individually use `present-when`).

The `<bitmap>` element configures the FSPEC. It is optional and must be the first child if present.

| Attribute | Description | Default |
|-----------|-------------|---------|
| `bits` | Total number of addressable bit positions in the bitmap | `8` |
| `ext` | Extension bit position within each 8-bit octet (0-7) or `"none"` | `"none"` |

### Extension Mechanism

When `ext` is specified (e.g., `ext="0"`), that bit position is reserved as an extension indicator:
- **1** = another FSPEC octet follows
- **0** = no more FSPEC octets

With `ext="0"` (ASTERIX-style), bits 7-1 are available for data in each octet (7 usable bits per octet).

When `ext` is `"none"` (default), the bitmap is a single fixed-size octet with no extension. Maximum fields = `bits` value (default 8).

## Bit Numbering

The bitmap is a **byte array**, not a multi-byte integer. Each octet is numbered independently:

- Bits 0-7: First octet (bit 7 = MSB, bit 0 = LSB)
- Bits 8-15: Second octet (if extended)
- Bits 16-23: Third octet, and so on

Byte index = `bit / 8`, bit-within-byte = `bit % 8`. Byte 0 is the first byte on the wire. There is no endianness concern -- each byte stands alone.

For ASTERIX FSPEC with `ext="0"`: bit 0 is the extension indicator (LSB), bits 7-1 carry data in each octet. FRN 1 (the most important item) sits at the MSB of the first octet, which is `bit="7"`.

## FSPEC Array Sizing

The generated FSPEC array size is determined by the **highest assigned bit**, not by `<bitmap bits="N">`.

A 16-bit bitmap with fields only at bits 0-4 generates a **1-byte** FSPEC. A bitmap with a field at bit 15 generates a 2-byte FSPEC.

Formula: `fspec_size = (max_bit / 8) + 1`

## Wire Format

1. Read FSPEC octets (check extension bit to decide whether to continue)
2. For each set bit, decode the corresponding data item
3. Items appear in bit order (high to low within each octet)

```
[FSPEC octet 1: bits 7-0][FSPEC octet 2: bits 15-8]...
[data for highest set bit][data for next set bit]...
```

## Valid Child Elements

Inside a `presence="bitmap"` struct, data-carrying children use a `bit` attribute to tie their presence to a bitmap bit:
- `<field>` (with `bit`)
- `<struct>` (with `bit`, must be named)
- `<array>` (with `bit`)
- `<choice>` (with `bit`)

`<reserved>` is valid inside bitmap structs but does **not** take a `bit` attribute -- unassigned bit positions simply remain unset in the FSPEC. There is no need to use `<reserved>` to "fill" unused bit positions.

## Wire Order

Fields are encoded/decoded in **bit order** (high to low within each octet), not declaration order. Declaration order only affects the generated code structure. This is the one exception to BMDL's general "wire order = declaration order" rule.

## Semantics

Each bitmap-controlled item is optional:
- Setting a field sets its corresponding FSPEC bit
- Clearing a field clears the bit
- On decode, only items whose FSPEC bits are set are read
- On encode, only items that are present are written, and their FSPEC bits are set accordingly

## Validation Rules

- `bit` values must be unique within a bitmap struct (no two elements share the same bit)
- The extension bit position (if `ext` is set) cannot be used for data fields
- `bit` and `present-when` are mutually exclusive on the same element
- `<reserved>` does not take a `bit` attribute inside bitmap structs -- unassigned bit positions simply remain unset

## Best Practices

- Design bitmap structs with future extensibility in mind. Leave higher bit positions unassigned for later additions; the extension mechanism handles growing the FSPEC automatically.
- Use the extension mechanism (`ext="0"`) for protocols that may add new data items in future versions.
- Bit 7 (MSB) is typically used for the most important/frequently-present data item.

## Common Pitfalls

- The FSPEC array size is determined by the **highest assigned bit**, not by `<bitmap bits="N">`. A 16-bit bitmap with fields only at bits 0-4 generates a 1-byte FSPEC.
- The extension bit position (e.g., `ext="0"`) is reserved for the continuation indicator and **cannot** be used for data fields.
- `<reserved>` does not take a `bit` attribute in bitmap structs. Unassigned FSPEC bit positions simply remain unset -- there is no need to "fill" them with `<reserved>`.
- Bit numbering: bit 7 is MSB of the first octet, bit 0 is LSB. Bits 8-15 are the second octet, etc.
- `<fx>` cannot appear inside a bitmap struct. Bitmap and FX are separate presence mechanisms and cannot be combined.
