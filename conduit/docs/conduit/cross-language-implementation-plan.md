# Cross-Language Support for Conduit: Java & Python via bgen + C ABI

## Context

Conduit is a C++ binary protocol library with a 6-stage code generator (`bgen`) that currently only emits C++ headers. The `cross-language-integration.md` design document outlines 8 strategies plus additional native-accessibility and direct-Transceiver-access sections for making Conduit accessible from other languages. This plan implements the highest-priority work — **multi-language bgen code generation backends** (Strategy 1), **C ABI wrapper layers** (Strategy 2 + codec-only C ABI), and the **`conduit-codec` / `conduit-transceiver` library split** — with **Java and Python** as the priority target languages. The bridge app (Strategy 4) is out of scope for now, but the groundwork laid here will support it and all other future strategies.

**Constraint:** All existing C++ functionality must be preserved. Nothing we add should break the existing library, its generated code, its tests, or its build. These are purely additive changes.

---

## Core Principle: Full Feature Parity Across Languages

**Every BMDL feature that the C++ backend supports must be supported identically by the Python and Java backends.** The language selection (`--language`) changes only the syntax of the output — never the capabilities. There are no features that are "C++ only," no features omitted from a backend because they're hard to express in that language, and no extra features invented for a specific language.

Concretely, this means every backend must handle:

| BMDL Feature | C++ | Python | Java |
|---|---|---|---|
| All primitive types (uint, int, float, bool, bytes, string) | Yes | Yes | Yes |
| All wire encodings (default/CB2, BNR, BNR_S, BCD, BCD_S) | Yes | Yes | Yes |
| All endianness modes (big, little) | Yes | Yes | Yes |
| String encodings (ASCII, UTF-8, IA5, EBCDIC) | Yes | Yes | Yes |
| String padding, trimming, termination, length-prefix | Yes | Yes | Yes |
| Enums, flags, scaled/offset types, constrained types | Yes | Yes | Yes |
| Nested structs, inline structs, inline enums | Yes | Yes | Yes |
| Arrays (fixed count, count-from, count star) | Yes | Yes | Yes |
| Choices (switch/case/otherwise) with all dispatch modes | Yes | Yes | Yes |
| Bitmaps (FSPEC-based optional fields) | Yes | Yes | Yes |
| FX extension blocks (variable-length extension chains) | Yes | Yes | Yes |
| Optional fields (present-when expressions) | Yes | Yes | Yes |
| Frames (header, payload, footer, sync patterns, length fields) | Yes | Yes | Yes |
| Sessions (decode_frame, encode_wrap, type dispatch) | Yes | Yes | Yes |
| Auto fields (id, length, count, increment, timestamp, config) | Yes | Yes | Yes |
| Direction filtering (send-only, receive-only) | Yes | Yes | Yes |
| Constants | Yes | Yes | Yes |
| Batch encoding (array payloads) | Yes | Yes | Yes |
| Constraint validation (immediate and deferred) | Yes | Yes | Yes |
| Reserved fields and alignment | Yes | Yes | Yes |
| Type ID computation (FNV-1a 64-bit) | Yes | Yes | Yes |
| Protocol descriptor / type registry | Yes | Yes | Yes |
| to_string / format_message human-readable output | Yes | Yes | Yes |

Similarly, both C ABI layers (codec-only and full Transceiver) must expose their respective complete API surfaces. The Python and Java bindings must in turn expose every C ABI function. No capability is lost at any layer boundary.

**Validation approach:** The same BMDL test fixtures used for C++ roundtrip tests (in `bgen/tests/fixtures/`) are used for Python and Java roundtrip tests. If a fixture produces correct C++ code, it must also produce correct Python and Java code. Any fixture that exercises a BMDL feature is a fixture that all backends must handle.

---

## Workstream Overview

| # | Workstream | Description |
|---|-----------|-------------|
| 0 | **`conduit-codec` / `conduit-transceiver` library split** | Split the monolithic `conduit` static lib into two build targets |
| 1 | **bgen multi-language architecture** | Refactor bgen's stage 6 to support `--language` flag; introduce codegen backend interface |
| 2 | **Python bgen backend** | Generate complete Python codec package from BMDL (messages, sessions, StreamFramer, BitReader/BitWriter) |
| 3 | **Java bgen backend** | Generate complete Java codec package from BMDL (messages, sessions, StreamFramer, BitReader/BitWriter) |
| 4a | **Codec-only C ABI (`libconduit_codec`)** | `extern "C"` flat API around session decode/encode/framing — no transport, no threading |
| 4b | **Full Transceiver C ABI (`libconduit_cabi`)** | `extern "C"` flat API around the full `Transceiver` — lifecycle, peers, transports, handlers |
| 5 | **Python Transceiver bindings** | `ctypes`/`cffi` wrapper providing full Pythonic Transceiver API |
| 6 | **Java Transceiver bindings** | JNA wrapper providing full Java Transceiver API |
| 7 | **Build system integration** | CMake options, third-party deps, install rules, packaging groundwork |
| 8 | **Tests** | Unit + integration tests for every workstream |

---

## Workstream 0: `conduit-codec` / `conduit-transceiver` Library Split

### Goal

Split the existing monolithic `conduit` static library into two separate build targets, as described in the "Making Conduit Natively Accessible Without Networking" section of the design doc:

```
conduit-codec  (pure computation, no OS deps)
├── BitReader / BitWriter       — bit-level I/O on byte spans
├── Endian utilities            — byte-order conversion
├── Error / Result<T>           — error propagation
├── Codec traits                — Encodable / Decodable / Message concepts
├── Generated message types     — structs with encode_bytes() / decode_bytes()
├── Generated sessions          — ISession implementations (decode_frame, encode_wrap)
└── StreamFramer                — stateful frame extraction from byte streams

conduit-transceiver  (platform-bound, threading + networking)
├── ITransport + implementations (TCP, UDP, Serial)
├── Transceiver orchestrator
├── BoundedQueue
├── HandlerRegistry
└── Worker thread pool
```

This split is the architectural foundation for all native accessibility strategies. The codec can be compiled, linked, and distributed independently for lightweight use cases (file parsing, test harnesses, codec-only C ABI). The full Transceiver depends on the codec and adds transport + threading.

### Changes

**`conduit/CMakeLists.txt`** — Split into two targets:

```cmake
# conduit_codec — pure computation, no OS deps (except threading for StreamFramer mutex)
add_library(conduit_codec STATIC
    src/core/error.cpp
    src/io/bit_reader.cpp
    src/io/bit_writer.cpp
    src/logging/logger.cpp
    src/transceiver/stream_framer.cpp
)

# conduit — full library (depends on conduit_codec, adds transport + threading)
add_library(conduit STATIC
    src/transceiver/peer.cpp
    src/transceiver/handler.cpp
    src/transceiver/message_log.cpp
    src/transceiver/transceiver.cpp
    src/transceiver/transport/tcp_client.cpp
    src/transceiver/transport/tcp_server.cpp
    src/transceiver/transport/udp.cpp
    src/transceiver/transport/serial_common.cpp
    # ... platform-specific sources
)
target_link_libraries(conduit PUBLIC conduit_codec)
```

**Key constraint:** Existing code that links `conduit` must continue to work unchanged. The `conduit` target now depends on `conduit_codec` and re-exports its headers. Any `#include <conduit/...>` that worked before still works.

### Files affected
- `conduit/CMakeLists.txt` (modify — split CONDUIT_SOURCES into codec and transceiver sets)
- No header changes — the include structure remains the same

---

## Workstream 1: bgen Multi-Language Architecture

### Goal
Refactor bgen so that stage 6 (code generation) is selectable via `--language <lang>`. The default is `cpp` (preserving current behavior exactly). New backends for `python` and `java` are plugged in alongside.

### Changes

**`conduit/bgen/src/main.cpp`** — Add `--language` CLI arg (default: `cpp`)
- Add `language` field to `CliArgs` struct, default `"cpp"`
- Parse `--language` flag in `parse_args()`
- Update `print_usage()` with `--language` documentation
- After stage 5, dispatch to the appropriate backend based on `args.language`
- C++ path remains identical (call same `generate_*` functions)

**`conduit/bgen/src/codegen/codegen_backend.hpp`** — New file: Backend interface
```cpp
namespace bgen::codegen {

// Abstract interface for language-specific code generation backends.
//
// CONTRACT: Every backend must handle the COMPLETE BMDL feature set.
// The backend interface receives the same fully-analyzed protocol data
// regardless of target language. All AST node types, wire encodings,
// framing constructs, and session features must be emitted. A backend
// that silently skips or stubs out a BMDL feature is a bug — the
// --language flag changes output syntax, never output capabilities.
struct CodegenBackend {
    virtual ~CodegenBackend() = default;

    // Generate all output files into the given directory.
    // Returns true on success, false on I/O or generation error.
    // The backend MUST emit code covering every type, struct, message,
    // frame, and session in the protocol — no feature may be omitted.
    virtual bool generate(
        const model::Protocol& protocol,
        const analyzer::TypeIndex& index,
        const analyzer::WireSizeInfo& sizes,
        const std::vector<analyzer::SessionInfo>& sessions,
        const std::string& ns,
        const std::filesystem::path& output_dir) = 0;

    // Return the language name (e.g. "cpp", "python", "java")
    virtual std::string_view language_name() const = 0;
};

// Factory: create backend by language name. Returns nullptr if unknown.
std::unique_ptr<CodegenBackend> create_backend(const std::string& language);

} // namespace bgen::codegen
```

**`conduit/bgen/src/codegen/codegen_backend.cpp`** — Factory implementation
- Returns `CppBackend` for `"cpp"`, `PythonBackend` for `"python"`, `JavaBackend` for `"java"`

**`conduit/bgen/src/codegen/cpp_backend.hpp/.cpp`** — Wrap existing C++ codegen
- Implements `CodegenBackend::generate()` by calling the existing `generate_constants()`, `generate_types()`, `generate_structs()`, `generate_messages()`, `generate_sessions()`, `generate_protocol()`, `generate_umbrella()` functions exactly as `main.cpp` does today
- Zero behavioral change for C++ output

### Files affected
- `conduit/bgen/src/main.cpp` (modify)
- `conduit/bgen/src/codegen/codegen_backend.hpp` (new)
- `conduit/bgen/src/codegen/codegen_backend.cpp` (new)
- `conduit/bgen/src/codegen/cpp_backend.hpp` (new)
- `conduit/bgen/src/codegen/cpp_backend.cpp` (new)
- `conduit/bgen/CMakeLists.txt` (add new sources)

### Reusable existing code
- All pipeline stages 1-5 are language-agnostic and shared: `model::Protocol`, `analyzer::TypeIndex`, `analyzer::WireSizeInfo`, `analyzer::SessionInfo`
- `name_utils.hpp` naming helpers (`to_snake_case`, `to_pascal_case`, etc.)
- `EmitContext` text emitter (`conduit/bgen/src/codegen/emit_context.hpp`)
- `write_file()` function from `main.cpp` (extract to a shared utility or keep in main)

---

## Workstream 2: Python bgen Backend

### Goal
`bgen --language python` generates a **complete, self-contained Python codec package** with native message classes, session logic, stream framing, and bit-level I/O — all in pure Python. Wire-compatible with C++ generated code. This is a pure-native codec library per the design doc's Suggestion 4 ("Pure-native codec libraries via bgen"). **Must handle every BMDL feature the C++ backend handles** — the full feature parity table above applies without exception.

### Output structure
```
<output_dir>/
├── __init__.py              # Package init, re-exports all public names
├── constants.py             # Named constants
├── types.py                 # Type wrappers (enums, scaled types, constrained types)
├── structs.py               # Struct dataclasses with encode/decode
├── messages.py              # Message dataclasses with TYPE_ID, TYPE_NAME
├── sessions.py              # Session classes (frame decode/encode dispatch)
├── codec.py                 # BitReader/BitWriter in pure Python
├── framer.py                # StreamFramer in pure Python (~100 lines)
├── protocol.py              # Protocol descriptor / type registry
└── errors.py                # Error/Result types
```

### Key design decisions

**Pure Python codec (`codec.py`):**
- Implement `BitReader` and `BitWriter` classes in Python that mirror `conduit/io/bit_reader.hpp` and `conduit/io/bit_writer.hpp`
- Must implement every read/write method the C++ BitReader/BitWriter supports: `read_bits`/`write_bits`, `read_bcd`/`write_bcd`, `read_bcd_signed`/`write_bcd_signed`, `read_sign_magnitude`/`write_sign_magnitude`, byte-aligned reads/writes, string operations (padded, trimmed, terminated, length-prefixed, packed-character IA5), float32/float64
- Support all wire encodings: default (binary), CB2, BCD, BCD_S, BNR, BNR_S
- Support all endianness modes (big-endian, little-endian)
- Support all string encodings: ASCII, UTF-8, IA5, EBCDIC
- Operate on `bytes`/`bytearray`/`memoryview`
- This is a runtime dependency, shipped alongside generated code (or as a small `conduit-codec` pip package)

