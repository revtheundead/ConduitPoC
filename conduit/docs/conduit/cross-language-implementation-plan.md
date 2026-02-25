# Cross-Language Support for Conduit: Java & Python via bgen + C ABI

## Context

Conduit is a C++ binary protocol library with a 6-stage code generator (`bgen`) that currently only emits C++ headers. The `cross-language-integration.md` design document outlines 8 strategies for making Conduit accessible from other languages. This plan implements the two highest-priority strategies — **multi-language bgen code generation backends** (Strategy 1) and **C ABI wrapper layer** (Strategy 2) — with **Java and Python** as the priority target languages. The bridge app (Strategy 4) is out of scope for now, but the groundwork laid here will support it later.

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

Similarly, the C ABI layer must expose the full `Transceiver` API surface — every operation available in C++ must be callable through the C ABI. The Python and Java C ABI bindings must in turn expose every C ABI function. No capability is lost at any layer boundary.

**Validation approach:** The same BMDL test fixtures used for C++ roundtrip tests (in `bgen/tests/fixtures/`) are used for Python and Java roundtrip tests. If a fixture produces correct C++ code, it must also produce correct Python and Java code. Any fixture that exercises a BMDL feature is a fixture that all backends must handle.

---

## Workstream Overview

| # | Workstream | Description |
|---|-----------|-------------|
| 1 | **bgen multi-language architecture** | Refactor bgen's stage 6 to support `--language` flag; introduce codegen backend interface |
| 2 | **Python bgen backend** | Generate Python encode/decode classes from BMDL |
| 3 | **Java bgen backend** | Generate Java encode/decode classes from BMDL |
| 4 | **C ABI wrapper library** | `extern "C"` flat API around `Transceiver` |
| 5 | **Python C ABI bindings** | `ctypes`/`cffi` wrapper module consuming the C ABI shared library |
| 6 | **Java C ABI bindings** | JNI/JNA wrapper consuming the C ABI shared library |
| 7 | **Build system integration** | CMake options, third-party deps, install rules |
| 8 | **Tests** | Unit + integration tests for every workstream |

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
`bgen --language python` generates a Python package with native message classes that can encode/decode BMDL-defined binary protocols. Wire-compatible with C++ generated code. **Must handle every BMDL feature the C++ backend handles** — the full feature parity table above applies without exception.

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
└── protocol.py              # Protocol descriptor / type registry
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
- `conduit/bgen/src/codegen/python_codec.hpp` (new — emits the `codec.py` runtime)
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
`bgen --language java` generates a Java package with message classes that encode/decode BMDL-defined binary protocols. Wire-compatible with C++ and Python. **Must handle every BMDL feature the C++ backend handles** — the full feature parity table above applies without exception.

### Output structure
```
<output_dir>/
├── Constants.java                # Named constants as static final fields
├── Types.java                    # Enums, flags, scaled type wrappers
├── <StructName>.java             # One file per struct (Java convention)
├── <MessageName>.java            # One file per message
├── <SessionName>Session.java     # Session class per frame
├── BitReader.java                # Binary codec reader
├── BitWriter.java                # Binary codec writer
├── ProtocolDescriptor.java       # Type registry
└── DecodedMessage.java           # Decoded message container
```

### Key design decisions

**Java codec (`BitReader.java`, `BitWriter.java`):**
- Java port of the bit-level I/O, operating on `byte[]` / `ByteBuffer`
- Must implement every read/write method the C++ BitReader/BitWriter supports: `readBits`/`writeBits`, `readBcd`/`writeBcd`, `readBcdSigned`/`writeBcdSigned`, `readSignMagnitude`/`writeSignMagnitude`, byte-aligned reads/writes, string operations (padded, trimmed, terminated, length-prefixed, packed-character IA5), float32/float64
- Support all wire encodings: default (binary), CB2, BCD, BCD_S, BNR, BNR_S
- Support all endianness modes (big-endian, little-endian)
- Support all string encodings: ASCII, UTF-8, IA5, EBCDIC
- Handle Java's lack of unsigned types: use next-wider signed type or explicit masking (e.g., `long` for uint32, `int` for uint16)

**Generated Java classes (1:1 with C++ generated classes):**
- Public final classes with private fields, getters, setters
- `public void encode(BitWriter writer)` method
- `public static T decode(BitReader reader)` static method
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
- `conduit/bgen/src/codegen/java_codec.hpp` (new — emits BitReader/BitWriter Java runtime)
- `conduit/bgen/src/codegen/java_types.cpp` (new)
- `conduit/bgen/src/codegen/java_structs.cpp` (new)
- `conduit/bgen/src/codegen/java_session.cpp` (new)

---

## Workstream 4: C ABI Wrapper Library (`libconduit_cabi`)

