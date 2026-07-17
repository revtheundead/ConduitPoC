# Arrays

[Back to index](index.md)

Arrays represent repeated elements -- either a fixed number or a dynamic count determined at decode time.

## Syntax

```xml
<array name="items" type="MyStruct" count="10"/>
<array name="items" count-from="num-items" type="Record"/>
<array name="items" count="*" type="Record"/>
<array name="items" count="*" type="Record" length="64"/>
<array name="items" count="*" type="Record" length-from="data-length"/>
<array name="items" count="fx" type="Octet"/>
```

Every `<array>` requires:
1. A `name` attribute
2. Exactly one count mechanism: `count`, `count-from`, `count="*"`, or `count="fx"`
3. An element type: either `type` attribute or inline children (mutually exclusive)

## Count Patterns

### Fixed Count

```xml
<array name="coordinates" count="3" type="float32"/>
```

Decodes exactly 3 elements.

### Dynamic Count (from Field)

```xml
<field name="count" type="uint8"/>
<array name="items" count-from="count">
  <field name="id" type="uint16"/>
  <field name="value" type="int32"/>
</array>
```

The count field determines how many elements are decoded.

### Until End of Container

```xml
<array name="records" count="*" type="Record"/>
```

Reads elements until the container is exhausted. Only valid in bounded contexts -- inside a message, a length-delimited field, or a length-bounded choice.

### FX-Terminated (`count="fx"`)

```xml
<type name="octet" base="uint" bits="7"/>
<array name="extents" count="fx" type="octet"/>
```

Models the ASTERIX pattern where an item extends an arbitrary number of times via a trailing **FX (field-extension) continuation bit**. Each *unit* on the wire is one element followed by a single FX bit:

```
[element bits][FX:1]   <- FX=1 means another unit follows
[element bits][FX:1]
[element bits][FX:0]   <- FX=0 ends the array
```

The decoder reads an element, then the FX bit, and repeats while the FX bit is `1`. An FX-terminated array always contains **at least one** element (the first unit is read unconditionally); on encode the FX bit after the last element is `0` and every earlier element's FX bit is `1`. The FX bit itself is automatically managed and never appears in the generated API.

Rules:
- The element must be **fixed-size** (a numeric/enum type, a fixed-length string, or a struct of fixed-size fields). The FX bit follows each element at a fixed offset, so a variable-length element is rejected.
- `count="fx"` is mutually exclusive with the other count mechanisms and with `length` / `length-from` -- the FX bit alone determines termination.
- Typically the element is 7 (or 15, 23, ...) data bits so that element + FX bit lands on a byte boundary, mirroring ASTERIX octets.

Inline element children work too:

```xml
<array name="extents" count="fx">
  <field name="a" bits="3"/>
  <field name="b" bits="4"/>
</array>
```

> `count="fx"` is the repeating counterpart to the [`<fx>` block](fx-blocks.md): an `<fx>` block chains *different* field groups, whereas `count="fx"` repeats the *same* element while the FX bit is set.

## Length-Bounded Arrays

An array can be bounded to a specific byte length using `length` (fixed) or `length-from` (from a field/expression):

```xml
<!-- Fixed byte budget -->
<array name="records" count="*" type="Record" length="64"/>

<!-- Byte budget from a field -->
<field name="data-length" type="uint16"/>
<array name="records" count="*" type="Record" length-from="data-length"/>
```

The decoder creates a sub-reader bounded to that many bytes and reads elements until the byte budget is exhausted.

### Bounded with Explicit Count

When both count and length are specified, the decoder reads exactly the specified number of elements and validates they fit within the byte budget:

```xml
<field name="n" type="uint8"/>
<field name="data-length" type="uint16"/>
<array name="records" count-from="n" type="Record" length-from="data-length"/>
```

### Exact Consumption

After decoding, the array content must exactly fill its byte budget. The decoder reports an error in either direction: if the sub-reader has unconsumed bytes remaining at the end (`ExactConsumptionFailed`), or if an element tries to read past the budget (`BufferUnderrun`). If the protocol intentionally includes trailing padding, add a trailing `<reserved>` or `<field type="bytes" length="*"/>` inside the array element to consume it.

## Inline Element Type

Instead of referencing a named type, define the element structure inline:

```xml
<array name="items" count-from="count">
  <field name="id" type="uint16"/>
  <field name="value" type="int32"/>
</array>
```

This is equivalent to defining a named struct and referencing it. `type` attribute and inline children are mutually exclusive.

## Single-Line Syntax

For arrays of a single type:

```xml
<!-- Primitive array -->
<array name="values" count="10" type="uint16"/>

<!-- Complex type array -->
<array name="positions" count="10" type="Position"/>

<!-- Variable count -->
<field name="count" type="uint8"/>
<array name="tracks" count-from="count" type="TrackRecord"/>
```

## Conditional Arrays

Arrays support `present-when` and `bit` for conditional presence:

```xml
<field name="has-items" type="bool"/>
<field name="count" type="uint8" present-when="has-items"/>
<array name="items" count-from="count" present-when="has-items">
  <field name="value" type="uint16"/>
</array>
```

Inside a bitmap struct:

```xml
<array name="tracks" bit="5" count-from="track-count" type="TrackRecord"/>
```

## Best Practices

- Use `count-from` with a previously-decoded count field for self-describing protocols.
- For element types reused elsewhere, prefer `type="MyStruct"` attribute over inline children.
- Fixed-count arrays decode exactly the specified number of elements.

## Common Pitfalls

- `count="*"` (until-end) arrays are only valid in bounded contexts -- inside a message, a length-delimited field, or a length-bounded choice. Using it at the top level of an unbounded struct is a validation error.
- Arrays with both `count` and `length` validate that elements exactly fill the byte budget. Leftover bytes cause a decode error.
- `type` attribute and inline children are mutually exclusive. The generator reports a parse error if both are present.
- At least one of `count`, `count-from`, or `count="*"` is required. The generator reports a parse error if none is specified.
