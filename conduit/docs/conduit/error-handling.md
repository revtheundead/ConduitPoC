# Error Handling

[Back to index](index.md)

Conduit uses `std::expected`-based error handling throughout. Every operation that can fail returns `Result<T>` or `VoidResult`, carrying a rich `Error` object on failure.

## ErrorCode

All error codes are grouped into ranges by category:

### Decode Errors (100--199)

| Code | Name | Description |
|------|------|-------------|
| 100 | `BufferUnderrun` | Not enough data to read the requested value |
| 101 | `InvalidFieldValue` | Field value is invalid for its type |
| 102 | `ConstraintViolation` | Decoded value violates a constraint |
| 103 | `UnknownEnumValue` | Decoded integer has no matching enum variant |
| 104 | `UnknownDiscriminator` | Choice discriminator has no matching case |
| 105 | `ExactConsumptionFailed` | Decode did not consume exactly the expected number of bytes |
| 106 | `NegativeLength` | A length expression evaluated to a negative number |
| 107 | `DecodingFailed` | General decode failure |
| 108 | `UnknownTypeId` | Session received a type ID it does not recognize |
| 109 | `MaxLengthExceeded` | Field exceeds its declared max-length |
| 110 | `StringEncodingError` | String encoding conversion failed |
| 111 | `ConstraintViolationDeferred` | Deferred constraint check failed |

### Encode Errors (200--299)

| Code | Name | Description |
|------|------|-------------|
| 200 | `EncodeConstraintViolation` | Value violates a constraint during encode |
| 201 | `MissingRequiredField` | A required field was not set before encode |
| 202 | `EncodingFailed` | General encode failure |
| 203 | `BufferOverrun` | Write exceeded buffer capacity |
| 204 | `StringTooLong` | String exceeds the field's max-length |
| 205 | `DirectionViolation` | Attempted to send a receive-only message type |

### Connection Errors (300--399)

| Code | Name | Description |
|------|------|-------------|
| 300 | `ConnectionRefused` | Remote host refused the connection |
| 301 | `ConnectionTimeout` | Connection attempt timed out |
| 302 | `ConnectionReset` | Connection was reset by peer |
| 303 | `ConnectionClosed` | Connection was closed gracefully |
| 304 | `HostNotFound` | DNS resolution failed |
| 305 | `NetworkUnreachable` | No route to host |
| 306 | `SocketError` | Low-level socket error |

### Transceiver Errors (400--499)

| Code | Name | Description |
|------|------|-------------|
| 400 | `NotRunning` | Operation requires a running transceiver |
| 401 | `AlreadyRunning` | `start()` called on an already-running transceiver |
| 402 | `PeerNotFound` | The specified PeerId does not exist |
| 403 | `QueueFull` | Dispatch queue is full and drop policy rejected the message |
| 404 | `InvalidConfig` | Configuration validation failed |
| 405 | `MultiplePeers` | `sole_peer()` called but multiple peers exist |
| 406 | `UnsupportedMessageType` | Session cannot encode the given type ID |
| 407 | `BatchNotSupported` | Session does not support batch encoding (non-array payload) |

### Internal Errors (900--999)

| Code | Name | Description |
|------|------|-------------|
| 900 | `InternalError` | Bug in conduit itself |
| 901 | `NotImplemented` | Feature not yet implemented |
| 902 | `InvalidArgument` | Invalid argument passed to a function |
| 903 | `InvalidState` | Object is in an invalid state for the operation |
| 904 | `Timeout` | Operation timed out |

## ErrorSeverity

```cpp
enum class ErrorSeverity { Debug, Info, Warning, Error, Critical, Fatal };
```

Each `Error` derives its severity from its code. Decode/encode errors (100--299) are `Warning` severity; connection/transceiver errors (300--499) are `Error`; internal errors (900+) are `Critical`. Exception: `Timeout` (904) is `Error` severity despite being in the 900 range, since timeouts are recoverable operational errors rather than internal failures.

## Error Class

