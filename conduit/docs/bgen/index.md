# bgen Documentation

[Back to top-level documentation](../index.md)

bgen is the BMDL code generator. It reads BMDL XML protocol definitions, validates them, and produces C++ header files that provide type-safe encode/decode for every message type in the protocol.

## Pipeline Overview

bgen processes a BMDL protocol definition in six stages:

1. **Build AST** -- Parse the root XML file, resolve imports, merge into a single `Protocol` AST
2. **Resolve Types** -- Build a `TypeIndex` for O(1) lookups, validate all type references
3. **Validate** -- Check all BMDL spec rules (field sizes, constraint consistency, etc.)
4. **Compute Wire Sizes** -- Determine fixed vs dynamic wire sizes for all structs/messages
5. **Analyze Sessions** -- Discover entry-points, leaf types, sync patterns, context fields
6. **Generate Code** -- Emit 7 C++ header files into the output directory

Each of the first three stages may accumulate errors within itself; if a stage fails, its errors are reported and bgen exits with a non-zero code. Stages 4-5 always succeed given valid input. Stage 6 can fail only with filesystem errors.

## Quick Start

Given a minimal BMDL file `my-protocol.bmdl.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bmdl version="1.0">
<protocol name="my-protocol" version="1.0">
  <defaults><endian>big</endian></defaults>
  <types>
    <type name="uint8" base="uint" bits="8"/>
    <type name="uint16" base="uint" bits="16"/>
  </types>
  <messages>
    <message name="Heartbeat">
      <field name="sequence" type="uint16"/>
      <field name="status" type="uint8"/>
    </message>
  </messages>
</protocol>
</bmdl>
```

Run bgen:

```
bgen --input my-protocol.bmdl.xml --output generated/
```

This produces 7 files in `generated/`:

| File | Purpose |
|------|---------|
| `constants.hpp` | Named constants (`inline constexpr`) |
| `types.hpp` | Type wrappers (enums, flags, scaled, constrained, strings) |
| `structs.hpp` | Struct classes with encode/decode |
| `messages.hpp` | Message classes with `TYPE_ID`, `encode_bytes()`, `decode_bytes()`, `wrap()` |
| `sessions.hpp` | Session classes implementing `ISession` for entry-point dispatch |
| `protocol.hpp` | `ProtocolDescriptor` with type registry and session factory |
| `my-protocol.hpp` | Umbrella header that includes all of the above |

Include the umbrella header in your application:

```cpp
#include "generated/my-protocol.hpp"

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

> **Name conversion:** bgen replaces all hyphens (`-`) with underscores (`_`) in generated C++ identifiers. A BMDL field named `msg-type` becomes the C++ accessor `msg_type()`, setter `set_msg_type()`, and member `msg_type_`. Protocol name `my-protocol` becomes namespace `my_protocol`. See [Naming Conventions](naming-conventions.md) for the full rules.

## Documentation Guide

| Document | Description |
|----------|-------------|
| [Command-Line Usage](cli.md) | CLI arguments, exit codes, error format, examples |
| [Processing Pipeline](pipeline.md) | Detailed description of each pipeline stage |
| [Generated File Structure](output-files.md) | Output files, include chain, runtime dependencies |
| [Type Code Generation](generated-types.md) | How BMDL types map to C++ (enums, flags, scaled, etc.) |
| [Struct & Message Code Generation](generated-structs.md) | Classes, accessors, encode/decode, choices, arrays, bitmap, FX |
| [Session Code Generation](generated-sessions.md) | Session classes, frame decode, wrap, sync, introspection |
| [Naming Conventions](naming-conventions.md) | BMDL-to-C++ name mapping rules |

## Related Documentation

- [BMDL Language Reference](../bmdl/index.md) -- The BMDL XML language specification
- [conduit Runtime Library](../conduit/index.md) -- The runtime library that generated code depends on
