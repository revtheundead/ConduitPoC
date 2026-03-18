# Generated File Structure

[Back to index](index.md)

bgen produces code files whose structure depends on the target language (`--language`). This page describes the output for all three backends. The C++ backend is documented most thoroughly as the reference implementation; Java and Python follow the same logical structure with language-appropriate adaptations.

## C++ Output Files

| File | Generator | Purpose |
|------|-----------|---------|
| `constants.hpp` | `generate_constants()` | Named constants as `inline constexpr` values |
| `types.hpp` | `generate_types()` | Type-level wrappers (enums, flags, scaled, constrained, strings) |
| `structs.hpp` | `generate_structs()` | Struct classes with encode/decode methods |
| `messages.hpp` | `generate_messages()` | Message classes with `TYPE_ID`, convenience methods, `wrap()` |
| `sessions.hpp` | `generate_sessions()` | Session classes implementing `ISession` |
| `protocol.hpp` | `generate_protocol()` | `ProtocolDescriptor` with type registry and session factory |
| `<protocol-name>.hpp` | `generate_umbrella()` | Umbrella header that includes everything |

## Include Chain

Each generated file explicitly includes its dependencies (not relying on transitive includes):

```
constants.hpp        (standalone, only <cstdint>)

types.hpp            (conduit/core/error.hpp, conduit/io/bit_reader.hpp, ...)

structs.hpp          ("types.hpp", "constants.hpp", conduit/core/error.hpp,
                      conduit/io/bit_reader.hpp, conduit/io/bit_writer.hpp, ...)

messages.hpp         ("structs.hpp", "types.hpp", "constants.hpp",
                      conduit/core/error.hpp, conduit/io/bit_reader.hpp,
                      conduit/io/bit_writer.hpp, ...)

sessions.hpp         ("messages.hpp", conduit/traits/session_traits.hpp)

protocol.hpp         ("sessions.hpp", conduit/traits/session_traits.hpp)

<protocol-name>.hpp  (includes all of the above)
```

You only need to include the umbrella header in most cases:

```cpp
#include "generated/my_protocol.hpp"
```

Or include individual files if you want to minimize compile times:

```cpp
#include "generated/types.hpp"      // Only type wrappers
#include "generated/messages.hpp"   // Types + structs + messages
```

## File Details

### constants.hpp

Contains named constants defined by `<const>` elements in BMDL:

```cpp
namespace my_protocol {
inline constexpr uint32_t SYNC_WORD = 0xDEADBEEF;
inline constexpr uint16_t MAX_LENGTH = 1024;
} // namespace my_protocol
```

The C++ type is derived from the constant's type reference (or defaults to `uint32_t`). Hex values preserve their hex format.

### types.hpp

Contains type-level wrappers. Simple types become `using` aliases; complex types become classes. See [Type Code Generation](generated-types.md) for details.

Runtime includes:
- `conduit/core/error.hpp` -- `Result<T>`, `VoidResult`, `Error`
- `conduit/io/bit_reader.hpp` -- `BitReader` for decode
- `conduit/io/bit_writer.hpp` -- `BitWriter` for encode
- `conduit/io/endian.hpp` -- `Endian::Big`, `Endian::Little`
- `conduit/string/encoding.hpp` -- (only if IA5 or EBCDIC string types are used)

### structs.hpp

Contains all struct classes generated from `<struct>` definitions, including:
- Forward declarations of all struct classes
- Nested child classes (inline arrays, inline choice cases)
- Bitmap structs with FSPEC encode/decode

Includes `types.hpp`, `constants.hpp`, and conduit runtime headers (`conduit/core/error.hpp`, `conduit/io/bit_reader.hpp`, `conduit/io/bit_writer.hpp`). Also includes standard library headers: `<algorithm>`, `<array>`, `<cstdint>`, `<optional>`, `<span>`, `<sstream>`, `<string>`, `<variant>`, `<vector>`. May conditionally include `conduit/string/encoding.hpp` when IA5/EBCDIC string types are used. See [Struct & Message Code Generation](generated-structs.md).

