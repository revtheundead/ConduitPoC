# Constraints

[Back to index](index.md)

Constraints restrict the valid range of field or type values. They are checked at encode time, decode time, and optionally at setter time.

## Syntax

```xml
<constraint equals="0xEB90"/>
<constraint min="1" max="100"/>
<constraint min="0" max="MAX_PAYLOAD"/>
<constraint equals="SYNC_WORD" validate="immediate"/>
<constraint min="-1000" max="1000" validate="deferred"/>
```

### Attributes

| Attribute | Description |
|-----------|-------------|
| `equals` | Exact value match (decimal, hex, or constant name) |
| `min` | Minimum value (inclusive) |
| `max` | Maximum value (inclusive) |
| `validate` | Timing: `immediate` (default) or `deferred` |

## Equals vs. Min/Max

- `equals` specifies an exact value constraint
- `min` and `max` specify a range (typically at least one is provided when not using `equals`)
- `equals` and `min`/`max` are **mutually exclusive** on the same `<constraint>` element

```xml
<!-- Exact value -->
<field name="sync" type="uint16">
  <constraint equals="SYNC_WORD"/>
</field>

<!-- Range -->
<field name="version" type="uint8">
  <constraint min="1" max="5"/>
</field>

<!-- One-sided range -->
<field name="count" type="uint16">
  <constraint min="1"/>
</field>
```

Constraint values accept decimal literals (`42`, `-10`), hexadecimal literals (`0xFF`), or named [constants](constants.md). Decimal and hex literals are useful for protocol magic numbers and sync words where a named constant would add unnecessary indirection.

## Validation Timing

| Value | On Set | On Encode | On Decode |
|-------|--------|-----------|-----------|
| `immediate` (default) | Validates, rejects violations | Validates, rejects violations | Validates, rejects violations |
| `deferred` | No validation | Validates, rejects violations | Validates, rejects violations |

### Immediate (Default)

```xml
<field name="cpu-load" type="uint8">
  <constraint max="100"/>
</field>
```

Setting `cpu-load` to a value greater than 100 produces an error immediately. A mutable escape hatch is available for cases where the caller knows the value is valid.

### Deferred

```xml
<field name="calibration-offset" type="int16">
  <constraint min="-1000" max="1000" validate="deferred"/>
</field>
```

Setting `calibration-offset` to any value succeeds without error. Validation occurs at encode time.

Use `deferred` for fields that are built incrementally or whose valid value depends on other fields being set first.

## Type-Level Constraints

Types can carry constraints that are inherited by all fields using the type:

```xml
<type name="protocol-version" base="uint" bits="8">
  <constraint min="1" max="10"/>
</type>

<!-- Inherits min=1, max=10 from type -->
<field name="version" type="protocol-version"/>

<!-- Narrows to min=2, max=5 (valid: within type range) -->
<field name="version" type="protocol-version">
  <constraint min="2" max="5"/>
</field>
```

A field can **narrow** a type constraint but cannot **relax** it.

## Field-Level Length Validation

Terminated strings with `max-length` have a decode-time safety limit:

```xml
<field name="name" type="string" terminated="null" max-length="64"/>
```

On decode, if the terminator is not found within `max-length` bytes, the decoder produces an error. This prevents unbounded reads from untrusted input. See [Strings](strings.md) for details.

## Validation Rules

- `equals` and `min`/`max` are mutually exclusive on the same `<constraint>`.
- When both `min` and `max` are specified, `min` must be less than or equal to `max`. Violations are reported as a validation error.
- Constraint values (`equals`, `min`, `max`) are checked against the field's bit width. A value that exceeds the representable range for the field (e.g., `max="300"` on an 8-bit unsigned field) is a validation error.
- A field can narrow a type-level constraint but cannot relax it.

## Frame-Level Constraint-Equals

In `<frame>` context, `<constraint equals="..."/>` has special semantics:

- **Encode**: The constrained value is automatically set by the generated `wrap()` and `encode_batch()` methods. Application code does not need to set it manually.
- **Decode**: The value is read from the wire but **not validated** against the constraint. Frame-level constraint-equals is encode-only. Sync word scanning is handled separately by the session's stream framing layer, not by per-message decode validation.

This differs from struct/message-level `equals` constraints, which validate on both encode and decode.

## Equals Constraints and Default Values

A `constraint equals="X"` automatically implies `default="X"` for the field. This means:

- **Non-optional fields:** The generated C++ member initializer uses the constrained value (e.g., `uint16_t sync_{0xEB90};`).
- **Optional fields:** The `value_or()` accessor returns the constrained value when the field is absent.

This eliminates the need to specify both `default` and `constraint equals` with the same value. If both are specified with different values, the constraint value takes priority and a generation-time warning is emitted. See [Fields: Default Values](fields.md#default-values) for details.

## Best Practices

- Use `equals` constraints on sync/magic fields. These are used to extract sync patterns for stream framing (see [Sessions](sessions.md)).
- Use `deferred` for fields whose valid value depends on other fields being set first (e.g., a checksum computed last).

## Common Pitfalls

- `validate` timing does **not** inherit. A field using a type with `<constraint validate="deferred">` still defaults to `immediate` at the field level unless explicitly overridden. Each `<constraint>` independently defaults to `immediate`.
- Immediate-constrained setters return errors that must be checked. Forgetting to check silently discards the error.
- Each field or type should have at most one `<constraint>` child. If multiple `<constraint>` elements are present, only the first is used; the rest are silently ignored.
- `equals` and `min`/`max` are mutually exclusive on the same `<constraint>`.

> **Backend limitation (Java/Python):** Java does not generate a `validate()` method for deferred constraints. Python does not implement deferred constraint validation at all. Only the C++ backend fully supports `validate="deferred"`. See [Limitations & Known Issues](../conduit/limitations.md).
