# Choices (Discriminated Unions)

[Back to index](index.md)

A choice dispatches to one of several case types based on a discriminator expression. This is BMDL's mechanism for variant/tagged-union types.

## Syntax

```xml
<field name="msg-type" type="message-type"/>

<choice name="payload" switch="msg-type">
  <case name="heartbeat" value="heartbeat">
    <field name="timestamp" type="uint64"/>
    <field name="sequence" type="uint32"/>
  </case>

  <case name="position" value="position">
    <field name="lat" type="wgs84"/>
    <field name="lon" type="wgs84"/>
  </case>

  <otherwise name="unknown">
    <field name="data" type="bytes" length="*"/>
  </otherwise>
</choice>
```

The `switch` attribute is required and must reference a previously decoded field or [expression](expressions.md).

## Case Matching

Each `<case>` requires exactly one of `value` or `range`:

### By Value

```xml
<case name="heartbeat" value="1">...</case>        <!-- Decimal -->
<case name="heartbeat" value="0x01">...</case>      <!-- Hex -->
<case name="heartbeat" value="heartbeat">...</case>  <!-- Enum name -->
<case name="heartbeat" value="MSG_HEARTBEAT">...</case>  <!-- Constant -->
```

Constant resolution: When a `value` matches `UPPER_SNAKE_CASE`, it is resolved as a named [constant](constants.md).

### By Range

```xml
<case name="standard" range="1..10">...</case>        <!-- Inclusive -->
<case name="extended" range="MIN_CAT..MAX_CAT">...</case>  <!-- Constants -->
```

Both endpoints are inclusive. Endpoints follow the same resolution rules as `value`.

### Otherwise (Default)

```xml
<otherwise name="unknown">
  <field name="data" type="bytes" length="*"/>
</otherwise>
```

- Must be the last child of `<choice>` if present
- Optional -- if omitted and no case matches, decode produces an error
- Implicitly receive-only (cannot encode an otherwise case)

## Type Reference Shorthand

When a case wraps an existing struct:

```xml
<case name="heartbeat" value="1" type="HeartbeatPayload"/>

<!-- Equivalent to -->
<case name="heartbeat" value="1">
  <field name="data" type="HeartbeatPayload" inline="true"/>
</case>
```

The same shorthand applies to `<otherwise>`:

```xml
<otherwise name="unknown" type="RawPayload"/>
```

## Naming with `typeName`

### Renaming Inline Cases

By default, inline case definitions generate C++ class names by prefixing the parent struct name: `ParentName_CaseName`. The `typeName` attribute overrides this to produce a cleaner, user-chosen class name:

```xml
<choice name="payload" switch="msg-type">
  <case name="heartbeat" value="1" typeName="HeartbeatPayload">
    <field name="timestamp" type="uint64"/>
    <field name="sequence" type="uint32"/>
  </case>

  <case name="position" value="2" typeName="PositionPayload">
    <field name="lat" type="wgs84"/>
    <field name="lon" type="wgs84"/>
  </case>

  <otherwise name="unknown" typeName="UnknownPayload">
    <field name="data" type="bytes" length="*"/>
  </otherwise>
</choice>
```

Without `typeName`, the generated classes would be `MyMessage_heartbeat`, `MyMessage_position`, and `MyMessage_payloadOtherwise`. With `typeName`, they are `HeartbeatPayload`, `PositionPayload`, and `UnknownPayload`.

### Renaming the Variant Alias

The `<choice>` element itself also accepts `typeName` to override the generated `std::variant` alias name. By default, the variant alias is `ParentName_ChoiceNameVariant`. With `typeName`, you can provide a cleaner name:

```xml
<choice name="payload" switch="msg-type" typeName="MessagePayload">
  <case name="heartbeat" value="1">...</case>
  <case name="position" value="2">...</case>
</choice>
```

Without `typeName`, the variant alias would be `MyMessage_PayloadVariant`. With `typeName="MessagePayload"`, it becomes `MessagePayload`.

### Rules

- `typeName` on `<case>` and `<otherwise>` is only valid on inline definitions -- it cannot be used when the `type` attribute is present (referencing an existing type).
- The value must be a valid C++ identifier: starts with a letter or underscore, contains only alphanumeric characters and underscores.
- The value must not be a C++ keyword (`class`, `struct`, `int`, etc.).
- The value must not conflict with any type, struct, or message name defined in the protocol.
- Each `typeName` value must be unique across the protocol.
- The value must not start with a double underscore (`__`), as this is reserved in C++.

`typeName` is also available on inline `<struct>` and `<array>` elements (see [naming conventions](../bgen/naming-conventions.md)).

## Length-Bounded Choices

A choice can be bounded to a specific byte length using `length` (fixed) or `length-from` (from a field/expression):

