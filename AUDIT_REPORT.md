# Conduit Library Audit Report

## Summary

Deep inspection of the Conduit library codebase covering C++ core, Java/Python
code generation backends, session codegen, and test coverage. Findings are
prioritized by severity and validated against the C++ reference implementation.

---

## FIXED BUGS

### 1. [CRITICAL] Java bitmap codegen referenced non-existent `readS16`/`readS32`/`writeS16`/`writeS32` methods

**File:** `bgen/src/codegen/java_backend.cpp`

The bitmap struct decode/encode code generated calls to `r.readS16()`,
`r.readS32()`, `w.writeS16()`, and `w.writeS32()`, but the generated
`BitReader.java` and `BitWriter.java` classes never define these methods.
Any protocol with a bitmap/FSPEC struct containing signed 16+ bit fields
would produce Java code that **fails to compile**.

**Fix:** All `readS16`/`readS32`/`writeS16`/`writeS32` calls in bitmap codegen
replaced with `readSignedBits(N)`/`writeSignedBits(value, N)`.

### 2. [CRITICAL] Java bitmap string encode called `writeString` with wrong argument count

**File:** `bgen/src/codegen/java_backend.cpp`

BitWriter's `writeString` signature is `writeString(String s, int len, int pad)`
(3 params), but bitmap encode generated calls with only 2 arguments, causing
a compile error.

**Fix:** Added `0` as the third argument for pad.

### 3. [CRITICAL] Java missing type-level string wrapper class generation

**File:** `bgen/src/codegen/java_backend.cpp`

The type generation condition omitted string types (`is_string`). Fields
referencing a type-level string definition (e.g., `type="callsign-str"`)
would produce Java code that calls `CallsignStr.decode(r)` on a class
that was never generated, causing a compilation failure.

**Fix:** Added `is_string` to the type generation condition and implemented
string wrapper class generation with `WIRE_SIZE` constant, `decode`/`encode`
methods, and proper trim handling.

### 4. [CRITICAL] IA5 string encoding performed wrong transformation (Java + Python)

**Files:** `java_backend.cpp`, `python_backend.cpp`

Both backends applied a 6-bit packed character transformation for IA5 strings:
decode masked to `& 0x3F` then added `0x40` for values < 32; encode subtracted
`0x40` for values >= `0x40`. The C++ reference (`encoding.hpp:64-72`) correctly
defines IA5 as 7-bit ASCII — simply masking the high bit with `& 0x7F`. The
incorrect transformation garbles any character outside the 0x00-0x3F range.

**Fix:** Changed both Java and Python to use `& 0x7F` for IA5, matching C++.

### 5. [CRITICAL] CB2 wire encoding wrongly treated as sign-magnitude (Java + Python)

**Files:** `java_backend.cpp`, `python_backend.cpp` (bitmap codegen path)

Both backends grouped `WireEncoding::CB2` with `WireEncoding::BNR_S` and
dispatched to sign-magnitude encoding. The C++ reference (`cpp_types.cpp:62-63`)
treats CB2 as standard two's complement. For negative values, sign-magnitude
and two's complement produce different bit patterns (e.g., -1 in 8-bit:
SM=`0x81`, TC=`0xFF`).

