# Bit I/O

[Back to index](index.md)

`BitReader` and `BitWriter` provide bit-level and byte-level encode/decode operations. They are the foundation for all generated struct/message `encode()` and `decode()` methods.

```cpp
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/io/endian.hpp>
```

## Endian Enum

```cpp
namespace conduit::io {
enum class Endian { Big, Little };
}
```

All multi-byte read/write functions default to `Endian::Big`. The protocol's `<defaults><endian>` setting controls which endianness generated code passes.

## BitReader

### Construction

```cpp
std::vector<uint8_t> data = {0xBE, 0xEF, 0x00, 0x42};
conduit::io::BitReader reader(std::span<const uint8_t>(data));
```

The reader does not copy the data -- it references the provided span. The data must remain valid for the reader's lifetime.

### Bit-Level Reading

| Method | Returns | Description |
|--------|---------|-------------|
| `read_bits(size_t count)` | `Result<uint64_t>` | Read `count` bits as unsigned (1--64) |
| `read_signed_bits(size_t count)` | `Result<int64_t>` | Read `count` bits as two's complement signed |

```cpp
auto flags = reader.read_bits(3);     // 3-bit unsigned field
auto offset = reader.read_signed_bits(13);  // 13-bit signed field
```

### Byte-Level Reading

| Method | Returns | Default Endian | Description |
|--------|---------|----------------|-------------|
| `read_u8()` | `Result<uint8_t>` | -- | Read 1 byte |
| `read_u16(Endian)` | `Result<uint16_t>` | Big | Read 2 bytes |
| `read_u32(Endian)` | `Result<uint32_t>` | Big | Read 4 bytes |
| `read_u64(Endian)` | `Result<uint64_t>` | Big | Read 8 bytes |
| `read_f16(Endian)` | `Result<float>` | Big | Read IEEE 754 half-precision float (2 bytes) |
| `read_f32(Endian)` | `Result<float>` | Big | Read IEEE 754 single-precision float (4 bytes) |
| `read_f64(Endian)` | `Result<double>` | Big | Read IEEE 754 double-precision float (8 bytes) |

> **Pitfall -- auto-alignment:** Byte-level reads, bulk reads (`read_bytes`, `read_string`), and `sub_reader()` all auto-align to the next byte boundary before reading. If you read a 3-bit field followed by `read_u16()`, the remaining 5 bits of the current byte are skipped. Generated code tracks alignment and falls back to `read_bits()` when not byte-aligned.

### Bulk Reading

| Method | Returns | Description |
|--------|---------|-------------|
| `read_bytes(size_t count)` | `Result<span<const uint8_t>>` | Read `count` bytes as a span into the underlying data |
| `read_string(size_t byte_length)` | `Result<std::string>` | Read `byte_length` bytes as a string |

### Aviation Wire Encodings

| Method | Returns | Description |
|--------|---------|-------------|
| `read_bcd(size_t bits)` | `Result<uint64_t>` | Unsigned BCD (bits must be multiple of 4) |
| `read_bcd_signed(size_t bits)` | `Result<int64_t>` | Signed BCD (MSB = sign, rest = BCD nibbles) |
| `read_sign_magnitude(size_t bits)` | `Result<int64_t>` | BNR_S: MSB = sign, rest = magnitude |

### Sub-Reader

```cpp
auto sub = reader.sub_reader(10);  // Result<BitReader>, bounded to next 10 bytes
```

Returns `Result<BitReader>` -- a new reader covering the next `byte_count` bytes, or an error if insufficient data remains. On success, the parent reader is advanced past the sub-reader's range. Useful for length-delimited fields.

> **Pitfall:** `sub_reader()` advances the parent. After creating a sub-reader, the parent's position is past the sub-reader's range.

### Position and State

