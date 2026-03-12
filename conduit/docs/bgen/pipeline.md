# Processing Pipeline

[Back to index](index.md)

bgen processes BMDL protocol definitions through six sequential stages. Each of the first three stages (AST, resolve, validate) may accumulate errors within itself; if a stage fails, its errors are reported and bgen exits before proceeding to the next stage. Stages 4-5 always succeed given valid input. Stage 6 can fail only with filesystem errors.

## Stage 1: Build AST

**Input:** Root BMDL XML file path
**Output:** `Protocol` AST (merged from all imported files)

1. Parse the root `.bmdl.xml` file into a `BmdlFile` AST
2. Recursively resolve `<import>` elements, parsing each imported file
3. Merge all files into a single `Protocol` structure containing:
   - Protocol name and version
   - Defaults (endian, string encoding/padding/trim)
   - All constants, types, structs, and messages in declaration order
4. Apply protocol-level defaults to types/fields that don't specify their own

Errors at this stage include XML syntax errors, missing import files, and duplicate definitions.

## Stage 2: Resolve Types

**Input:** `Protocol` AST
**Output:** `TypeIndex` (O(1) lookup by name for all types, structs, messages, constants, and frames)

1. Build an index mapping every named definition to its AST node
2. Validate that all `type_ref` attributes reference existing definitions
3. Resolve type inheritance and attribute propagation

The `TypeIndex` contains separate maps for types, structs, messages, constants, and frames. It is used by all subsequent stages for fast name resolution. Errors include unresolved type references and circular dependencies.

## Stage 3: Validate

**Input:** `Protocol` AST + `TypeIndex`
**Output:** Validated protocol (no additional data structure -- validation is pass/fail)

Checks all BMDL specification rules:

- **Type rules**: Valid base types, bit widths, enum value ranges, flag bit positions
- **Field rules**: Valid type references, compatible attributes, expression validity
- **Struct rules**: Non-empty children, valid nesting
- **Choice rules**: Valid switch expressions, non-overlapping case values
- **Array rules**: Valid count/length expressions
- **Constraint rules**: Consistent min/max/equals values
- **Expression rules**: Valid operators, reachable field references (no forward references)

If `--validate-only` is specified, bgen exits successfully after this stage.

If `--dump-ast` is specified, bgen prints the resolved AST to stdout and exits.

## Stage 4: Compute Wire Sizes

**Input:** `Protocol` AST + `TypeIndex`
**Output:** `WireSizeInfo` (maps struct/message names to fixed wire sizes)

For each struct and message, determines whether it has a fixed wire size (constant number of bytes) or dynamic size:

- **Fixed size**: All fields have known bit widths, no variable-length fields, no optional fields. The wire size is the sum of all field sizes, computed in bits and rounded up to bytes.
- **Dynamic size**: Any variable-length field (star-length, length-from), optional field (bitmap bit, present-when), choice, or dynamic array makes the size dynamic.

Fixed-size structs get a `static constexpr size_t WIRE_SIZE` constant in the generated code. Dynamic-size structs simply do not have `WIRE_SIZE`.

## Stage 5: Analyze Sessions

**Input:** `Protocol` AST + `TypeIndex`
**Output:** `vector<SessionInfo>` (one per frame)

For each `<frame>` in the protocol:

1. **Create one SessionInfo** per frame with `is_frame_based = true`
2. **Discover leaf types**: Each `<message>` becomes a leaf type with its `id` as a constraint, its `direction` (send/receive/both), and any `auto="increment"` fields
3. **Extract frame metadata**: ID field (`auto="id"`), length field (`auto="length"` with optional offset), sync pattern (`constraint equals` on leading field), config fields (`auto="config(key)"`)
4. **Compute min frame header size**: Sum of all header field bit widths (for partial-header reading)
5. **Extract frame length info**: Bit offset, bit width, and endianness of the length field for efficient partial-header parsing
6. **Check type_id collisions**: Verify that no two leaf types within the same session hash to the same `type_id` (FNV-1a). Sessions with collisions are skipped with an error

## Stage 6: Generate Code

**Input:** `Protocol` AST + `TypeIndex` + `WireSizeInfo` + `vector<SessionInfo>` + namespace string + target language
**Output:** Code files in the output directory (format depends on `--language`)

The target language is selected by `--language` (`cpp`, `java`, or `python`; default: `cpp`). **Stages 1--5 are identical regardless of target language** -- only this stage differs per backend. Each backend receives the same resolved AST, type index, wire size information, and session analysis results.

1. Create the output directory (if it doesn't exist)
2. Derive the namespace from `--namespace`, `<defaults><namespace>`, or protocol name (hyphens to underscores)
3. Dispatch to the selected language backend and generate files

**C++ backend** (`cpp_backend.cpp`) generates 7 header files:
   - `constants.hpp`, `types.hpp`, `structs.hpp`, `messages.hpp`, `sessions.hpp`, `protocol.hpp`, `<protocol-name>.hpp`

**Java backend** (`java_backend.cpp`) generates `.java` source files:
   - `BitReader.java` / `BitWriter.java`, per-type/struct/message classes, session classes, `Constants.java`

**Python backend** (`python_backend.cpp`) generates `.py` module files:
   - `bit_io.py`, `constants.py`, `types.py`, `structs.py`, `messages.py`, `sessions.py`, `protocol.py`

If any file write fails, bgen reports the error and exits with code 3.

See [Generated File Structure](output-files.md) for details on each backend's output files.