**Fix:** Separated CB2 from BNR_S. CB2 now falls into the
`readSignedBits`/`writeSignedBits` path (two's complement).

### 6. [HIGH] Java bitmap choice decode was a TODO stub

**File:** `bgen/src/codegen/java_backend.cpp`

Bitmap choice fields were assigned `null` with a `// TODO` comment. Any
protocol with choice fields inside bitmap structs would silently produce
null values in Java.

**Fix:** Implemented full choice decode with switch expression evaluation
and case dispatch, matching the non-bitmap choice decode pattern.

### 7. [HIGH] Python bitmap choice decode generated `object.decode(r)` — runtime crash

**File:** `bgen/src/codegen/python_backend.cpp`

When a `ChoiceDef` was registered as a bitmap field, it set
`py_type = "object"` and `is_struct = true`. The decode then generated
`object.decode(r)`, calling Python's builtin `object` class which has no
`decode` method, causing `AttributeError` at runtime.

**Fix:** Added explicit `is_choice` handling in both decode and encode paths
with proper if/elif dispatch based on the choice's `switch_expr`.

### 8. [MEDIUM] Java `ShiftRight` expression used `>>` instead of `>>>`

**File:** `bgen/src/codegen/java_backend.cpp`

Java's `>>` is arithmetic right shift (sign-extending), while protocol
semantics require logical shift (zero-extending), which is `>>>` in Java.

**Fix:** Changed `>>` to `>>>`.

### 9. [MEDIUM] Python EBCDIC conversion table incomplete

**File:** `bgen/src/codegen/python_backend.cpp`

The EBCDIC tables were built from a sparse pair list covering only ~93
printable characters. The C++ reference provides full 256-entry Code Page 037
tables. Any EBCDIC byte not in the Python pair set silently mapped to `0x00`.

**Fix:** Replaced with full 256-entry lookup tables matching C++ exactly.

### 10. [MEDIUM] Python `encode_batch` missing message-level config and timestamps

**File:** `bgen/src/codegen/python_backend.cpp`

`encode_batch` set frame-level config and auto-increment fields, but did NOT
set message-level config fields on individual payload items or auto-timestamp
fields (both present in `encode_wrap`).

**Fix:** Added message-level config field application to each payload item
and auto-timestamp field generation, matching `encode_wrap`.

### 11. [MEDIUM] Python `decode_frame` returned `None` instead of `[]` on error

**File:** `bgen/src/codegen/python_backend.cpp`

The method declares return type `list[dict]` but returned `None` on decode
failure, breaking the type contract.

**Fix:** Changed `return None` to `return []`.

---

## REMAINING KNOWN ISSUES (Not Fixed — Documented for Future Work)

### Missing Features (C++ has, Java/Python don't)

| # | Severity | Feature | Description |
|---|----------|---------|-------------|
| 1 | HIGH | Inline structs (`is_inline`) | C++ flattens inline struct fields into parent; Java/Python nest as sub-objects |
| 2 | HIGH | Deferred constraint validation | C++ generates `validate()` for deferred constraints; Python skips entirely |
| 3 | HIGH | Auto-length with `field_ref` | C++ measures specific child field byte length; Python writes 0 placeholder, never patches for non-empty `field_ref` |
| 4 | MEDIUM | Array `length_from` bounded decode | C++ creates bounded sub-reader; Python reads until reader exhausted |
| 5 | MEDIUM | JSON serialization | C++ generates `to_json`/`from_json`; no equivalent in Java/Python |
| 6 | MEDIUM | `format_outbound` session method | C++ has outbound formatting with auto-field overlays |
| 7 | LOW | `WIRE_SIZE` for struct classes | Only generated for string type classes in Python |
| 8 | LOW | `__eq__`/`__hash__` for structs | Python only generates `__eq__` for type classes, no `__hash__` anywhere |

### Remaining Bugs (Lower Priority)

| # | Severity | Description |
|---|----------|-------------|
| 1 | MEDIUM | Python `__repr__` crashes with `hex(None)` for nullable numeric fields with hex format |
| 2 | MEDIUM | Python missing string trim for `length_from`, `length_prefix`, `length_star` strings |
| 3 | MEDIUM | Java enum value type is always `int` regardless of bit width — truncates values for enums > 32 bits |
| 4 | LOW | Python `length_from` expressions use `py_expr` instead of `py_expr_ctx` (cannot reference outer-scope fields) |
| 5 | LOW | Python `decode_frame` swallows all exceptions silently (including programming bugs) |

### Performance Issues

| # | Description |
|---|-------------|
| 1 | Generated Java/Python BitReader reads multi-byte values bit-by-bit instead of direct memory access |
| 2 | Large reserved fields in Python use `write_bits(0, N)` which loops bit-by-bit |

---

## Architectural Discrepancies (Java/Python vs C++)

| Feature | C++ | Java | Python |
|---------|-----|------|--------|
| Type-level string wrappers | Full | **Fixed** | Partial (fixed-length only) |
| Bitmap choice decode | Full | **Fixed** | **Fixed** |
| CB2 wire encoding | Two's complement | **Fixed** | **Fixed** |
| IA5 string encoding | 7-bit mask (`& 0x7F`) | **Fixed** | **Fixed** |
| EBCDIC tables | Full 256-entry | Full 256-entry | **Fixed** |
| `auto_fields` metadata | Full | Missing | Missing |
| Send-only warnings | Full | Full | Missing |
| `encode_batch` config/timestamps | Full | Full | **Fixed** |
| `equals`/`hashCode` | `operator==` default | Missing | `__eq__` partial |
| JSON serialization | Full | Missing | Missing |
| Deferred constraint validation | `validate()` method | No separate method | Missing |
| Inline struct flattening | Full | Missing | Missing |
| `readS16`/`readS32` methods | N/A (typed reads) | **Fixed** (was compile error) | N/A |
| ShiftRight operator | `>>` (logical on unsigned) | **Fixed** (`>>>`) | `>>` (correct in Python) |

---

## Files Modified

| File | Fixes Applied |
|------|---------------|
| `bgen/src/codegen/java_backend.cpp` | #1, #2, #3, #4, #5, #6, #8 |
| `bgen/src/codegen/python_backend.cpp` | #4, #5, #7, #9, #10, #11 |