### Goal
Expose the **full** `Transceiver` API surface through a C-linkage shared library, enabling FFI from any language. Every operation available to a C++ user of `Transceiver` must be callable through this C ABI — lifecycle, peer management, single and batch messaging, handler registration/removal, state/error callbacks, statistics, and query methods. No capability is hidden behind the FFI boundary.

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

/* Error codes (mirrors conduit::ErrorCode subset) */
typedef int32_t conduit_error_t;
#define CONDUIT_OK 0
#define CONDUIT_ERR_INVALID_ARGUMENT -1
#define CONDUIT_ERR_ALREADY_RUNNING -2
#define CONDUIT_ERR_NOT_RUNNING -3
#define CONDUIT_ERR_PEER_NOT_FOUND -4
#define CONDUIT_ERR_SEND_FAILED -5
#define CONDUIT_ERR_ENCODE_FAILED -6
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
    /* Additional fields for serial config, etc. */
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

/* Session factory: user provides a function that creates session bytes
   (actually, the C ABI uses session names registered in a global registry) */
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

/* Messaging */
conduit_error_t conduit_send(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t* data, size_t len);

conduit_callback_id conduit_on_message(
    conduit_transceiver_t* xcvr,
    uint64_t type_id,
    conduit_msg_callback_t callback,
    void* user_data);

conduit_callback_id conduit_on_any_message(
    conduit_transceiver_t* xcvr,
    conduit_msg_callback_t callback,
    void* user_data);

/* State & error callbacks */
conduit_callback_id conduit_on_state_change(
    conduit_transceiver_t* xcvr,
    conduit_state_callback_t callback,
    void* user_data);

conduit_callback_id conduit_on_error(
    conduit_transceiver_t* xcvr,
    conduit_error_callback_t callback,
    void* user_data);

/* Query */
size_t conduit_peer_count(const conduit_transceiver_t* xcvr);
int32_t conduit_peer_state(const conduit_transceiver_t* xcvr, conduit_peer_id peer);

/* Session registry: register session factories by name for C ABI usage */
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
- Error handling: C++ exceptions → error codes; `Result<T>` → `conduit_error_t`
- Thread safety: same guarantees as `Transceiver` (callbacks fire on I/O thread)

### Build target
- New CMake target: `conduit_cabi` (SHARED library)
- Links `conduit` (static)
- Exports only `extern "C"` symbols
- Build option: `CONDUIT_BUILD_CABI` (OFF by default)

### Files
- `conduit/include/conduit/cabi/conduit_cabi.h` (new)
- `conduit/src/cabi/conduit_cabi.cpp` (new)
- `conduit/CMakeLists.txt` (modify — add `CONDUIT_BUILD_CABI` option and target)

---

## Workstream 5: Python C ABI Bindings

### Goal
A Python module (`conduit.transceiver`) that wraps `libconduit_cabi` via `ctypes`, providing a Pythonic API. **Must expose every function in the C ABI** — lifecycle, peer management, send/send_batch, handler registration/removal, state/error callbacks, statistics, and query methods. Combined with the Python bgen-generated message types (Workstream 2), this gives full end-to-end Python support with zero capability loss relative to C++.

### Files
- `conduit/bindings/python/conduit/__init__.py` (new)
- `conduit/bindings/python/conduit/transceiver.py` (new — ctypes wrapper)
- `conduit/bindings/python/conduit/types.py` (new — Python types for transport config, etc.)
- `conduit/bindings/python/setup.py` or `pyproject.toml` (new)

### API sketch
```python
from conduit import Transceiver, UdpTransport
from my_protocol import Heartbeat, create_heartbeat_session

with Transceiver() as t:
    peer = t.add_peer("rx", "heartbeat_session", UdpTransport("0.0.0.0:5000"))

    @t.on(Heartbeat)
    def handle_hb(peer_id, msg):
        print(f"seq={msg.sequence}")

    t.start()
    # ... blocks or returns based on usage pattern
```

### Design notes
- `ctypes.cdll.LoadLibrary()` to load `libconduit_cabi.so`
- Python callbacks → C function pointers via `ctypes.CFUNCTYPE`
- Raw bytes from message callback → decoded using Python-generated message classes
- Context manager (`__enter__`/`__exit__`) for lifecycle
- Thread safety: GIL is released during C calls; callbacks acquire GIL

---

## Workstream 6: Java C ABI Bindings

### Goal
A Java library that wraps `libconduit_cabi` via JNA (Java Native Access), providing a Java API. **Must expose every function in the C ABI** — lifecycle, peer management, send/send_batch, handler registration/removal, state/error callbacks, statistics, and query methods. Combined with Java bgen-generated message types (Workstream 3), this gives full end-to-end Java support with zero capability loss relative to C++.

