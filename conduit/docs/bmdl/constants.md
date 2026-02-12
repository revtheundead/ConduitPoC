# Constants

[Back to index](index.md)

Named values for magic numbers, sync words, version identifiers, and other protocol-specific literals.

## Syntax

```xml
<constants>
  <const name="SYNC_WORD" type="uint16" value="0xEB90"/>
  <const name="PROTOCOL_VERSION" type="uint8" value="1"/>
  <const name="MAX_PAYLOAD" type="uint32" value="65535"/>
</constants>
```

All three attributes are required on every `<const>`:

| Attribute | Description |
|-----------|-------------|
| `name` | Constant name (`UPPER_SNAKE_CASE` by convention) |
| `type` | Integer type reference (any type with `uint` or `int` base) |
| `value` | Decimal or `0x`-prefixed hexadecimal literal |

## Usage

Constants can be referenced in several contexts:

### In Constraints

```xml
<field name="sync" type="uint16">
  <constraint equals="SYNC_WORD"/>
</field>
```

### In Choice Cases

```xml
<choice name="body" switch="msg-type">
  <case name="heartbeat" value="MSG_HEARTBEAT">...</case>
  <case name="config" value="MSG_CONFIG">...</case>
</choice>
```

### In Range Endpoints

```xml
<case name="standard" range="MIN_CAT..MAX_CAT">...</case>
```

### In Expressions

Constants can appear in any [expression](expressions.md) context (`present-when`, `length-from`, `count-from`, `switch`):

```xml
<field name="extra" type="bytes" length="*"
       present-when="remaining >= MIN_EXTRA_SIZE"/>
```

## Naming Convention

Constant names **should** be `UPPER_SNAKE_CASE`. The expression parser uses casing to distinguish constants from field references:

- `UPPER_SNAKE_CASE` tokens are resolved as constant references
- `lower-case` or `lower_case` tokens are resolved as field references

## Best Practices

- Use constants for sync words, magic numbers, and version identifiers to avoid magic literals scattered across the protocol.
- Use hex (`0x`) for byte-level values (sync words, masks) and decimal for logical values (counts, versions).
- Define all discriminator values as constants when a protocol has many message types, making the choice cases self-documenting.

## Validation Rules

- Constant names must be unique within a protocol. Duplicate constant names are a validation error.
- Constant values are checked against the declared type's bit width. A value that exceeds the representable range (e.g., `value="256"` with `type="uint8"`) is a validation error.

## Common Pitfalls

- Constant names **should** be `UPPER_SNAKE_CASE`. Non-conforming names (e.g., `syncWord`) are accepted by the parser, but the expression parser cannot resolve them -- they would be interpreted as field references. Use `UPPER_SNAKE_CASE` to ensure constants are referenceable in expressions.
- The `type` attribute is required on every `<const>`. Omitting it is a parse error.
- Constants are intended for integer types (`uint` or `int` base). Non-integer types are not rejected by the parser, but their behavior is unspecified.
