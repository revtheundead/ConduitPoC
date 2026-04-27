# Naming Conventions

[Back to index](index.md)

bgen converts BMDL names to language-appropriate identifiers. The core C++ conversion rules are implemented in `name_utils.hpp`. Java and Python backends apply additional language-specific conventions.

> **Core rule (C++):** All hyphens (`-`) in BMDL names become underscores (`_`). Casing is preserved -- bgen does not convert between PascalCase and snake_case (except for factory function names). BMDL authors should choose names that are already valid C++ after hyphen substitution.

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
- **`to_pascal_case(name)`**: Capitalizes each segment separated by hyphens/underscores (e.g., `"my-field"` -> `"MyField"`, `"heartbeat"` -> `"Heartbeat"`). Used by codegen for inline enum type names (see [Inline Enum Names](#inline-enum-names)).

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

Frame names are converted for session class and factory function names:

| BMDL Frame Name | Session Class | Factory Function |
|------------------|--------------|-----------------|
| `Frame` | `FrameSession` | `create_frame_session()` |
| `MyFrame` | `MyFrameSession` | `create_my_frame_session()` |
| `data-frame` | `data_frameSession` | `create_data_frame_session()` |

The session class uses `to_cpp_type_name(name) + "Session"`. The factory function uses `"create_" + to_lower_snake_case(name) + "_session"`.

**`to_lower_snake_case(name)`** converts PascalCase to snake_case by inserting underscores before uppercase letters that follow a non-uppercase, non-separator character. Hyphens and underscores are both normalized to `_`. Consecutive uppercase letters are **not** separated -- e.g. `"HTTPParser"` becomes `"httpparser"`, not `"h_t_t_p_parser"`.

## Child Class Names

Inline structs, array elements, and choice cases that define children inline generate child classes. Child names are **always** prefixed with the parent's resolved C++ class name using `resolve_child_class_name()`:

```
parent_cpp_name + "_" + to_cpp_type_name(child_bmdl_name)
```

This applies recursively -- a grandchild inherits the fully-qualified parent name:

| Context | BMDL Name | Resolved C++ Name |
|---------|-----------|-------------------|
| Top-level struct `items` | `items` | `items` |
| Child struct `spf` inside message `Cat007UplinkRecord` | `spf` inside `items` | `Cat007UplinkRecord_items_spf` |
| Array element inside message `MyMessage` | `records` (array) | `MyMessage_recordsElement` |
| Otherwise case of choice `payload` in `MyMessage` | `payload` (otherwise) | `MyMessage_payloadOtherwise` |

Specific patterns:
- **Inline struct children:** `parent + "_" + to_cpp_type_name(bmdl_name)`
- **Array elements:** `parent + "_" + to_cpp_type_name(array_name + "Element")`
- **Otherwise cases:** `parent + "_" + to_cpp_type_name(choice_name + "Otherwise")`

Top-level structs and messages (with empty `parent_name`) use `to_cpp_type_name(bmdl_name)` directly.

## `typeName` Override

The `typeName` attribute overrides the auto-generated class name for inline definitions. When present, the value is used directly as the C++ class name instead of the parent-prefixed convention:

| Element | BMDL | Default Name | With `typeName` |
|---------|------|-------------|-----------------|
| Inline case | `<case name="heartbeat" typeName="HeartbeatPayload">` | `MyMessage_heartbeat` | `HeartbeatPayload` |
| Otherwise | `<otherwise name="unknown" typeName="UnknownPayload">` | `MyMessage_payloadOtherwise` | `UnknownPayload` |
| Inline struct | `<struct name="header" typeName="MsgHeader">` | `MyMessage_header` | `MsgHeader` |
| Array element | `<array name="items" typeName="ArrayItem">` | `MyMessage_itemsElement` | `ArrayItem` |
| Choice variant | `<choice name="payload" typeName="MessagePayload">` | `MyMessage_PayloadVariant` | `MessagePayload` |

**Restrictions:**

- Only valid on inline definitions (cannot combine with `type` attribute)
- Must be a valid C++ identifier (letters, digits, underscores; cannot start with a digit)
- Must not be a C++ keyword or reserved identifier (no `__` prefix)
- Must not conflict with any type, struct, or message name in the protocol
- Must be unique across all `typeName` values in the protocol
- Not valid on top-level `<struct>` definitions (which already have proper names)

See [choices documentation](../bmdl/choices.md#naming-with-typename) for usage examples.

## Inline Enum Names

Fields with inline `<enum>` definitions generate a standalone enum type. The name uses `to_pascal_case` on both the parent class name and the field name:

```
to_pascal_case(current_parent) + "_" + to_pascal_case(field_name)
```

| Parent Class | Field Name | Enum Type Name |
|-------------|------------|----------------|
| `Cat048Record` | `msg-type` | `Cat048Record_MsgType` |
| `Heartbeat` | `status` | `Heartbeat_Status` |
| `Cat007UplinkRecord_items` | `code` | `Cat007UplinkRecordItems_Code` |

Note that `to_pascal_case` converts underscores to word boundaries, so `Cat007UplinkRecord_items` becomes `Cat007UplinkRecordItems` in the enum prefix.

## Variant Type Aliases

Choice fields generate a namespace-scope `using` alias for their `std::variant`. The alias is always prefixed with the parent class name:

```
to_cpp_type_name(current_parent) + "_" + to_cpp_type_name(choice_name) + "Variant"
```

| Parent Class | BMDL Choice Name | C++ Variant Alias |
|-------------|-------------------|------------------|
| `MyMessage` | `payload` | `MyMessage_payloadVariant` |
| `Cat048Record` | `msg-body` | `Cat048Record_msg_bodyVariant` |

The alias is emitted as a namespace-scope `using` declaration before the parent class definition. When `current_parent_` is empty (top-level), the alias omits the prefix (e.g., `payloadVariant`).

The `typeName` attribute on `<choice>` overrides this variant alias name entirely. For example, `<choice name="payload" typeName="MessagePayload">` generates `using MessagePayload = std::variant<...>` instead of `MyMessage_payloadVariant`.

## Reserved Keyword Handling

Each backend handles reserved-word collisions differently. Authoring a BMDL
field whose generated name would clash with a target language's keyword can
fail at validation, get auto-renamed, or both — depending on which language
the validator knows about and whether the keyword is shared.

| Backend | Strategy | Where it happens |
|---------|----------|------------------|
| C++ | **Validation error.** A name that maps to a C++23 keyword (or alternative operator token) is rejected before codegen runs. | `is_cpp_keyword(name)` in `bgen/src/codegen/name_utils.hpp`, called from the validator's `check_keyword_collision()`. |
| Java | **Auto-rename: trailing `_` suffix.** A field whose camelCase form is a Java reserved word (or one of the literals `true`, `false`, `null`) is emitted with `_` appended (e.g. `interface` → `interface_`, `class` → `class_`). | `is_java_keyword(name)` / `j_field()` in `bgen/src/codegen/java_backend.cpp`. |
| Python | **Auto-rename: trailing `_` suffix.** A field whose snake_case form is a Python reserved word is emitted with `_` appended (e.g. `class` → `class_`, `for` → `for_`, `True` → `True_`). | `is_py_keyword(name)` / `py_field()` in `bgen/src/codegen/python_backend.cpp`. |

> **Cross-language consequence:** Names that are reserved in C++ but *not* in
> Java/Python (or vice versa) behave asymmetrically. For example, `class` is
> reserved in all three: C++ rejects it, Java emits `class_`, Python emits
> `class_`. But `interface` is only reserved in Java: a field named
> `interface` validates and compiles in C++ and Python, but the Java backend
> renames it to `interface_`. If you need stable cross-backend identifiers,
> avoid any name in the union of all three keyword sets.

Examples of auto-renamed fields:

| BMDL field | C++ | Java | Python |
|------------|-----|------|--------|
| `class` | *validation error* | `class_` | `class_` |
| `interface` | `interface()` / `set_interface()` | `interface_` (renamed) | `interface` (not a Py keyword) |
| `for` | *validation error* | `for_` | `for_` |
| `null` | `null()` / `set_null()` | `null_` (renamed) | `null` (not a Py keyword) |

## Type Shadowing

When a struct field has the same name as its type after conversion (e.g., field `msg_type` of type `msg_type`), the generated code qualifies the type with the full namespace to avoid GCC `-Wchanges-meaning` errors:

```cpp
// Instead of: msg_type msg_type_{};
::my_protocol::msg_type msg_type_{};  // Qualified to avoid shadowing
```

---

## Java Naming Conventions

The Java backend converts BMDL names to Java-idiomatic identifiers:

- **Fields:** camelCase -- `msg-type` becomes `msgType`, `sensor-id` becomes `sensorId`
- **Classes:** PascalCase -- `Heartbeat` stays `Heartbeat`, `sensor-reading` becomes `SensorReading`
- **Constants:** UPPER_SNAKE_CASE -- `SYNC_WORD` stays `SYNC_WORD`
- **Enum values:** UPPER_SNAKE_CASE -- `heartbeat` becomes `HEARTBEAT`
- **Packages:** Derived from namespace, lowercase with dots

| BMDL Name | Context | Java Name |
|-----------|---------|-----------|
| `msg-type` | field | `msgType` |
| `sensor-id` | field | `sensorId` |
| `Heartbeat` | message | `Heartbeat` |
| `status-flags` | type | `StatusFlags` |
| `MyFrame` | session | `MyFrameSession` |

---

## Python Naming Conventions

The Python backend converts BMDL names to Python-idiomatic identifiers:

- **Fields/attributes:** snake_case -- `msg-type` becomes `msg_type` (same as C++)
- **Classes:** PascalCase -- `Heartbeat` stays `Heartbeat`, `data-frame` becomes `DataFrame`
- **Constants:** UPPER_SNAKE_CASE -- same as BMDL
- **Enum values:** UPPER_SNAKE_CASE -- `heartbeat` becomes `HEARTBEAT`
- **Modules:** snake_case -- derived from namespace

| BMDL Name | Context | Python Name |
|-----------|---------|-------------|
| `msg-type` | field | `msg_type` |
| `sensor-id` | field | `sensor_id` |
| `Heartbeat` | message | `Heartbeat` |
| `status-flags` | type | `StatusFlags` |
| `MyFrame` | session | `MyFrameSession` |

---

## Cross-Language Name Comparison

| BMDL | C++ | Java | Python |
|------|-----|------|--------|
| `msg-type` (field) | `msg_type()` / `set_msg_type()` | `msgType` (public field) | `msg_type` (attribute) |
| `sensor-id` (field) | `sensor_id()` / `set_sensor_id()` | `sensorId` (public field) | `sensor_id` (attribute) |
| `Heartbeat` (message) | `class Heartbeat` | `class Heartbeat` | `class Heartbeat` |
| `status-flags` (type) | `class status_flags` | `class StatusFlags` | `class StatusFlags` |
| `MyFrame` (session) | `MyFrameSession` / `create_my_frame_session()` | `MyFrameSession` | `MyFrameSession` |
| `SYNC_WORD` (constant) | `SYNC_WORD` | `SYNC_WORD` | `SYNC_WORD` |