### Third-party dependency: JNA
- Include JNA JAR in `conduit/third_party/jna/` or reference as Maven dependency
- JNA was chosen over raw JNI for simpler binding code (no `.h` generation, no `javah`)

### Files
- `conduit/bindings/java/src/main/java/io/conduit/Transceiver.java` (new)
- `conduit/bindings/java/src/main/java/io/conduit/CabiBindings.java` (new — JNA interface)
- `conduit/bindings/java/src/main/java/io/conduit/TransportConfig.java` (new)
- `conduit/bindings/java/src/main/java/io/conduit/PeerId.java` (new)
- `conduit/bindings/java/build.gradle` or `pom.xml` (new)

### Design notes
- JNA `Library` interface maps C ABI functions
- `Transceiver` class wraps the opaque handle
- Callbacks via JNA `Callback` interface
- Raw bytes from message callback → decoded using Java-generated message classes

---

## Workstream 7: Build System Integration

### CMake options (additions to `conduit/CMakeLists.txt`)

```cmake
option(CONDUIT_BUILD_CABI         "Build C ABI shared library"              OFF)
option(CONDUIT_BUILD_PYTHON_BINDINGS "Build Python bindings (requires CABI)" OFF)
option(CONDUIT_BUILD_JAVA_BINDINGS   "Build Java bindings (requires CABI)"  OFF)
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
├── include/conduit/cabi/           # C ABI public header
│   └── conduit_cabi.h
├── src/cabi/                       # C ABI implementation
│   └── conduit_cabi.cpp
├── bindings/
│   ├── python/                     # Python ctypes bindings
│   │   └── conduit/
│   │       ├── __init__.py
│   │       ├── transceiver.py
│   │       └── types.py
│   └── java/                       # Java JNA bindings
│       └── src/main/java/io/conduit/
│           ├── Transceiver.java
│           ├── CabiBindings.java
│           └── ...
├── bgen/src/codegen/
│   ├── codegen_backend.hpp         # Backend interface
│   ├── codegen_backend.cpp         # Backend factory
│   ├── cpp_backend.hpp/.cpp        # Existing C++ wrapped as backend
│   ├── python_backend.hpp/.cpp     # Python code generator
│   ├── python_codec.hpp            # Python BitReader/BitWriter emitter
│   ├── python_types.cpp            # Python types emitter
│   ├── python_structs.cpp          # Python structs emitter
│   ├── python_session.cpp          # Python sessions emitter
│   ├── java_backend.hpp/.cpp       # Java code generator
│   ├── java_codec.hpp              # Java BitReader/BitWriter emitter
│   ├── java_types.cpp              # Java types emitter
│   ├── java_structs.cpp            # Java structs emitter
│   └── java_session.cpp            # Java sessions emitter
├── tests/
│   ├── test_cabi.cpp               # C ABI unit tests
│   └── ...
└── third_party/
    └── jna/                        # JNA JAR for Java bindings
```

---

## Workstream 8: Tests

### 8a. bgen backend tests (extend existing test suite)

**Critical: all backends run against the same BMDL fixtures.** The existing fixtures in `bgen/tests/fixtures/` cover the full BMDL feature set — every backend must successfully generate code from every fixture that the C++ backend handles. This is the primary mechanism for enforcing feature parity: if a fixture exercises bitmaps and the Python backend can't emit bitmap code, the test fails.

**Test Python codegen output** — `conduit/bgen/tests/test_python_codegen.cpp`
- Run Python backend on **all** test fixtures used by C++ backend tests (e.g., `all_types.bmdl.xml`, `struct_features.bmdl.xml`, `bitmap_fx.bmdl.xml`, `arrays_choices.bmdl.xml`, `frame_basic.bmdl.xml`, `asterix.bmdl.xml`, `sentry_link.bmdl.xml`, etc.)
- Verify generated `.py` files exist and contain expected class names, type IDs, method signatures
- Pattern: same as `test_codegen_output.cpp` but for Python output

**Test Java codegen output** — `conduit/bgen/tests/test_java_codegen.cpp`
- Same pattern for Java output, same fixture coverage

**CLI tests** — extend `test_parser.cpp` or new `test_cli.cpp`
- Verify `--language python` and `--language java` flags are accepted
- Verify `--language unknown` produces an error
- Verify default (`--language cpp` or no flag) produces identical output to current behavior

### 8b. Python roundtrip tests