| Method | Returns | Description |
|--------|---------|-------------|
| `remaining_bytes()` | `size_t` | Bytes remaining (floor, ignoring sub-byte offset) |
| `remaining_bits()` | `size_t` | Total bits remaining |
| `bit_position()` | `size_t` | Current absolute bit position |
| `at_end()` | `bool` | True if no bits remain |
| `is_byte_aligned()` | `bool` | True if bit offset within current byte is 0 |
| `skip_bits(size_t)` | `VoidResult` | Advance position by N bits |
| `align_to_byte()` | `void` | Skip to next byte boundary (no-op if aligned) |
| `align_to(size_t)` | `void` | Align to N-byte boundary |
| `reset()` | `void` | Reset position to start |
| `underlying_data()` | `span<const uint8_t>` | Access the underlying data span |

## BitWriter

### Construction

```cpp
conduit::io::BitWriter writer(256);  // initial capacity 256 bytes
conduit::io::BitWriter writer;       // default 256 bytes
```

The writer manages its own dynamically-growing buffer.

### Bit-Level Writing

| Method | Parameters | Description |
|--------|------------|-------------|
| `write_bits(uint64_t, size_t)` | value, count | Write `count` bits from `value` (0--64; 0 is a no-op) |
| `write_signed_bits(int64_t, size_t)` | value, count | Write `count` bits as two's complement |

### Byte-Level Writing

| Method | Default Endian | Description |
|--------|----------------|-------------|
| `write_u8(uint8_t)` | -- | Write 1 byte |
| `write_u16(uint16_t, Endian)` | Big | Write 2 bytes |
| `write_u32(uint32_t, Endian)` | Big | Write 4 bytes |
| `write_u64(uint64_t, Endian)` | Big | Write 8 bytes |
| `write_f16(float, Endian)` | Big | Write IEEE 754 half-precision float (2 bytes) |
| `write_f32(float, Endian)` | Big | Write IEEE 754 single-precision float (4 bytes) |
| `write_f64(double, Endian)` | Big | Write IEEE 754 double-precision float (8 bytes) |

> **Auto-alignment:** All byte-level writes, `write_bytes()`, and `write_string()` auto-align to the next byte boundary before writing. If you write a 3-bit field followed by `write_u16()`, the remaining 5 bits are zero-padded and a new byte starts.

### Bulk Writing

| Method | Description |
|--------|-------------|
| `write_bytes(span<const uint8_t>)` | Write raw bytes |
| `write_string(string_view, size_t, char)` | Write string padded to `padded_length` with `padding` char (default `\0`). Returns `false` if truncated. |

> **Pitfall:** `write_string` silently truncates strings longer than `padded_length`. It returns `false` when this happens but does not produce an error.

### Aviation Wire Encodings

| Method | Description |
|--------|-------------|
| `write_bcd(uint64_t, size_t)` | Write unsigned BCD |
| `write_bcd_signed(int64_t, size_t)` | Write signed BCD |
| `write_sign_magnitude(int64_t, size_t)` | Write BNR_S sign-magnitude |

### Alignment and Patching

| Method | Description |
|--------|-------------|
| `align_to_byte()` | Pad remaining bits in current byte with zeros (no-op if already aligned) |
| `align_to(size_t n)` | Align to N-byte boundary with zero padding. For example, `align_to(4)` advances to the next 4-byte boundary. |
| `patch_u8(size_t, uint8_t)` | Overwrite a byte at `byte_offset` |
| `patch_u16(size_t, uint16_t, Endian)` | Overwrite 2 bytes at `byte_offset` |
| `patch_u32(size_t, uint32_t, Endian)` | Overwrite 4 bytes at `byte_offset` |

Patch methods overwrite previously written data at the given **byte offset** without advancing the write position. They return `[[nodiscard]] bool` -- `false` if the offset is out of range. On failure, they also set the writer's internal error state (`ErrorCode::BufferOverrun`), which causes a subsequent `finish()` to return an error. These are used by generated frame encode logic to backpatch length fields after the full payload is written.

### Output

```cpp
auto result = writer.finish();  // Result<std::vector<uint8_t>>
if (result) {
    auto& bytes = *result;
    // bytes contains the encoded data
}
```

`finish()` returns an error if the writer has accumulated an error state.

| Method | Returns | Description |
|--------|---------|-------------|
| `finish()` | `Result<vector<uint8_t>>` | Extract the written bytes |
| `size_bytes()` | `size_t` | Current size in bytes (rounded up) |
| `size_bits()` | `size_t` | Current size in bits |
| `is_byte_aligned()` | `bool` | True if bit position is on a byte boundary |
| `clear()` | `void` | Reset writer to empty state |

