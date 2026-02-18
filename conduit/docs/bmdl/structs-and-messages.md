# Structs & Messages

[Back to index](index.md)

Structs and messages are the primary containers for fields in BMDL. A **struct** is a reusable field group. A **message** is a top-level entry point with standalone encode/decode semantics.

## Struct Contexts

The behavior of `<struct>` depends on where it appears:

| Context | Behavior |
|---------|----------|
| Inside `<types>` | Defines a reusable type |
| Inside `<message>` | Named nested class |
| Inside `<struct>` | Named nested group |
| Inside `<case>` / `<otherwise>` | Named inline group |
| Inside `<array>` | Named element type |
| Inside `<fx>` | Named nested group |

All `<struct>` elements require a `name` attribute. Use `inline="true"` on the referencing `<field>` to flatten a struct's fields into the parent scope. See [Fields](fields.md#inline-struct-fields) for details.

### Presence Control

A `<struct>` may carry a `presence` attribute to control how child fields are included on the wire. The only valid value is `"bitmap"`, which enables bitmap-controlled (FSPEC) presence. See [Bitmap](bitmap.md) for details.

When `presence` is omitted (the default), all child fields are unconditionally present unless they individually use `present-when`.

## Reusable Struct (in `<types>`)

```xml
<types>
  <struct name="DataSourceId">
    <doc>SAC/SIC pair</doc>
    <field name="sac" type="uint8"/>
    <field name="sic" type="uint8"/>
  </struct>
</types>
```

A struct in `<types>` defines a reusable type that can be referenced by fields, arrays, and other constructs. It supports encode to / decode from a bit stream, but does not support standalone byte-level serialization.

## Named Nested Struct

```xml
<message name="TrackUpdate">
  <struct name="header">
    <field name="version" type="uint8"/>
    <field name="flags" type="uint8"/>
  </struct>

  <struct name="position">
    <field name="lat" type="wgs84"/>
    <field name="lon" type="wgs84"/>
  </struct>
</message>
```

Named nested structs produce a nested type within the parent. Fields of the parent access the nested struct as a single compound value.

## Grouping Fields with Conditional Presence

To conditionally include a group of fields, use a named `<struct>` with `present-when`:

```xml
<message name="Example">
  <field name="version" type="uint8"/>
  <struct name="v2-data" present-when="version >= 2">
    <field name="new-field-1" type="uint16"/>
    <field name="new-field-2" type="uint16"/>
  </struct>
</message>
```

The fields `new-field-1` and `new-field-2` are accessed through the `v2_data` accessor. The nested struct is conditionally present based on the `present-when` expression.

> **Note:** All `<struct>` elements require a `name` attribute. Anonymous (unnamed) structs are not supported.

## Conditional Struct

```xml
<field name="flags" type="uint8"/>
<struct name="extended-data" present-when="flags & 0x80">
  <field name="extra1" type="uint32"/>
  <field name="extra2" type="uint32"/>
</struct>
```

Named conditional structs are optional. On decode, the struct is only read when the condition evaluates to true. On encode, the struct is only written when present.

## Messages

Messages are top-level entry points with standalone serialization semantics. Unlike standalone structs (which encode/decode within a bit stream), messages support encoding directly to bytes and decoding from bytes.

### Messages with Frame

When a `<frame>` is present, every message requires an `id` attribute and optionally a `direction` attribute:

```xml
<frame name="MyFrame">
  <field name="msg-type" type="uint8" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
</frame>

<messages>
  <message id="1" name="Heartbeat">
    <field name="timestamp" type="uint32"/>
  </message>
  <message id="2" name="Status">
    <field name="code" type="uint8"/>
  </message>
</messages>
```

#### Message ID

The `id` attribute is **required** on all messages when a frame exists. The frame's `auto="id"` field defines the ID format (type, size). All message IDs must be valid literals for that type.

**ID uniqueness rules:**
- Same `id` + same `direction` (or both `direction="both"`) = validation error
- Same `id` + different explicit directions (`send` vs `receive`) = OK
- When two messages share an `id`, both must have explicit `direction` attributes

**Examples:**
```xml
<!-- Different IDs (standard case) -->
<message id="1" name="Heartbeat">...</message>
<message id="2" name="Status">...</message>

<!-- Same ID, different directions (asymmetric protocol) -->
<message id="7" name="Cat007Downlink" direction="receive">...</message>
<message id="7" name="Cat007Uplink" direction="send">...</message>
```

#### Message Direction

| Value | Decode | Encode |
|-------|--------|--------|
| `both` (default) | Yes | Yes |
| `receive` | Yes | Warning logged on encode |
| `send` | Skipped in decode switch | Yes |

#### Generated Constants

Each message class includes:
- `TYPE_ID` -- 64-bit FNV-1a hash of the message name (for session dispatch)
- `TYPE_NAME` -- string view of the message name
- `ID_VALUE` -- the `id` attribute value, typed to match the frame's `auto="id"` field

Messages can also be referenced as types in fields, arrays, and other constructs, just like structs.

## Inline Fields

The `inline="true"` attribute on a field flattens the referenced struct's fields into the parent:

```xml
<struct name="Header">
  <field name="sync" type="uint16"/>
  <field name="type" type="uint8"/>
  <field name="length" type="uint16"/>
</struct>

<message name="Frame">
  <field name="header" type="Header" inline="true"/>
  <!-- sync, type, length directly accessible on Frame -->
</message>
```

Inlining must not produce duplicate field names. The generator reports an error if an inlined struct introduces a name that already exists in the parent scope.

## Wire Size

Whether a type has a fixed or dynamic wire size is determined from its fields:

| Situation | Result |
|-----------|--------|
| All fields have fixed sizes | Wire size is a compile-time constant |
| Any field is variable-size | Wire size is computed at runtime |

Variable-size situations include: variable-length strings, variable-count arrays, optional fields, choices with different-sized cases, and FX extension blocks.

## Best Practices

- Keep structs small and focused. Extract reusable sub-structures into `<types>` for clarity.
- Use `inline="true"` on a field referencing a named struct to flatten its fields into the parent scope -- useful for logical grouping without creating a nested type in the API.

## Common Pitfalls

- Only `<message>` elements produce standalone byte-level encode/decode. Structs defined in `<types>` encode/decode within a bit stream only.
- All structs require a `name` attribute. Use `inline="true"` on the referencing field to flatten fields into the parent. If the inlined struct has a field with the same name as an existing parent field, the generator reports an error.
