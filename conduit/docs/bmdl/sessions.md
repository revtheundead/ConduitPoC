# Sessions & Frames

[Back to index](index.md)

Sessions provide higher-level framing and dispatch semantics for stream-based protocols. In v2, they are derived from `<frame>` definitions. In v1, they are derived from `<message role="entry-point">` declarations.

## Frames (v2)

A `<frame>` defines the wire-level transport envelope. It separates framing concerns (message ID, length, sync) from application messages:

```xml
<frame name="MyFrame">
  <field name="sync" type="uint16">
    <constraint equals="0xBEEF"/>
  </field>
  <field name="msg-type" type="uint8" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
</frame>
```

### Frame Structure

A frame consists of:
1. **Header fields** -- fields before `<payload/>`
2. **`<payload/>`** -- marks where message content goes
3. **Footer fields** (optional) -- fields after `<payload/>`

### Frame Rules

- At most one `<frame>` per protocol
- Exactly one `<payload/>` element
- Exactly one `auto="id"` field (for message dispatch)
- At most one `auto="length"` field (for frame sizing); must be ≤ 32 bits
- At least one header field before `<payload/>`
- Header and footer may only contain scalar fields (no inline structs, arrays, or choices)
- `auto="config(key)"` keys must be unique within the frame
- At least one `<message>` must exist when a `<frame>` is defined

### Generated Frame Class

The frame generates a class with:
- `PayloadVariant` -- `std::variant<Msg1, Msg2, ...>` of all message types
- `wrap(const MsgType&)` -- static factory that creates a frame wrapping a message (auto-sets ID)
- `encode()` -- writes header, payload, footer, with auto-length backpatching
- `decode()` -- reads header, dispatches on ID to decode the correct message type
- `encode_bytes()` / `decode_bytes()` -- convenience byte-level serialization

### Payload Modes

| Attribute | Behavior |
|-----------|----------|
| `<payload/>` | Single message payload |
| `<payload count="*"/>` | Array of messages (multiple records per frame) |
| `<payload length-from="expr"/>` | Payload length determined by an expression (escape hatch for non-standard framing) |

### Config Fields

`auto="config(key)"` fields are populated from a Config struct provided at session creation:

```xml
<frame name="ConfigFrame">
  <field name="system-id" type="uint8" auto="config(system-id)"/>
  <field name="msg-type" type="uint8" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
</frame>
```

This generates a `Config` struct with a `system_id` member, and the session constructor takes a `Config` parameter.

## Entry-Point Messages (v1)

The `role="entry-point"` attribute marks a message as a wire-level entry point:

```xml
<message name="Frame" role="entry-point">
  <field name="sync" type="uint16">
    <constraint equals="SYNC"/>
  </field>
  <field name="msg-type" type="msg-type"/>
  <field name="length" type="uint16"/>
  <field name="sequence" type="uint8" auto="increment"/>

  <choice name="body" switch="msg-type" length-from="length - 6">
    <case name="heartbeat" value="heartbeat" type="HeartbeatBody"/>
    <case name="sensor" value="sensor" type="SensorBody"/>
    <case name="config" value="config" type="ConfigBody" direction="send"/>
    <otherwise name="unknown">
      <field name="data" type="bytes" length="*"/>
    </otherwise>
  </choice>
</message>
```

Rules:
- `role` is optional. When omitted from all messages, no session metadata is produced.
- Multiple messages may have `role="entry-point"`, each producing an independent session.
- `role` is only valid on `<message>`.

## Leaf Type Discovery

The generator walks the entry-point message's structure to discover **leaf types** -- the concrete messages or structs delivered to application code. The walk follows:

1. `<choice>` elements -- each case type is a candidate
2. `<array>` elements -- the element type is followed regardless of count mode (`count="*"`, `count="N"`, or `count-from`)
3. Recursion continues into each candidate. Types with no further dispatch choices or arrays are leaf types.

### Example: Simple Protocol

```
Frame
  +-- body: choice(msg-type)
       |-- case heartbeat -> HeartbeatBody     <- leaf
       |-- case sensor    -> SensorBody        <- leaf
       |-- case config    -> ConfigBody        <- leaf
       +-- otherwise      -> raw bytes         <- unknown handler
```

Leaf types: `HeartbeatBody`, `SensorBody`, `ConfigBody`.

### Example: ASTERIX

```
AsterixFrame
  +-- blocks: array count="*" of DataBlock
       +-- records: choice(cat)
            |-- case cat001 -> array count="*" of Cat001Record  <- leaf
            |-- case cat048 -> array count="*" of Cat048Record  <- leaf
            +-- otherwise   -> raw bytes
```

Leaf types: `Cat001Record`, `Cat048Record`.

