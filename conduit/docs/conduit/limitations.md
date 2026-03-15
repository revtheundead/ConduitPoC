# Limitations & Known Issues

[Back to index](index.md)

This page documents the current feature gaps between the C++, Java, and Python backends, known bugs, and general limitations of the Conduit library. The project has undergone four audit rounds resulting in 50+ verified bug fixes and feature additions across the Java and Python backends.

## Backend Feature Parity

The C++ backend is the reference implementation with full feature coverage. The Java and Python backends are production-usable with near-complete feature parity after four audit rounds.

### Resolved Features (now present in all backends)

| Feature | Round Fixed | Notes |
|---------|-----------|-------|
| Inline structs (`is_inline`) | Round 3 | Java/Python flatten inline struct fields into parent |
| Deferred constraint validation (`validate()`) | Round 3 | All backends generate `validate()` for deferred constraints |
| Auto-length with `field_ref` | Round 3 | Backpatch logic corrected in both backends |
| Choice decode range cases (`lo..hi`) | Round 3 | Java/Python now handle range values |
| Choice decode direction filtering | Round 3 | Java/Python filter by send/receive direction |
| Array `length_from` bounded decode | Round 3 | Both backends create bounded sub-readers |
| `format_outbound` session method | Round 3 | Available in all backends |
| JSON serialization | Round 4 | Java generates `toMap()`/`fromMap()`; Python generates `to_dict()`/`from_dict()` |
| `WIRE_SIZE` for struct classes | Round 4 | All fixed-size structs/messages/bitmaps emit `WIRE_SIZE` constant |
| `equals()`/`hashCode()` for structs | Round 4 | Java generates `equals()`/`hashCode()`; Python adds `__hash__` |
| `toString()`/`__repr__` for Flags/Scaled | Round 4 | Both backends generate human-readable string representations |

### Architectural Differences

| Feature | C++ | Java | Python |
|---------|-----|------|--------|
| Field access | Getter/setter methods | Public fields | Public attributes |
| Optional fields | `std::optional<T>` | Boxed types (nullable) | `None` sentinel |
| Variants/choices | `std::variant<...>` | `Object` with `instanceof` | Dynamic typing |
| Error handling | `Result<T>`/`VoidResult` | Exceptions | Exceptions |
| String encoding | Compile-time dispatch | Runtime lookup | Runtime lookup |
| Constraint validation | Setter returns `VoidResult` | Manual validation | Manual validation |
| JSON format | `to_json`/`from_json` (nlohmann) | `toMap()`/`fromMap()` (Map-based) | `to_dict()`/`from_dict()` (dict-based) |

## Known Bugs (Lower Priority)

All MEDIUM-severity bugs from previous rounds have been fixed. No known bugs remain above LOW severity.

| Severity | Description | Status |
|----------|-------------|--------|
| ~~MEDIUM~~ | ~~Python auto-length backpatch uses wrong measurement base and inverted modifier~~ | Fixed (Round 3) |
| ~~MEDIUM~~ | ~~Java enum value type is always `int` regardless of bit width~~ | Fixed (Round 4) |
| ~~MEDIUM~~ | ~~Java choice decode missing `length_from`/`length` bounded sub-reader~~ | Fixed (Round 3) |
| ~~LOW~~ | ~~Python `length_from` expressions use wrong scope for outer-scope field references~~ | Fixed (Round 3) |
| ~~LOW~~ | ~~Python `decode_frame` swallows all exceptions silently~~ | Fixed (Round 4) -- now catches only `DecodeError`/`ConstraintError` |
| ~~LOW~~ | ~~Python auto-count encode doesn't handle optional arrays~~ | Fixed (Round 4) -- `None` arrays treated as length 0 |
| ~~LOW~~ | ~~Python encode doesn't check field constraints~~ | Fixed (Round 3) |

## Performance Differences Across Backends

| Concern | Description |
|---------|-------------|
| Bit-by-bit reads | Java/Python `BitReader`/`BitWriter` now use **byte-aligned fast paths** for whole-byte reads/writes, falling back to bit-by-bit only for unaligned access |
| Reserved field encoding | Python writes large reserved fields using `write_bits(0, N)` which uses the fast path when byte-aligned |