```cpp
#include <conduit/core/error.hpp>

conduit::Error err(conduit::ErrorCode::BufferUnderrun,
                   "need 4 bytes, have 2");

err.code();          // ErrorCode::BufferUnderrun
err.message();       // "need 4 bytes, have 2"
err.location();      // std::source_location of construction
err.timestamp();     // set at construction time
err.context();       // "" (empty unless set)
err.has_context();   // false

// Add context (returns a copy)
auto err2 = err.with_context("while decoding Heartbeat.sequence");
err2.context();      // "while decoding Heartbeat.sequence"

// Category checks
err.is_decode_error();      // true (code 100-199)
err.is_encode_error();      // false
err.is_connection_error();  // false
err.is_transceiver_error(); // false
err.is_recoverable();       // depends on code

// Formatting
err.format();        // "[ERROR] BufferUnderrun: need 4 bytes, have 2 (file.cpp:42)"
err.format_short();  // "BufferUnderrun: need 4 bytes, have 2"

// Static helpers
Error::code_to_string(ErrorCode::BufferUnderrun);     // "BUFFER_UNDERRUN"
Error::severity_to_string(ErrorSeverity::Error);       // "ERROR"

// Comparison (by error code only — ignores message, context, location, timestamp)
Error a(ErrorCode::BufferUnderrun, "msg A");
Error b(ErrorCode::BufferUnderrun, "msg B");
a == b;  // true (same code)
```

### Default Constructor

`Error()` is default-constructible. A default-constructed error has code `ErrorCode::InternalError`, an empty message, no context, and an epoch timestamp (since no `std::chrono::system_clock::now()` call is made).

## Result Types

```cpp
template<typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;
```

Usage:

```cpp
Result<uint16_t> read_header(BitReader& r) {
    auto val = r.read_u16();
    if (!val) return std::unexpected(val.error());
    return *val;
}

// Check and use
auto result = read_header(reader);
if (result) {
    uint16_t header = *result;
} else {
    log_error(result.error().format());
}
```

## Convenience Macros

### CONDUIT_ERROR

Creates an `Error` with the current source location:

```cpp
return std::unexpected(CONDUIT_ERROR(ErrorCode::InvalidArgument, "port must be > 0"));
```

### CONDUIT_TRY

Early return on error, discarding the success value. Works with both `Result<T>` and `VoidResult`:

```cpp
VoidResult process(BitReader& r) {
    CONDUIT_TRY(r.skip_bits(8));         // skip header
    CONDUIT_TRY(read_payload(r));        // propagate error
    return {};
}
```

### CONDUIT_TRY_ASSIGN

Early return on error, assigning the unwrapped value on success:

```cpp
Result<Frame> decode_frame(BitReader& r) {
    CONDUIT_TRY_ASSIGN(uint16_t, sync, r.read_u16());
    CONDUIT_TRY_ASSIGN(uint8_t, length, r.read_u8());
    // sync and length are now usable as local variables
    ...
}
```

> **Pitfall:** `CONDUIT_TRY_ASSIGN` must be used inside a braced block, not directly inside a bare `if`/`else`/`for`.

### CONDUIT_TRY_CONTEXT

Same as `CONDUIT_TRY` but attaches a context string to the error:

```cpp
CONDUIT_TRY_CONTEXT(msg.encode(writer), "encoding Heartbeat for peer 5");
```

### CONDUIT_ENSURE

Validates a condition, returning an error if false:

```cpp
CONDUIT_ENSURE(length > 0, ErrorCode::NegativeLength, "length must be positive");
```

## Patterns

### Propagating errors through call chains

```cpp
Result<Message> parse(std::span<const uint8_t> data) {
    BitReader r(data);
    CONDUIT_TRY_ASSIGN(uint16_t, sync, r.read_u16());
    CONDUIT_ENSURE(sync == 0xBEEF, ErrorCode::InvalidFieldValue, "bad sync");
    CONDUIT_TRY_ASSIGN(Message, msg, Message::decode(r));
    return msg;
}
```

### Handling errors at the boundary

```cpp
auto result = parse(raw_data);
if (!result) {
    auto& err = result.error();
    if (err.is_decode_error()) {
        stats.decode_errors++;
        logger.warn(err.format());
    } else {
        logger.error(err.format());
    }
}
```

## See Also

- [Bit I/O](bit-io.md) -- `BitReader` and `BitWriter` methods return `Result<T>`
- [Transceiver](transceiver.md) -- `start()`, `send()`, and `add_peer()` return `VoidResult` or `Result<PeerId>`
- [Transports](transports.md) -- Connection error codes (300--399) originate from transports
- [Logging](logging.md) -- `ErrorSeverity` is separate from the logging `Level` enum