Types behind non-dispatched structs (those without inner `<choice>` or `<array>` elements) are not followed further. Both `<choice>` and `<array>` (any count mode) trigger traversal.

## Sync Pattern

If the entry-point message (v1) or frame header (v2) starts with a field carrying `<constraint equals="..."/>`, the sync pattern is recorded. The constraint value can reference a named constant or be a direct numeric literal. Higher-level layers use it for stream synchronization -- scanning for the sync word to find message boundaries and recover from corruption or partial reads.

```xml
<field name="sync" type="uint16">
  <constraint equals="SYNC"/>  <!-- Sync pattern: byte representation of SYNC -->
</field>
```

The sync pattern is extracted from the first constrained-equals field encountered during a sequential scan of the entry-point message's fields. The scan stops at any optional, variable-length, or dispatch element.

## Frame Length

The frame length field's location (bit offset, bit width, endianness) is extracted from the entry-point message. This allows higher-level layers to read the length field from a partial header to determine how many bytes to read for a complete frame.

## Auto-Increment Fields

The `auto="increment"` attribute marks fields that the session manages automatically during encode wrapping:

```xml
<field name="sequence" type="uint8" auto="increment"/>
```

- An internal counter is maintained per auto-increment field, starting at 0
- On each encode-wrap operation, the current counter value is written, then the counter increments
- The counter wraps at the type's maximum value (e.g., 255 -> 0 for `uint8`)
- Only valid on unsigned integer fields (`uint` base)
- Only meaningful in session context -- standalone encode operations ignore the `auto` attribute

Fields with `<constraint equals="..."/>` and fields referenced by `length-from` are already implicitly auto-managed by the wrapping logic. The `auto` attribute covers the remaining cases where intent cannot be inferred.

## Entry-Point Context

When an entry-point message has concrete fixed fields (sync, msg-type, length, sequence, etc.), a lightweight context is produced containing those fields. This context is passed to inner case type decode operations, allowing them to reference entry-point fields during decode.

Inside the case type, expressions can reference entry-point fields by name. Local fields shadow context fields with the same name.

Context fields are collected from the entry-point's concrete fields, excluding:
- Choice blocks (dispatch elements, not concrete data)
- Conditional fields (`bit`, `present-when`)
- Dynamic arrays
- FX extension blocks

For `inline="true"` fields, the inlined struct's fields are flattened into the context.

## Direction Filtering

[Direction](choices.md#direction) on `<case>` elements controls session behavior:

| Direction | Decode | Encode (wrap) |
|-----------|--------|---------------|
| `both` (default) | Yes | Yes |
| `receive` | Yes | Yes (warning logged) |
| `send` | Yes (warning logged) | Yes |

All leaf types participate in all session methods regardless of direction. Direction only controls whether a warning is logged for opposite-direction usage.

`<otherwise>` is always receive-only.

## Wrapping Table

For each sendable leaf type, the generator records the reverse path needed to wrap a leaf object back into the entry-point structure:

| Property | Source |
|----------|--------|
| Discriminator values | `<case>` `value` attributes |
| Length fields | `length-from` expressions solved in reverse |
| Constrained fields | `<constraint equals="..."/>` values |
| Auto-increment fields | Internal counters |

## Edge Cases

| Scenario | Behavior |
|----------|----------|
| No `role="entry-point"` in any message | No session metadata produced |
| Entry-point with no `<choice>` or `<array>` | Single leaf type (the entry-point message itself) |
| Entry-point with `<array>` but no `<choice>` | Homogeneous stream, one leaf type (the array element type) |
| Multiple `role="entry-point"` messages | Independent sessions, leaf types may overlap |
| `<otherwise>` in a dispatch choice | Represents unrecognized discriminator values. Always receive-only. |
| Nested choices in a leaf type | Part of the leaf's structure, not session dispatch |

## Best Practices

- Use `direction="send"` on cases for request-type messages and `direction="receive"` for response-type messages to model asymmetric protocols.
- Always start entry-point messages with a sync field (`<constraint equals="..."/>`) for reliable stream framing.
- Use `auto="increment"` on sequence numbers so the session manages them automatically.

## Common Pitfalls

- The sync pattern is extracted from the **first** field with `<constraint equals="..."/>`. If the entry-point doesn't start with a constrained sync field, no sync pattern is available.
- `auto="increment"` only applies in session context (encode wrapping). Standalone encode operations ignore auto fields.
- Leaf types are discovered by walking `<choice>` cases and `<array>` elements (any count mode). Types behind non-dispatched structs without inner choices or arrays are leaves.
- Context only includes concrete, non-optional, non-dynamic fields from the entry-point. Choices, FX blocks, and dynamic arrays are excluded.
