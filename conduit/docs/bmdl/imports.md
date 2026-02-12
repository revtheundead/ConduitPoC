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

`<import>` is valid as a direct child of both `<protocol>` and `<bmdl>`. This allows library files (which have no `<protocol>` wrapper) to import other libraries.

```xml
<!-- In a protocol file -->
<protocol name="my-protocol" version="1.0">
  <import href="stdlib.bmdl.xml"/>
  <import href="common/types.bmdl.xml" ns="common"/>
  ...
</protocol>

<!-- In a library file -->
<bmdl version="1.0">
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

A BMDL file may omit the `<protocol>` wrapper and contain definition blocks directly under `<bmdl>`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="1.0">
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

When the generator resolves all files, exactly one file must serve as the protocol entry point. In v1, this means a `<protocol>` wrapper. In v2, a `<frame>` at the root level of a `<bmdl>` file also satisfies this requirement (no `<protocol>` wrapper needed). The generator reports distinct errors for:
- No `<protocol>` found -- no entry point for generation
- Multiple `<protocol>` wrappers found -- ambiguous entry point

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
- Keep library files self-contained (no `<protocol>` wrapper, just `<bmdl>` with definition blocks).
- Extract common types (integer aliases, coordinates, callsigns) into a stdlib file and import it everywhere.

## Common Pitfalls

- The importing protocol's `<defaults>` **are** applied to library definitions during the build phase. Library types without explicit attributes will inherit the importer's defaults. Use explicit attributes on library types to ensure consistent behavior.
- Importing without `ns` merges types into the current namespace. If two namespace-free imports define the same type name, it's a validation error.
- Exactly one file in the import tree must contain a `<protocol>` wrapper. Zero or multiple `<protocol>` wrappers is an error.
