# Generated File Structure

[Back to index](index.md)

bgen produces 7 C++ header files. Each file is self-contained (includes its own dependencies) and follows a strict include chain.

## Output Files

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
#include "generated/my-protocol.hpp"
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

The file name matches the protocol name (with hyphens preserved), e.g., `my-protocol.hpp`.

## File Naming

- All generated files use `.hpp` extension
- All generated files begin with `// Generated by bgen - DO NOT EDIT`
- All generated files use `#pragma once` for include guards
- The umbrella header file name preserves the original protocol name (including hyphens)
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

## See Also

- [conduit Runtime Library](../conduit/index.md) -- Documentation for each conduit header listed above
- [Type Code Generation](generated-types.md) -- How BMDL types map to C++ in `types.hpp`
- [Struct & Message Code Generation](generated-structs.md) -- How structs/messages are generated in `structs.hpp` and `messages.hpp`
- [Session Code Generation](generated-sessions.md) -- How sessions are generated in `sessions.hpp`
