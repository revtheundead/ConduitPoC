# BMDL/bgen Backend Audit: Java & Python vs C++ Reference

## Summary

This audit compares the Java and Python code generation backends against the C++
reference implementation and the BMDL/bgen documentation. Findings are categorized
as **Missing** (feature not implemented), **Incomplete** (partially implemented),
or **Incorrect** (generates wrong code).

---

## 1. Missing Features

### 1.1 Bitmap Choice Decode (Java: Missing, Python: Incorrect)

**Severity: High**

When a `<choice>` element is controlled by a bitmap bit (FSPEC), neither the Java
nor Python backend generates correct decode logic.

- **Java** (`java_backend.cpp:1462-1466`): Explicitly unimplemented with a TODO
  comment. Sets the choice field to `null` instead of performing switch-based
  dispatch:
  ```java
  result.field = null; // TODO: choice decode in bitmap
  ```

- **Python** (`python_backend.cpp:1525-1535`): Sets `py_type = "object"` for
  bitmap choice fields and then attempts to call `object.decode(r)` at line 1631,
  which would produce invalid Python code.

- **C++ Reference** (`cpp_structs.cpp:1639-1649, 1901-1921`): Correctly sets the
  choice type to the variant alias name via `get_variant_alias_name()`, and the
  bitmap decode path at line 1901 handles it via the `bf.is_struct` branch which
  calls `Type::decode(r)` on the variant type.

### 1.2 encode_batch for Array-Payload Sessions (Java: Missing)

**Severity: Medium**

The Java backend does not generate the `encodeBatch()` method on session classes.
The C++ backend generates `encode_batch()` and the Python backend generates
`encode_batch()` for protocols with `payload count="*"` (array payloads).

- **C++** (`cpp_session.cpp:244-289`): Generates `encode_batch` when
  `si.payload_is_array` is true.
- **Python** (`python_backend.cpp:2936-2970`): Generates `encode_batch` for array
  payloads.
- **Java**: No `encode_batch`/`encodeBatch` method found anywhere in
  `java_backend.cpp`.

### 1.3 Float Special Value Tests (All: Missing Tests)

**Severity: Low**

The C++ tests explicitly verify roundtrip behavior for float special values:
- `float32 NaN roundtrip`
- `float32 +Infinity roundtrip`
- `float32 -Infinity roundtrip`
- `float32 negative zero roundtrip`
- `float64 NaN/Infinity/negative zero roundtrips`

Neither Java nor Python test suites include these test cases. The codecs likely
handle them correctly via IEEE 754 pass-through, but this is unverified.

---

## 2. Incomplete Features

### 2.1 Flags/Scaled Type toString/repr (Java & Python: Missing)

**Severity: Low**

- **C++**: Generates `to_string()` for Flags types and Scaled types with
  human-readable output.
- **Java**: Flags classes lack a `toString()` override; scaled wrapper types also
  lack `toString()`. Both fall back to default `Object.toString()` which prints
  class hash, not field values.
- **Python**: Flags classes lack `__repr__`; scaled wrapper types lack `__repr__`.
  The `__eq__` is generated for both, but display methods are absent.

### 2.2 Struct/Message equals() (Java: Missing)

**Severity: Medium**

- **C++**: Generates `operator==(const T&) const = default` for all struct and
  message types.
- **Python**: Generates `__eq__` for wrapper types (Flags, Scaled, Constrained).
  Struct/message types use field-by-field comparison in practice (dataclass-style
  public fields).
- **Java**: Does NOT override `equals()` or `hashCode()` on generated struct and
  message classes. Java uses object identity by default, meaning two structs with
  identical field values will not compare as equal using `.equals()`. This can
  cause subtle test failures when using equality assertions.

### 2.3 to_string with Auto-Field Overrides (Java & Python: Missing)

**Severity: Low**

- **C++** (`cpp_structs_decode.cpp:66-100`): Generates a second `to_string()`
  overload that accepts `span<pair<string,string>> overrides` for auto-managed
  frame fields (sync, sequence, length, etc.). This allows the session layer to
  display actual auto-field values in format_message output.
- **Java**: `toString()` does not accept overrides.
- **Python**: `__repr__` does not accept overrides.

### 2.4 Auto-Fields Metadata Return (Java & Python: Missing)

**Severity: Low**

- **C++**: `encode_wrap()` returns `EncodeResult` containing both the encoded
  bytes and an `auto_fields` vector of `{name, value}` pairs documenting what
  auto-managed values were applied.
