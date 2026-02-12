# Document Structure

[Back to index](index.md)

Every BMDL file is an XML document rooted at `<bmdl>`. There are three flavors: **v2 protocol files** (flat, with `<frame>`), **v1 protocol files** (with a `<protocol>` wrapper), and **library files** (shared type definitions).

## Root Element

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="2.0">
  ...
</bmdl>
```

The `version` attribute is required. The current schema version is `"2.0"`.

## v2 Protocol Definition (Recommended)

A v2 protocol file uses a flat structure with a `<frame>` element for wire-level transport:

```xml
<bmdl version="2.0">
  <defaults>
    <endian>big</endian>
    <namespace>my-protocol</namespace>
  </defaults>

  <types>...</types>

  <frame name="MyFrame">
    <field name="msg-type" type="uint8" auto="id"/>
    <field name="length" type="uint16" auto="length"/>
    <payload/>
  </frame>

  <messages>
    <message id="1" name="Heartbeat">...</message>
    <message id="2" name="Status">...</message>
  </messages>
</bmdl>
```

The `<namespace>` element in `<defaults>` sets the C++ namespace. All `<message>` elements require an `id` attribute when a `<frame>` is present. See [Sessions](sessions.md) for frame details.

## v1 Protocol Definition (Legacy)

A v1 protocol file contains a single `<protocol>` element:

```xml
<bmdl version="1.0">
  <protocol name="my-protocol" version="1.0">
    <doc>Human-readable protocol description.</doc>

    <defaults>...</defaults>

    <constants>...</constants>
    <types>...</types>
    <messages>...</messages>
  </protocol>
</bmdl>
```

Both `name` and `version` are required on `<protocol>`. In v1, message dispatch is handled via `<choice>` elements inside entry-point messages.

### Flexible Ordering

A protocol may contain multiple `<constants>`, `<types>`, and `<messages>` blocks in any order. The generator merges all blocks before processing. Types are always generated before messages regardless of declaration order.

`<types>` blocks can contain both `<type>` and `<struct>` elements. This allows grouping helper structs alongside their related types for organizational clarity:

```xml
<protocol name="my-protocol" version="1.0">
  <types>
    <!-- types and helper structs -->
    <type name="uint8" base="uint" bits="8"/>
    <struct name="Position">
      <field name="x" type="uint8"/>
      <field name="y" type="uint8"/>
    </struct>
  </types>

  <messages><!-- first set --></messages>

  <!-- Second batch for organizational clarity -->
  <types><!-- more types --></types>
  <messages><!-- more messages --></messages>
</protocol>
```

## Defaults

The `<defaults>` block sets protocol-wide defaults for byte order, string handling, and namespace:

```xml
<defaults>
  <endian>big</endian>
  <namespace>my-protocol</namespace>
  <string-encoding>ascii</string-encoding>
  <string-padding>null</string-padding>
  <string-trim>right</string-trim>
</defaults>
```

| Setting | Values | Built-in Default | Description |
|---------|--------|------------------|-------------|
| `endian` | `big`, `little` | `big` | Byte order for multi-byte integers |
| `namespace` | valid C++ identifier | protocol name | C++ namespace for generated code |
| `string-encoding` | `ascii`, `utf8`, `ia5`, `ebcdic` | `ascii` | Character encoding for strings |
| `string-padding` | `null`, `space`, `none` | `null` | How fixed-length strings are padded on encode |
| `string-trim` | `left`, `right`, `both`, `none` | `right` | Which end to trim padding on decode |

Defaults are inherited by types and fields unless explicitly overridden at the type or field level.

## Documentation Element

The `<doc>` element is valid as a child of any element. It is purely for documentation and is ignored during code generation.

```xml
<message name="Heartbeat">
  <doc>Periodic keepalive message sent every 5 seconds.</doc>
  <field name="timestamp" type="uint32">
    <doc>Unix epoch seconds since 1970-01-01.</doc>
  </field>
</message>
```

## Annotations

The `<annotation>` element attaches custom metadata (key-value pairs) to named constructs:

```xml
<!-- Text content form -->
<field name="track-id" type="uint16">
  <annotation name="display-name">Track Identifier</annotation>
  <annotation name="json-name">trackId</annotation>
</field>

<!-- Attribute form (equivalent) -->
<field name="track-id" type="uint16">
  <annotation name="display-name" value="Track Identifier"/>
</field>
```

`<annotation>` is valid on: `<field>`, `<type>`, `<struct>`, `<message>`, `<array>`, `<choice>`, `<case>`, `<otherwise>`, and `<const>`.

It is **not** valid on: `<reserved>`, `<align>`, `<bitmap>`, `<fx>`, or `<import>`.

Annotations are passed through to generated code as metadata. The `name` attribute is required.

## Library Files

A BMDL file may omit the `<protocol>` wrapper and contain `<constants>`, `<types>`, and/or `<messages>` blocks directly under `<bmdl>`. This is the library file format, useful for shared type definitions.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="1.0">
  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
  </types>
</bmdl>
```

Library files are consumed via `<import>`. See [Imports](imports.md) for details.

### Protocol Requirement

When the generator resolves all files (the root file and all transitive imports), exactly one file must serve as the protocol entry point. In v1, this requires a `<protocol>` wrapper. In v2, a `<frame>` element at the root level of a `<bmdl>` file also satisfies this requirement without a `<protocol>` wrapper. The generator reports an error if zero or multiple protocol entry points are found.

## Element Reference

| Element | Purpose | Required Attributes |
|---------|---------|---------------------|
| `<bmdl>` | Document root | `version` |
| `<protocol>` | Protocol definition | `name`, `version` |
| `<defaults>` | Protocol-wide defaults | -- |
| `<doc>` | Documentation (ignored by generator) | -- |
| `<annotation>` | Custom metadata | `name` |

## Best Practices

- Always set `<defaults>` explicitly -- relying on built-in defaults makes the protocol harder to understand for readers who don't know the BMDL defaults.
- Use `<doc>` liberally. It's free (ignored by the generator) and helps future maintainers understand the protocol intent.
- Organize types and messages into logical groups using multiple `<types>` / `<messages>` blocks.

## Common Pitfalls

- Library files do not support a `<defaults>` block themselves, but the **importing protocol's** `<defaults>` are applied to library types during the build phase. This means a library type's wire behavior (endianness, string encoding, etc.) depends on who imports it. Use explicit attributes on individual `<type>` and `<field>` elements when consistent behavior is needed regardless of importer. See [Imports](imports.md) for details.
- The `version` attribute on `<bmdl>` is the BMDL language version (e.g. `"1.0"` or `"2.0"`), not the protocol version. The protocol version goes on `<protocol version="...">`.
