# Transports

[Back to index](index.md)

Transports handle the physical I/O -- connecting to remote endpoints, sending and receiving raw bytes. Conduit provides four built-in transports covering the most common communication patterns.

## ITransport Interface

```cpp
#include <conduit/transceiver/transport/itransport.hpp>

namespace conduit::transceiver::transport {

class ITransport {
public:
    virtual ~ITransport() = default;

    [[nodiscard]] virtual VoidResult start(TransportCallbacks cb) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual VoidResult send(PeerId peer,
                                          std::span<const uint8_t> data) = 0;

    [[nodiscard]] virtual bool is_stream_oriented() const noexcept = 0;
    [[nodiscard]] virtual bool is_multi_peer() const noexcept = 0;

    virtual void pause() {}   // back-pressure: stop reading
    virtual void resume() {}  // back-pressure: resume reading
};

}
```

Each transport owns its I/O thread(s). The `Transceiver` calls `start()` / `stop()` and routes data through the callbacks.

## TransportCallbacks

```cpp
struct TransportCallbacks {
    std::function<void(PeerId, std::span<const uint8_t>)> on_data_received;
    std::function<PeerId()> on_peer_connected;
    std::function<void(PeerId)> on_peer_disconnected;
    std::function<void(PeerId, net::ConnectionState)> on_state_changed;
};
```

All callbacks fire on the transport's I/O thread. Implementations must be lightweight -- typically just pushing data to a queue.

## ConnectionState

```cpp
#include <conduit/net/connection_state.hpp>

enum class ConnectionState {
    Disconnected,   // Not connected
    Connecting,     // Connection in progress
    Connected,      // Active connection
    Reconnecting,   // Lost connection, attempting reconnect
    Failed          // Permanent failure (max attempts reached)
};

constexpr std::string_view to_string(ConnectionState state) noexcept;
// "Disconnected", "Connecting", "Connected", "Reconnecting", "Failed"
```

## TCP Client

Single-peer, stream-oriented transport with automatic reconnection.

```cpp
#include <conduit/transceiver/transport/tcp_client.hpp>

struct TcpClientConfig {
    std::string host;
    uint16_t port = 0;
    ReconnectPolicy reconnect;               // see below
    size_t recv_buffer_size = 65536;
    std::chrono::milliseconds connect_timeout{10000};
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `host` | (required) | Hostname or IP address |
| `port` | (required) | TCP port number |
| `reconnect` | enabled, 1s initial, 30s max | Reconnection policy |
| `recv_buffer_size` | 65536 | Size of the receive buffer |
| `connect_timeout` | 10s | Timeout for initial connection |

Properties: `is_stream_oriented() = true`, `is_multi_peer() = false`.

## TCP Server

Multi-peer, stream-oriented transport. Accepts incoming connections.

```cpp
#include <conduit/transceiver/transport/tcp_server.hpp>

struct TcpServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 0;                       // 0 = OS picks an ephemeral port
    size_t max_clients = 64;
    size_t recv_buffer_size = 65536;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `bind_address` | `"0.0.0.0"` | Interface to bind to |
| `port` | 0 | Port to listen on (0 = ephemeral) |
| `max_clients` | 64 | Maximum simultaneous connections |
| `recv_buffer_size` | 65536 | Per-client receive buffer size |

The `TcpServerTransport` class provides `local_port()` to query the actual bound port (useful with port 0).

Properties: `is_stream_oriented() = true`, `is_multi_peer() = true`.

## UDP

Datagram transport. Each UDP datagram is one complete protocol frame -- no stream framing needed.

```cpp
#include <conduit/transceiver/transport/udp.hpp>

struct UdpConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t bind_port = 0;                  // 0 = ephemeral
    std::string remote_address;              // set for single-peer "connected" mode
    uint16_t remote_port = 0;
    size_t recv_buffer_size = 65536;
    size_t max_datagram_size = 65507;        // Max UDP payload for IPv4
    size_t max_peers = 1024;                 // multi-peer mode only, 0 = unlimited
    std::chrono::seconds peer_timeout{0};    // multi-peer mode only, 0 = no timeout
};
```