```xml
<field name="type" type="uint8"/>
<field name="len" type="uint16"/>
<choice name="payload" switch="type" length-from="len - 3">
  <case name="text" value="1">
    <field name="data" type="string" length="*"/>
  </case>
  <case name="binary" value="2">
    <field name="data" type="bytes" length="*"/>
  </case>
  <otherwise name="unknown">
    <field name="data" type="bytes" length="*"/>
  </otherwise>
</choice>
```

Inside a bounded choice, `length="*"` and `remaining` refer to the bounded scope (the sub-reader), not the outer container. This allows `count="*"` arrays and `length="*"` fields within cases to consume only the bytes allocated to this choice.

**Exact consumption:** After decoding the matched case, any unconsumed bytes in the sub-reader cause an error. Cases that don't consume all bytes should include a trailing `<field type="bytes" length="*"/>`.

## Direction

The `direction` attribute on `<case>` filters encode/decode behavior for [session](sessions.md) generation:

```xml
<choice name="body" switch="msg-type" length-from="length - 6">
  <case name="heartbeat" value="MSG_HEARTBEAT" type="HeartbeatBody" direction="receive"/>
  <case name="sensor" value="MSG_SENSOR" type="SensorBody" direction="receive"/>
  <case name="config" value="MSG_CONFIG" type="ConfigBody" direction="send"/>
  <case name="alert" value="MSG_ALERT" type="AlertBody"/>  <!-- both (default) -->
</choice>
```

| Value | Meaning |
|-------|---------|
| `both` | Sendable and receivable (default when omitted) |
| `receive` | Receivable only; encoding triggers a warning at the session level |
| `send` | Sendable only; decoding triggers a warning at the session level |

`direction` is only meaningful for session metadata -- all leaf types participate in both `decode_frame()` and `encode_wrap()` regardless of direction. A warning is logged when a direction-constrained type is used in the opposite direction. `direction` is not valid on `<otherwise>` (it is implicitly receive-only).

### Direction-Qualified Overlapping Cases

Two cases may share the same discriminator value when one is `direction="send"` and the other is `direction="receive"`. This models asymmetric protocols where the same discriminator value maps to different types depending on direction (e.g., uplink vs. downlink messages).

```xml
<choice name="body" switch="tag">
  <case name="uplink"   value="TAG_SHARED" type="UplinkPayload"   direction="send"/>
  <case name="downlink" value="TAG_SHARED" type="DownlinkPayload" direction="receive"/>
  <case name="common"   value="TAG_COMMON" type="CommonPayload"/>
</choice>
```

**Validation**: Overlap detection operates per direction lane. The send lane contains all `send` and `both` cases; the receive lane contains all `receive` and `both` cases. Overlaps within a lane are errors; overlaps across lanes (send-only vs. receive-only) are allowed.

**Decode behavior**: When two cases share a discriminator value, the generated decode logic emits only the receive (or both) variant for that value. The send-only case is skipped in the decode path, so the wire bytes are always decoded as the receive type. Non-colliding send-only cases are still decoded normally.

**Encode behavior**: Both variants remain in the generated variant type and `encode_wrap()`. Encoding either direction works; a warning is logged for opposite-direction usage.

## Conditional Choices

Choices support `present-when` and `bit` for conditional presence:

```xml
<field name="has-payload" type="bool"/>
<choice name="payload" switch="payload-type" present-when="has-payload">
  <case name="text" value="1">...</case>
  <case name="binary" value="2">...</case>
</choice>
```

## Validation Rules

- A `<choice>` requires at least one `<case>` child.
- Case names must be unique within a `<choice>` -- two cases cannot share the same `name` attribute.
- Cases must not overlap within the same direction lane (e.g., `value="5"` and `range="1..10"` both match 5). Two cases may share the same value only when one is `direction="send"` and the other is `direction="receive"` (see [Direction-Qualified Overlapping Cases](#direction-qualified-overlapping-cases)).
- `<otherwise>` must be the last child.
- The `switch` expression must reference a previously decoded field.
- Each `<case>` requires exactly one of `value` or `range`.

## Best Practices

- Add `<otherwise>` for forward-compatible protocols -- without it, an unrecognized discriminator causes a decode error.
- Use `direction="send"` / `direction="receive"` to model asymmetric protocols (request/response patterns).
- Use `length` or `length-from` on choices when the wire format includes a length prefix bounding the variant payload. This enables `length="*"` and `remaining` inside case bodies.

## Common Pitfalls

- Cases must not overlap within the same direction lane. A `value="5"` and a `range="1..10"` in the same choice is a validation error unless they have complementary directions (`send` vs. `receive`).
- Without `<otherwise>`, an unrecognized discriminator value causes a decode error.
- `<otherwise>` is implicitly receive-only -- you can decode unknown types into it but cannot encode an otherwise case.
- The `switch` expression must reference a previously decoded field -- you can't switch on a field that appears after the choice.

