# Limitations & Known Issues

[Back to index](index.md)

This page documents the current feature gaps between the C++, Java, and Python backends, known bugs, and general limitations of the Conduit library. The project has undergone three audit rounds resulting in 40 verified bug fixes across the Java and Python backends.

## Backend Feature Parity

The C++ backend is the reference implementation with full feature coverage. The Java and Python backends are production-usable but have feature gaps in some advanced areas.

### Missing Features (C++ has, Java/Python don't)

| Feature | Java | Python | Severity | Description |
|---------|------|--------|----------|-------------|
| Inline structs (`is_inline`) | Missing | Missing | HIGH | C++ flattens inline struct fields into the parent; Java/Python nest as sub-objects |
| Deferred constraint validation | No `validate()` | Missing | HIGH | C++ generates a `validate()` method for deferred constraints |
| Auto-length with `field_ref` | Partial | Broken | HIGH | Python writes 0 placeholder but never patches for non-empty `field_ref` |
| JSON serialization | Missing | Missing | MEDIUM | C++ generates `to_json`/`from_json`; no equivalent in Java/Python |
| `format_outbound` session method | Present | Present | MEDIUM | Added in Round 3 audit -- now available in all backends |
| Choice decode range cases (`lo..hi`) | Exact only | Exact only | MEDIUM | C++ handles range values; Java/Python match only exact values |
| Choice decode direction filtering | Ignored | Ignored | MEDIUM | C++ filters by send/receive direction; Java/Python ignore direction |
| Array `length_from` bounded decode | Unbounded | Unbounded | MEDIUM | C++ creates bounded sub-reader; Python reads until exhausted |
| `WIRE_SIZE` for struct classes | Missing | Partial | LOW | Python only generates for string type classes |
| `equals()`/`hashCode()` for structs | Missing | Partial | LOW | Java lacks `equals()`/`hashCode()`; Python has `__eq__` but no `__hash__` |
| `toString()`/`__repr__` for Flags/Scaled | Missing | Missing | LOW | Falls back to default object representation |

### Architectural Differences

| Feature | C++ | Java | Python |
|---------|-----|------|--------|
| Field access | Getter/setter methods | Public fields | Public attributes |
| Optional fields | `std::optional<T>` | Boxed types (nullable) | `None` sentinel |
| Variants/choices | `std::variant<...>` | `Object` with `instanceof` | Dynamic typing |
| Error handling | `Result<T>`/`VoidResult` | Exceptions | Exceptions |
| String encoding | Compile-time dispatch | Runtime lookup | Runtime lookup |
| Constraint validation | Setter returns `VoidResult` | Manual validation | Manual validation |

## Known Bugs (Lower Priority)

| Severity | Description |
|----------|-------------|
| MEDIUM | Python auto-length backpatch uses wrong measurement base and inverted modifier |
| MEDIUM | Java enum value type is always `int` regardless of bit width -- truncates >32-bit enums |
| MEDIUM | Java choice decode missing `length_from`/`length` bounded sub-reader |
| LOW | Python `length_from` expressions use wrong scope for outer-scope field references |
| LOW | Python `decode_frame` swallows all exceptions silently (including programming bugs) |
| LOW | Python auto-count encode doesn't handle optional arrays |
| LOW | Python encode doesn't check field constraints |

## Performance Differences Across Backends

| Concern | Description |
|---------|-------------|
| Bit-by-bit reads | Java/Python `BitReader` reads multi-byte values one bit at a time instead of using direct memory access like C++ |
| Reserved field encoding | Python writes large reserved fields using `write_bits(0, N)` which loops bit-by-bit |

These performance patterns mean that Java and Python backends will be measurably slower than C++ for codec operations. For transport-dominated workloads (where codec is ~7--9% of total time), the impact is smaller.

## General Limitations

- **No asyncio integration.** The Python bindings are synchronous. Async support is planned but not yet implemented.
- **No dynamic schema loading.** BMDL schemas must be processed by `bgen` at build time. There is no runtime schema parsing.
- **Single sequence counter per session.** All auto-increment fields within a session share one counter. If your protocol needs independent counters per message type, you'll need to manage them externally.
- **UDP large messages.** Under aggressive burst conditions (back-to-back sends with no pacing), messages larger than approximately 50 bytes experience significant drop rates in the test environment. Use pacing or TCP for reliable delivery of larger messages.
- **Worker thread scaling.** Adding worker threads only helps when handler processing time exceeds ~1--10 us per message. For lightweight handlers, single-threaded dispatch is faster (see [Benchmarks & Performance](performance.md)).

## Test Coverage

Java and Python tests are primarily **codegen output string checks** -- they verify that generated source code matches expected strings but do not compile or execute the generated code at the integration level. Wire encoding correctness is only fully tested through C++ roundtrip tests.

Python has 640 runtime test cases covering 13 of 60+ BMDL fixtures. Areas with limited Python runtime test coverage include FX blocks, bitmap structs, EBCDIC strings, inline structs, and auto fields.

## Audit History

Three audit rounds have been performed against the C++ reference implementation:

| Round | Bugs Fixed | Key Findings |
|-------|-----------|--------------|
| 1 | 11 | Java bitmap codegen compile errors, IA5/CB2 wire encoding, string wrapper generation |
| 2 | 12 | FX terminal bit (both backends), config field truncation, string trim defaults |
| 3 | 17 | Python wire encoding dispatch, auto-length backpatch, FX null safety, inline struct support |
| **Total** | **40** | All fixes verified against 37,069 assertions across 1,057 C++ test cases |

For the complete audit findings including per-file diffs, see [Backend Audit Findings](../backend-audit-findings.md).

## See Also

- [Benchmarks & Performance](performance.md) -- Detailed performance data
- [Cross-Language Integration Strategies](cross-language-integration.md) -- Future integration plans
