# Conduit Library Audit Report

## Summary

Deep inspection of the Conduit library codebase (726 source files) covering C++
core, Java/Python code generation backends, session codegen, and test coverage.
Findings are prioritized by severity and validated to eliminate false positives.

---

## CRITICAL BUGS (Will cause compilation errors in generated code)

### 1. Java bitmap codegen references non-existent `readS16`/`readS32`/`writeS16`/`writeS32` methods

**Files:** `bgen/src/codegen/java_backend.cpp` lines 1533-1534, 1572-1573, 1648-1649, 1679-1680

The bitmap struct decode/encode code generates calls to `r.readS16()`,
`r.readS32()`, `w.writeS16()`, and `w.writeS32()`, but the generated
`BitReader.java` and `BitWriter.java` classes never define these methods. The
generated BitReader only provides: `readU8`, `readU16`, `readU32`, `readU64`,
`readBits`, `readSignedBits`. The generated BitWriter only provides: `writeU8`,
`writeU16`, `writeU32`, `writeU64`, `writeBits`, `writeSignedBits`.

Any protocol with a bitmap/FSPEC struct containing signed 16-bit or 32-bit
integer fields will produce Java code that **fails to compile**.

**Fix:** Replace `readS16`/`readS32` calls with appropriate `readSignedBits(N)`
calls, and `writeS16`/`writeS32` with `writeSignedBits(value, N)` calls.

### 2. Java bitmap string encode calls `writeString` with wrong argument count

**File:** `bgen/src/codegen/java_backend.cpp` lines 1658, 1660

The generated `BitWriter.writeString` method signature is
`writeString(String s, int len, int pad)` (3 parameters), but the bitmap encode
code generates calls with only 2 arguments:
- Line 1658: `w.writeString(m, len)` -- missing `pad` argument
- Line 1660: `w.writeString(m, m.length())` -- missing `pad` argument

The non-bitmap encode path (line 1057) correctly passes all 3 arguments.

**Fix:** Add the `pad` parameter (typically `0` for null padding or `0x20` for
space padding based on field attributes).

### 3. Java missing type-level string wrapper class generation

**File:** `bgen/src/codegen/java_backend.cpp` line 3197

The type generation condition is:
```cpp
if (is_enum || is_flags || has_scale || t.constraint)
```

This omits `is_string` (present as `is_string_type` in the C++ backend at
`cpp_structs_helpers.cpp:133`). If a field references a type-level string
definition (e.g., `type="callsign-str"`), the generated Java code will call
`CallsignStr.decode(r)` on a class that was never generated, causing a
**compilation failure**.

The Python backend handles this correctly at line 897
(`else if (is_string && t.length)`).

**Fix:** Add `|| is_string` to the type generation condition, and implement
string wrapper class generation for Java (matching C++ `emit_string_type`).

---

## HIGH SEVERITY BUGS (Wrong runtime behavior or silent data corruption)

### 4. Java and Python bitmap codegen: CB2 wire encoding wrongly treated as sign-magnitude

**Files:**
- `java_backend.cpp` lines 1568-1569, 1675-1676
- `python_backend.cpp` lines 1724, 1823

Both Java and Python bitmap codegen group `WireEncoding::CB2` with
`WireEncoding::BNR_S` and treat it as sign-magnitude encoding. However, the C++
codegen (`cpp_structs_helpers.cpp:296`) treats CB2 as **standard two's
complement** (same read path as Default). This means bitmap fields with CB2 wire
encoding will decode/encode incorrectly in Java and Python.

**Fix:** Remove CB2 from the sign-magnitude branch and handle it as standard
two's complement.

### 5. Java bitmap choice decode is stubbed out (TODO)

**File:** `bgen/src/codegen/java_backend.cpp` line 1515

```cpp
ctx.line(m + " = null; // TODO: choice decode in bitmap");
```

When a bitmap/FSPEC struct contains a choice field controlled by a bit, the Java
decode code simply assigns `null`. This means any protocol with choice fields
inside bitmap structs will silently produce `null` values in Java.

### 6. Python bitmap codegen silently ignores choice fields

**File:** `bgen/src/codegen/python_backend.cpp` lines 1673+

Unlike Java which at least has a TODO stub, Python's bitmap decode code doesn't
check for `bf.is_choice` at all. If `bf.is_choice` is true and `bf.is_struct`
is false, the code falls through to primitive handling, potentially producing
wrong data or runtime errors.

### 7. Python `encode_batch` missing `id_field` and `timestamp_fields` assignment

**File:** `bgen/src/codegen/python_backend.cpp` lines 2994-3027

Python's `encode_batch` creates a raw frame (`frame = FrameClass()`) but does
NOT set:
- The `id_field_name` (present in C++ at `cpp_session.cpp:269-271` and Java at
  `java_backend.cpp:3051-3053`)
- The `timestamp_fields` (present in C++ at `cpp_session.cpp:315-328` and Java
  at `java_backend.cpp:3095-3104`)

This means batch-encoded frames will have missing message type identifiers and
timestamps, causing decode failures or wrong message routing.

### 8. Python `decode_frame` return type annotation is wrong

**File:** `bgen/src/codegen/python_backend.cpp` lines 2899, 2907

The return type annotation says `list[dict]` but the method returns `None` on
decode failure (line 2907: `return None`). Should be
`list[dict] | None` or `Optional[list[dict]]`.

### 9. Java `ShiftRight` expression uses arithmetic shift `>>` instead of logical `>>>`

**File:** `bgen/src/codegen/java_backend.cpp` line 122

Java's `>>` is arithmetic right shift (sign-extending), while `>>>` is logical
right shift (zero-extending). For unsigned integer expressions, `>>` will
propagate sign bits. C++ `>>` performs logical shift on unsigned types, so the
Java code produces different results when shift-right expressions operate on
unsigned data.

