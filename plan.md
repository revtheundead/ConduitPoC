# Plan: Fix Codegen Class Overlap and Missing Attribute Support

## Problem Summary

The Java and Python code generation backends have two categories of critical bugs:

1. **Inline type class name collisions**: When multiple messages/structs define inline children with the same BMDL name (e.g., `items`, `TypeA`), the generated classes silently overwrite each other. In Java, `collect_inline_types()` outputs `j_class(sd->name) + ".java"` without any parent prefix, so `MsgAlpha.TypeA` and `MsgBeta.TypeA` both produce `TypeA.java` — the second overwrites the first. In Python, `emit_py_class(ctx, sd->name, ...)` produces a flat `class TypeA:` — the second definition replaces the first in the same module.

2. **Ignored AST attributes**: The `typeName` attribute on `StructDef`, `ArrayDef`, `ChoiceDef`, `CaseDef`, and `OtherwiseDef` is fully parsed into the AST (`std::optional<std::string> type_name`) and respected by the C++ backend, but completely ignored by both Java and Python backends. Additional attributes that may be partially or fully ignored are enumerated below.

3. **Weak test coverage**: Existing tests for the `inline_case_collision.bmdl.xml` fixture only assert `CHECK(!py->files.empty())` — they don't verify that distinct messages produce distinct classes with correct contents, so the overwrite bug was never caught.

## Root Cause Analysis

### C++ Backend (Reference — Correct)

The C++ backend in `cpp_structs.cpp:828-892` (`emit_child_class_defs`) uses a multi-layer strategy:

- **Parent-prefixed naming**: `resolve_child_class_name()` at `cpp_structs_expr.cpp:288-301` prepends the parent class name: `to_cpp_type_name(parent_name) + "_" + name`. So `MsgAlpha`'s inline `TypeA` becomes `MsgAlpha_TypeA`, and `MsgBeta`'s becomes `MsgBeta_TypeA`.
- **`typeName` override**: Before the parent prefix is applied, it checks `type_name_overrides_` — if `sd->type_name` is set, that value is used directly instead of the auto-generated parent-prefixed name.
- **Deduplication set**: `emitted_classes_` prevents any class from being emitted twice.

### Java Backend (Broken)

`java_backend.cpp:1402-1440` (`collect_inline_types`) does **not** pass `current_type_name` (the parent) through to the output filename or class name. On line 1414:
```cpp
out_files.push_back({j_class(sd->name) + ".java", code});
```
And `generate_j_class` at line 1176 uses `j_class(name)` directly. Neither incorporates the parent name. The `type_name` field of the AST nodes is never read.

### Python Backend (Broken)

`python_backend.cpp:1487-1520` (`emit_py_inline_types`) passes `sd->name` directly to `emit_py_class`, which at line 1356 emits `class PascalCase(sd->name):` — no parent prefix. The `type_name` field is never read.

---

## Implementation Plan

### Phase 1: Fix Inline Type Name Collision in Java Backend

**File**: `conduit/bgen/src/codegen/java_backend.cpp`

**Step 1a**: Add a name resolution function for Java inline types that mirrors C++ logic.

