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

The `TypeIndex` contains separate maps for types, structs, messages, constants, and frames (v2). It is used by all subsequent stages for fast name resolution. Errors include unresolved type references and circular dependencies.

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
**Output:** `vector<SessionInfo>` (one per frame or entry-point message)

### Frame-based Analysis (v2)

When a `<frame>` is present, the analyzer takes a simpler path:

1. **Create one SessionInfo** per frame with `is_frame_based = true`
2. **Discover leaf types**: Each `<message>` becomes a leaf type with its `id` as a constraint
3. **Extract frame metadata**: ID field (auto="id"), length field (auto="length"), sync pattern (constraint-equals), config fields (auto="config(key)")
4. **Compute min frame header size**: Sum of all header field bit widths
5. **Extract frame length info**: Bit offset, bit width, and endianness of the length field for efficient partial-header parsing

### Entry-point Analysis (v1)

For each message marked with `role="entry-point"`:

1. **Discover leaf types**: Walk the message tree through choices and arrays to find all terminal (leaf) types reachable from the entry-point. Each leaf gets:
   - A unique `type_id` (FNV-1a hash of the type name)
   - An access path (sequence of choice/array/struct navigation steps from entry-point to leaf)
   - Direction constraints (send-only, receive-only, or both)
   - Auto-increment field info
   - Constraint field info (for `wrap()` overloads)

2. **Find sync pattern**: Scan the entry-point's leading fixed fields for constraint-equals values that form a byte sync pattern.

3. **Compute min frame header size**: Sum the bit widths of leading fixed fields until the first variable-length or optional element.

4. **Compute frame length expression**: Look for a field whose name contains "length" or "size" in the leading fixed fields, recording its bit offset and width for efficient partial-header parsing.

5. **Collect context fields**: Gather all concrete (non-optional, non-variable, non-choice) fields from the entry-point for use as decode context in inner types.

6. **Check type_id collisions**: Verify that no two leaf types within the same session hash to the same `type_id`. Sessions with collisions are skipped with an error. Collision detection is per-session -- the same type appearing as a leaf in multiple sessions is not an error.

## Stage 6: Generate Code

**Input:** `Protocol` AST + `TypeIndex` + `WireSizeInfo` + `vector<SessionInfo>` + namespace string
**Output:** 7 C++ header files in the output directory

1. Create the output directory (if it doesn't exist)
2. Derive the namespace from `--namespace`, `<defaults><namespace>`, or protocol name (hyphens to underscores)
3. Generate and write each file:
   - `constants.hpp` -- Named constants
   - `types.hpp` -- Type wrappers
   - `structs.hpp` -- Struct classes (includes context structs)
   - `messages.hpp` -- Message classes. For v2: includes Frame class with `PayloadVariant`, `wrap()`, encode/decode with length backpatch. For v1: includes `wrap()` overloads on entry-point messages.
   - `sessions.hpp` -- Session classes implementing `ISession`. For v2: frame-based dispatch using message ID switch. For v1: trie-based access path dispatch.
   - `protocol.hpp` -- `ProtocolDescriptor` with type registry
   - `<protocol-name>.hpp` -- Umbrella header

If any file write fails, bgen reports the error and exits with code 3.

See [Generated File Structure](output-files.md) for details on each output file.
