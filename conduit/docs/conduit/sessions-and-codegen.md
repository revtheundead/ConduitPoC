# Sessions & Generated Code

[Back to index](index.md)

Sessions bridge generated protocol code and the conduit runtime. bgen produces session classes that implement `ISession`, enabling the `Transceiver` to decode incoming frames and encode outgoing messages without knowing the protocol details.

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

    // Encode a typed message (by type_id) into wire bytes
    [[nodiscard]] virtual Result<std::vector<uint8_t>>
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
};

}
```

Users do not implement `ISession` -- bgen generates implementations. You only interact with sessions through factory functions and the `Transceiver`.

## DecodedMessage

When a frame is decoded, the session returns one or more `DecodedMessage` values:

```cpp
struct DecodedMessage {
    uint64_t type_id;              // Compile-time FNV-1a hash (matches T::TYPE_ID)
    std::string_view type_name;    // e.g., "Cat048Record"
    std::any payload;              // The typed message object
    std::vector<uint8_t> raw;      // Copy of raw wire bytes
};
```

The `Transceiver` unwraps the `std::any` payload using `std::any_cast<const T&>` when dispatching to typed handlers. You rarely interact with `DecodedMessage` directly.

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

bgen generates a factory function for each session in the protocol.

### Frame-based (v2)

When a `<frame>` is present, the factory is named from the frame:

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

### Entry-point (v1)

For v1 protocols with `role="entry-point"` messages, the factory is named from the entry-point:

```cpp
std::unique_ptr<conduit::traits::ISession>
    create_<lower_snake_case(entry_point_name)>_session();
```

For example, an entry-point named `Frame` in the `asterix` namespace produces:

```cpp
std::unique_ptr<conduit::traits::ISession>
    asterix::create_frame_session();
```

## Wiring to the Transceiver

Pass the factory to `TransceiverConfig::add_peer()`:

```cpp
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

TransceiverConfig config;
config.add_peer("my-peer",
                my_protocol::create_frame_session,
                UdpConfig{.bind_port = 5000});

Transceiver xcvr(std::move(config));
```

For multi-peer transports (TCP server), each connecting client gets its own session instance created by the factory.

## Direct Encode/Decode (No Transceiver)

### Frame-based (v2)

With v2 protocols, use the Frame class directly:

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

Individual messages also support standalone encode/decode (without the frame envelope):

```cpp
auto bytes = hb.encode_bytes();  // Result<std::vector<uint8_t>>
auto msg = my_protocol::Heartbeat::decode_bytes(payload_data);
```

### Entry-point (v1)

Generated message classes provide standalone convenience methods:

```cpp
// Encode a message to bytes
my_protocol::Heartbeat hb;
hb.set_sequence(42);
auto bytes = hb.encode_bytes();  // Result<std::vector<uint8_t>>

// Decode bytes to a message
auto msg = my_protocol::Heartbeat::decode_bytes(raw_data);
if (msg) {
    std::cout << msg->sequence() << "\n";
}
```

You can also use `BitReader`/`BitWriter` directly:

```cpp
// Manual encode
conduit::io::BitWriter writer;
CONDUIT_TRY(hb.encode(writer));
auto bytes = writer.finish();

// Manual decode
conduit::io::BitReader reader(data);
auto msg = my_protocol::Heartbeat::decode(reader);
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

## Per-Record vs Batch Delivery

The `dispatch` attribute on `<array>` elements inside inline `<case>` blocks controls how `decode_frame()` delivers array contents. See [Array Dispatch](../bmdl/choices.md#array-dispatch) for the BMDL syntax.

### Batch Delivery (Default)

With `dispatch="batch"` (or no `dispatch` attribute), the case wrapper type is the leaf. `decode_frame()` returns **one `DecodedMessage`** containing the wrapper, with the full array accessible via `.items()`:

```cpp
// Handler receives the case wrapper type (e.g., cat007_downlink)
handler.on<asterix::cat007_downlink>([](const auto& wrapper) {
    for (auto& rec : wrapper.items()) {
        // Process each Cat007DownlinkRecord with full DataBlock context
    }
});
```

### Per-Record Delivery

With `dispatch="per-record"`, the array element type is the leaf. `decode_frame()` iterates the array and returns **one `DecodedMessage` per element**:

```cpp
// Handler fires once per record, not once per DataBlock
handler.on<asterix::Cat007DownlinkRecord>([](const auto& rec) {
    // Process individual record — array boundary is lost
});
```

### Direct Message Decode

For full structural access regardless of the `dispatch` setting, decode the frame directly:

```cpp
auto frame = asterix::AsterixFrame::decode_bytes(raw_bytes);
for (auto& block : frame->blocks()) {
    if (std::holds_alternative<asterix::cat007_downlink>(block.records())) {
        auto& cat007 = std::get<asterix::cat007_downlink>(block.records());
        for (auto& rec : cat007.items()) {
            // Process record with full DataBlock context
        }
    }
}
```

This bypasses the session entirely and gives full access to the decoded frame hierarchy.

## See Also

- [Error Handling](error-handling.md) -- `Result<T>` and `VoidResult` used by session and codec methods
- [Bit I/O](bit-io.md) -- `BitReader`/`BitWriter` used by `Encodable` and `Decodable` concepts
- [Transceiver](transceiver.md) -- Wires sessions to transports for automatic dispatch
- [Stream Framing](stream-framing.md) -- `StreamFramer` uses `ISession` framing metadata
- [Transports](transports.md) -- Transport configs passed to `TransceiverConfig::add_peer()`