Create a helper function (similar to C++'s `resolve_child_class_name`):
```cpp
std::string j_resolve_inline_name(const std::string& bmdl_name,
                                   const std::string& parent_name,
                                   const std::optional<std::string>& type_name_override) {
    if (type_name_override && !type_name_override->empty())
        return *type_name_override;
    std::string name = j_class(bmdl_name);
    if (!parent_name.empty())
        name = j_class(parent_name) + name;
    return name;
}
```

**Step 1b**: Modify `collect_inline_types` to accept and propagate `parent_name`, and use the new resolver for both:
  - The class name passed to `generate_j_class` (so the Java class declaration uses the prefixed name)
  - The output filename: `resolved_name + ".java"`

For each inline type category:
  - **StructDef**: `j_resolve_inline_name(sd->name, current_type_name, sd->type_name)`
  - **ArrayDef** (anonymous): `j_resolve_inline_name(ad->name, current_type_name, ad->type_name)`
  - **CaseDef**: `j_resolve_inline_name(cs.name, current_type_name, cs.type_name)`
  - **OtherwiseDef**: `j_resolve_inline_name(cd->otherwise->name, current_type_name, cd->otherwise->type_name)`

**Step 1c**: Update all call sites within `collect_inline_types` to pass the resolved name through to `generate_j_class` and the output file pair. The `generate_j_class` function signature may need an additional `class_name` parameter (distinct from `bmdl_name` which is used for `tid_map` lookups and `TYPE_NAME` constants).

**Step 1d**: Update field type resolution. When Java codegen encounters a field whose `type_ref` points to an inline struct/array, the generated Java type reference must use the resolved (parent-prefixed or typeName-overridden) name. This affects:
  - `j_resolve_field()` at line 153: for `is_struct` / `is_enum`, it uses `j_class(f.type_ref)` — this must use the resolved inline name.
  - Decode calls: `j_class(type_ref) + ".decode(r)"` must use the resolved name.
  - Array element instantiation: `new ArrayList<ResolvedName>()`.

To accomplish this, maintain a `std::unordered_map<std::string, std::string>` mapping BMDL inline names to resolved Java class names, populated during `collect_inline_types`, and used in field/decode/encode emission.

### Phase 2: Fix Inline Type Name Collision in Python Backend

**File**: `conduit/bgen/src/codegen/python_backend.cpp`

**Step 2a**: Add analogous name resolution for Python:
```cpp
std::string py_resolve_inline_name(const std::string& bmdl_name,
                                    const std::string& parent_name,
                                    const std::optional<std::string>& type_name_override) {
    if (type_name_override && !type_name_override->empty())
        return to_pascal_case(*type_name_override);
    std::string name = py_class(bmdl_name);
    if (!parent_name.empty())
        name = py_class(parent_name) + name;
    return name;
}
```

**Step 2b**: Modify `emit_py_inline_types` to resolve names using parent prefix and pass the resolved name to `emit_py_class`.

**Step 2c**: Update `emit_py_class` to accept an optional resolved class name (distinct from bmdl_name) for the `class` declaration, while still using `bmdl_name` for `TYPE_NAME` string constants and `tid_map` lookups.

**Step 2d**: Update field type references in Python codegen: `py_resolve_field()`, decode calls, and array instantiation must use the resolved inline name (same inline-name-map approach as Java).

### Phase 3: Handle `typeName` Attribute in Both Backends

This is implicitly covered by Steps 1a/2a — the resolver functions check `type_name_override` first. However, we must also handle `typeName` on:

- **`ChoiceDef.type_name`**: In C++, this overrides the variant alias name. Java doesn't have variant types (uses `Object` or interface), but the naming is still relevant for any case-dispatch wrapper classes. Python uses Union type hints. Both should respect `typeName` for any wrapper/alias naming.
- **`ArrayDef.type_name`**: When an array has inline children, the element class name should use `typeName` if present.
- **`OtherwiseDef.type_name`**: The otherwise case class name should use `typeName`.

### Phase 4: Audit and Fix Other Missing Attribute Support

Perform a systematic comparison of all AST attributes handled by C++ but missed by Java/Python. Based on analysis:

**4a. `auto="increment"` and `auto="timestamp"` in struct/message encode**
- C++ handles these via the session layer (they're frame-level auto fields managed by the session).
- Java and Python handle `auto="id"`, `auto="length"`, and `auto="count"` in struct encode, and handle `auto="increment"`, `auto="timestamp"`, `auto="config"` in session encode.
- Verify: do Java/Python correctly handle ALL `AutoKind` variants in ALL contexts (struct encode, frame encode, decode)? If any are missing or incomplete, fix them.

**4b. `auto="length(field)"` with `ArithModifier` using field operands**
- The `ArithModifier` struct supports `field_ref` (field operand). C++ handles `modifier.is_field_operand()`. Verify Java/Python handle this case.

**4c. `DisplayFormat` (format="hex", "binary", "octal") — CONFIRMED MISSING**
- C++ respects this in `to_string()` output. **Verified**: Neither Java nor Python references `DisplayFormat`, `format_explicit`, or `Hex` anywhere in their backends. The `toString()`/`__repr__()` output always uses decimal. Must be added.

**4d. Inline enum fields — CONFIRMED MISSING**
- C++ generates inline enums for fields with `enum_values` at `cpp_structs.cpp:880-888`. **Verified**: Neither Java nor Python ever references `f->enum_values` or `f.enum_values` on Field objects. Fields that define enum values inline (not via type_ref) are silently treated as plain integers. Must be added — emit a corresponding enum/IntEnum definition and use the enum type for the field.

**4e. `default_value` / `initial` on fields — ALREADY HANDLED**
- Verified: Both Java (`java_backend.cpp:829`) and Python (`python_backend.cpp:1338`) already check `f->default_value` and apply it. No fix needed.

**4f. `Constraint` on fields in encode/decode — PARTIALLY HANDLED**
- Both backends validate constraints at the type level (for named types with constraints). Verify that field-level constraints (`Field.constraint`) are also checked during struct encode/decode, not just at the type wrapper level.

**4g. `present_when` on struct/array/choice elements — ALREADY HANDLED**
- Verified: Both backends handle `present_when` on fields, structs, arrays, and choices. No fix needed.

**4h. `Direction` filtering on CaseDef — LOW PRIORITY**
- Java/Python don't filter cases by direction in struct-level codegen (this is mainly relevant for session-level dispatch, which both backends handle). Lower priority unless real protocols depend on direction-filtered cases within structs.

### Phase 5: Strengthen Test Coverage

**File**: `conduit/bgen/tests/test_java_codegen_extended.cpp` and `conduit/bgen/tests/test_python_codegen_extended.cpp`

**5a. Create a new test fixture** `inline_struct_overlap.bmdl.xml`:
- Define two messages (`MsgFoo`, `MsgBar`) that each contain an inline struct with the same BMDL name (`items`), but with **different fields**.
- This reproduces the exact class-overwrite scenario.

**5b. Add collision tests for Java** that verify:
- Both messages generate code successfully.
- The inline struct classes have **distinct** names (e.g., `MsgFooItems.java` and `MsgBarItems.java`).
- Each generated class contains the correct fields (not the other message's fields).
- The generated classes are referenced correctly by their parent message encode/decode code.

**5c. Add collision tests for Python** that verify:
- The `structs.py` or `messages.py` output contains two distinct class definitions.
- Each class has the correct fields.
- Parent message classes reference the correct child class.

**5d. Add `typeName` tests**: Create a fixture with `typeName` overrides on inline structs, arrays, choice cases, and otherwise. Verify both Java and Python use the overridden names instead of auto-generated names.

**5e. Extend the `inline_case_collision.bmdl.xml` test**: The existing test just checks `!files.empty()`. Add assertions that:
- `MsgAlpha`'s `TypeA` and `MsgBeta`'s `TypeA` produce distinct classes.
- Each class has the correct fields (`alpha-val` vs `beta-x`/`beta-y`).

**5f. Add comprehensive attribute tests**:
- `auto="increment"` in session context.
- `auto="timestamp"` in session context.
- `auto="config(key)"` in session context.
- `auto="length(field) - N"` with arithmetic modifier.
- `typeName` on each element type (struct, array, case, otherwise, choice).
- Inline enums on fields.
- `format="hex"` in toString/repr output.

### Phase 6: Verify Correctness

**6a.** Build and run all tests (C++, Java codegen, Python codegen).

**6b.** Run the `inline_case_collision.bmdl.xml` fixture through both Java and Python backends and manually inspect output to confirm distinct class names.

**6c.** Run any existing integration or roundtrip tests to verify no regressions.

---

## File Change Summary

| File | Changes |
|------|---------|
| `conduit/bgen/src/codegen/java_backend.cpp` | Add `j_resolve_inline_name`, modify `collect_inline_types`, `generate_j_class`, `j_resolve_field`, decode/encode emission to use parent-prefixed names and respect `typeName` |
| `conduit/bgen/src/codegen/python_backend.cpp` | Add `py_resolve_inline_name`, modify `emit_py_inline_types`, `emit_py_class`, `py_resolve_field`, decode/encode emission to use parent-prefixed names and respect `typeName` |
| `conduit/bgen/tests/fixtures/inline_struct_overlap.bmdl.xml` | New fixture: two messages with same-named inline structs having different fields |
| `conduit/bgen/tests/fixtures/type_name_override.bmdl.xml` | New fixture: typeName overrides on struct, array, case, otherwise |
| `conduit/bgen/tests/test_java_codegen_extended.cpp` | New tests for collision, typeName, and attribute coverage |
| `conduit/bgen/tests/test_python_codegen_extended.cpp` | New tests for collision, typeName, and attribute coverage |

## Risk Assessment

- **High confidence**: The parent-prefix naming strategy is proven correct by the C++ backend. Porting it to Java/Python follows the same pattern.
- **Key risk**: Updating field type references (Steps 1d/2d) must be comprehensive — any missed reference will produce broken code that refers to the old unprefixed name. The inline-name-map approach mitigates this by providing a single lookup point.
- **Backward compatibility**: Generated code will have different class names (e.g., `MsgFooItems` instead of `Items`). This is a **breaking change** for any existing users of the Java/Python generated code, but the old behavior was incorrect (silently wrong code), so this is a necessary fix.
