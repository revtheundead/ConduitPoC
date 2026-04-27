# Bitmap-Controlled Structs (FSPEC)

[Back to index](index.md)

Bitmap-controlled structs use a Field Specification (FSPEC) bitmap to control which data items are present on the wire. This pattern is central to ASTERIX and similar protocols.

## Syntax

```xml
<struct name="items" presence="bitmap">
  <bitmap bits="16" ext="7"/>

  <!-- First octet: bits 0..7 (wire order, MSB first within each byte) -->
  <field name="data-source" bit="0" type="DataSourceId"/>
  <field name="time-of-day" bit="1" type="uint24"/>

  <struct name="position" bit="2">
    <field name="latitude" type="wgs84"/>
    <field name="longitude" type="wgs84"/>
  </struct>

  <field name="altitude" bit="3" type="int16"/>
  <field name="track-number" bit="4" type="uint16"/>
  <field name="status" bit="5" type="uint8"/>
  <field name="mode3a" bit="6" type="uint16"/>
  <!-- bit 7 is the FX (extension) indicator -->

  <!-- Second octet: bits 8..15 -->
  <field name="callsign" bit="8" type="callsign"/>
  <field name="aircraft-address" bit="9" type="uint24"/>
  <field name="flight-level" bit="10" type="int16"/>
  <!-- ...
       bit 15 is the FX of the second octet
  -->
</struct>
```

## Bit Numbering

BMDL uses **wire-order bit numbering**: `bit="0"` is the first bit transmitted on the wire (the MSB of the first FSPEC byte), `bit="7"` is the LSB of the first FSPEC byte, `bit="8"` is the first bit of the second FSPEC byte, and so on. The numbers count up monotonically with transmission order — there is no jump at byte boundaries.

| Wire position (byte:bit MSB-first within byte) | BMDL `bit` |
|---|---|
| byte 0, bit 0 (MSB)  |  0 |
| byte 0, bit 1        |  1 |
| byte 0, bit 2        |  2 |
| byte 0, bit 3        |  3 |
| byte 0, bit 4        |  4 |
| byte 0, bit 5        |  5 |
| byte 0, bit 6        |  6 |
| byte 0, bit 7 (LSB)  |  7 |
| byte 1, bit 0 (MSB)  |  8 |
| byte 1, bit 7 (LSB)  | 15 |
| byte 2, bit 0 (MSB)  | 16 |
| byte 2, bit 7 (LSB)  | 23 |

The internal byte/in-byte split is `byte_idx = bit / 8`, `bit_in_byte_msb_first = bit % 8`. The encoded bit mask is `1 << (7 - bit_in_byte_msb_first)` — this is the same direction-of-shift as ASTERIX FSPEC.

### ASTERIX FRN to BMDL bit

ASTERIX numbers its Field Reference Numbers (FRN) starting from 1 and counts up across the wire, with FX bits also counted. The BMDL wire-order numbering matches ASTERIX FRN minus one for byte 0 entries; the offset grows by one for each FSPEC byte crossed because BMDL uses every wire position (data bits *and* FX) while ASTERIX numbers only the data FRNs:

| Wire position | ASTERIX FRN | BMDL `bit` |
|---|---|---|
| byte 0, MSB | FRN 1  |  0 |
| byte 0, MSB-1 | FRN 2  |  1 |
| ... | ... | ... |
| byte 0, LSB (FX) | — (FX) |  7 |
| byte 1, MSB | FRN 8  |  8 |
| byte 1, LSB (FX) | — (FX) | 15 |
| byte 2, MSB | FRN 15 | 16 |

## Bitmap Configuration

The `presence` attribute on `<struct>` accepts a single value: `"bitmap"`. When `presence` is omitted, all child fields are unconditionally present (unless they individually use `present-when`).

The `<bitmap>` element configures the FSPEC. It is optional and must be the first child if present.

| Attribute | Description | Default |
|-----------|-------------|---------|
| `bits` | Total number of addressable bit positions in the bitmap | `8` |
| `ext` | Extension bit position within each 8-bit octet (0-7, where `0` = MSB and `7` = LSB) or `"none"` | `"none"` |
| `endian` | FSPEC byte order: `"big"` (default — logical byte 0 sent first) or `"little"` (byte order reversed) | `"big"` |

### Extension Mechanism

When `ext` is specified, that bit position is reserved as an extension indicator in every FSPEC octet:
- **1** = another FSPEC octet follows
- **0** = no more FSPEC octets

For ASTERIX-style FSPECs the FX bit lives at the LSB of each byte, which is `ext="7"` under wire-order numbering. Bits 0..6 (within each byte) are then available for data — 7 usable bits per octet.

When `ext` is `"none"` (default), the bitmap is a single fixed-size octet with no extension. Maximum fields = `bits` value (default 8).

### Endianness

