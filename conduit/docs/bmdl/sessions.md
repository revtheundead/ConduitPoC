# Sessions & Frames

[Back to index](index.md)

Sessions provide higher-level framing and dispatch semantics for stream-based protocols. They are derived from `<frame>` definitions.

## Frames

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
- At most one `auto="length"` field (for frame sizing); must be <= 32 bits
- At least one header field before `<payload/>`
- Header and footer may only contain scalar fields (no inline structs, arrays, or choices); additionally, frame fields cannot use `present-when`, `terminated`, `length-prefix`, `length-from`, or `length="*"`
- `auto="config(key)"` keys must be unique within the frame
- At least one `<message>` must exist when a `<frame>` is defined

### Message ID Field Types

The `auto="id"` field is the dispatch discriminator, so its type is validated strictly at parse time:

| Id field type | Allowed | Notes |
|---------------|---------|-------|
| Integer (`uint` / `int`) | Yes | The conventional case. Each `<message id="N">` is a numeric literal that must fit the field's bit width. |
| Enum | Yes | Each `<message id="...">` is a numeric literal **or** a value name declared in the enum. The generated `ID_VALUE` constant is the enum type (cast from the numeric id), and dispatch compares the enum's underlying value. |
| Float | No | Rejected -- a float cannot be a discriminator (it is not even a valid switch quantity). |
| Bool | No | Rejected. |
| Raw bytes | No | Rejected. |
| String | No | Rejected -- string message ids are unconventional and unsupported. Map the string values to an `<enum>` and use that as the id field type instead. |

Using an enum id field:

```xml
<type name="msg-id" base="uint" bits="8">
  <enum>
    <value name="heartbeat" id="1"/>
    <value name="status" id="2"/>
  </enum>
</type>

<frame name="EnumFrame">
  <field name="msg-type" type="msg-id" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
</frame>

<messages>
  <message id="1" name="Heartbeat"><field name="ts" type="uint16"/></message>
  <message id="2" name="Status"><field name="code" type="uint16"/></message>
</messages>
```

### Generated Frame Class

The frame generates a class with:
- `PayloadVariant` -- `std::variant<Msg1, Msg2, ...>` of all message types
- `wrap(const MsgType&)` -- static factory that creates a frame wrapping a message (auto-sets ID and constraint-equals fields)
- `encode()` -- writes header, payload, footer, with auto-length backpatching
- `decode()` -- reads header, dispatches on ID to decode the correct message type
- `encode_bytes()` / `decode_bytes()` -- convenience byte-level serialization

### Payload Modes

| Attribute | Behavior |
|-----------|----------|
| `<payload/>` | Single message payload |
| `<payload count="*"/>` | Array of messages (multiple records per frame). Enables `encode_batch()` on the session and batch `wrap(std::span)` on the frame class. |
| `<payload length-from="expr"/>` | Payload length determined by an expression (escape hatch for non-standard framing) |

### Config Fields

`auto="config(key)"` fields are populated from a Config struct provided at session creation. Config fields are valid in both frame headers and message definitions:

```xml
<frame name="ConfigFrame">
  <field name="system-id" type="uint8" auto="config(system-id)"/>
  <field name="msg-type" type="uint8" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
</frame>

<messages>
  <message id="1" name="Telemetry">
    <field name="station-id" type="uint8" auto="config(station-id)"/>
    <field name="value" type="uint16"/>
  </message>
</messages>
```

This generates a nested `Config` struct with members for all config keys (e.g., `system_id` and `station_id`). The session factory function accepts a `Config` parameter. Frame-level config fields are set on the frame during encode wrapping. Message-level config fields are set on a copy of the message before wrapping it in the frame.

Config keys must be unique across the frame and all messages. Config fields in inlined structs are also supported.

### Length Arithmetic

The `auto="length"` and `auto="length(payload)"` fields support arithmetic modifiers with the operators `+`, `-`, `*`, `/`:

```xml
<field name="length" type="uint16" auto="length - 3"/>
<field name="payload-len" type="uint16" auto="length(payload) * 2"/>
```

During encode, the frame writes the modified value (e.g., `total_bytes - 3`) into the length field. This is useful for protocols where the length field excludes the header size or uses a different unit.

