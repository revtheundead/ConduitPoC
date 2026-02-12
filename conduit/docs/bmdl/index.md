# BMDL Documentation

[Back to top-level documentation](../index.md)

Binary Message Definition Language (BMDL) is an XML-based language for defining binary message formats. It provides a declarative way to specify the structure, types, and semantics of binary protocols.

## Design Principles

- **Wire order = declaration order** -- Fields are encoded/decoded in the order they appear (exception: bitmap structs use bit order)
- **No forward references** -- Expressions can only reference previously-declared fields
- **Explicit sizes** -- Every field's size must be determinable at decode time
- **Strongly typed** -- Every construct maps to a specific wire representation
- **Generator-friendly** -- Every construct has unambiguous encode/decode semantics
- **Minimal core** -- Few concepts that compose well
- **Wire-format agnostic** -- Same language describes big-endian, little-endian, bit-packed, byte-aligned
- **Readable definitions** -- Protocol specs can serve as documentation
- **Practical** -- Handles 99% of real-world binary protocols

## Quick Example

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="2.0">
  <defaults>
    <endian>big</endian>
    <namespace>example</namespace>
  </defaults>

  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
    <type name="uint32" base="uint" bits="32"/>
  </types>

  <frame name="ExampleFrame">
    <field name="msg-type" type="uint8" auto="id"/>
    <field name="length" type="uint16" auto="length"/>
    <payload/>
  </frame>

  <messages>
    <message id="1" name="Heartbeat">
      <field name="timestamp" type="uint32"/>
      <field name="sequence" type="uint16"/>
      <field name="status" type="uint8"/>
    </message>
  </messages>
</bmdl>
```

This defines a protocol with a `<frame>` that handles wire-level transport (message ID dispatch and length framing) and a `Heartbeat` message occupying 7 bytes of payload. The frame adds 3 bytes of header (1 byte ID + 2 bytes length) for a total of 10 bytes on the wire.

## Documentation Guide

| Document | Description |
|----------|-------------|
| [Document Structure](document-structure.md) | Root element, protocol, defaults, doc, annotations |
| [Constants](constants.md) | Named constant definitions and usage |
| [Types](types.md) | Type system: primitives, enums, flags, scaled, constrained |
| [Strings](strings.md) | String handling: encoding, padding, trim, terminated, packed characters |
| [Fields](fields.md) | Field definitions, all attributes, presence, defaults, initial values |
| [Constraints](constraints.md) | Constraint system: equals, min/max, immediate/deferred validation |
| [Structs & Messages](structs-and-messages.md) | Structs, messages, nesting, inlining, entry-points |
| [Arrays](arrays.md) | Array definitions, count patterns, length-bounded arrays |
| [Choices](choices.md) | Discriminated unions, cases, otherwise, direction |
| [Bitmap](bitmap.md) | Bitmap-controlled structs, FSPEC, extension bits |
| [FX Blocks](fx-blocks.md) | FX extension chains, nesting, semantics |
| [Wire Encodings](wire-encodings.md) | BCD, BCD_S, BNR, BNR_S, CB2 encoding details |
| [Expressions](expressions.md) | Expression grammar, operators, field references, `remaining` |
| [Imports](imports.md) | Import system, namespaces, library files, defaults propagation |
| [Sessions](sessions.md) | Entry-points, leaf type discovery, sync patterns, auto-increment |

## Supported Protocol Patterns

| Pattern | Example Protocols | Support |
|---------|-------------------|---------|
| Fixed-size messages | ARINC 429, simple sensors | Full |
| Variable-length with length prefix | Most framed protocols | Full |
| Bit-packed fields | ADS-B, Mode S, CAN | Full |
| Presence bitmap (FSPEC) | ASTERIX | Full |
| Tag-Length-Value | BER, many proprietary | Full |
| Discriminated unions | Message type dispatch | Full |
| Nested structures | ASN.1-like | Full |
| Common headers (composition) | Most protocols | Full |
| Null-terminated strings | C-style protocols | Full |
| Self-describing length | TLV protocols | Full |
| FX extension chains | ASTERIX compound items | Full |
| Checksums/CRCs | Many protocols | App handles |
| Encryption/Compression | Secure protocols | Out of scope |

## Out of Scope

| Feature | Reason | Workaround |
|---------|--------|------------|
| Checksums/CRCs | Too many variations | Application validates before decode |
| Encryption | Security domain | Application decrypts before decode |
| Compression | Many algorithms | Application decompresses before decode |
| Computed fields | Complex dependencies | Application computes after decode |
| Cross-message state | Stateful protocols | Application tracks state |
| Streaming | No message boundaries | Application frames the stream |

## Related Documentation

- [bgen Code Generator](../bgen/index.md) -- Reads BMDL definitions and generates C++ header files
- [conduit Runtime Library](../conduit/index.md) -- The runtime library for sending/receiving typed messages

## Specification Version

This documentation covers BMDL v2.0.
