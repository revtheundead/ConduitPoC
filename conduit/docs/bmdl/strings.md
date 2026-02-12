# String Handling

[Back to index](index.md)

BMDL supports multiple string representations on the wire, controlled by attributes on `<type>` and `<field>` elements.

## String Attributes

| Attribute | Values | Description | Default |
|-----------|--------|-------------|---------|
| `encoding` | `ascii`, `utf8`, `ia5`, `ebcdic` | Character encoding | From `<defaults>` or `ascii` |
| `padding` | `null`, `space`, `none` | Pad character on encode (fixed-length only) | From `<defaults>` or `null` |
| `trim` | `left`, `right`, `both`, `none` | Which end to trim padding on decode | From `<defaults>` or `right` |
| `length` | integer or `"*"` | Fixed byte length, or rest of container | -- |
| `length-from` | expression | Length from a previously decoded field | -- |
| `length-prefix` | type reference | Self-describing length prefix (any `uint`-based type) | -- |
| `length-includes-prefix` | `true` | Length value includes prefix size itself | `false` |
| `terminated` | `null`, `newline`, `crlf`, `0xNN` | Terminator byte(s) | -- |
| `max-length` | integer | Safety limit for terminated strings | -- |
| `char-bits` | 1-8 | Bits per character (default: 8) | `8` |

## Attribute Inheritance

String attributes are resolved with a three-level priority:

1. **Field-level** (highest) -- attributes on `<field>` override everything
2. **Type-level** -- attributes on the referenced `<type>`
3. **Protocol defaults** (lowest) -- from `<defaults>` or built-in defaults

## Fixed-Length Strings

Occupy exactly `length` bytes on the wire. Shorter values are padded; decoded values are trimmed.

```xml
<!-- Type-level definition -->
<type name="callsign" base="string" length="8" padding="space" trim="right"/>

<!-- Field-level usage -->
<field name="callsign" type="callsign"/>

<!-- Or inline on field -->
<field name="callsign" type="string" length="8" padding="space" trim="right"/>
```

**Wire format:** Always exactly `length` bytes. On encode, the string is written and padded to fill. On decode, the full `length` bytes are read and padding is trimmed from the specified side.

## Packed Characters (`char-bits`)

Strings can use fewer than 8 bits per character, packing characters at the specified bit width:

```xml
<!-- ICAO aircraft identification: 8 chars x 6 bits = 48 bits = 6 bytes on wire -->
<type name="icao-ident" base="string" length="8" encoding="ia5" char-bits="6"/>
```

**Wire size:** `length * char-bits` bits total. For 8 characters at 6 bits = 48 bits = 6 bytes.

**Encoding:** Each character is stored using the low `char-bits` bits of its code point. For IA-5 with `char-bits="6"`, the encodable subset is `0x20`-`0x5F`.

**Restrictions:** `char-bits` is valid on fixed-length `string` and `bytes` fields (`length="N"`). It is incompatible with:
- `terminated`
- `length-prefix`
- `length-from`
- `length="*"`

Valid values are 1 to 8 inclusive. A value outside this range is a parse error.

## Null-Terminated Strings

Read characters until a terminator byte is found:

```xml
<field name="label" type="string" terminated="null"/>
<field name="line" type="string" terminated="newline"/>
<field name="record" type="string" terminated="0x1E"/>
```

**Supported terminators:**

| Value | Byte(s) |
|-------|---------|
| `null` | `0x00` |
| `newline` | `0x0A` |
| `crlf` | `0x0D 0x0A` |
| `0xNN` | Any byte value in hex |

Use `max-length` as a safety limit for untrusted input:

```xml
<field name="label" type="string" terminated="null" max-length="256"/>
```

## Length-Prefixed Strings

The string length is embedded as a prefix on the wire:

```xml
<!-- Basic: prefix = payload length -->
<field name="name" type="string" length-prefix="uint8"/>

<!-- Length includes the prefix byte itself -->
<field name="data" type="string" length-prefix="uint8" length-includes-prefix="true"/>
```

**Wire format for `length-prefix="uint8"`:**
```
[length: 1 byte][characters: length bytes]
```

**Wire format for `length-prefix="uint8" length-includes-prefix="true"`:**
```
[length: 1 byte][characters: (length - 1) bytes]
```

`length-prefix` must reference an unsigned integer type. `length-includes-prefix` is only valid when `length-prefix` is present.

## Variable-Length Strings

### Rest of Container

```xml
<field name="payload" type="string" length="*"/>
```

Consumes all remaining bytes in the current container. Only valid in bounded contexts (messages, length-delimited fields).

### Length from Field

```xml
<field name="name-length" type="uint16"/>
<field name="name" type="string" length-from="name-length"/>
```

Length is determined by a previously decoded field or [expression](expressions.md).

## Best Practices

- Common pattern: `length="8" padding="space" trim="right"` for fixed-length callsigns/identifiers.
- Always set `max-length` on terminated strings when decoding untrusted input to prevent unbounded reads.
- For IA-5 and EBCDIC: encoding is applied transparently, but only the ASCII subset is well-tested. Document any non-ASCII usage.

## Common Pitfalls

- `char-bits` is incompatible with `terminated`, `length-prefix`, `length-from`, and `length="*"`. It only works with fixed-length strings.
- `padding` and `trim` are separate concerns. `padding` affects encode (how to fill remaining bytes), `trim` affects decode (what to strip). Setting `padding="space"` without `trim="right"` means decoded strings retain trailing spaces.
- The importing protocol's `<defaults>` are applied to library string types during the build phase. A library string type without explicit `encoding`, `padding`, or `trim` attributes will inherit the importer's defaults. Use explicit attributes to ensure consistent behavior across importers.