### Error State

The writer accumulates errors internally rather than returning `Result` from each write. This means you can chain multiple write calls without checking each one -- if any write fails, the error is captured and all subsequent writes become no-ops.

```cpp
writer.has_error();   // true if any write operation failed
writer.error();       // const Error& — the accumulated error (asserts if no error)
writer.clear_error(); // reset error state
```

`finish()` checks this state: if an error is set, it returns the error, clears the writer's data and error state, and resets the bit position. If no error, it aligns to byte boundary and returns the written bytes.

> **Pitfall:** Only call `error()` after checking `has_error()` -- it asserts that an error exists and will abort if called without one.

## Endian Utilities

The `conduit::io` namespace also provides standalone byte-order detection, byte-swap, and buffer I/O functions.

### Byte Order Detection

```cpp
constexpr bool conduit::io::is_little_endian() noexcept;
constexpr bool conduit::io::is_big_endian() noexcept;
```

### Byte Swap

```cpp
constexpr uint16_t byte_swap(uint16_t value) noexcept;
constexpr uint32_t byte_swap(uint32_t value) noexcept;
constexpr uint64_t byte_swap(uint64_t value) noexcept;
```

### Buffer Reads

Read multi-byte values directly from a byte buffer without constructing a `BitReader`:

| Function | Returns | Description |
|----------|---------|-------------|
| `read_u16(span, offset, Endian)` | `uint16_t` | Read 2 bytes |
| `read_u32(span, offset, Endian)` | `uint32_t` | Read 4 bytes |
| `read_u64(span, offset, Endian)` | `uint64_t` | Read 8 bytes |
| `read_f16(span, offset, Endian)` | `float` | Read IEEE 754 half-precision float (via `memcpy`) |
| `read_f32(span, offset, Endian)` | `float` | Read IEEE 754 single-precision float (via `memcpy`) |
| `read_f64(span, offset, Endian)` | `double` | Read IEEE 754 double-precision float (via `memcpy`) |

**Bounds checking:** If `offset + N` exceeds the span size, integer reads return 0 and float reads return 0.0. No error is signaled -- the caller is responsible for ensuring the offset is valid.

### Buffer Writes

Write multi-byte values directly to a byte buffer without constructing a `BitWriter`:

| Function | Description |
|----------|-------------|
| `write_u16(span, offset, uint16_t, Endian)` | Write 2 bytes |
| `write_u32(span, offset, uint32_t, Endian)` | Write 4 bytes |
| `write_u64(span, offset, uint64_t, Endian)` | Write 8 bytes |
| `write_f16(span, offset, float, Endian)` | Write IEEE 754 half-precision float (via `memcpy`) |
| `write_f32(span, offset, float, Endian)` | Write IEEE 754 single-precision float (via `memcpy`) |
| `write_f64(span, offset, double, Endian)` | Write IEEE 754 double-precision float (via `memcpy`) |

**Bounds checking:** If `offset + N` exceeds the span size, writes are silently skipped (no-op). No error is signaled -- the caller is responsible for ensuring the offset is valid.

### Example

```cpp
uint16_t swapped = conduit::io::byte_swap(uint16_t{0xBEEF});  // 0xEFBE

uint16_t val = conduit::io::read_u16(data, offset, Endian::Big);
conduit::io::write_u32(buffer, offset, 0xDEADBEEF, Endian::Big);

float f = conduit::io::read_f32(data, offset, Endian::Big);
conduit::io::write_f64(buffer, offset, 3.14, Endian::Big);
```

## See Also

- [Error Handling](error-handling.md) -- `Result<T>` and `VoidResult` returned by read/write operations
- [Sessions & Generated Code](sessions-and-codegen.md) -- Generated `encode()`/`decode()` methods use `BitReader`/`BitWriter` internally
- [String Encoding](string-encoding.md) -- Wire-encoding conversion (IA5, EBCDIC) applied after `read_string()`/before `write_string()`
