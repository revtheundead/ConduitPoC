# Sessions & Generated Code

[Back to index](index.md)

Sessions bridge generated protocol code and the conduit runtime. bgen produces session classes that implement `ISession`, enabling the `Transceiver` to decode incoming frames and encode outgoing messages without knowing the protocol details.

> **Multi-language support:** bgen generates session classes in all three backends (C++, Java, Python). The C++ `ISession` interface documented below is the reference implementation. Java and Python sessions provide equivalent functionality through language-idiomatic APIs — see [Session Code Generation](../bgen/generated-sessions.md) for the per-backend details and [Limitations & Known Issues](limitations.md) for current feature gaps.

## ISession Interface

```cpp
#include <conduit/traits/session_traits.hpp>

namespace conduit::traits {

class ISession {
public:
    virtual ~ISession() = default;

    // Decode a raw frame into one or more typed messages
    [[nodiscard]] virtual Result<std::vector<DecodedMessage>>
        decode_frame(std::span<const uint8_t> data) = 0;

    // Encode a typed message (by type_id) into wire bytes + metadata
    [[nodiscard]] virtual Result<EncodeResult>
        encode_wrap(uint64_t type_id, const std::any& payload) = 0;

    // Stream framing metadata
    [[nodiscard]] virtual std::span<const uint8_t> sync_pattern() const = 0;
    [[nodiscard]] virtual size_t min_frame_header_size() const = 0;
    [[nodiscard]] virtual size_t extract_frame_length(
        std::span<const uint8_t> header) const = 0;

    // Type introspection
    [[nodiscard]] virtual std::span<const uint64_t> leaf_type_ids() const = 0;
    [[nodiscard]] virtual std::string_view type_name(uint64_t type_id) const = 0;

    // Direction introspection (for transceiver-level send blocking)
    [[nodiscard]] virtual bool is_receive_only(uint64_t /*type_id*/) const { return false; }

    virtual void reset() = 0;

    // Message logging support (optional)
    [[nodiscard]] virtual std::string format_message(uint64_t type_id,
                                                      const std::any& payload) const { return {}; }
    [[nodiscard]] virtual std::string format_outbound(
        uint64_t type_id, const std::any& payload,
        std::span<const std::pair<std::string, std::string>> auto_fields) const {
        return format_message(type_id, payload);
    }
    [[nodiscard]] virtual std::string_view protocol_name() const { return "unknown"; }
};

}
```

Users do not implement `ISession` -- bgen generates implementations. You only interact with sessions through factory functions and the `Transceiver`.

`ISession` also provides `encode_batch()` for array-payload protocols:

```cpp
// Default implementation returns BatchNotSupported.
// Array-payload sessions override this to pack multiple messages into one frame.
[[nodiscard]] virtual Result<EncodeResult>
    encode_batch(uint64_t type_id, std::span<const std::any> payloads);
```

## DecodedMessage

When a frame is decoded, the session returns one or more `DecodedMessage` values:

```cpp
struct DecodedMessage {
    uint64_t type_id;              // Compile-time FNV-1a hash (matches T::TYPE_ID)
    std::string_view type_name;    // e.g., "Cat048Record"
    std::any payload;              // The typed message object
    std::vector<uint8_t> raw;      // Copy of raw wire bytes (entire frame)
};
```

The `raw` field contains a copy of the entire frame byte buffer passed to `decode_frame()`. For array-payload frames with multiple messages, each `DecodedMessage` receives the same frame bytes. This is useful for forwarding/relay scenarios where the original wire bytes must be preserved.

The `Transceiver` unwraps the `std::any` payload using `std::any_cast<const T&>` when dispatching to typed handlers. You rarely interact with `DecodedMessage` directly.

## EncodeResult

`encode_wrap()` and `encode_batch()` return an `EncodeResult` containing both the encoded bytes and metadata about auto-managed fields:

```cpp
struct EncodeResult {
    std::vector<uint8_t> bytes;                               // Encoded frame bytes
    std::vector<std::pair<std::string, std::string>> auto_fields; // name-value pairs
};
```

- **`bytes`**: The fully encoded frame ready for transmission.
- **`auto_fields`**: Name-value pairs of auto-managed fields set during encoding (id, length, sequence counter, timestamp). Used by the transceiver for outbound message logging via `ISession::format_outbound()`.

## Codec Concepts

```cpp
#include <conduit/traits/codec_traits.hpp>

namespace conduit::traits {

template<typename T>
concept Encodable = requires(const T& t, io::BitWriter& w) {
    { t.encode(w) } -> std::same_as<VoidResult>;
};

template<typename T>
concept Decodable = requires(io::BitReader& r) {
    { T::decode(r) } -> std::same_as<Result<T>>;
};

template<typename T>
concept Message = Encodable<T> && Decodable<T> && requires {
    { T::TYPE_ID } -> std::convertible_to<uint64_t>;
    { T::TYPE_NAME } -> std::convertible_to<std::string_view>;
};

}
```

The `Message` concept constrains the template parameter on `Transceiver::send<T>()` and `Transceiver::on<T>()` -- only generated message/leaf types satisfy it.

## Session Factory Pattern

bgen generates a factory function for each session in the protocol. Factory functions exist in all three backends:

| Backend | Factory Pattern | Returns |
|---------|----------------|---------|
| C++ | `create_<lower_snake_case(frame)>_session()` | `std::unique_ptr<ISession>` |
| Java | `new <Frame>Session()` | `<Frame>Session` instance |
| Python | `<Frame>Session()` | `<Frame>Session` instance |

When a `<frame>` is present, the C++ factory is named from the frame:

```cpp
// In generated sessions.hpp
std::unique_ptr<conduit::traits::ISession>
    create_<lower_snake_case(frame_name)>_session();
```

For example, a frame named `MyFrame` in the `my_protocol` namespace produces:

```cpp
std::unique_ptr<conduit::traits::ISession>
    my_protocol::create_my_frame_session();
```

If the frame has `auto="config(key)"` fields, the factory requires a `Config` parameter:

```cpp
MyFrameSession::Config config;
config.system_id = 42;
auto session = my_protocol::create_my_frame_session(config);
```

## Wiring to the Transceiver

Pass the factory to `TransceiverConfig::add_peer()`:

```cpp
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

TransceiverConfig config;
config.add_peer("my-peer",
                my_protocol::create_my_frame_session,
                UdpConfig{.bind_port = 5000});

Transceiver xcvr(std::move(config));
```

For multi-peer transports (TCP server), each connecting client gets its own session instance created by the factory.

## Direct Encode/Decode (No Transceiver)

Use the Frame class directly for frame-level encode/decode:

```cpp
// Wrap and encode a message
my_protocol::Heartbeat hb;
hb.set_sequence(42);
auto frame = my_protocol::MyFrame::wrap(hb);  // auto-sets msg_type and length
auto bytes = frame.encode_bytes();             // Result<std::vector<uint8_t>>

// Decode a frame and extract the message
auto decoded = my_protocol::MyFrame::decode_bytes(raw_data);
if (decoded) {
    std::visit([](const auto& msg) {
        // Handle each message type
    }, decoded->payload());
}
```

For array-payload frames (`<payload count="*"/>`), a batch `wrap()` overload accepts a span of messages:

```cpp
// Batch wrap: multiple records in one frame
std::vector<my_protocol::Record> records = { ... };
auto frame = my_protocol::MyFrame::wrap(std::span{records});
auto bytes = frame.encode_bytes();
```

Individual messages also support standalone encode/decode without the frame envelope, using `encode_bytes()` / `decode_bytes()` convenience methods, or using `BitReader`/`BitWriter` directly:

```cpp
// Standalone message encode/decode (no frame)
auto bytes = hb.encode_bytes();  // Result<std::vector<uint8_t>>
auto msg = my_protocol::Heartbeat::decode_bytes(payload_data);

// Or using BitReader/BitWriter directly
conduit::io::BitWriter writer;
CONDUIT_TRY(hb.encode(writer));
auto raw = writer.finish();

conduit::io::BitReader reader(data);
auto decoded = my_protocol::Heartbeat::decode(reader);
```

The same patterns apply in Java and Python:

**Java:**
```java
// Standalone encode/decode
byte[] bytes = hb.encodeBytes();
Heartbeat msg = Heartbeat.decodeBytes(payloadData);

// Frame wrap and decode
MyFrame frame = MyFrame.wrap(hb);
byte[] frameBytes = frame.encodeBytes();
```

**Python:**
```python
# Standalone encode/decode
data = hb.encode_bytes()
msg = Heartbeat.decode_bytes(payload_data)

# Frame wrap and decode
frame = MyFrame.wrap(hb)
frame_bytes = frame.encode_bytes()
```

## Manual Session-Level Usage

For protocols where leaf types are BMDL structs (not messages), or when you need direct control over the transport, you can use sessions without the `Transceiver`. This is the approach used in the SentryLink example:

```cpp
#include <sentry_link/sessions.hpp>
#include <conduit/transceiver/stream_framer.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>

// Create session and framer
auto session = sentry_link::create_frame_session();
auto framer = std::make_unique<conduit::transceiver::StreamFramer>(*session);

// Encode: wrap a leaf body into a complete frame
// Use the leaf type's TYPE_ID constant directly:
auto bytes = session->encode_wrap(HeartbeatBody::TYPE_ID, my_heartbeat);
// Or look up by name via the session's type registry:
// for (auto id : session->leaf_type_ids())
//     if (session->type_name(id) == "HeartbeatBody") { type_id = id; break; }

// Decode: feed raw bytes through framer, then decode frames
auto frames = framer->push_data(incoming_data);
for (auto& frame : *frames) {
    auto decoded = session->decode_frame(frame);
    for (auto& dm : *decoded) {
        if (dm.type_name == "HeartbeatBody") {
            auto& hb = std::any_cast<const HeartbeatBody&>(dm.payload);
        }
    }
}
```

> **Pitfall:** Sessions are not thread-safe. Each peer must have its own session instance. The `Transceiver` handles this automatically, but manual usage requires care.

> **Pitfall:** `std::any_cast` throws `std::bad_any_cast` if the type does not match. Use `dm.type_name` or `dm.type_id` to check the type before casting.

## Direct Message Decode

For full structural access, decode the frame directly and use `std::visit` or `std::holds_alternative`:

```cpp
auto frame = my_protocol::MyFrame::decode_bytes(raw_bytes);
if (frame) {
    std::visit([](const auto& msg) {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, my_protocol::Heartbeat>) {
            // Process Heartbeat
        }
    }, frame->payload());
}
```

This bypasses the session entirely and gives full access to the decoded frame hierarchy.

## See Also

- [Error Handling](error-handling.md) -- `Result<T>` and `VoidResult` used by session and codec methods
- [Bit I/O](bit-io.md) -- `BitReader`/`BitWriter` used by `Encodable` and `Decodable` concepts
- [Transceiver](transceiver.md) -- Wires sessions to transports for automatic dispatch
- [Stream Framing](stream-framing.md) -- `StreamFramer` uses `ISession` framing metadata
- [Transports](transports.md) -- Transport configs passed to `TransceiverConfig::add_peer()`