The byte-aligned fast path optimization (added in Round 4) significantly reduces overhead for the common case where fields are byte-aligned. For transport-dominated workloads (where codec is ~7--9% of total time), the impact is smaller.

## General Limitations

- **No dynamic schema loading.** BMDL schemas must be processed by `bgen` at build time. There is no runtime schema parsing.
- **Worker thread scaling.** Adding worker threads only helps when handler processing time exceeds ~1--10 us per message. For lightweight handlers, single-threaded dispatch is faster (see [Benchmarks & Performance](performance.md)).

## Recently Resolved Limitations

| Limitation | Resolution |
|------------|-----------|
| No asyncio integration | `AsyncTransceiver` wrapper now provides full `async`/`await` support for the Python bindings, including async send, async message handlers, async context manager, and `MessageStream` async iteration. C++ I/O thread callbacks are bridged to the asyncio event loop via `loop.call_soon_threadsafe()`. |
| Single sequence counter per session | The session-wide sequence counter is shared across all message types within a session. This is by design — calling `encode_wrap` for different message types increments the same counter. Use `reset()` to reset the counter to zero, and `sequence_counter()` to read its current value. |
| UDP drop rates for large messages under burst | The UDP transport now explicitly sets `SO_RCVBUF` and `SO_SNDBUF` on the socket to match the configured `recv_buffer_size` and `send_buffer_size` (default 65536 bytes each). This significantly reduces kernel-level drops under burst conditions. A new `send_buffer_size` config field is available in `UdpConfig` for tuning. |

## Test Coverage

Java and Python tests include both **codegen output string checks** and **runtime encode/decode integration tests**. Wire encoding correctness is tested through C++ roundtrip tests and Python runtime roundtrip tests.

Python runtime tests now cover all 82 valid BMDL fixtures with encode/decode roundtrip tests across the following categories:

| Category | Fixtures | Test File |
|----------|----------|-----------|
| Core types & encodings | all_types, wire_encodings, bytes_numeric, boundary_types, mixed_endian | test_coverage_audit.py, test_roundtrip.py |
| FX blocks | fx_advanced, fx_block, fx_choice, fx_ia5_string, fx_string, bitmap_fx, empty_fx | test_fx_coverage.py |
| Bitmap structs | bitmap_advanced, bitmap_wide_fixed, nested_bitmap | test_bitmap_coverage.py |
| EBCDIC strings | ebcdic_strings | test_ebcdic_coverage.py |
| Inline structs | inline_struct, inline_struct_overlap, inline_case_collision, inline_enum, inline_field_types | test_inline_struct_coverage.py |
| Auto fields | auto_sequence, auto_struct_length, auto_count | test_auto_field_coverage.py |
| Frames | frame_*, frame_collision, frame_count, frame_length_arith, frame_timestamp | test_frame_coverage.py |
| Constraints | constraints, constraints_extended, constraint_relaxation, constraint_tighten | test_misc_coverage.py |
| Strings | string_features, string_prefix_incl, ebcdic_strings | test_ebcdic_coverage.py, test_coverage_audit.py |
| Remaining fixtures | annotation_scope, asterix, bit_alignment, choice_no_switch, etc. | test_misc_coverage.py |

## Audit History

Four audit rounds have been performed against the C++ reference implementation:

| Round | Bugs Fixed | Key Findings |
|-------|-----------|--------------|
| 1 | 11 | Java bitmap codegen compile errors, IA5/CB2 wire encoding, string wrapper generation |
| 2 | 12 | FX terminal bit (both backends), config field truncation, string trim defaults |
| 3 | 17 | Python wire encoding dispatch, auto-length backpatch, FX null safety, inline struct support |
| 4 | 10+ | JSON serialization, WIRE_SIZE, equals/hashCode/__hash__, toString/__repr__, BitReader/BitWriter fast paths, enum truncation fix, decode_frame safety, optional array handling |
| **Total** | **50+** | All fixes verified against C++ reference test suite |

For the complete audit findings including per-file diffs, see [Backend Audit Findings](../backend-audit-findings.md).

## See Also

- [Benchmarks & Performance](performance.md) -- Detailed performance data
- [Cross-Language Integration Strategies](cross-language-integration.md) -- Future integration plans
