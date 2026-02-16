# Expression Language

[Back to index](index.md)

BMDL includes an expression language used in four attribute contexts: `present-when`, `length-from`, `count-from`, and `switch`.

## Contexts

| Attribute | Used On | Purpose |
|-----------|---------|---------|
| `present-when` | `<field>`, `<struct>`, `<array>`, `<choice>` | Conditional presence (boolean) |
| `length-from` | `<field>`, `<array>`, `<choice>` | Byte length from expression |
| `count-from` | `<array>` | Element count from expression |
| `switch` | `<choice>` | Discriminator value |

All four contexts accept the full expression grammar.

## Grammar

Operators listed from lowest to highest precedence:

| Precedence | Operators | Description |
|------------|-----------|-------------|
| 1 (lowest) | `or` | Logical OR |
| 2 | `and` | Logical AND |
| 3 | `==` `!=` `<` `<=` `>` `>=` | Comparison (single, not chainable) |
| 4 | `\|` | Bitwise OR |
| 5 | `^` | Bitwise XOR |
| 6 | `&` | Bitwise AND |
| 7 | `<<` `>>` | Shift |
| 8 | `+` `-` | Additive |
| 9 | `*` `/` `%` | Multiplicative |
| 10 | `not` `!` `-` `~` | Unary (prefix) |
| 11 (highest) | Primary | Literals, field refs, constants, `remaining`, `(expr)` |

### Formal Grammar

```
expr        = or-expr
or-expr     = and-expr ("or" and-expr)*
and-expr    = cmp-expr ("and" cmp-expr)*
cmp-expr    = bitor-expr (cmp-op bitor-expr)?
cmp-op      = "==" | "!=" | "<" | "<=" | ">" | ">="
bitor-expr  = xor-expr ("|" xor-expr)*
xor-expr    = bitand-expr ("^" bitand-expr)*
bitand-expr = shift-expr ("&" shift-expr)*
shift-expr  = add-expr (("<<" | ">>") add-expr)*
add-expr    = mul-expr (("+" | "-") mul-expr)*
mul-expr    = unary (("*" | "/" | "%") unary)*
unary       = ("not" | "!" | "-" | "~") unary | primary
primary     = field-ref | constant | number | "true" | "false"
            | "remaining" | "(" expr ")"
field-ref   = identifier ("." identifier)*
identifier  = [a-zA-Z_][a-zA-Z0-9_-]*
constant    = [A-Z][A-Z0-9_]*
number      = [0-9]+ | "0x" [0-9a-fA-F]+
```

## Field References

Fields are referenced by their BMDL name:

```xml
present-when="has-altitude"
length-from="total-length - 3"
switch="header.type & 0x0F"
```

### Scope Resolution

1. Look for the identifier in the current scope (current struct/message)
2. If not found, look in the parent scope (enclosing struct/message)
3. Continue up to the root message

### Dotted Paths

Access fields in named nested structs:

```xml
<message name="Frame">
  <struct name="header">
    <field name="type" type="uint8"/>
    <field name="flags" type="uint8"/>
  </struct>

  <field name="data" type="bytes" length-from="header.flags"/>
</message>
```

### Outer-Scope References and Generated Code

When an expression references a field from a parent scope, the generated C++ `decode()` method for the inner struct receives the outer field value as an additional parameter. For example, if a nested struct's `length-from` references a parent field `len`, the generated decode signature becomes `decode(BitReader& r, uint16_t len)` instead of `decode(BitReader& r)`. The parent struct's decode code automatically passes the required values.

This is transparent in BMDL — you simply reference the field by name. The code generator handles parameter threading automatically.

### No Forward References

Referenced fields must be previously decoded. Since wire order = declaration order, you can only reference fields declared before the current position.

## Constant References

`UPPER_SNAKE_CASE` identifiers are resolved as named [constants](constants.md):

```xml
present-when="remaining >= MIN_EXTRA_SIZE"
switch="msg-type"  <!-- lowercase = field reference -->
```

The parser uses casing to distinguish:
- `UPPER_SNAKE_CASE` -> constant reference
- `lower-case` or `lower_case` -> field reference

