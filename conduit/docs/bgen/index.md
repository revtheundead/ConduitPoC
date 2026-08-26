# bgen Documentation

[Back to top-level documentation](../index.md)

bgen is the BMDL code generator. It reads BMDL XML protocol definitions, validates them, and produces type-safe encode/decode code in **C++**, **Java**, or **Python**. All three backends generate wire-compatible output from the same BMDL source -- messages encoded by one language can be decoded by any other.

## Pipeline Overview

bgen processes a BMDL protocol definition in six stages:

1. **Build AST** -- Parse the root XML file, resolve imports, merge into a single `Protocol` AST
2. **Resolve Types** -- Build a `TypeIndex` for O(1) lookups, validate all type references
3. **Validate** -- Check all BMDL spec rules (field sizes, constraint consistency, etc.)
4. **Compute Wire Sizes** -- Determine fixed vs dynamic wire sizes for all structs/messages
5. **Analyze Sessions** -- Discover frames, leaf types, sync patterns, auto fields
6. **Generate Code** -- Emit code files in the selected target language (C++, Java, or Python)

Stages 1--5 are language-agnostic. Only stage 6 differs per backend. Each of the first three stages may accumulate errors within itself; if a stage fails, its errors are reported and bgen exits with a non-zero code. Stages 4-5 always succeed given valid input. Stage 6 can fail only with filesystem errors.

## Quick Start

Given a minimal BMDL file `my-protocol.bmdl.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="2.0">
  <defaults>
    <endian>big</endian>
    <namespace>my-protocol</namespace>
  </defaults>

  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
  </types>

  <frame name="MyFrame">
    <field name="msg-type" type="uint8" auto="id"/>
    <field name="length" type="uint16" auto="length"/>
    <payload/>
  </frame>

  <messages>
    <message id="1" name="Heartbeat">
      <field name="sequence" type="uint16"/>
      <field name="status" type="uint8"/>
    </message>
  </messages>
</bmdl>
```

### Generating C++ (default)

```bash
bgen --input my-protocol.bmdl.xml --output generated/
```

This produces 8 header files in `generated/`:

| File | Purpose |
|------|---------|
| `constants.hpp` | Named constants (`inline constexpr`) |
| `types.hpp` | Type wrappers (enums, flags, scaled, constrained, strings) |
| `structs.hpp` | Struct classes with encode/decode |
| `messages.hpp` | Message classes with `TYPE_ID`, `encode_bytes()`, `decode_bytes()`; Frame class with `wrap()` |
| `sessions.hpp` | Session classes implementing `ISession` for frame-based dispatch |
| `protocol.hpp` | `ProtocolDescriptor` with type registry and session factory |
| `json.hpp` | nlohmann/json `to_json`/`from_json` overloads (only pulls in `<nlohmann/json.hpp>` when included) |
| `my_protocol.hpp` | Umbrella header that includes all of the above **except `json.hpp`** (which stays opt-in to avoid a hard `<nlohmann/json.hpp>` dependency) |

### Generating Java

```bash
bgen --input my-protocol.bmdl.xml --output generated/ --language java
```

This produces Java source files including:

| File | Purpose |
|------|---------|
| `BitReader.java` / `BitWriter.java` | Bit-level I/O utilities |
| `Constants.java` | Named constants |
| Type wrapper classes (e.g., `MsgType.java`) | Enums, flags, scaled, constrained, string wrappers |
| Struct/message classes (e.g., `Heartbeat.java`) | Message classes with `encode()`/`decode()`, `TYPE_ID`, `TYPE_NAME` |
| Session classes (e.g., `MyFrameSession.java`) | Session with `decodeFrame()`, `encodeWrap()` |

### Generating Python

```bash
bgen --input my-protocol.bmdl.xml --output generated/ --language python
```

This produces Python module files including:

| File | Purpose |
|------|---------|
| `bit_io.py` | `BitReader`/`BitWriter` classes |
| `constants.py` | Named constants |
| `types.py` | Type wrappers (enums, flags, scaled, constrained, strings) |
| `structs.py` | Struct classes with `encode()`/`decode()` |
| `messages.py` | Message classes with `TYPE_ID`, `encode_bytes()`/`decode_bytes()`; Frame class |
| `sessions.py` | Session classes with `decode_frame()`, `encode_wrap()` |
| `protocol.py` | Protocol descriptor with type registry |

## Usage Examples

**C++:**

```cpp
#include "generated/my_protocol.hpp"

// Decode a message
auto msg = my_protocol::Heartbeat::decode_bytes(data);
if (msg) {
    std::cout << msg->sequence() << "\n";
}

// Encode a message
my_protocol::Heartbeat hb;
hb.set_sequence(42);
hb.set_status(1);
auto bytes = hb.encode_bytes();
```

**Java:**

```java
import my_protocol.Heartbeat;

// Decode a message
Heartbeat msg = Heartbeat.decode(new BitReader(data));
System.out.println(msg.sequence);

// Encode a message
Heartbeat hb = new Heartbeat();
hb.sequence = 42;
hb.status = 1;
byte[] bytes = hb.encodeBytes();
```

**Python:**

```python
from my_protocol.messages import Heartbeat
from my_protocol.bit_io import BitReader, BitWriter

# Decode a message
msg = Heartbeat.decode(BitReader(data))
print(msg.sequence)

# Encode a message
hb = Heartbeat()
hb.sequence = 42
hb.status = 1
data = hb.encode_bytes()
```

> **Name conversion:** bgen converts BMDL names to language-appropriate identifiers. In C++, hyphens become underscores (`msg-type` -> `msg_type()`). In Java, hyphens become camelCase (`msg-type` -> `msgType`). In Python, hyphens become underscores (`msg-type` -> `msg_type`). See [Naming Conventions](naming-conventions.md) for the full rules.

## Language Backends

All three backends generate code from the same resolved AST -- they receive identical `Protocol`, `TypeIndex`, `WireSizeInfo`, and `SessionInfo` data. The differences are purely syntactic:

| Aspect | C++ | Java | Python |
|--------|-----|------|--------|
| Field access | `msg.sequence()` / `msg.set_sequence(42)` | `msg.sequence` (public field) | `msg.sequence` (public attribute) |
| Optional fields | `std::optional<T>` | Boxed types (`Integer`, `Long`, etc.) | `None` sentinel |
| Variants | `std::variant<A, B>` | `Object` + `instanceof` | Dynamic typing |
| Error handling | `Result<T>` / `VoidResult` | Exceptions | Exceptions |
| Include/import | `#include "types.hpp"` | `import my_protocol.Heartbeat;` | `from my_protocol.messages import Heartbeat` |

## Documentation Guide

| Document | Description |
|----------|-------------|
| [Command-Line Usage](cli.md) | CLI arguments (including `--language`), exit codes, error format, examples |
| [Processing Pipeline](pipeline.md) | Detailed description of each pipeline stage |
| [Generated File Structure](output-files.md) | Output files per backend, include chain, runtime dependencies |
| [Type Code Generation](generated-types.md) | How BMDL types map to C++, Java, and Python |
| [Struct & Message Code Generation](generated-structs.md) | Classes, accessors, encode/decode, choices, arrays, bitmap, FX |
| [Session Code Generation](generated-sessions.md) | Session classes, frame decode, wrap, sync, introspection |
| [Naming Conventions](naming-conventions.md) | BMDL-to-code name mapping rules for all backends |

## Related Documentation

- [BMDL Language Reference](../bmdl/index.md) -- The BMDL XML language specification
- [conduit Runtime Library](../conduit/index.md) -- The runtime library that generated code depends on
- [Limitations & Design Boundaries](../conduit/limitations.md) -- Scope boundaries and design decisions