**`conduit/tests/python/`** — Python test scripts (run via pytest or unittest)
- `test_codec.py` — Test every Python `BitReader`/`BitWriter` method against known byte sequences. Must cover all wire encodings (BCD, BCD_S, BNR, BNR_S, default), both endianness modes, all string encodings (ASCII, UTF-8, IA5, EBCDIC), and all string modes (padded, trimmed, terminated, length-prefixed)
- `test_roundtrip.py` — Generate Python code from **all** test fixtures that C++ uses, then encode → decode and verify every field matches. This is the primary parity verification: if C++ passes a roundtrip test on a fixture, Python must too
- `test_wire_compat.py` — Encode in Python, decode in C++ (and vice versa) to verify wire compatibility. Use shared golden byte sequences: C++ encodes known messages to binary files, Python reads and decodes them (and the reverse). This proves the generated code from different backends produces identical wire formats
- CMake integration: `add_test(NAME python_roundtrip COMMAND python3 -m pytest ...)`
- Requires: Python 3.9+ on the test machine

### 8c. Java roundtrip tests

**`conduit/tests/java/`** — Java test files (JUnit or plain main classes)
- `TestCodec.java` — Test every Java `BitReader`/`BitWriter` method (same coverage as Python codec tests — all wire encodings, endianness, string modes)
- `TestRoundtrip.java` — Encode → decode roundtrips on all test fixtures (same fixture set as C++ and Python)
- `TestWireCompat.java` — Cross-language wire compatibility using same golden byte sequences as Python tests
- CMake integration: `add_test(NAME java_roundtrip COMMAND java ...)`
- Requires: JDK 17+ on the test machine

### 8d. C ABI tests — `conduit/tests/test_cabi.cpp`
- Test every C ABI function — no function in the header should be untested
- Test lifecycle: create → start → stop → destroy
- Test peer management: add peer (single + multi-peer), query count/state, peer lookup
- Test message send/receive through C ABI callbacks (single and batch)
- Test handler registration and removal (per-type and any-message)
- Test state change and error callbacks
- Test error code mapping (every `conduit_error_t` value)
- Test session registry (register, lookup, use)
- Uses Catch2 (same framework as existing tests)

### 8e. Cross-language integration tests
- Python script that loads `libconduit_cabi`, creates transceiver, sends a message encoded with Python-generated types, and verifies receipt via callback
- Java program that does the same
- These are optional/advanced and can be added incrementally

---

## Implementation Order

The implementation should proceed in this order to build on each completed layer:

1. **Workstream 1** — bgen multi-language architecture (backend interface + `--language` flag)
2. **Workstream 2** — Python bgen backend (codec.py + generated Python types)
3. **Workstream 3** — Java bgen backend (BitReader/BitWriter.java + generated Java types)
4. **Workstream 4** — C ABI wrapper library
5. **Workstream 7** — Build system integration (CMake options, BgenGenerate.cmake updates)
6. **Workstream 8a** — bgen backend codegen tests (Python + Java output verification)
7. **Workstream 8b** — Python roundtrip tests
8. **Workstream 8c** — Java roundtrip tests
9. **Workstream 8d** — C ABI tests
10. **Workstream 5** — Python C ABI bindings
11. **Workstream 6** — Java C ABI bindings
12. **Workstream 8e** — Cross-language integration tests

### Phase 1 (Foundation): Workstreams 1, 7
### Phase 2 (Code Generation): Workstreams 2, 3, 8a
### Phase 3 (C ABI): Workstreams 4, 8d
### Phase 4 (Language Bindings): Workstreams 5, 6, 8b, 8c, 8e

---

## Critical Files Reference

### Existing files to modify
| File | Change |
|------|--------|
| `conduit/bgen/src/main.cpp` | Add `--language` flag, dispatch to backends |
| `conduit/bgen/CMakeLists.txt` | Add new codegen source files |
| `conduit/CMakeLists.txt` | Add `CONDUIT_BUILD_CABI`, `CONDUIT_BUILD_PYTHON_BINDINGS`, `CONDUIT_BUILD_JAVA_BINDINGS` options + targets |
| `conduit/cmake/BgenGenerate.cmake` | Add `LANGUAGE` parameter |
| `conduit/bgen/tests/CMakeLists.txt` | Add Python/Java codegen tests |
| `conduit/tests/CMakeLists.txt` | Add C ABI tests, Python/Java test targets |

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

---

## Verification Plan

### Build verification
```bash
cd conduit && mkdir build && cd build

# Default build (unchanged behavior)
cmake .. -DCONDUIT_BUILD_TESTS=ON
cmake --build .
ctest

# Full build with new features
cmake .. \
  -DCONDUIT_BUILD_TESTS=ON \
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

# Python
./bgen --input test.bmdl.xml --output out_py --language python
ls out_py/  # __init__.py, constants.py, types.py, structs.py, ...

# Java
./bgen --input test.bmdl.xml --output out_java --language java
ls out_java/  # Constants.java, BitReader.java, ...
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
# Run C ABI unit tests
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
