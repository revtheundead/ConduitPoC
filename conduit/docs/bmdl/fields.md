# Fields

[Back to index](index.md)

Fields are the fundamental data elements in BMDL. Every piece of data on the wire is described by a `<field>`, a `<reserved>`, or an `<align>`.

## Basic Syntax

```xml
<!-- Using a named type -->
<field name="altitude" type="int16"/>

<!-- Using inline size (signed integer) -->
<field name="altitude" bits="16" signed="true"/>
```

Every `<field>` requires a `name` attribute and at least one of `type`, `bits`, or `bytes` to determine its wire type.

## Inline Size Specification

For one-off fields, specify size directly without defining a type:

```xml
<!-- Simple bit width (unsigned by default) -->
<field name="code" bits="12"/>

<!-- Signed -->
<field name="offset" bits="12" signed="true"/>

<!-- Combined bytes and bits: total = (bytes * 8) + bits -->
<field name="large-field" bytes="2" bits="3"/>  <!-- 19 bits -->
<field name="timestamp" bytes="6"/>              <!-- 48 bits -->
```

When `type` is omitted but `bits` and/or `bytes` is present, an anonymous integer type is inferred.

## All Field Attributes

| Attribute | Description | Example |
|-----------|-------------|---------|
| `name` | Identifier (required) | `"altitude"` |
| `type` | Type reference | `"uint16"`, `"Position"` |
| `bits` | Bit width for inline integers | `"12"` |
| `bytes` | Byte width for inline integers (combinable with `bits`) | `"2"` |
| `signed` | Signed integer (when using `bits`/`bytes`) | `"true"` |
| `length` | Byte length for `bytes`/`string` fields | `"8"` or `"*"` |
| `length-from` | Length from expression | `"payload-length"` |
| `length-prefix` | Self-describing length prefix | `"uint8"` |
| `length-includes-prefix` | Length value includes prefix size | `"true"` |
| `bit` | Bitmap bit position (bitmap structs only) | `"7"` |
| `present-when` | Condition for field presence | `"flags & 0x80"` |
| `default` | Default value when absent on decode (optional fields only) | `"0"` |
| `initial` | Construction default (non-optional fields only) | `"0"`, `"online"` |
| `inline` | Flatten struct/message fields into parent | `"true"` |
| `endian` | Byte order override | `"little"` |
| `format` | Display format hint | `"hex"`, `"octal"`, `"binary"` |
| `wire-encoding` | Wire encoding override | `"bcd"`, `"bnr-s"` |
| `encoding` | String character encoding | `"ascii"`, `"ia5"` |
| `padding` | String padding | `"space"` |
| `trim` | String trim side | `"right"` |
| `terminated` | String terminator | `"null"` |
| `max-length` | Max length for terminated strings | `"256"` |
| `char-bits` | Bits per character | `"6"` |
| `base` | Explicit base type for inline fields | `"float"`, `"int"`, `"string"`, `"bool"` |
| `auto` | Auto-managed strategy (frames/sessions) | `"id"`, `"length"`, `"length(field)"`, `"count(field)"`, `"config(key)"`, `"increment"`, `"timestamp"` |

## Inline Type Modifiers

Fields can carry inline type modifications:

```xml
<field name="altitude" bits="16" signed="true">
  <scale>25</scale>
  <unit>feet</unit>
</field>

<field name="mode3a" bits="12" format="octal">
  <constraint max="4095"/>
</field>

<field name="msg-type" bits="8">
  <enum>
    <value id="1" name="heartbeat"/>
    <value id="2" name="position"/>
  </enum>
</field>
```

Inline `<enum>`, `<flags>`, and `<scale>`/`<offset>` are mutually exclusive (same rule as on `<type>`).

### Inline Base Types

The `base` attribute allows defining a field's primitive type inline, without a separate named `<type>` definition:

```xml
<!-- Inline float32 / float64 -->
<field name="temperature" bits="32" base="float"/>
<field name="latitude" bits="64" base="float"/>

<!-- Inline signed integer (equivalent to signed="true") -->
<field name="offset" bits="16" base="int"/>

<!-- Inline string with encoding, padding, and trim -->
<field name="callsign" base="string" length="8" encoding="ia5" padding="space" trim="right"/>

<!-- Inline bool (defaults to 1 bit if bits omitted) -->
<field name="active" bits="1" base="bool"/>
```

**Supported base values:**

| Base | Description | Required attributes |
|------|-------------|-------------------|
| `float` | IEEE 754 float | `bits` (exactly 32 or 64) |
| `int` | Signed integer | `bits` |
| `uint` | Unsigned integer | `bits` |
| `string` | Character string | `length`, or `length-from`/`terminated` |
| `bool` | Boolean (1-bit unsigned) | `bits` (default 1) |

**Rules:**
- `base` and `type` are mutually exclusive -- `base` is for inline definitions only
- `base="float"` cannot combine with `signed`, `wire-encoding`, inline `<enum>`, or `<flags>`
- For raw byte data, use `bytes="N"` instead of `base="bytes"`

