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
```

Every `<array>` requires:
1. A `name` attribute
2. Exactly one count mechanism: `count`, `count-from`, or `count="*"`
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

After decoding, if the sub-reader has unconsumed bytes remaining, the decoder reports an error. The array content must exactly fill the byte budget. If the protocol intentionally includes trailing padding, add a trailing `<reserved>` or `<field type="bytes" length="*"/>` inside the array element to consume it.

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