- **Java**: `encodeWrap()` returns a Map with "bytes", "type_id", "type_name" but
  no auto-fields metadata.
- **Python**: `encode_wrap()` returns a dict with similar keys but no auto-fields.

---

## 3. Missing Test Coverage

The following C++ test scenarios have no equivalent in the Java or Python test
suites. These tests verify important session and codec behaviors.

### 3.1 Direction-Qualified Session Tests (Java & Python: Missing)

The C++ test suite (`test_generated_session.cpp`) includes 10+ tests for the
`direction_qualified` protocol:

| C++ Test | Java | Python |
|----------|------|--------|
| decode shared discriminator produces receive variant | Missing | Missing |
| decode shared discriminator never produces send variant | Missing | Missing |
| encode_wrap UplinkPayload (send-only) succeeds | Missing | Missing |
| encode_wrap DownlinkPayload (receive-only) succeeds | Missing | Missing |
| common case decodes normally | Missing | Missing |
| wrap UplinkPayload sets correct discriminator | Missing | Missing |
| wrap DownlinkPayload sets correct discriminator | Missing | Missing |
| decode_frame extracts receive variant | Missing | Missing |
| decode_frame does NOT extract send variant | Missing | Missing |

### 3.2 Auto-Increment Wrap-Around Tests (Java & Python: Missing)

| C++ Test | Java | Python |
|----------|------|--------|
| auto-increment wraps at 16-bit counter width | Missing | Missing |
| auto-increment is per-session not per-type | Missing | Missing |
| auto-increment 8-bit wrap-around (sentry_link, 256+ msgs) | Missing | Missing |

### 3.3 Sentry Link Session Tests (Java & Python: Missing)

The `sentry_link` protocol with 4 message types and 8-bit sequence counter has
generated code but no dedicated pure-codec tests (only CABI integration tests).

### 3.4 Config Field Tests (Java & Python: Missing)

| C++ Test | Java | Python |
|----------|------|--------|
| config field survives session reset | Missing | Missing |

Note: The `frame_config` protocol is not among the generated Java/Python fixtures,
so this test cannot be ported without generating the fixture first.

### 3.5 Float Special Value Roundtrip Tests (Java & Python: Missing)

| C++ Test | Java | Python |
|----------|------|--------|
| float32 NaN roundtrip | Missing | Missing |
| float32 +Infinity roundtrip | Missing | Missing |
| float32 -Infinity roundtrip | Missing | Missing |
| float32 negative zero roundtrip | Missing | Missing |
| float64 NaN roundtrip | Missing | Missing |
| float64 +Infinity roundtrip | Missing | Missing |
| float64 -Infinity roundtrip | Missing | Missing |
| float64 negative zero roundtrip | Missing | Missing |

### 3.6 Frame Error Cases (Java & Python: Missing)

| C++ Test | Java | Python |
|----------|------|--------|
| decode_frame with truncated data returns error | Missing | Missing |
| decode_frame unknown ID produces error | Missing | Missing |
| ID wire byte matches message ID_VALUE | Missing | Missing |

### 3.7 Wire Encoding Overflow Tests (Java & Python: Missing)

| C++ Test | Java | Python |
|----------|------|--------|
| BCD overflow on encode - 16-bit (10000 fails) | Missing | Missing |
| BCD overflow on encode - 12-bit (1000 fails) | Missing | Missing |
| BCD_S overflow on encode | Missing | Missing |
| Wire encoding tight packing (14 bytes, 105 bits) | Missing | Missing |

---

## 4. Documentation vs Implementation Discrepancies

### 4.1 bgen docs claim "Every backend must handle the COMPLETE BMDL feature set"

From `codegen_backend.hpp`: *"A backend that silently skips or stubs out a BMDL
feature is a bug."* The bitmap choice decode TODO in Java and the `object.decode(r)`
in Python directly violate this stated contract.

### 4.2 Session docs describe auto_fields in encode_wrap result

The bgen session documentation describes `EncodeResult` as containing auto-field
metadata. Neither Java nor Python sessions return this metadata.

---

## 5. Recommendations

1. **Fix bitmap choice decode** in both Java and Python backends (high priority).
2. **Add `encodeBatch`** to the Java backend for array-payload sessions.
3. **Add `toString()`/`__repr__`** to Flags and Scaled wrapper types in
   Java/Python.
4. **Add `equals()`/`hashCode()`** to Java-generated struct/message classes.
5. **Add missing tests** for direction-qualified sessions, auto-increment
   wrap-around, sentry-link sessions, float special values, frame error cases,
   and wire encoding overflow.
