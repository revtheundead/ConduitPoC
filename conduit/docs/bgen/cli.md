# Command-Line Usage

[Back to index](index.md)

## Synopsis

```
bgen --input <root.bmdl.xml> --output <dir> [--language <lang>] [--namespace <ns>] [options]
```

## Arguments

| Argument | Required | Description |
|----------|----------|-------------|
| `--input <path>` | Yes | Path to the root BMDL XML file |
| `--output <dir>` | Yes* | Output directory for generated code |
| `--language <lang>` | No | Target language: `cpp` (default), `java`, `python` |
| `--namespace <ns>` | No | Override the namespace/package (default: protocol name) |
| `--verbose` | No | Show informational and diagnostic output |
| `--validate-only` | No | Parse and validate without generating code |
| `--dump-ast` | No | Parse, resolve, validate, then print AST and exit |
| `--version` | No | Print bgen version and exit |
| `--help`, `-h` | No | Print usage help and exit |

*`--output` is not required when using `--validate-only` or `--dump-ast`.

## Language Selection

The `--language` argument selects the code generation backend. If omitted, C++ is used.

| Language | Value | Output |
|----------|-------|--------|
| C++ | `cpp` (default) | 8 `.hpp` header files in the output directory: `constants.hpp`, `types.hpp`, `structs.hpp`, `messages.hpp`, `sessions.hpp`, `protocol.hpp`, `json.hpp`, and an umbrella `<protocol>.hpp` |
| Java | `java` | `.java` source files in a package subdirectory. The package is the namespace with `.` as path separator: `--output src/gen` with package `io.conduit.asterix` produces `src/gen/io/conduit/asterix/*.java`. |
| Python | `python` | `.py` module files in the output directory: `bit_io.py`, `types.py`, `structs.py`, `messages.py`, `sessions.py`, `protocol.py`, and `__init__.py` |

All backends produce wire-compatible serialization from the same BMDL schema.

## Namespace Rules

The `--namespace` argument overrides the default namespace, which is derived from the protocol name. The interpretation depends on the target language:

- **C++:** Used as the C++ namespace. May contain `::` separators (e.g., `my::proto`).
- **Java:** Used as the Java package name (e.g., `com.example.myprotocol`).
- **Python:** Used as the Python module path.

General rules:

- Hyphens are converted to underscores automatically
- Each segment must start with a letter or underscore, followed by letters, digits, or underscores

C++-specific:

- May be separated by `::`
- Cannot start or end with `::`
- Cannot contain empty segments (`::::`)

Examples:

```
--namespace myproto           # C++: namespace myproto { ... }
--namespace my::proto         # C++: namespace my::proto { ... }
--namespace my-protocol       # C++: namespace my_protocol { ... }  (hyphen -> underscore)
```

When `--namespace` is not specified, the namespace is derived from `<defaults><namespace>`:

```xml
<defaults><namespace>my_protocol</namespace></defaults>
<!-- C++: namespace my_protocol { ... } -->
<!-- Java: package my_protocol; -->
<!-- Python: my_protocol/ module directory -->
```

Hyphens in namespace values are automatically converted to underscores.

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (code generated, or validation passed, or `--help`/`--version`) |
| 1 | Parse, resolve, or validation error |
| 2 | Cannot create output directory (filesystem error) |
| 3 | I/O error writing output files |

## Error and Warning Output

All diagnostic output is written to stderr.

### Structured Errors (Parse/Resolve/Validate)

Errors from the parse, resolve, and validate stages include source location information:

```
file.bmdl.xml:12:5: error: unknown type reference 'foo'
```

The format is `<file>:<line>:<column>: error: <message>`. If line information is unavailable (e.g., for errors detected outside of XML parsing), the fallback format `<file>:offset(<byte-offset>)` is used.

### Logger Messages

The bgen logger uses the `bgen:` prefix. Errors and warnings are **always** shown regardless of verbose mode:

```
bgen: error: cannot create output directory 'bad/path': permission denied
bgen: warning: file.bmdl.xml:offset(100): deprecated feature used
```

Parse warnings from the BMDL XML are always reported:

```
bgen: warning: file.bmdl.xml:offset(100): deprecated feature used
```

### Verbose Mode

With `--verbose`, bgen additionally reports progress through each pipeline stage (these are suppressed by default):

```
bgen: parsing my-protocol.bmdl.xml
bgen: protocol 'my-protocol' v2.0
bgen: 12 types, 5 structs, 8 messages, 2 constants
bgen: resolving types...
bgen: validating...
bgen: computing wire sizes...
bgen: analyzing sessions...
bgen: session 'MyFrame' -> 6 leaf types
bgen: generating code in namespace 'my_protocol'...
bgen: generated 8 files in output/
```

## Examples

Generate C++ code from a protocol (default):

```bash
bgen --input protocols/asterix.bmdl.xml --output generated/asterix/
```

Generate Java code:

```bash
bgen --input protocols/asterix.bmdl.xml --output generated/asterix/ --language java
```

Generate Python code:

```bash
bgen --input protocols/asterix.bmdl.xml --output generated/asterix/ --language python
```

Validate without generating:

```bash
bgen --input protocols/asterix.bmdl.xml --validate-only
```

Generate with a custom namespace:

```bash
bgen --input proto.bmdl.xml --output gen/ --namespace my::custom::ns
```

Verbose output for debugging:

```bash
bgen --input proto.bmdl.xml --output gen/ --verbose
```

Dump the resolved AST:

```bash
bgen --input proto.bmdl.xml --dump-ast
```