**Single-peer mode:** Set `remote_address` and `remote_port`. The socket is "connected" to one destination. `is_multi_peer() = false`.

**Multi-peer mode:** Leave `remote_address` empty. Inbound datagrams from different source addresses create distinct peers. `is_multi_peer() = true`.

Properties: `is_stream_oriented() = false`.

## Serial

Single-peer, stream-oriented serial port transport. Cross-platform (Win32 COM / POSIX termios).

```cpp
#include <conduit/transceiver/transport/serial.hpp>

struct SerialConfig {
    std::string port;                        // "COM3" or "/dev/ttyUSB0"
    uint32_t baud_rate = 9600;
    uint8_t data_bits = 8;
    Parity parity = Parity::None;
    StopBits stop_bits = StopBits::One;
    FlowControl flow_control = FlowControl::None;
    size_t recv_buffer_size = 4096;
};
```

### Serial Enums

```cpp
enum class Parity      { None, Odd, Even };
enum class StopBits    { One, Two };
enum class FlowControl { None, Hardware, Software };
```

Properties: `is_stream_oriented() = true`, `is_multi_peer() = false`.

### Serial Enum Helpers

Each serial enum has a `to_string()` overload:

```cpp
constexpr std::string_view to_string(Parity p);      // "None", "Odd", "Even"
constexpr std::string_view to_string(StopBits s);     // "1", "2"
constexpr std::string_view to_string(FlowControl f);  // "None", "Hardware", "Software"
```

### validate_serial_config

```cpp
VoidResult validate_serial_config(const SerialConfig& config);
```

Validates a `SerialConfig` before use. Called internally by `SerialTransport::start()`, but can also be called manually for early validation.

## ReconnectPolicy

Used by TCP client (and potentially other client transports) for automatic reconnection with exponential backoff.

```cpp
#include <conduit/transceiver/transport/reconnect_policy.hpp>

struct ReconnectPolicy {
    bool enabled = true;
    std::chrono::milliseconds initial_delay{1000};  // 1 second
    std::chrono::milliseconds max_delay{30000};     // 30 seconds
    double backoff_multiplier = 2.0;
    uint32_t max_attempts = 0;                       // 0 = unlimited
};
```

The delay starts at `initial_delay` and is multiplied by `backoff_multiplier` after each failed attempt, capped at `max_delay`:

```
attempt 1: initial_delay                                    (e.g., 1s)
attempt 2: initial_delay * backoff_multiplier               (e.g., 2s)
attempt 3: initial_delay * backoff_multiplier^2             (e.g., 4s)
...
attempt N: min(initial_delay * backoff_multiplier^(N-1), max_delay)
```

Set `enabled = false` to disable reconnection entirely. Set `max_attempts = 0` for unlimited retries (the default).

## When to Use Which Transport

| Scenario | Transport | Rationale |
|----------|-----------|-----------|
| Connect to a known server | TCP Client | Reliable, ordered, auto-reconnect |
| Accept connections from devices | TCP Server | Multi-peer, ordered streams |
| Broadcast / multicast protocols | UDP | Datagram-based, no connection setup |
| Sensor data over RS-232/RS-485 | Serial | Direct hardware connection |
| High-throughput LAN protocol | UDP | Lower latency, no head-of-line blocking |
| Protocols with sync words/length headers | TCP or Serial | Stream framing handles reassembly |
| Protocols where each packet = one message | UDP | No framing overhead |

## See Also

- [Error Handling](error-handling.md) -- `VoidResult` returned by `ITransport::start()`, `send()`, and `validate_serial_config()`
- [Stream Framing](stream-framing.md) -- How stream-oriented transports (TCP, Serial) are reassembled into frames
- [Transceiver](transceiver.md) -- The orchestrator that owns and manages transports
- [Configuration](configuration.md) -- `TransportConfig` variant and `PeerConfig` for declarative setup