**Pure Python StreamFramer (`framer.py`):**
- Reimplements the `StreamFramer` algorithm from `conduit/src/transceiver/stream_framer.cpp` in pure Python
- Scan for sync pattern, extract header length, buffer until frame complete
- ~100 lines per the design doc — straightforward port
- Combined with generated sessions, yields a fully native Python implementation that can parse protocol streams with zero C++ dependency

**Generated Python classes (1:1 with C++ generated classes):**
- Use `@dataclass` for struct and message types
- Typed fields with Python type annotations
- `encode(writer: BitWriter) -> None` instance method
- `decode(reader: BitReader) -> T` classmethod
- `validate() -> None` method for deferred constraint checking (mirrors C++ `validate()`)
- `to_string() -> str` method (mirrors C++ `to_string()`)
- `__eq__` for equality comparison (mirrors C++ `operator==`)
- `TYPE_ID: int` and `TYPE_NAME: str` class variables on messages
- `ID_VALUE` class variable for frame-based protocol message IDs
- `WIRE_SIZE: int | None` class variable (int for fixed-size, None for dynamic — mirrors C++ `WIRE_SIZE`)
- `encode_bytes() -> bytes` and `decode_bytes(data: bytes) -> T` convenience methods
- Enum types as Python `enum.IntEnum` with `to_string()`, encode/decode helpers
- Flags types as Python classes with bool property per flag + `raw` accessor
- Scaled/offset types as wrapper classes with `value` and `raw` properties (mirrors C++ wrappers)
- Constrained types with validating setters (mirrors C++ constraint wrappers)
- Optional fields as `Optional[T]` with `None` default, plus `has_X()` / `clear_X()` helpers
- Arrays as `list[T]` (fixed-count, count-from, and count-star all supported)
- Choices as tagged unions (Python doesn't have `std::variant`; use a wrapper class with `kind` discriminator + typed property accessors) — all dispatch modes supported including range-based, direction-filtered, and otherwise
- Bitmap structs with FSPEC-based encoding (all fields optional, presence bits)
- FX extension blocks (variable-length chains, MSB continuation bit)
- Reserved fields (skip on encode, discard on decode)
- Alignment padding

**Frame class (mirrors C++ Frame):**
- `PayloadVariant` equivalent (discriminated wrapper over all message types)
- `wrap()` overloads: create frame, auto-set ID
- `encode()` / `decode()` with header, length placeholder, payload, footer, length backpatch

**Session classes (mirrors C++ `ISession` implementation):**
- `decode_frame(data: bytes) -> list[DecodedMessage]`
- `encode_wrap(type_id: int, msg) -> EncodeResult`
- `encode_batch(type_id: int, msgs: list) -> EncodeResult` (for array-payload protocols)
- `sync_pattern() -> bytes`
- `min_frame_header_size() -> int`
- `extract_frame_length(header: bytes) -> int`
- `leaf_type_ids() -> list[int]`
- `type_name(type_id: int) -> str`
- `is_receive_only(type_id: int) -> bool`
- `reset()` (reset session state — sequence counters, etc.)
- `format_message(type_id: int, payload) -> str`
- Auto-field management: sequence counters (`auto="increment"`), timestamps (`auto="timestamp"`), config fields (`auto="config(key)"`)

### New source files
- `conduit/bgen/src/codegen/python_backend.hpp` (new)
- `conduit/bgen/src/codegen/python_backend.cpp` (new)
- `conduit/bgen/src/codegen/python_codec.hpp` (new — emits the `codec.py` and `framer.py` runtime)
- `conduit/bgen/src/codegen/python_types.cpp` (new)
- `conduit/bgen/src/codegen/python_structs.cpp` (new)
- `conduit/bgen/src/codegen/python_session.cpp` (new)

### Reusable from existing C++ codegen
- Same traversal patterns as `cpp_structs.cpp` for walking `StructChild` variants
- `name_utils.hpp` for name conversions (add `to_python_name()` helpers)
- `EmitContext` for text emission (indentation-based, perfect for Python)
- Wire size info from `WireSizeInfo` for fixed-size optimizations
- Session analysis from `SessionInfo` for frame dispatch tables

---

## Workstream 3: Java bgen Backend

### Goal
`bgen --language java` generates a **complete, self-contained Java codec package** with message classes, session logic, stream framing, and bit-level I/O — all in pure Java. Wire-compatible with C++ and Python. This is a pure-native codec library per the design doc's Suggestion 4. **Must handle every BMDL feature the C++ backend handles** — the full feature parity table above applies without exception.

### Output structure
```
<output_dir>/
├── Constants.java                # Named constants as static final fields
├── Types.java                    # Enums, flags, scaled type wrappers
├── <StructName>.java             # One file per struct (Java convention)
├── <MessageName>.java            # One file per message
├── <SessionName>Session.java     # Session class per frame
├── <FrameName>Frame.java         # Frame class per entry-point
├── BitReader.java                # Binary codec reader
├── BitWriter.java                # Binary codec writer
├── StreamFramer.java             # Stream framing logic
├── ProtocolDescriptor.java       # Type registry
├── DecodedMessage.java           # Decoded message container
├── EncodeResult.java             # Encode result container
└── ConduitError.java             # Error types
```

### Key design decisions

**Java codec (`BitReader.java`, `BitWriter.java`):**
- Java port of the bit-level I/O, operating on `byte[]` / `ByteBuffer`
- Must implement every read/write method the C++ BitReader/BitWriter supports: `readBits`/`writeBits`, `readBcd`/`writeBcd`, `readBcdSigned`/`writeBcdSigned`, `readSignMagnitude`/`writeSignMagnitude`, byte-aligned reads/writes, string operations (padded, trimmed, terminated, length-prefixed, packed-character IA5), float32/float64
- Support all wire encodings: default (binary), CB2, BCD, BCD_S, BNR, BNR_S
- Support all endianness modes (big-endian, little-endian)
- Support all string encodings: ASCII, UTF-8, IA5, EBCDIC
- Handle Java's lack of unsigned types: use next-wider signed type or explicit masking (e.g., `long` for uint32, `int` for uint16)

**Java StreamFramer (`StreamFramer.java`):**
- Port of the `StreamFramer` algorithm in pure Java
- Same sync-scan, header-extract, frame-buffer logic
- Operates on `byte[]` / `ByteBuffer`

**Generated Java classes (1:1 with C++ generated classes):**
- Public final classes with private fields, getters, setters
- `public void encode(BitWriter writer)` method
- `public static T decode(BitReader reader)` static method
- `public byte[] encodeBytes()` and `public static T decodeBytes(byte[] data)` convenience methods
- `public void validate()` method for deferred constraint checking (mirrors C++ `validate()`)
- `public String toString()` override (mirrors C++ `to_string()`)
- `public boolean equals(Object)` and `public int hashCode()` (mirrors C++ `operator==`)
- `public static final long TYPE_ID` and `public static final String TYPE_NAME` on messages
- `public static final int ID_VALUE` for frame-based protocol message IDs
- `public static final Integer WIRE_SIZE` (non-null for fixed-size, null for dynamic — mirrors C++ `WIRE_SIZE`)
- Enum types as Java `enum` with `fromValue()`/`toValue()` methods, `toString()`
- Flags types as Java classes with boolean getter/setter per flag + `raw()` accessor
- Scaled/offset types as wrapper classes with `value()` and `raw()` methods (mirrors C++ wrappers)
- Constrained types with validating setters (mirrors C++ constraint wrappers)
- Optional fields as `@Nullable` with `hasX()` / `getX()` / `setX()` / `clearX()` pattern
- Arrays as `List<T>` (with `ArrayList` backing) — fixed-count, count-from, and count-star all supported
- Choices as sealed interfaces (Java 17+) with concrete record implementations per case — all dispatch modes supported including range-based, direction-filtered, and otherwise
- Bitmap structs with FSPEC-based encoding (all fields optional, presence bits)
- FX extension blocks (variable-length chains, MSB continuation bit)
- Reserved fields (skip on encode, discard on decode)
- Alignment padding

**Frame class (mirrors C++ Frame):**
- Sealed interface `PayloadVariant` over all message types
- `wrap()` static factory methods: create frame, auto-set ID
- `encode()` / `decode()` with header, length placeholder, payload, footer, length backpatch

**Session classes (mirrors C++ `ISession` implementation):**
- `decodeFrame(byte[] data) -> List<DecodedMessage>`
- `encodeWrap(long typeId, Object msg) -> EncodeResult`
- `encodeBatch(long typeId, List<?> msgs) -> EncodeResult` (for array-payload protocols)
- `syncPattern() -> byte[]`
- `minFrameHeaderSize() -> int`
- `extractFrameLength(byte[] header) -> int`
- `leafTypeIds() -> long[]`
- `typeName(long typeId) -> String`
- `isReceiveOnly(long typeId) -> boolean`
- `reset()` (reset session state — sequence counters, etc.)
- `formatMessage(long typeId, Object payload) -> String`
- Auto-field management: sequence counters (`auto="increment"`), timestamps (`auto="timestamp"`), config fields (`auto="config(key)"`)

**Java version target:** Java 17+ (for sealed interfaces and pattern matching)

### New source files
- `conduit/bgen/src/codegen/java_backend.hpp` (new)
- `conduit/bgen/src/codegen/java_backend.cpp` (new)
- `conduit/bgen/src/codegen/java_codec.hpp` (new — emits BitReader/BitWriter/StreamFramer Java runtime)
- `conduit/bgen/src/codegen/java_types.cpp` (new)
- `conduit/bgen/src/codegen/java_structs.cpp` (new)
- `conduit/bgen/src/codegen/java_session.cpp` (new)

---

## Workstream 4a: Codec-Only C ABI (`libconduit_codec`)

### Goal
Expose the codec layer through a minimal C-linkage shared library that handles encode, decode, framing, and introspection **without any transport or threading**. This is smaller and simpler than the full Transceiver C ABI. It compiles to a shared library with no socket, threading, or OS dependencies beyond the C runtime. Per the design doc: "Any language that can load a `.so`/`.dll` can use it immediately."

### Public header: `conduit/include/conduit/cabi/conduit_codec_cabi.h`

```c
#ifndef CONDUIT_CODEC_CABI_H
#define CONDUIT_CODEC_CABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct conduit_session conduit_session_t;
typedef struct conduit_framer conduit_framer_t;

/* Error codes */
typedef int32_t conduit_error_t;
#define CONDUIT_OK 0
#define CONDUIT_ERR_DECODE_FAILED -1
#define CONDUIT_ERR_ENCODE_FAILED -2
#define CONDUIT_ERR_UNKNOWN_TYPE -3
#define CONDUIT_ERR_UNKNOWN_SESSION -4
#define CONDUIT_ERR_BUFFER_TOO_SMALL -5
#define CONDUIT_ERR_UNKNOWN -99

/* Decoded message (returned from decode) */
typedef struct {
    uint64_t type_id;
    const char* type_name;
    const uint8_t* data;
    size_t data_len;
} conduit_decoded_msg_t;

/* Encode result */
typedef struct {
    uint8_t* data;
    size_t data_len;
} conduit_encode_result_t;

/* Frame extracted by framer */
typedef struct {
    const uint8_t* data;
    size_t data_len;
} conduit_frame_t;

/* Session lifecycle */
conduit_session_t* conduit_session_create(const char* session_type);
void conduit_session_destroy(conduit_session_t* session);
void conduit_session_reset(conduit_session_t* session);

/* Decode: raw bytes -> structured messages */
conduit_error_t conduit_decode_frame(
    conduit_session_t* session,
    const uint8_t* data, size_t len,
    conduit_decoded_msg_t** out_msgs, size_t* out_count);
void conduit_free_decoded_msgs(conduit_decoded_msg_t* msgs, size_t count);

/* Encode: type_id + field bytes -> wire bytes */
conduit_error_t conduit_encode_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    conduit_encode_result_t* out_result);
void conduit_free_encode_result(conduit_encode_result_t* result);

/* Batch encode (array-payload protocols) */
conduit_error_t conduit_encode_batch(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* payload_lens, size_t count,
    conduit_encode_result_t* out_result);

/* Introspection */
const char* conduit_session_type_name(conduit_session_t* session, uint64_t type_id);
size_t conduit_session_leaf_type_count(conduit_session_t* session);
const uint64_t* conduit_session_leaf_type_ids(conduit_session_t* session);
int conduit_session_is_receive_only(conduit_session_t* session, uint64_t type_id);
const char* conduit_session_protocol_name(conduit_session_t* session);

/* Human-readable formatting */
conduit_error_t conduit_format_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    char* buf, size_t buf_len, size_t* out_written);

/* Stream framing (for users doing their own I/O) */
conduit_framer_t* conduit_framer_create(conduit_session_t* session);
void conduit_framer_destroy(conduit_framer_t* framer);
conduit_error_t conduit_framer_feed(
    conduit_framer_t* framer,
    const uint8_t* data, size_t len,
    conduit_frame_t** out_frames, size_t* out_count);
void conduit_free_frames(conduit_frame_t* frames, size_t count);

/* Session registry */
typedef void* (*conduit_session_factory_fn)(void);
void conduit_register_session(const char* name, conduit_session_factory_fn factory);

/* Version */
const char* conduit_codec_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CONDUIT_CODEC_CABI_H */
```

### Implementation: `conduit/src/cabi/conduit_codec_cabi.cpp`

- Each function wraps the corresponding `ISession` or `StreamFramer` method
- `conduit_session_t*` is a wrapper around `std::unique_ptr<traits::ISession>`
- `conduit_framer_t*` is a wrapper around `StreamFramer`
- Session creation uses a string-named registry: bgen-generated code registers each session factory at library load time
- Error handling: C++ exceptions → error codes; `Result<T>` → `conduit_error_t`
- No threading, no sockets — pure computation

### Build target
- New CMake target: `conduit_codec_cabi` (SHARED library)
- Links `conduit_codec` (static) — NOT the full `conduit`
- Exports only `extern "C"` symbols
- Build option: `CONDUIT_BUILD_CODEC_CABI` (OFF by default)

### Files
- `conduit/include/conduit/cabi/conduit_codec_cabi.h` (new)
- `conduit/src/cabi/conduit_codec_cabi.cpp` (new)

---

## Workstream 4b: Full Transceiver C ABI (`libconduit_cabi`)

### Goal
Expose the **full** `Transceiver` API surface through a C-linkage shared library, enabling FFI from any language. Every operation available to a C++ user of `Transceiver` must be callable through this C ABI — lifecycle, peer management, single and batch messaging, handler registration/removal, state/error callbacks, statistics, and query methods. No capability is hidden behind the FFI boundary.

Per the design doc's "Direct Transceiver Access" section, the ~12 operations map naturally to C functions:
- Setup: `conduit_create`, `conduit_add_peer`
- Lifecycle: `conduit_start`, `conduit_stop`, `conduit_is_running`
- Messaging: `conduit_send`, `conduit_send_batch`, `conduit_on_message`, `conduit_remove_handler`
- Observability: `conduit_on_state_change`, `conduit_on_error`, `conduit_peer_state`, `conduit_stats`

### Public header: `conduit/include/conduit/cabi/conduit_cabi.h`

```c
#ifndef CONDUIT_CABI_H
#define CONDUIT_CABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct conduit_transceiver conduit_transceiver_t;
typedef uint32_t conduit_peer_id;
typedef uint32_t conduit_callback_id;

/* Error codes (mirrors conduit::ErrorCode) */
typedef int32_t conduit_error_t;
#define CONDUIT_OK 0
#define CONDUIT_ERR_INVALID_ARGUMENT -1
#define CONDUIT_ERR_ALREADY_RUNNING -2
#define CONDUIT_ERR_NOT_RUNNING -3
#define CONDUIT_ERR_PEER_NOT_FOUND -4
#define CONDUIT_ERR_SEND_FAILED -5
#define CONDUIT_ERR_ENCODE_FAILED -6
#define CONDUIT_ERR_BATCH_NOT_SUPPORTED -7
#define CONDUIT_ERR_UNKNOWN -99

/* Transport configuration */
typedef enum {
    CONDUIT_TRANSPORT_UDP,
    CONDUIT_TRANSPORT_TCP_CLIENT,
    CONDUIT_TRANSPORT_TCP_SERVER,
    CONDUIT_TRANSPORT_SERIAL
} conduit_transport_type_t;

typedef struct {
    conduit_transport_type_t type;
    const char* address;        /* "host:port" for TCP/UDP, device path for serial */
    uint32_t baud_rate;         /* Serial only */
} conduit_transport_config_t;

/* Callbacks */
typedef void (*conduit_msg_callback_t)(
    conduit_peer_id peer,
    uint64_t type_id,
    const char* type_name,
    const uint8_t* data, size_t len,
    void* user_data);

typedef void (*conduit_state_callback_t)(
    conduit_peer_id peer,
    int32_t new_state,
    void* user_data);

typedef void (*conduit_error_callback_t)(
    conduit_peer_id peer,
    const char* peer_name,
    conduit_error_t error_code,
    const char* error_message,
    void* user_data);

/* Session names — string-based lookup into the session registry.
   bgen registers each generated session at library load time.
   Foreign languages never construct C++ session objects directly. */
typedef const char* conduit_session_name_t;

/* Lifecycle */
conduit_transceiver_t* conduit_create(void);
void conduit_destroy(conduit_transceiver_t* xcvr);
conduit_error_t conduit_start(conduit_transceiver_t* xcvr);
void conduit_stop(conduit_transceiver_t* xcvr);
int conduit_is_running(const conduit_transceiver_t* xcvr);

/* Peer management */
conduit_error_t conduit_add_peer(
    conduit_transceiver_t* xcvr,
    const char* name,
    conduit_session_name_t session_name,
    const conduit_transport_config_t* transport,
    conduit_peer_id* out_peer_id);

conduit_error_t conduit_peer_by_name(
    const conduit_transceiver_t* xcvr,
    const char* name,
    conduit_peer_id* out_peer_id);

conduit_error_t conduit_sole_peer(
    const conduit_transceiver_t* xcvr,
    conduit_peer_id* out_peer_id);

/* Messaging */
conduit_error_t conduit_send(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t* data, size_t len);

conduit_error_t conduit_send_batch(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* lens, size_t count);

/* Handler registration/removal */
conduit_callback_id conduit_on_message(
    conduit_transceiver_t* xcvr,
    uint64_t type_id,
    conduit_msg_callback_t callback,
    void* user_data);

conduit_callback_id conduit_on_any_message(
    conduit_transceiver_t* xcvr,
    conduit_msg_callback_t callback,
    void* user_data);

int conduit_remove_handler(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id);

/* State & error callbacks */
conduit_callback_id conduit_on_state_change(
    conduit_transceiver_t* xcvr,
    conduit_state_callback_t callback,
    void* user_data);

int conduit_remove_state_change(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id);

conduit_callback_id conduit_on_error(
    conduit_transceiver_t* xcvr,
    conduit_error_callback_t callback,
    void* user_data);

int conduit_remove_error_callback(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id);

/* Query */
size_t conduit_peer_count(const conduit_transceiver_t* xcvr);
int32_t conduit_peer_state(const conduit_transceiver_t* xcvr, conduit_peer_id peer);

/* Session registry */
typedef void* (*conduit_session_factory_t)(void);
void conduit_register_session(const char* name, conduit_session_factory_t factory);

/* Version */
const char* conduit_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CONDUIT_CABI_H */
```

### Implementation: `conduit/src/cabi/conduit_cabi.cpp`

- Each function wraps the corresponding `Transceiver` method
- `conduit_transceiver_t*` is a `reinterpret_cast` to/from `conduit::transceiver::Transceiver*`
- Message callbacks receive raw bytes (the `DecodedMessage::raw` field) so language bindings can decode using their own generated types
- Session creation uses the same string-named registry as 4a — `conduit_add_peer` resolves the session name to a factory internally
- Error handling: C++ exceptions → error codes; `Result<T>` → `conduit_error_t`
- Thread safety: same guarantees as `Transceiver` (callbacks fire on worker thread)

### Build target
- New CMake target: `conduit_cabi` (SHARED library)
- Links `conduit` (static) — the full library including transports
- Exports only `extern "C"` symbols
- Build option: `CONDUIT_BUILD_CABI` (OFF by default)

### Files
- `conduit/include/conduit/cabi/conduit_cabi.h` (new)
- `conduit/src/cabi/conduit_cabi.cpp` (new)
- `conduit/CMakeLists.txt` (modify — add `CONDUIT_BUILD_CODEC_CABI` and `CONDUIT_BUILD_CABI` options + targets)

---

## Workstream 5: Python Transceiver Bindings

### Goal
A Python module (`conduit.transceiver`) that wraps `libconduit_cabi` via `ctypes`, providing a full Pythonic Transceiver API. **Must expose every function in the C ABI** — lifecycle, peer management, send/send_batch, handler registration/removal, state/error callbacks, statistics, and query methods. Combined with the Python bgen-generated message types (Workstream 2), this gives full end-to-end Python support with zero capability loss relative to C++.

Per the design doc's "Direct Transceiver Access" section, the target UX:

```python
from conduit import Transceiver, UdpConfig
from my_protocol import Heartbeat, SensorReading, create_session

with Transceiver() as t:
    t.add_peer("radar", create_session, UdpConfig(bind="0.0.0.0:5000"))

    @t.on(Heartbeat)
    def handle_hb(msg):
        print(f"seq={msg.sequence}, status={msg.status}")

    @t.on(SensorReading)
    def handle_sr(msg):
        db.insert(msg.sensor_id, msg.value)

    t.start()
    t.wait()
```

### Files
- `conduit/bindings/python/conduit/__init__.py` (new)
- `conduit/bindings/python/conduit/transceiver.py` (new — ctypes wrapper around `libconduit_cabi`)
- `conduit/bindings/python/conduit/codec_binding.py` (new — ctypes wrapper around `libconduit_codec` for codec-only use)
- `conduit/bindings/python/conduit/types.py` (new — Python types for transport config, etc.)
- `conduit/bindings/python/pyproject.toml` (new)

### Design notes
- `ctypes.cdll.LoadLibrary()` to load `libconduit_cabi.so` (or `libconduit_codec.so` for codec-only)
- Python callbacks → C function pointers via `ctypes.CFUNCTYPE`
- Raw bytes from message callback → decoded using Python-generated message classes
- Context manager (`__enter__`/`__exit__`) maps to `conduit_create` / `conduit_destroy`
- `@t.on(Heartbeat)` decorator calls `conduit_on_message` under the hood
- **Callback threading:** When C++ worker thread fires callback, the wrapper acquires the GIL, deserializes bytes into a Python message object (via bgen-generated decode), and calls the user's function
- Thread safety: GIL is released during C calls; callbacks acquire GIL

---

## Workstream 6: Java Transceiver Bindings

### Goal
A Java library that wraps `libconduit_cabi` via JNA (Java Native Access), providing a full Java Transceiver API. **Must expose every function in the C ABI** — lifecycle, peer management, send/send_batch, handler registration/removal, state/error callbacks, statistics, and query methods. Combined with Java bgen-generated message types (Workstream 3), this gives full end-to-end Java support with zero capability loss relative to C++.

Per the design doc's "Direct Transceiver Access" section, the target UX:

```java
import com.conduit.Transceiver;
import com.conduit.UdpConfig;
import com.myprotocol.Heartbeat;
import com.myprotocol.SensorReading;
import static com.myprotocol.Sessions.createSession;

public class Main {
    public static void main(String[] args) {
        try (var t = new Transceiver()) {
            t.addPeer("radar", createSession(),
                      new UdpConfig("0.0.0.0", 5000));

            t.on(Heartbeat.class, msg ->
                System.out.printf("seq=%d, status=%d%n",
                    msg.sequence(), msg.status()));

            t.on(SensorReading.class, msg ->
                db.insert(msg.sensorId(), msg.value()));

            t.start();
            t.await();
        }
    }
}
```

### Third-party dependency: JNA
- Include JNA JAR in `conduit/third_party/jna/`
- JNA was chosen over raw JNI for simpler binding code (no `.h` generation, no `javah`)

### Files
- `conduit/bindings/java/src/main/java/io/conduit/Transceiver.java` (new — `AutoCloseable`, wraps opaque handle)
- `conduit/bindings/java/src/main/java/io/conduit/CabiBindings.java` (new — JNA interface mapping C ABI functions)
- `conduit/bindings/java/src/main/java/io/conduit/CodecBindings.java` (new — JNA interface mapping codec-only C ABI)
- `conduit/bindings/java/src/main/java/io/conduit/TransportConfig.java` (new)
- `conduit/bindings/java/src/main/java/io/conduit/PeerId.java` (new)
- `conduit/bindings/java/build.gradle` or `pom.xml` (new)

### Design notes
- JNA `Library` interface maps C ABI functions
- `Transceiver` class wraps the opaque handle, implements `AutoCloseable` (`close()` calls `conduit_destroy`)
- Callbacks via JNA `Callback` interface, dispatched through a `MethodHandle` lookup table
- **Callback threading:** C++ worker thread triggers JNA callback → must `AttachCurrentThread` to the JVM before invoking user's lambda
- Raw bytes from message callback → decoded using Java-generated message classes

---

## Workstream 7: Build System Integration

### CMake options (additions to `conduit/CMakeLists.txt`)

```cmake
option(CONDUIT_BUILD_CODEC_CABI     "Build codec-only C ABI shared library"    OFF)
option(CONDUIT_BUILD_CABI           "Build full Transceiver C ABI shared lib"  OFF)
option(CONDUIT_BUILD_PYTHON_BINDINGS "Build Python bindings (requires CABI)"   OFF)
option(CONDUIT_BUILD_JAVA_BINDINGS   "Build Java bindings (requires CABI)"    OFF)
```

### Library targets
```cmake
# conduit_codec — pure computation (Workstream 0)
add_library(conduit_codec STATIC ...)

# conduit — full library, depends on conduit_codec (Workstream 0)
add_library(conduit STATIC ...)
target_link_libraries(conduit PUBLIC conduit_codec)

# conduit_codec_cabi — codec-only C ABI shared lib (Workstream 4a)
add_library(conduit_codec_cabi SHARED src/cabi/conduit_codec_cabi.cpp)
target_link_libraries(conduit_codec_cabi PRIVATE conduit_codec)

# conduit_cabi — full Transceiver C ABI shared lib (Workstream 4b)
add_library(conduit_cabi SHARED src/cabi/conduit_cabi.cpp)
target_link_libraries(conduit_cabi PRIVATE conduit)
```

### BgenGenerate.cmake updates
- Add `LANGUAGE` parameter to `bgen_generate()` function
- When `LANGUAGE` is `python`, output `.py` files and create a Python package target
- When `LANGUAGE` is `java`, output `.java` files and create a Java source target
- Default `LANGUAGE` is `cpp` (backward compatible)

### Third-party additions under `conduit/third_party/`
- `conduit/third_party/jna/` — JNA JAR (for Java bindings)
- No additional third-party deps needed for Python (`ctypes` is stdlib)
- No additional third-party deps needed for the pure-Python codec (generated alongside)

### Directory structure (new additions)
```
conduit/
├── include/conduit/cabi/
│   ├── conduit_codec_cabi.h       # Codec-only C ABI header
│   └── conduit_cabi.h             # Full Transceiver C ABI header
├── src/cabi/
│   ├── conduit_codec_cabi.cpp     # Codec-only C ABI implementation
│   ├── conduit_cabi.cpp           # Full Transceiver C ABI implementation
│   └── session_registry.cpp       # Shared session name → factory registry
├── bindings/
│   ├── python/
│   │   └── conduit/
│   │       ├── __init__.py
│   │       ├── transceiver.py     # ctypes wrapper for libconduit_cabi
│   │       ├── codec_binding.py   # ctypes wrapper for libconduit_codec
│   │       └── types.py
│   └── java/
│       └── src/main/java/io/conduit/
│           ├── Transceiver.java
│           ├── CabiBindings.java
│           ├── CodecBindings.java
│           └── ...
├── bgen/src/codegen/
│   ├── codegen_backend.hpp/.cpp   # Backend interface + factory
│   ├── cpp_backend.hpp/.cpp       # Existing C++ wrapped as backend
│   ├── python_backend.hpp/.cpp    # Python code generator
│   ├── python_codec.hpp           # Python BitReader/BitWriter/StreamFramer emitter
│   ├── python_types.cpp
│   ├── python_structs.cpp
│   ├── python_session.cpp
│   ├── java_backend.hpp/.cpp      # Java code generator
│   ├── java_codec.hpp             # Java BitReader/BitWriter/StreamFramer emitter
│   ├── java_types.cpp
│   ├── java_structs.cpp
│   └── java_session.cpp
├── tests/
│   ├── test_codec_cabi.cpp        # Codec C ABI tests
│   ├── test_cabi.cpp              # Full Transceiver C ABI tests
│   ├── python/                    # Python roundtrip + binding tests
│   └── java/                      # Java roundtrip + binding tests
└── third_party/
    └── jna/                       # JNA JAR for Java bindings
```

---

## Workstream 8: Tests

### 8a. bgen backend tests (extend existing test suite)

**Critical: all backends run against the same BMDL fixtures.** The existing fixtures in `bgen/tests/fixtures/` cover the full BMDL feature set — every backend must successfully generate code from every fixture that the C++ backend handles. This is the primary mechanism for enforcing feature parity: if a fixture exercises bitmaps and the Python backend can't emit bitmap code, the test fails.

**Test Python codegen output** — `conduit/bgen/tests/test_python_codegen.cpp`
- Run Python backend on **all** test fixtures used by C++ backend tests (e.g., `all_types.bmdl.xml`, `struct_features.bmdl.xml`, `bitmap_fx.bmdl.xml`, `arrays_choices.bmdl.xml`, `frame_basic.bmdl.xml`, `asterix.bmdl.xml`, `sentry_link.bmdl.xml`, etc.)
- Verify generated `.py` files exist and contain expected class names, type IDs, method signatures
- Verify `codec.py`, `framer.py`, and `errors.py` runtime files are emitted
- Pattern: same as `test_codegen_output.cpp` but for Python output

**Test Java codegen output** — `conduit/bgen/tests/test_java_codegen.cpp`
- Same pattern for Java output, same fixture coverage
- Verify `BitReader.java`, `BitWriter.java`, `StreamFramer.java` runtime files are emitted

**CLI tests** — extend `test_parser.cpp` or new `test_cli.cpp`
- Verify `--language python` and `--language java` flags are accepted
- Verify `--language unknown` produces an error
- Verify default (`--language cpp` or no flag) produces identical output to current behavior

### 8b. Python roundtrip tests

**`conduit/tests/python/`** — Python test scripts (run via pytest or unittest)
- `test_codec.py` — Test every Python `BitReader`/`BitWriter` method against known byte sequences. Must cover all wire encodings (BCD, BCD_S, BNR, BNR_S, default), both endianness modes, all string encodings (ASCII, UTF-8, IA5, EBCDIC), and all string modes (padded, trimmed, terminated, length-prefixed)
- `test_framer.py` — Test Python `StreamFramer` against known byte streams with sync patterns, partial frames, multi-frame buffers
- `test_roundtrip.py` — Generate Python code from **all** test fixtures that C++ uses, then encode → decode and verify every field matches. This is the primary parity verification: if C++ passes a roundtrip test on a fixture, Python must too
- `test_wire_compat.py` — Encode in Python, decode in C++ (and vice versa) to verify wire compatibility. Use shared golden byte sequences: C++ encodes known messages to binary files, Python reads and decodes them (and the reverse). This proves the generated code from different backends produces identical wire formats
- CMake integration: `add_test(NAME python_roundtrip COMMAND python3 -m pytest ...)`
- Requires: Python 3.9+ on the test machine

### 8c. Java roundtrip tests

**`conduit/tests/java/`** — Java test files (JUnit or plain main classes)
- `TestCodec.java` — Test every Java `BitReader`/`BitWriter` method (same coverage as Python codec tests — all wire encodings, endianness, string modes)
- `TestFramer.java` — Test Java `StreamFramer` (same scenarios as Python framer tests)
- `TestRoundtrip.java` — Encode → decode roundtrips on all test fixtures (same fixture set as C++ and Python)
- `TestWireCompat.java` — Cross-language wire compatibility using same golden byte sequences as Python tests
- CMake integration: `add_test(NAME java_roundtrip COMMAND java ...)`
- Requires: JDK 17+ on the test machine

### 8d. Codec C ABI tests — `conduit/tests/test_codec_cabi.cpp`
- Test every codec C ABI function — no function in the header should be untested
- Test session lifecycle: create → decode → encode → destroy
- Test stream framing: create framer, feed bytes, extract frames
- Test introspection: type names, leaf type IDs, protocol name
- Test format_message output
- Test session registry (register, lookup, use)
- Test error codes (unknown session, decode failure, buffer too small)
- Uses Catch2

### 8e. Full Transceiver C ABI tests — `conduit/tests/test_cabi.cpp`
- Test every C ABI function — no function in the header should be untested
- Test lifecycle: create → start → stop → destroy
- Test peer management: add peer (single + multi-peer), query count/state, peer lookup by name, sole_peer
- Test message send/receive through C ABI callbacks (single and batch)
- Test handler registration and removal (per-type and any-message)
- Test state change and error callbacks (registration + removal)
- Test error code mapping (every `conduit_error_t` value)
- Test session registry (register, lookup, use)
- Uses Catch2 (same framework as existing tests)

### 8f. Cross-language integration tests
- Python script that loads `libconduit_cabi`, creates transceiver, sends a message encoded with Python-generated types, and verifies receipt via callback
- Java program that does the same
- Python script that loads `libconduit_codec` for codec-only decode/encode roundtrip
- These are optional/advanced and can be added incrementally

---

## Implementation Order

The implementation should proceed in this order to build on each completed layer:

### Phase 0 (Architectural Foundation): Workstream 0
1. **Workstream 0** — `conduit-codec` / `conduit-transceiver` library split
   - *Verify: all existing tests still pass with the split*

### Phase 1 (bgen Foundation): Workstreams 1, 7
2. **Workstream 1** — bgen multi-language architecture (backend interface + `--language` flag)
3. **Workstream 7** — Build system integration (CMake options, BgenGenerate.cmake updates)

### Phase 2 (Code Generation): Workstreams 2, 3, 8a
4. **Workstream 2** — Python bgen backend (codec.py + framer.py + generated Python types/sessions)
5. **Workstream 3** — Java bgen backend (BitReader/BitWriter/StreamFramer.java + generated Java types/sessions)
6. **Workstream 8a** — bgen backend codegen tests (Python + Java output verification)

### Phase 3 (C ABI): Workstreams 4a, 4b, 8d, 8e
7. **Workstream 4a** — Codec-only C ABI (`libconduit_codec`)
8. **Workstream 4b** — Full Transceiver C ABI (`libconduit_cabi`)
9. **Workstream 8d** — Codec C ABI tests
10. **Workstream 8e** — Full Transceiver C ABI tests

### Phase 4 (Language Bindings): Workstreams 5, 6, 8b, 8c, 8f
11. **Workstream 5** — Python Transceiver bindings
12. **Workstream 6** — Java Transceiver bindings
13. **Workstream 8b** — Python roundtrip tests
14. **Workstream 8c** — Java roundtrip tests
15. **Workstream 8f** — Cross-language integration tests

---

## Critical Files Reference

### Existing files to modify
| File | Change |
|------|--------|
| `conduit/CMakeLists.txt` | Split `conduit` into `conduit_codec` + `conduit`; add CABI options + targets |
| `conduit/bgen/src/main.cpp` | Add `--language` flag, dispatch to backends |
| `conduit/bgen/CMakeLists.txt` | Add new codegen source files |
| `conduit/cmake/BgenGenerate.cmake` | Add `LANGUAGE` parameter |
| `conduit/bgen/tests/CMakeLists.txt` | Add Python/Java codegen tests |
| `conduit/tests/CMakeLists.txt` | Add codec CABI tests, full CABI tests, Python/Java test targets |

### Existing files/APIs to reuse (do NOT rewrite)
| File | What to reuse |
|------|--------------|
| `conduit/bgen/src/model/ast.hpp` | `Protocol`, `TypeDef`, `StructDef`, `MessageDef`, `FrameDef`, all AST types |
| `conduit/bgen/src/analyzer/type_resolver.hpp` | `TypeIndex`, `ResolvedDef` |
| `conduit/bgen/src/analyzer/wire_sizer.hpp` | `WireSizeInfo` |
| `conduit/bgen/src/analyzer/session_analyzer.hpp` | `SessionInfo`, `LeafTypeInfo`, `fnv1a_hash()` |
| `conduit/bgen/src/codegen/name_utils.hpp` | All naming helpers |
| `conduit/bgen/src/codegen/emit_context.hpp` | `EmitContext` text emitter |
| `conduit/include/conduit/traits/session_traits.hpp` | `ISession` interface (C ABI wraps this) |
| `conduit/include/conduit/transceiver/transceiver.hpp` | `Transceiver` class (C ABI wraps this) |
| `conduit/include/conduit/core/error.hpp` | `Result<T>`, `VoidResult`, `ErrorCode` |
| `conduit/src/transceiver/stream_framer.cpp` | `StreamFramer` (codec C ABI wraps this; Python/Java ports reimplement) |

---

## Verification Plan

### Build verification
```bash
cd conduit && mkdir build && cd build

# Default build (unchanged behavior — conduit_codec split is transparent)
cmake .. -DCONDUIT_BUILD_TESTS=ON
cmake --build .
ctest

# Full build with all new features
cmake .. \
  -DCONDUIT_BUILD_TESTS=ON \
  -DCONDUIT_BUILD_CODEC_CABI=ON \
  -DCONDUIT_BUILD_CABI=ON \
  -DCONDUIT_BUILD_PYTHON_BINDINGS=ON \
  -DCONDUIT_BUILD_JAVA_BINDINGS=ON
cmake --build .
ctest
```

### bgen CLI verification
```bash
# C++ (default, must be identical to current behavior)
./bgen --input test.bmdl.xml --output out_cpp
./bgen --input test.bmdl.xml --output out_cpp --language cpp

# Python (complete self-contained codec package)
./bgen --input test.bmdl.xml --output out_py --language python
ls out_py/  # __init__.py, constants.py, types.py, structs.py, messages.py, sessions.py, codec.py, framer.py, ...

# Java (complete self-contained codec package)
./bgen --input test.bmdl.xml --output out_java --language java
ls out_java/  # Constants.java, BitReader.java, BitWriter.java, StreamFramer.java, ...
```

### Roundtrip verification
```bash
# Python: encode a message, decode it back, verify fields match
python3 -m pytest conduit/tests/python/

# Java: compile generated code, run roundtrip tests
cd conduit/tests/java && javac *.java && java TestRoundtrip

# Cross-language: encode in Python, decode in C++ (or vice versa)
python3 conduit/tests/python/test_wire_compat.py
```

### C ABI verification
```bash
# Codec-only C ABI tests
./conduit_tests --test-case="[codec_cabi]*"

# Full Transceiver C ABI tests
./conduit_tests --test-case="[cabi]*"
```

### Regression verification
```bash
# ALL existing tests must still pass unchanged
ctest --output-on-failure
# Specifically verify bgen_tests and conduit_tests
./bgen_tests
./conduit_tests
```
