# String Encoding

[Back to index](index.md)

Conduit provides conversion functions for the string encodings used in binary protocols. Generated code calls these automatically during encode/decode -- direct use is rarely needed.

```cpp
#include <conduit/string/encoding.hpp>
```

## Encoding Enum

```cpp
namespace conduit::string {

enum class Encoding { ASCII, IA5, EBCDIC, UTF8 };

}
```

## Conversion Functions

### to_ascii

```cpp
std::string to_ascii(std::string_view input, Encoding from);
```

Converts a wire-encoded string to ASCII.

### from_ascii

```cpp
std::string from_ascii(std::string_view input, Encoding to);
```

Converts an ASCII string to wire encoding for transmission.

## Encoding Behaviors

| Encoding | to_ascii | from_ascii |
|----------|----------|------------|
| `ASCII` | Passthrough (copy) | Passthrough (copy) |
| `UTF8` | Passthrough (copy) | Passthrough (copy) |
| `IA5` | Mask high bit: `c & 0x7F` per byte | Mask high bit: `c & 0x7F` per byte |
| `EBCDIC` | Code Page 037 lookup table | Code Page 037 reverse lookup table |

IA5 (International Alphabet No. 5) is the ITU-T 7-bit character set, functionally identical to ASCII in the printable range. The conversion masks the high bit to handle 8-bit data that may have the MSB set.

EBCDIC conversion uses the IBM Code Page 037 (Latin-1) mapping tables. Both directions use 256-entry lookup tables for O(1) per-character conversion.

## Generated Code Integration

When a BMDL field specifies `encoding="ia5"` or `encoding="ebcdic"`, the generated decode method calls `to_ascii()` after reading the raw bytes, and the generated encode method calls `from_ascii()` before writing. You do not need to call these functions manually for generated types.

## Example

```cpp
using conduit::string::Encoding;
using conduit::string::to_ascii;
using conduit::string::from_ascii;

// EBCDIC → ASCII
std::string wire_data = /* bytes from wire */;
std::string text = to_ascii(wire_data, Encoding::EBCDIC);

// ASCII → EBCDIC for transmission
std::string encoded = from_ascii("HELLO", Encoding::EBCDIC);
```

## See Also

- [Bit I/O](bit-io.md) -- `BitReader::read_string()` and `BitWriter::write_string()` handle raw bytes; encoding conversion is applied separately
- [Sessions & Generated Code](sessions-and-codegen.md) -- Generated encode/decode methods call these functions automatically
