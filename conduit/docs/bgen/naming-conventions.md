# Naming Conventions

[Back to index](index.md)

bgen converts BMDL names to C++ identifiers using consistent rules. All conversions are implemented in `name_utils.hpp`.

> **Core rule:** All hyphens (`-`) in BMDL names become underscores (`_`) in C++. Casing is preserved -- bgen does not convert between PascalCase and snake_case (except for factory function names). BMDL authors should choose names that are already valid C++ after hyphen substitution.

## Field Names

BMDL field names use kebab-case. bgen converts them to snake_case:

| BMDL Name | C++ Member | C++ Accessor (getter) | C++ Setter | C++ Mutable |
|-----------|-----------|----------------------|------------|-------------|
| `my-field` | `my_field_` | `my_field()` | `set_my_field()` | `mutable_my_field()` |
| `msg_type` | `msg_type_` | `msg_type()` | `set_msg_type()` | `mutable_msg_type()` |
| `x` | `x_` | `x()` | `set_x()` | `mutable_x()` |

The conversion:
- **`to_snake_case(name)`**: Replaces all hyphens with underscores
- **`to_member_name(name)`**: `to_snake_case(name) + "_"` (trailing underscore)
- **`to_accessor_name(name)`**: `to_snake_case(name)` (no trailing underscore)
- **`to_pascal_case(name)`**: Capitalizes each segment separated by hyphens/underscores (e.g., `"my-field"` -> `"MyField"`, `"heartbeat"` -> `"Heartbeat"`). Defined in `name_utils.hpp` but not currently used by codegen.

## Type Names

BMDL type names are used as-is with hyphens replaced by underscores:

| BMDL Name | C++ Type Name |
|-----------|---------------|
| `uint16` | `uint16` |
| `msg-type` | `msg_type` |
| `MsgType` | `MsgType` |
| `status-flags` | `status_flags` |

The conversion function is **`to_cpp_type_name(name)`**, which is equivalent to `to_snake_case(name)`.

Note: BMDL developers are expected to use C++-compatible names in their definitions. bgen does not attempt PascalCase conversion for type names -- it preserves the original casing with only hyphen-to-underscore substitution.

## Enum Value Names

Enum values follow the same rule as type names -- hyphens become underscores:

| BMDL Value | C++ Enum Value |
|------------|----------------|
| `heartbeat` | `heartbeat` |
| `Heartbeat` | `Heartbeat` |
| `data-msg` | `data_msg` |
| `DATA_MSG` | `DATA_MSG` |

The conversion function is **`to_enum_value_name(name)`**, equivalent to `to_snake_case(name)`.

## Constant Names

Constants are emitted **verbatim** using their BMDL name with no conversion applied (no hyphen-to-underscore substitution):

| BMDL Name | C++ Constant |
|-----------|-------------|
| `SYNC_WORD` | `SYNC_WORD` |
| `MAX_LENGTH` | `MAX_LENGTH` |

Constants are emitted as `inline constexpr <type> NAME = value;`. Since no name conversion is applied, BMDL authors should use C++-compatible constant names (typically `UPPER_SNAKE_CASE`).

## Namespace Names

The C++ namespace is derived from the protocol name or `--namespace` argument:

| Source | C++ Namespace |
|--------|--------------|
| `my-protocol` | `my_protocol` |
| `asterix` | `asterix` |
| `my::custom::ns` | `my::custom::ns` |

Hyphens are converted to underscores. The `::` separator is preserved for nested namespaces.

## Storage Type Selection

Integer field bit widths map to the smallest C++ type that fits:

| Bit Width | Unsigned | Signed |
|-----------|----------|--------|
| 1-8 | `uint8_t` | `int8_t` |
| 9-16 | `uint16_t` | `int16_t` |
| 17-32 | `uint32_t` | `int32_t` |
| 33-64 | `uint64_t` | `int64_t` |

Float types: up to 32 bits -> `float`, 33-64 bits -> `double`.

The conversion functions are **`storage_type_for_bits(bits, is_signed)`** and **`primitive_cpp_type(base, bits, is_signed)`**.

## Session and Factory Names

Entry-point names are converted for session class and factory function names:

| BMDL Entry-Point | Session Class | Factory Function |
|------------------|--------------|-----------------|
| `Frame` | `FrameSession` | `create_frame_session()` |
| `MyFrame` | `MyFrameSession` | `create_my_frame_session()` |
| `data-frame` | `data_frameSession` | `create_data_frame_session()` |

The session class uses `to_cpp_type_name(name) + "Session"`. The factory function uses `"create_" + to_lower_snake_case(name) + "_session"`.

**`to_lower_snake_case(name)`** converts PascalCase to snake_case by inserting underscores before uppercase letters that follow a non-uppercase, non-separator character. Hyphens and underscores are both normalized to `_`. Consecutive uppercase letters are **not** separated -- e.g. `"HTTPParser"` becomes `"httpparser"`, not `"h_t_t_p_parser"`.

## Child Class Names

Inline structs, array elements, and choice cases that define children inline generate child classes. The naming uses the parent context to avoid collisions:

- Inline struct children: `to_cpp_type_name(bmdl_name)` (with parent disambiguation if needed)
- Array elements: `to_cpp_type_name(array_name + "Element")`
- Otherwise cases: `to_cpp_type_name(choice_name + "Otherwise")`

## Variant Type Aliases

Choice fields generate a `using` alias for their `std::variant`:

| BMDL Choice Name | C++ Variant Alias |
|-------------------|------------------|
| `payload` | `payloadVariant` |
| `msg-body` | `msg_bodyVariant` |

The alias is `to_cpp_type_name(choice_name) + "Variant"`.

## C++ Keyword Avoidance

`name_utils.hpp` defines **`is_cpp_keyword(name)`**, which checks identifiers against all standard C++20 keywords and alternative operator tokens (`and`, `or`, `not`, `bitand`, `bitor`, etc.). It returns `true` if the name would collide with a C++ keyword. The validator calls this during validation to warn when a BMDL name would produce a C++ keyword after conversion (e.g., a field named `or` would warn about colliding with the C++ keyword `or`).

## Type Shadowing

When a struct field has the same name as its type after conversion (e.g., field `msg_type` of type `msg_type`), the generated code qualifies the type with the full namespace to avoid GCC `-Wchanges-meaning` errors:

```cpp
// Instead of: msg_type msg_type_{};
::my_protocol::msg_type msg_type_{};  // Qualified to avoid shadowing
```
