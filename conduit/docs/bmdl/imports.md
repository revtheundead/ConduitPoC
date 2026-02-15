# Import System

[Back to index](index.md)

BMDL files can import types, structs, messages, and constants from other BMDL files using the `<import>` element.

## Syntax

```xml
<import href="bmdl-stdlib.xml"/>
<import href="asterix/common/types.bmdl.xml" ns="asterix"/>
```

| Attribute | Description | Required |
|-----------|-------------|----------|
| `href` | Path to the BMDL file | Yes |
| `ns` | Prefix for imported types | No |

## Placement

`<import>` is valid as a direct child of `<bmdl>`:

```xml
<!-- In a protocol file -->
<bmdl version="2.0">
  <defaults><namespace>my-protocol</namespace></defaults>
  <import href="stdlib.bmdl.xml"/>
  <import href="common/types.bmdl.xml" ns="common"/>
  ...
</bmdl>

<!-- In a library file -->
<bmdl version="2.0">
  <import href="stdlib.bmdl.xml"/>
  <types>...</types>
</bmdl>
```

## Namespaced Imports

When `ns` is specified, imported types must be prefixed:

```xml
<import href="asterix/common/types.bmdl.xml" ns="asterix"/>

<message name="MyMessage">
  <field name="source" type="asterix:DataSourceId"/>
</message>
```

## Namespace-Free Imports

When `ns` is omitted, imported types are merged into the current namespace and can be referenced directly:

```xml
<import href="bmdl-stdlib.xml"/>

<message name="MyMessage">
  <field name="value" type="uint16"/>  <!-- From stdlib -->
</message>
```

If two namespace-free imports define the same type name, the generator reports a name collision error. Namespaced imports never collide since they require distinct prefixes.

## Path Resolution

- Paths are resolved relative to the importing file's directory
- Both `/` and `\` are accepted as path separators (normalized internally)
- Directory traversal with `..` is allowed

```
protocols/
├── stdlib.bmdl.xml
├── asterix/
│   ├── common/
│   │   └── types.bmdl.xml
│   └── asterix.bmdl.xml      (imports ../stdlib.bmdl.xml)
└── custom/
    └── my-protocol.bmdl.xml  (imports ../asterix/common/types.bmdl.xml)
```

## What Gets Imported

All `<constants>`, `<types>`, and `<messages>` from the imported file are available to the importing protocol. Nested imports are resolved transitively.

## Library Files

A BMDL file without `<defaults>` is a library file, containing definition blocks directly under `<bmdl>`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="2.0">
  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
  </types>
</bmdl>
```

Library files are useful for shared type definitions (e.g., a standard library of common integer types).

## Defaults Propagation

Library files do not support their own `<defaults>` block. At parse time, library files receive built-in defaults (big endian, ascii, null padding, right trim). However, during the build phase the **importing protocol's** `<defaults>` are applied to all library definitions (types, structs, messages, frames).

This means a library type's wire behavior can vary depending on who imports it. For example, a library defining a string type will use the importer's `string-encoding` default. If consistent behavior is required regardless of importer, specify attributes explicitly on the type or field (e.g., `endian="big"`, `encoding="ascii"`).

## Transitive and Diamond Imports

- **Transitive:** If A imports B and B imports C, then C's types are available to A.
- **Diamond:** If A imports B and C, and both B and C import D, then D is processed exactly once.

Each file is processed at most once, identified by its resolved absolute path. Circular imports are resolved safely by the same deduplication rule.

## Protocol Requirement

When the generator resolves all files, exactly one file must have a `<defaults>` block, marking it as the protocol entry point. The generator reports distinct errors for:
- No protocol file found -- expected `<defaults>` or `<frame>` in at least one file
- Multiple protocol files found -- only one file may define `<defaults>` or `<frame>`

## Example Project Structure

```
protocols/
├── stdlib.bmdl.xml                (uint8, uint16, float32, etc.)
├── asterix/
│   ├── common/
│   │   └── types.bmdl.xml        (DataSourceId, wgs84, callsign)
│   ├── categories/
│   │   ├── cat001.bmdl.xml
│   │   ├── cat048.bmdl.xml
│   │   └── cat253.bmdl.xml
│   ├── frame.bmdl.xml            (DataBlock, AsterixFrame)
│   └── asterix.bmdl.xml          (root - imports everything)
└── custom/
    └── my-protocol.bmdl.xml      (can import from ../asterix/common/)
```

## Best Practices

- Use namespaces for third-party or shared libraries to avoid name collisions.
- Keep library files self-contained (just `<bmdl>` with definition blocks, no `<defaults>`).
- Extract common types (integer aliases, coordinates, callsigns) into a stdlib file and import it everywhere.

## Common Pitfalls

- The importing protocol's `<defaults>` **are** applied to library definitions during the build phase. Library types without explicit attributes will inherit the importer's defaults. Use explicit attributes on library types to ensure consistent behavior.
- Importing without `ns` merges types into the current namespace. If two namespace-free imports define the same type name, it's a validation error.
- Exactly one file in the import tree must be a protocol file (has a `<defaults>` block). Zero or multiple protocol files is an error.
