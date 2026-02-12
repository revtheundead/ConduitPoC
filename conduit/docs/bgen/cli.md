# Command-Line Usage

[Back to index](index.md)

## Synopsis

```
bgen --input <root.bmdl.xml> --output <dir> [--namespace <ns>] [options]
```

## Arguments

| Argument | Required | Description |
|----------|----------|-------------|
| `--input <path>` | Yes | Path to the root BMDL XML file |
| `--output <dir>` | Yes* | Output directory for generated code |
| `--namespace <ns>` | No | Override the C++ namespace (default: protocol name) |
| `--verbose` | No | Show informational and diagnostic output |
| `--validate-only` | No | Parse and validate without generating code |
| `--dump-ast` | No | Parse, resolve, validate, then print AST and exit |
| `--version` | No | Print bgen version and exit |
| `--help`, `-h` | No | Print usage help and exit |

*`--output` is not required when using `--validate-only` or `--dump-ast`.

## Namespace Rules

The `--namespace` argument overrides the default namespace, which is derived from the protocol name. Namespace rules:

- Must be valid C++ identifier(s), optionally separated by `::`
- Hyphens are converted to underscores automatically
- Each segment must start with a letter or underscore, followed by letters, digits, or underscores
- Cannot start or end with `::`
- Cannot contain empty segments (`::::`)

Examples:

```
--namespace myproto           # namespace myproto { ... }
--namespace my::proto         # namespace my::proto { ... }
--namespace my-protocol       # namespace my_protocol { ... }  (hyphen → underscore)
```

When `--namespace` is not specified, the namespace is derived using a priority chain:

1. **`<defaults><namespace>`** -- if the BMDL file defines `<defaults><namespace>my_ns</namespace></defaults>`, that value is used
2. **Protocol `name` attribute** -- otherwise, the protocol's `name` attribute is used with hyphens converted to underscores

```xml
<!-- Priority 1: namespace from <defaults> -->
<defaults><namespace>my_protocol</namespace></defaults>
<!-- generates: namespace my_protocol { ... } -->

<!-- Priority 2: protocol name fallback -->
<protocol name="my-protocol" ...>
<!-- generates: namespace my_protocol { ... } -->
```

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
file.bmdl.xml:offset(42): error: unknown type reference 'foo'
```

The format is `<file>:offset(<byte-offset>): error: <message>`.

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
bgen: protocol 'my-protocol' v1.0
bgen: 12 types, 5 structs, 8 messages, 2 constants
bgen: resolving types...
bgen: validating...
bgen: computing wire sizes...
bgen: analyzing sessions...
bgen: entry-point 'Frame' -> 6 leaf types
bgen: generating code in namespace 'my_protocol'...
bgen: generated 7 files in output/
```

## Examples

Generate code from a protocol:

```bash
bgen --input protocols/asterix.bmdl.xml --output generated/asterix/
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