`<bitmap endian="…">` controls the byte order of the FSPEC on the wire and is also the default endianness for any multi-byte primitive bitmap fields that don't override it themselves. The two cases:

- **`endian="big"` (default)**: Logical byte 0 (the byte holding the lowest BMDL bit numbers, e.g. `bit="0"..bit="7"`) is transmitted first. ASTERIX uses this.
- **`endian="little"`**: The FSPEC byte array is reversed before write — the highest-numbered logical byte is sent first, the lowest last. Multi-byte primitive bitmap fields default to little-endian byte order for their values.

The `bit="N"` numbering is *independent* of endianness. `bit="0"` always means "logical byte 0, MSB". A schema can be flipped from big to little endian by changing only the `<bitmap endian="…">` (and inheriting field endians) — the `bit` numbers stay the same. Only the bytes on the wire reorder.

## FSPEC Array Sizing

The generated FSPEC array size is determined by the **highest assigned bit**, not by `<bitmap bits="N">`.

A 16-bit bitmap with fields only at bits 0-4 generates a **1-byte** FSPEC. A bitmap with a field at bit 15 generates a 2-byte FSPEC.

Formula: `fspec_size = (max_bit / 8) + 1`

(The `bits` attribute on `<bitmap>` is a sanity bound: `bit="N"` values must be `< bits`. The actual FSPEC size depends only on which bits are populated.)

## Wire Format

1. Read FSPEC octets. With `ext`: keep reading while the extension bit of the current octet is set. Without `ext`: read exactly `(max_bit / 8) + 1` octets.
2. If `endian="little"`, reverse the read FSPEC array.
3. For each set bit (in ascending BMDL `bit` order), decode the corresponding data item. The first encoded/decoded item is the lowest BMDL bit number that is present, which corresponds to the leftmost bit on the wire — the highest-priority field.

```
[FSPEC byte 0][FSPEC byte 1]...[data for bit N₁][data for bit N₂]...
                                 (N₁ < N₂ < ... — lowest BMDL bit number first)
```

## Valid Child Elements

Inside a `presence="bitmap"` struct, data-carrying children use a `bit` attribute to tie their presence to a bitmap bit:
- `<field>` (with `bit`)
- `<struct>` (with `bit`, must be named)
- `<array>` (with `bit`)
- `<choice>` (with `bit`)

`<reserved>` is valid inside bitmap structs but does **not** take a `bit` attribute — unassigned bit positions simply remain unset in the FSPEC. There is no need to use `<reserved>` to "fill" unused bit positions.

## Field Order on the Wire

Fields are encoded and decoded in **wire order**: byte ascending, then BMDL bit number ascending within each byte. Declaration order in the schema only affects the layout of the generated class and the order in which `to_string()` lists the fields.

This is one of two exceptions to BMDL's general "wire order = declaration order" rule (the other being FX-extended structs).

## Semantics

Each bitmap-controlled item is optional:
- Setting a field sets its corresponding FSPEC bit
- Clearing a field clears the bit
- On decode, only items whose FSPEC bits are set are read
- On encode, only items that are present are written, and their FSPEC bits are set accordingly

## Validation Rules

- `bit` values must be unique within a bitmap struct (no two elements share the same bit position).
- The extension bit position collides with any data element whose `bit % 8 == ext` — that is, the FX position is reserved in *every* FSPEC octet, not just the first.
- `bit` and `present-when` are mutually exclusive on the same element.
- `<reserved>` does not take a `bit` attribute inside bitmap structs.

## Best Practices

- Design bitmap structs with future extensibility in mind. Leave higher bit positions unassigned for later additions; the extension mechanism handles growing the FSPEC automatically.
- Use the extension mechanism (typically `ext="7"` for ASTERIX-style protocols) when you may add new data items in future versions.
- `bit="0"` (MSB of byte 0) is typically used for the most important / most frequently present data item. The lowest bit number declared is encoded first on the wire.

## Common Pitfalls

- The FSPEC array size is determined by the **highest assigned bit**, not by `<bitmap bits="N">`. A 16-bit bitmap with fields only at bits 0-4 generates a 1-byte FSPEC.
- The extension bit position (e.g., `ext="7"` for ASTERIX-style LSB FX) is reserved in *every* FSPEC octet — a field at `bit="7"`, `bit="15"`, `bit="23"`, … all collide with `ext="7"` and are rejected by the validator.
- `<reserved>` does not take a `bit` attribute in bitmap structs. Unassigned FSPEC bit positions simply remain unset — there is no need to "fill" them with `<reserved>`.
- `<fx>` cannot appear inside a bitmap struct. Bitmap and FX are separate presence mechanisms and cannot be combined.
- Switching `endian` flips both the FSPEC byte order *and* the default endianness of multi-byte primitive bitmap fields that don't override `endian` themselves. If you want a little-endian FSPEC but big-endian field values, set `endian="little"` on `<bitmap>` and `endian="big"` on each multi-byte `<field>` explicitly.