### messages.hpp

Contains all message classes generated from `<message>` definitions. Each message class has:
- `TYPE_ID`, `TYPE_NAME`, and `ID_VALUE` static members
- `encode_bytes()` / `decode_bytes()` convenience methods

When a `<frame>` is present, also contains the Frame class with `PayloadVariant`, `wrap()` overloads, and encode/decode with auto-length backpatching.

Includes `structs.hpp`, `types.hpp`, `constants.hpp`, and conduit runtime headers (`conduit/core/error.hpp`, `conduit/io/bit_reader.hpp`, `conduit/io/bit_writer.hpp`). Uses `<any>` and `<string_view>` (replacing `<string>` from `structs.hpp`). See [Struct & Message Code Generation](generated-structs.md).

### sessions.hpp

Contains session classes (one per frame) implementing `conduit::traits::ISession`. See [Session Code Generation](generated-sessions.md).

Includes `messages.hpp`, `conduit/traits/session_traits.hpp`, and standard library headers (`<any>`, `<memory>`, `<span>`, `<string_view>`, `<vector>`). May conditionally include `conduit/logging/logger.hpp` when direction-constrained types exist.

### protocol.hpp

Contains a `ProtocolDescriptor` struct with:
- Protocol `name` and `version` as `constexpr string_view`
- A `TypeInfo` struct with `type_id`, `type_name`, and `groups`
- A `types` array of all leaf types across all sessions
- `find()` methods for lookup by `type_id` or `type_name`
- `create_session()` factory (delegates to the first session's factory)

Includes `sessions.hpp`, `conduit/traits/session_traits.hpp`, and standard library headers (`<array>`, `<cstdint>`, `<memory>`, `<span>`, `<string_view>`).

### Umbrella Header (`<protocol-name>.hpp`)

Includes all generated headers in dependency order:

```cpp
#include "constants.hpp"
#include "types.hpp"
#include "structs.hpp"
#include "messages.hpp"
#include "sessions.hpp"
#include "protocol.hpp"
```

The file name is derived from the protocol name using `to_lower_snake_case()`, which converts hyphens to underscores and PascalCase to snake_case. For example, `my-protocol` produces `my_protocol.hpp`, and `MyProtocol` produces `my_protocol.hpp`.

## File Naming

- All generated files use `.hpp` extension
- All generated files begin with `// Generated by bgen - DO NOT EDIT`
- All generated files use `#pragma once` for include guards
- The umbrella header file name uses `to_lower_snake_case()` on the protocol name (hyphens become underscores, PascalCase becomes snake_case)
- All other file names are fixed (`constants.hpp`, `types.hpp`, etc.)

## Runtime Dependencies

Generated code requires the conduit runtime library headers:

| Header | Used By |
|--------|---------|
| `conduit/core/error.hpp` | types, structs, messages (error types: `Result<T>`, `VoidResult`, `Error`) |
| `conduit/io/bit_reader.hpp` | types, structs, messages (decode) |
| `conduit/io/bit_writer.hpp` | types, structs, messages (encode) |
| `conduit/io/endian.hpp` | types (endian-aware read/write) |
| `conduit/string/encoding.hpp` | types, structs, messages (only when IA5/EBCDIC string types exist) |
| `conduit/traits/session_traits.hpp` | sessions, protocol (`ISession` interface) |
| `conduit/logging/logger.hpp` | sessions (only when direction-constrained types exist) |

---

## Java Output Files

The Java backend generates one `.java` file per class, plus shared utility classes.

| File | Purpose |
|------|---------|
| `BitReader.java` | Bit-level reader (reads bits, bytes, BCD, sign-magnitude from a byte array) |
| `BitWriter.java` | Bit-level writer (accumulates bits into a byte array) |
| `ConduitCodecException.java` | Runtime exception for codec errors (underflow, constraint violations) |
| `Constants.java` | Named constants as `public static final` fields |
| `Protocol.java` | Protocol descriptor with type registry and session factory |
| Per-type `.java` files | Wrapper classes for enums, flags, scaled, constrained, and string types |
| Per-struct `.java` files | Struct classes with public fields, `encode(BitWriter)`/`static decode(BitReader)` |
| Per-message `.java` files | Message classes with `TYPE_ID`, `TYPE_NAME`, `ID_VALUE`, `encodeBytes()`/`decodeBytes()` |
| Frame `.java` file | Frame class with `wrap()`, encode/decode with length backpatching |
| Session `.java` files | Session classes with `decodeFrame()`, `encodeWrap()`, `formatMessage()` |

### Java Field Access

Java uses **public fields** rather than getter/setter methods:

```java
Heartbeat msg = Heartbeat.decode(new BitReader(data));
int seq = msg.sequence;         // direct field access
msg.sequence = 42;              // direct field assignment
```

### Java Runtime Dependencies

Generated Java code is self-contained -- `BitReader.java` and `BitWriter.java` are generated alongside the protocol code. No external library dependencies are required for codec operations. For transport access, the JNI or Panama bindings (`conduit/bindings/java/`) link against the native conduit library.

---

## Python Output Files

The Python backend generates a module directory with `.py` files.

| File | Purpose |
|------|---------|
| `bit_io.py` | `BitReader`/`BitWriter` classes with bit-level operations |
| `constants.py` | Named constants |
| `types.py` | Type wrappers (enums, flags, scaled, constrained, string types) |
| `structs.py` | Struct classes with `encode(BitWriter)`/`decode(BitReader)` class methods |
| `messages.py` | Message classes with `TYPE_ID`, `TYPE_NAME`, `encode_bytes()`/`decode_bytes()`; Frame class |
| `sessions.py` | Session classes with `decode_frame()`, `encode_wrap()`, `format_message()` |
| `protocol.py` | Protocol descriptor with type registry and session factory |
| `__init__.py` | Package initializer |

### Python Field Access

Python uses **public attributes** set in `__init__`:

```python
msg = Heartbeat.decode(BitReader(data))
seq = msg.sequence              # direct attribute access
msg.sequence = 42               # direct attribute assignment
```

### Python Runtime Dependencies

Generated Python code is self-contained -- `bit_io.py` provides the `BitReader`/`BitWriter` implementation. No external package dependencies are required for codec operations. For transport access, the ctypes bindings (`conduit/bindings/python/`) link against the native conduit shared library.

---

## Cross-Backend Comparison

| Concept | C++ | Java | Python |
|---------|-----|------|--------|
| Bit I/O | `conduit::io::BitReader` (library) | `BitReader.java` (generated) | `bit_io.BitReader` (generated) |
| Constants | `inline constexpr` | `public static final` | Module-level variables |
| Type wrappers | Classes with `value()`/`set_value()` | Classes with public `value` field | Classes with public `value` attribute |
| Struct fields | Private members + getters/setters | Public fields | Public attributes |
| Optional fields | `std::optional<T>` | Boxed types (nullable `Integer`, `Long`) | `None` sentinel |
| Encode | `msg.encode(BitWriter&)` | `msg.encode(BitWriter)` | `msg.encode(BitWriter)` |
| Decode | `Msg::decode(BitReader&)` | `Msg.decode(BitReader)` | `Msg.decode(BitReader)` |
| Convenience | `encode_bytes()` / `decode_bytes(span)` | `encodeBytes()` / `decodeBytes(byte[])` | `encode_bytes()` / `decode_bytes(bytes)` |
| Session | `ISession` interface | Class with virtual methods | Class with methods |

## See Also

- [conduit Runtime Library](../conduit/index.md) -- Documentation for each conduit header listed above
- [Type Code Generation](generated-types.md) -- How BMDL types map to code in each backend
- [Struct & Message Code Generation](generated-structs.md) -- How structs/messages are generated
- [Session Code Generation](generated-sessions.md) -- How sessions are generated
- [Limitations & Design Boundaries](../conduit/limitations.md) -- Scope boundaries and design decisions
