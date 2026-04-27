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

Constant names **must** be `UPPER_SNAKE_CASE` to be referenceable in expressions. The expression parser distinguishes constants from field references by casing — there is no symbol table fall-through:

- A token is treated as a **constant reference** only if it starts with an uppercase letter and every remaining character is an uppercase letter, digit, or underscore (e.g., `SYNC_WORD`, `MAX_LEN_2`).
- Any other identifier (lowercase, mixed-case, hyphenated) is treated as a **field reference**, regardless of whether a constant with that name exists.

Constants whose declared name does not match `UPPER_SNAKE_CASE` (e.g., `syncWord`, `Sync_Word`) are accepted by the XML parser, but they cannot be referenced in `present-when`, `switch`, `length-from`, `count-from`, or any other expression — the expression parser will treat the reference as a field name and the validator will fail with "unknown field". Always use `UPPER_SNAKE_CASE` for constants.

## Best Practices

- Use constants for sync words, magic numbers, and version identifiers to avoid magic literals scattered across the protocol.
- Use hex (`0x`) for byte-level values (sync words, masks) and decimal for logical values (counts, versions).
- Define all discriminator values as constants when a protocol has many message types, making the choice cases self-documenting.

## Validation Rules

- Constant names must be unique within a protocol. Duplicate constant names are a validation error.
- Constant values are checked against the declared type's bit width. A value that exceeds the representable range (e.g., `value="256"` with `type="uint8"`) is a validation error.

## Common Pitfalls

- Constant names **must** be `UPPER_SNAKE_CASE` to be referenceable. Non-conforming names (e.g., `syncWord`) are accepted by the XML parser and emitted as named constants in generated code, but they are silently invisible to the expression parser — every reference resolves to a field lookup that fails. Use `UPPER_SNAKE_CASE` for any constant you intend to reference from `present-when`, `switch`, `length-from`, etc.
- The `type` attribute is required on every `<const>`. Omitting it is a parse error.
- Constants are intended for integer types (`uint` or `int` base). Non-integer types are not rejected by the parser, but their behavior is unspecified.