Note: type, struct, and message names (used in `type="..."` attributes, not in expressions) are not restricted by this grammar -- they follow general XML naming rules.

## Special Keywords

| Keyword | Description |
|---------|-------------|
| `remaining` | Bytes remaining in current container |
| `true` | Boolean true |
| `false` | Boolean false |

### `remaining` Context

`remaining` is only valid where the container size is known:
- Inside messages (total bytes provided to decode)
- Inside length-delimited fields (`length`, `length-from`, `length-prefix`)
- Inside fixed-count arrays with fixed-size elements
- Inside length-bounded arrays (`length`/`length-from` on `<array>`)
- Inside length-bounded choices (`length`/`length-from` on `<choice>`)

Using `remaining` where container size is unknown is a validation error.

## Lexer: Maximal Munch for Hyphens

The lexer uses maximal munch, which affects `-` in identifier names:

```xml
length-from="total-length"      <!-- ONE identifier: "total-length" -->
length-from="total-length - 3"  <!-- Subtraction: "total-length" minus 3 -->
length-from="total-length-3"    <!-- ONE identifier: "total-length-3" -->
```

`total-length` is a single identifier, not `total` minus `length`. To subtract, add spaces around `-`.

This only affects `-`. All other operators (`+`, `*`, `&`, etc.) are unambiguous because their characters are not valid in identifiers.

## Boolean Evaluation

Expressions in `present-when` are evaluated as boolean:
- Non-zero integers and `true` are truthy
- Zero and `false` are falsy

Bitwise expressions are valid as conditions without explicit comparison:

```xml
present-when="flags & 0x80"     <!-- true if bit 7 is set -->
present-when="version >= 2 and has-extension"
```

## Non-Negative Results

Expressions in `length-from` and `count-from` must evaluate to a non-negative integer at decode time. A negative result produces a decode error.

## Examples

```xml
<!-- Conditional presence -->
present-when="flags & 0x80"
present-when="version >= 2 and has-extension"
present-when="remaining >= 4"

<!-- Length computation -->
length-from="total-length - header-size"
length-from="payload-length"

<!-- Count computation -->
count-from="num-items - 1"

<!-- Discriminator -->
switch="msg-type"
switch="header.type & 0x0F"
switch="category"
```

## Validation Rules

- No forward references (fields must be previously decoded)
- No division or modulo by zero in constant expressions (caught at validation time)
- Constant references must exist (undefined constants are errors)
- `remaining` only valid in bounded contexts
- Expression nesting depth is limited to 128 levels. Expressions exceeding this limit produce a parse error.
- Shift amounts must be in the range [0, 64). A shift expression with a constant amount outside this range is a validation error.

## Auto Expression Grammar

The `auto` attribute on fields uses a **separate, simpler grammar** that is distinct from the expression language described above. Auto expressions are not general-purpose expressions — they follow a fixed pattern:

```
auto-expr = "id" | "increment" | "timestamp"
          | "length" [arith-modifier]
          | "length(" field-ref ")" [arith-modifier]
          | "count(" field-ref ")"
          | "config(" key ")"

arith-modifier = ("+" | "-" | "*" | "/" | "%") (number | field-ref)
```

Auto expressions are parsed by a dedicated parser (`auto_expr_parser`), not by the general expression parser. They do not support full expression syntax — only a single arithmetic modifier with one operand is allowed.

See [Fields](fields.md#auto-managed-fields) for details on each auto expression kind.

## Best Practices

- Use [constants](constants.md) for magic numbers in expressions to improve readability.
- When subtracting from a hyphenated field name, always use spaces: `total-length - 3`.

## Common Pitfalls

- Hyphenated identifiers use maximal munch: `total-length` is ONE identifier, not `total` minus `length`. To subtract, add spaces: `total-length - 3`.
- Forward references are not allowed. Expressions can only reference fields that appear before them in declaration order (which equals wire order).
- `remaining` is only valid in bounded contexts. Using it where the container size is unknown is a validation error.
- Constant references must be `UPPER_SNAKE_CASE`. The parser uses casing to distinguish constants from field references.