At frame level, only integer literal operands are allowed. Field operands are supported in struct/message-level `auto="length(field)"` expressions (see [Fields](fields.md)).

### Footer Fields

Footer fields appear after `<payload/>`:

```xml
<frame name="FooterFrame">
  <field name="msg-type" type="uint8" auto="id"/>
  <field name="length" type="uint16" auto="length"/>
  <payload/>
  <field name="checksum" type="uint16"/>
</frame>
```

When footer fields exist, the decode path creates a bounded sub-reader for the payload region so that payload decoding does not consume footer bytes. Footer fields are read after the payload is fully decoded.

## Leaf Type Discovery

Each `<message>` defined alongside a `<frame>` becomes a **leaf type** -- a concrete message delivered to application code. The session records:

- The message name and its `TYPE_ID` (FNV-1a hash)
- The `id` attribute value (for frame dispatch)
- Direction constraints (`send`, `receive`, or `both`)
- Auto-increment fields

## Sync Pattern

The first field in the frame header that carries `<constraint equals="..."/>` is recorded as the sync pattern (the scan stops as soon as it finds one — it does not have to be the very first header field, but in practice that is where sync words live). The constraint value can reference a named constant or be a direct numeric literal. Higher-level layers use the recorded sync pattern for stream synchronization -- scanning for the sync word to find message boundaries and recover from corruption or partial reads.

```xml
<field name="sync" type="uint16">
  <constraint equals="SYNC"/>  <!-- Sync pattern: byte representation of SYNC -->
</field>
```

The sync pattern is extracted from the first constrained-equals field encountered during a sequential scan of the frame's header fields.

## Frame Length

The frame length field's location (bit offset, bit width, endianness) is extracted from the frame header. This allows higher-level layers to read the length field from a partial header to determine how many bytes to read for a complete frame.

When no `auto="length"` field is present, the session's `extract_frame_length()` returns the header size as a fallback (suitable for fixed-size protocols or transport-framed protocols).

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

## Auto-Timestamp Fields

The `auto="timestamp"` attribute marks fields that the session populates with the current time during encode wrapping:

```xml
<field name="ts" type="uint32" auto="timestamp"/>
```

- On each encode-wrap operation, the field is set to milliseconds since Unix epoch using `std::chrono::system_clock`
- The value is masked to the field's bit width (e.g., a `uint32` wraps every ~49 days)
- Only valid on unsigned integer fields (`uint` base)
- Unlike `auto="increment"`, timestamp is stateless -- `reset()` does not affect it
- Only meaningful in session context -- standalone encode operations ignore the `auto` attribute

## Direction Filtering

The `direction` attribute on `<message>` controls session behavior:

| Direction | Decode | Encode (wrap) |
|-----------|--------|---------------|
| `both` (default) | Yes | Yes |
| `receive` | Yes | Error on send |
| `send` | Skipped in decode switch | Yes |

When two messages share the same `id` with opposite directions (one `send`, one `receive`), the decode path uses the `receive` variant and the encode path uses the `send` variant.

## Edge Cases

| Scenario | Behavior |
|----------|----------|
| No `<frame>` and no messages | No session metadata produced |
| Frame with a single message | Single leaf type, dispatch switch has one case |
| `<payload count="*"/>` | Array payload; decode loops until payload bytes exhausted |
| All messages `direction="send"` | Decode switch has no cases; all IDs return `UnknownDiscriminator` |

## Best Practices

- Use `direction="send"` on request messages and `direction="receive"` on response messages to model asymmetric protocols.
- Always start frame headers with a sync field (`<constraint equals="..."/>`) for reliable stream framing.
- Use `auto="increment"` on sequence numbers so the session manages them automatically.
- Use `auto="config(key)"` for fields like system identifiers that are fixed for the lifetime of a session.

## Common Pitfalls

- The sync pattern is extracted from the **first** field with `<constraint equals="..."/>`. If the frame doesn't start with a constrained sync field, no sync pattern is available.
- `auto="increment"` only applies in session context (encode wrapping). Standalone encode operations ignore auto fields.
- `auto="timestamp"` is stateless (not affected by `reset()`). Only meaningful in session context.