A field referencing a named type that already has `<scale>` or `<offset>` **cannot** redefine them.

## Presence and Optional Fields

Fields become optional through three mechanisms:

### Condition-Based Presence

```xml
<field name="has-altitude" type="bool"/>
<field name="altitude" type="int16" present-when="has-altitude"/>
```

On decode, `altitude` is only read when `has-altitude` evaluates to true. On encode, `altitude` is only written when present.

### Bitmap Presence

Inside a `presence="bitmap"` struct, fields use `bit` to tie presence to a bitmap bit:

```xml
<field name="callsign" bit="7" type="callsign"/>
```

See [Bitmap](bitmap.md) for details.

### FX Extension

Fields inside `<fx>` blocks are automatically optional. See [FX Blocks](fx-blocks.md).

### Semantics of Optional Fields

Optional fields support the following operations:

- **Presence check** -- whether the field has a value
- **Access** -- read the current value (error if absent and no default; returns default if one exists)
- **Set** -- assign a value and mark the field as present
- **Clear** -- remove the value and mark the field as absent

## Default Values

The `default` attribute provides a decode-time fallback for optional fields:

```xml
<field name="version" type="uint8" present-when="has-version" default="1"/>
```

On decode, if the field is absent:
- Presence check returns false
- Accessing the value returns `1` (the default)

On encode, absent fields are omitted from the wire regardless of their default value. The `default` attribute only affects decode-time behavior.

The `default` attribute applies to any optional field regardless of its presence mechanism (`present-when`, `bit`, or FX extension).

`default` accepts decimal literals, hex literals (`0x`), enum value names, named constants, or string literals.

## Construction Defaults (Initial Values)

The `initial` attribute sets the value a newly constructed object starts with:

```xml
<field name="status" type="device-status" initial="online"/>
<field name="cpu-load" type="uint8" initial="0">
  <constraint max="100"/>
</field>
```

**Rules:**
- When omitted, fields are zero-initialized (integers: 0, bools: false, strings: empty)
- `initial` is valid on non-optional fields with primitive or enum types
- Not valid on optional fields (use `default` instead), struct fields, array fields, or bytes fields
- `initial` and `default` are mutually exclusive on the same field
- Accepts decimal, hex, enum value names, named constants, or string literals
- Can be specified as an attribute (`initial="value"`) or as a child element (`<initial>value</initial>`). When both are present, the attribute takes priority.

## Inline Struct Fields

The `inline="true"` attribute flattens a referenced struct's fields into the parent scope:

```xml
<struct name="Header">
  <field name="sync" type="uint16"/>
  <field name="type" type="uint8"/>
  <field name="length" type="uint16"/>
</struct>

<message name="Frame">
  <field name="header" type="Header" inline="true"/>
  <!-- sync, type, length accessible directly on Frame -->
</message>
```

When inlined, the struct's fields behave as if they were declared directly in the parent. Expressions in sibling elements can reference inlined field names directly (e.g., `switch="type"`).

`inline` is only valid when the field's type is a struct or message. Inlining must not produce duplicate field names in the parent scope.

## Auto-Managed Fields

The `auto` attribute marks fields for automatic management by frames and sessions:

```xml
<!-- Frame fields (valid inside <frame>) -->
<field name="msg-type" type="uint8" auto="id"/>
<field name="length" type="uint16" auto="length"/>
<field name="length" type="uint16" auto="length - 3"/>
<field name="system-id" type="uint8" auto="config(system-id)"/>

<!-- Session fields (valid inside <message>) -->
<field name="sequence" type="uint8" auto="increment"/>
```

### Auto Expression Reference

| Expression | Context | Description |
|------------|---------|-------------|
| `auto="id"` | Frame | Marks the message ID field. Used for dispatch during decode and auto-set during encode. |
| `auto="length"` | Frame | Total frame length. Auto-computed and backpatched during encode. |
| `auto="length {op} N"` | Frame | Frame length with arithmetic (see below). |
| `auto="length(field)"` | Frame, Struct, Message | Byte length of a specific sibling field. Backpatched during encode. |
| `auto="length(field) {op} N"` | Frame, Struct, Message | Byte length of a sibling field with arithmetic modifier (literal operand). |
| `auto="length(field) {op} other"` | Struct, Message | Byte length with field operand (see below). |
| `auto="count(field)"` | Frame, Struct, Message | Element count of a sibling array field. Auto-computed during encode. Does not support arithmetic modifiers. |
| `auto="config(key)"` | Frame | Value from session configuration. |
| `auto="increment"` | Session | Auto-incrementing counter, wrapping at type maximum. |
| `auto="timestamp"` | Session | Milliseconds since Unix epoch (system clock), masked to field bit width. Unsigned integer only. |

### Length Arithmetic Modifiers

