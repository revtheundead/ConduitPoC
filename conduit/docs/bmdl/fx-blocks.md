# FX Extension Chains

[Back to index](index.md)

The `<fx>` element models extension-bit chains where each group of fields (an "extent") is followed by a single bit that indicates whether another group follows. This pattern is common in ASTERIX compound data items.

## Basic Usage

```xml
<struct name="TrackStatus">
  <!-- First extent: 7 data bits + FX bit (always present) -->
  <field name="cnf" bits="1"/>
  <field name="rad" bits="2"/>
  <field name="dou" bits="1"/>
  <field name="mah" bits="1"/>
  <field name="cdm" bits="2"/>
  <fx>
    <!-- Second extent: 7 data bits + FX bit -->
    <field name="tre" bits="1"/>
    <field name="gho" bits="1"/>
    <field name="sup" bits="1"/>
    <field name="tcc" bits="1"/>
    <reserved bits="3"/>
    <fx>
      <!-- Third extent: 7 data bits + FX bit -->
      <field name="additional" bits="7"/>
      <!-- No inner <fx> means chain ends here -->
    </fx>
  </fx>
</struct>
```

## Semantics

1. Fields **before** the first `<fx>` in a struct are always present (the first extent)
2. The last bit of each extent is a 1-bit FX indicator: `1` = more extents follow, `0` = end
3. `<fx>` blocks nest to form multi-level chains. Each nesting level adds another extent with its own FX bit
4. The FX bit is **automatically managed** -- it does not appear as a field in the generated API

## Wire Format

For the `TrackStatus` example:

```
[cnf:1][rad:2][dou:1][mah:1][cdm:2][FX:1]       <- always present (8 bits)
  if FX=1:
  [tre:1][gho:1][sup:1][tcc:1][reserved:3][FX:1] <- second extent (8 bits)
    if FX=1:
    [additional:7][FX:1]                          <- third extent (8 bits)
      FX is always 0 here (no inner <fx>)
```

Each extent totals the sum of its declared data bits plus 1 FX bit. Typically 8 bits per extent (7 data + 1 FX), but this is not required.

## Flat vs. Nested FX

### Flat FX (ALL-OR-NOTHING)

A single `<fx>` with no nesting uses all-or-nothing semantics:

```xml
<struct name="SimpleExtension">
  <field name="a" bits="7"/>
  <fx>
    <field name="b" bits="7"/>
  </fx>
</struct>
```

- Setting **any** item inside the `<fx>` triggers encoding of **all** items with zero defaults for unset items
- After decode, **all** items in the extent appear present
- No per-item continuation control

### Nested FX (Per-Item Control)

Nesting `<fx>` blocks provides per-item continuation:

```xml
<struct name="CompoundItem">
  <field name="a" bits="7"/>
  <fx>
    <field name="b" bits="7"/>
    <fx>
      <field name="c" bits="7"/>
    </fx>
  </fx>
</struct>
```

- Each nesting level independently controls whether its fields are present
- Setting `c` automatically sets FX bits for `b`'s extent (propagation upward)

## Encoding Behavior

On encode, FX bit values are determined automatically:
- If **any** field in an `<fx>` extent has been set, the preceding FX bit is `1` and the extent is written
- If a nested `<fx>` has set fields, all parent FX bits leading to it are also `1`
- If no fields in an extent are set, the FX bit is `0` and the extent (and all deeper extents) are omitted

## Structural Rules

| Rule | Description |
|------|-------------|
| Valid parents | `<struct>`, `<message>`, or another `<fx>` |
| Must not be empty | At least one child element required |
| Max one per level | At most one `<fx>` per nesting level (no sibling `<fx>` elements) |
| Not in arrays | `<fx>` cannot appear inside `<array>` |
| Not in choices | `<fx>` cannot appear inside `<choice>`, `<case>`, or `<otherwise>` |
| Not in bitmap | `<fx>` cannot appear inside `presence="bitmap"` structs |

## Valid Children

`<fx>` can contain:
- `<field>`
- `<struct>` (named)
- `<array>`
- `<choice>`
- `<reserved>`
- `<align>`
- Nested `<fx>`

## Best Practices

- FX blocks are most useful for ASTERIX-style protocols where fields are chained with extension bits.
- Use nested `<fx>` for per-item continuation control. Use flat `<fx>` only when the protocol truly uses all-or-nothing semantics.
- Fields before the first `<fx>` are always present -- put the most important fields there.

## Common Pitfalls

- Flat FX blocks (single `<fx>`, no nesting) use **all-or-nothing** semantics. Setting any item triggers encoding of all items with zero defaults for unset items. For per-item control, use nested `<fx>` blocks.
- `<fx>` cannot appear inside `<array>`, `<choice>`, `<case>`, `<otherwise>`, or bitmap structs. Use a struct wrapper if needed.
- At most one `<fx>` per nesting level -- two sibling `<fx>` elements is a validation error.
- The FX bit is not exposed in the API. You don't set or read it directly; it's managed based on which fields are set.