---

## MEDIUM SEVERITY ISSUES

### 10. Python `decode_frame` swallows all exceptions silently

**File:** `bgen/src/codegen/python_backend.cpp` lines 2901-2908

```python
try:
    frame = FrameClass.decode_bytes(data)
except Exception:
    return None
```

All exceptions (including programming bugs like `AttributeError`,
`TypeError`, etc.) are caught and silently converted to `None`. The C++ version
properly propagates specific error codes. The Java version does not wrap in
try/catch. This makes Python protocol debugging very difficult.

### 11. Python session missing send-only message warnings

**File:** `bgen/src/codegen/python_backend.cpp`

C++ and Java `decode_frame` methods both emit warnings when receiving send-only
message types. Python's `decode_frame` has no equivalent logging/warning.

### 12. Java/Python sessions missing `auto_fields` metadata in encode results

**Files:** `java_backend.cpp` lines 2949-3023, `python_backend.cpp` lines 2924-2981

C++ session's `encode_wrap` and `encode_batch` record auto-managed fields
(sequence counter values, timestamps, config fields) into
`result.auto_fields`. Neither Java nor Python return this metadata --
they only return `{'bytes': data, 'type_id': type_id}`.

### 13. Java enum value type is always `int` regardless of bit width

**File:** `bgen/src/codegen/java_backend.cpp` lines 3213-3214, 3218

All Java enums use `int` for their value. The `readBits` call casts to `int`,
which would **truncate values** for enums wider than 32 bits. C++ uses the
appropriate sized type.

### 14. Java constants are all generated as `long` without `L` suffix validation

**File:** `bgen/src/codegen/java_backend.cpp` line 782

All constants are typed as `long`, and the value string is used as-is from BMDL.
If `c.value` is a hex literal like `"0xFFFFFFFF"` without `L` suffix, Java may
interpret it as a negative `int` before widening.

### 15. Python string type wrappers only generated for fixed-length strings

**File:** `bgen/src/codegen/python_backend.cpp` line 897

The condition `is_string && t.length` means only fixed-length string types get
wrapper classes. String types with `char_bits` (packed), `terminated`, or other
attributes are silently skipped. C++ handles all variants.

---

## PERFORMANCE ISSUES

### 16. Generated Java/Python BitReader reads multi-byte values bit-by-bit

**Files:**
- `java_backend.cpp` lines 502-503 (readU16), 508-509 (readU32), etc.
- `python_backend.cpp` lines 483-485 (read_u16), 488-489 (read_u32), etc.

Multi-byte reads like `readU16` call `readBits(8)` twice through the bit-level
loop, then wrap in a ByteBuffer/struct.unpack. The C++ `BitReader` uses
`align_to_byte()` and direct memory access for byte-aligned multi-byte reads,
which is significantly faster. For high-throughput protocol parsing, this bit-by-
bit approach creates unnecessary overhead.

---

## TEST COVERAGE GAPS

### 17. No tests for bitmap choice decode (Java or Python)

Given that bitmap choice decode is incomplete (TODO stub in Java, missing in
Python), there are no tests that would catch these failures. Any protocol using
choice fields inside FSPEC/bitmap structs is untested.

### 18. No tests for type-level string wrappers in Java

Since Java doesn't generate string type wrappers (Bug #3), there are no
corresponding tests. Python does have some string type handling but it's limited
to fixed-length strings.

### 19. CB2 wire encoding not tested in bitmap contexts

The CB2 mishandling as sign-magnitude (Bug #4) is untested. Tests exist for BCD
and BNR_S but not for CB2 specifically in bitmap codegen paths.

### 20. Missing edge case tests for signed types in Java bitmap codegen

The `readS16`/`readS32`/`writeS16`/`writeS32` bug (#1) would be caught by any
test using signed 16-bit or 32-bit fields inside bitmap structs, but no such
tests exist.

### 21. Python encode_batch with id_field is untested

Tests cover `encode_wrap` (which uses `Frame.wrap()`) but don't test
`encode_batch` with protocols that have `id_field_name` set, which would reveal
Bug #7.

---

## ARCHITECTURAL DISCREPANCIES (Java/Python vs C++)

| Feature | C++ | Java | Python |
|---------|-----|------|--------|
| Type-level string wrappers | Full | Missing | Partial (fixed-length only) |
| Bitmap choice decode | Full | TODO stub | Missing |
| CB2 wire encoding | Two's complement | Sign-magnitude (wrong) | Sign-magnitude (wrong) |
| `auto_fields` metadata | Full | Missing | Missing |
| Send-only warnings | Full | Full | Missing |
| `encode_batch` id_field | Full | Full | Missing |
| `encode_batch` timestamps | Full | Full | Missing |
| `equals`/`hashCode` | `operator==` default | Missing | `__eq__` partial |
| JSON serialization | Full | Missing | Missing |
| Deferred constraint validation | `validate()` method | No separate method | Missing |
| Sequence counter accessor | `sequence_counter()` | `sequenceCounter()` | Missing |
| `readS16`/`readS32` methods | N/A (uses typed reads) | Missing (compile error) | N/A |

---

## Files Affected

| File | Bug IDs |
|------|---------|
| `bgen/src/codegen/java_backend.cpp` | #1, #2, #3, #4, #5, #9, #12, #13, #14 |
| `bgen/src/codegen/python_backend.cpp` | #4, #6, #7, #8, #10, #11, #12, #15 |
| `bgen/src/codegen/cpp_session.cpp` | (reference baseline) |
| `bgen/src/codegen/cpp_structs_helpers.cpp` | (reference baseline) |