Length auto-expressions support arithmetic modifiers with the operators `+`, `-`, `*`, `/`, and `%`. The operand can be an integer literal or a sibling field name:

```xml
<!-- Integer literal operands -->
<field name="length" type="uint16" auto="length - 3"/>
<field name="length" type="uint16" auto="length * 2"/>
<field name="half-len" type="uint8" auto="length(data) / 2"/>

<!-- Field operands (struct/message only) -->
<field name="overhead" type="uint8"/>
<field name="adjusted-len" type="uint8" auto="length(data) - overhead"/>
```

The wire value is computed as: `computed_byte_length {op} operand`.

**Restrictions:**
- At frame level, only integer literal operands are allowed (field operands require decoded values not available during stream parsing).
- The `%` operator is not allowed at frame level (no inverse for frame length recovery).
- Division or modulo by zero is a validation error. Multiplication by zero is also rejected.
- Field operands must reference existing sibling fields in the same scope.
- `auto="length"` is not valid inside `<fx>` blocks (dynamic FX layout would corrupt backpatch offsets).
- `auto="id"` and `auto="config(key)"` are only valid inside `<frame>` definitions.

### Frame Auto Fields

- `auto="id"` -- Exactly one per frame. The message's `id` attribute value is written into this field during encode. The message `id` attribute values must be valid literals for this field's type.
- `auto="length"` -- At most one per frame. The total frame length is auto-computed and backpatched during encode. Supports arithmetic: `auto="length - 3"` encodes `total_frame_bytes - 3`, `auto="length * 2"` encodes `total_frame_bytes * 2`.
- `auto="length(payload)"` -- Like `auto="length"`, but computes the byte length of the payload only (excluding header and footer fields). In frame context, the field reference must be `payload`. Supports arithmetic modifiers with literal operands (e.g., `auto="length(payload) - 1"`).
- `auto="count(payload)"` -- For `<payload count="*"/>` (array payloads). Auto-computes the number of payload items during encode.
- `auto="config(key)"` -- Values provided via a Config struct at session creation. Useful for fields like system identifiers that are fixed for the lifetime of a session.

### Struct/Message Auto Fields

- `auto="count(field)"` -- Auto-computes the size of a sibling array during encode. The field reference must name a sibling array in the same struct/message. Example: `<field name="count" type="uint8" auto="count(items)"/>` followed by `<array name="items" ... count-from="count"/>`.
- `auto="length(field)"` -- Auto-computes the byte length of a sibling field during encode. Uses a zero-placeholder and backpatch approach. Supports arithmetic modifiers including field operands. Example: `<field name="len" type="uint8" auto="length(data) / 2"/>`.

### Session Auto Fields

- `auto="increment"` -- Only valid on unsigned integer fields. The session maintains an internal counter starting at 0, incrementing after each encode-wrap, wrapping at the type's maximum value.
- `auto="timestamp"` -- Only valid on unsigned integer fields. The session sets the field to milliseconds since Unix epoch using the system clock, masked to the field's bit width.

See [Sessions](sessions.md) for details.

## Reserved

`<reserved>` represents bits/bytes that exist on the wire but are not meaningful:

```xml
<reserved bits="4"/>
<reserved bytes="2"/>
<reserved bytes="1" bits="4"/>  <!-- 12 bits -->
```

- On decode: bits are consumed but discarded
- On encode: written as zeros
- No accessor is generated
- At least one of `bits` or `bytes` is required
- Valid anywhere `<field>` is valid

Inside a `presence="bitmap"` struct, `<reserved>` does not take a `bit` attribute. Unassigned bit positions simply remain unset in the bitmap.

## Alignment

`<align>` pads to a byte boundary:

```xml
<field name="type" type="uint8"/>
<align to="4"/>  <!-- Pad to next 4-byte boundary -->
<field name="value" type="uint32"/>
```

- The `to` attribute specifies the byte alignment boundary (must be a positive power of 2)
- On decode: bits are consumed to reach the boundary
- On encode: zero-padding is written to reach the boundary
- Valid anywhere `<field>` is valid

## Best Practices

- Use `<reserved>` for protocol-specified padding/unused bits -- it is self-documenting and ensures correct wire layout.
- Prefer named types over inline `bits`/`bytes` for fields that appear more than once.
- Use `initial` for enum fields that don't have a zero-valued member.

## Common Pitfalls

- `initial` and `default` are mutually exclusive AND serve different purposes: `initial` is the construction-time value (non-optional fields), `default` is the decode-time fallback (optional fields only).
- BMDL uses tight packing -- there is no implicit alignment between fields. A 3-bit field followed by a 16-bit field packs at bit offset 3, not byte-aligned. Use `<align to="1"/>` for explicit byte alignment.
- `inline="true"` flattens a struct's fields into the parent. If the inlined struct has a field with the same name as an existing parent field, it is a validation error.
- `bit` and `present-when` are mutually exclusive on the same element.
