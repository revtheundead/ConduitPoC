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

    // Human-readable transport type for logging.
    [[nodiscard]] virtual std::string_view transport_type() const noexcept { return "unknown"; }
};

}
```

Each transport owns its I/O thread(s). The `Transceiver` calls `start()` / `stop()` and routes data through the callbacks.

## TransportCallbacks

```cpp
struct TransportCallbacks {
    std::function<void(PeerId, std::span<const uint8_t>)> on_data_received;
    // remote_endpoint: "ip:port" for TCP/UDP, device path for serial
    std::function<PeerId(std::string remote_endpoint)> on_peer_connected;
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

## TCP Networking Concepts

Every TCP connection has two endpoints, each identified by an IP address and port number. When a **client** connects to a **server**, the server's port is the well-known port you configured (e.g., 5000), but the client's port is an **ephemeral port** -- a temporary port number assigned by the operating system (typically in the range 49152--65535). The client does not choose this port; the OS picks one automatically.

This means when you see a log entry like:

```
SEND peer=clients/1 remote=127.0.0.1:60630 proto=asterix transport=tcp-server ...
```

The `60630` is the ephemeral port the OS assigned to the client's end of the connection. It changes every time the client reconnects. The server's own listen port (e.g., 5000) does not appear in the `remote=` field because that is the local side of the server's connection.

**Which to use?**

- **TCP Client (`TcpClientConfig`)** -- Your application initiates the connection. You configure the remote server's `host` and `port`. Your local port is chosen by the OS. Use this when you know the address of the server you want to talk to.
- **TCP Server (`TcpServerConfig`)** -- Your application listens for incoming connections. You configure the `port` to listen on. Clients connect to you. Use this when other devices or applications need to connect to your application.

## TCP Client

Single-peer, stream-oriented transport with automatic reconnection.

```cpp
#include <conduit/transceiver/transport/tcp_client.hpp>

struct TcpClientConfig {
    std::string host;
    uint16_t port = 0;
    ReconnectPolicy reconnect{};             // see below; defaults to enabled
    size_t recv_buffer_size = 65536;
    std::chrono::milliseconds connect_timeout{10000};
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `host` | (required) | Hostname or IP address of the server to connect to |
| `port` | (required) | TCP port on the server |
| `reconnect` | enabled, 1s initial, 30s max | Reconnection policy |
| `recv_buffer_size` | 65536 | Size of the receive buffer |
| `connect_timeout` | 10s | Timeout for initial connection |

The client does not have a configurable local port -- the OS assigns an ephemeral port for the client's end of the connection. This is standard TCP behavior and is appropriate for virtually all use cases.

In message logs, `remote=` shows the server's `host:port` (e.g., `remote=10.0.1.5:5000`).

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

In message logs, `remote=` shows the connecting client's IP and ephemeral port (e.g., `remote=192.168.1.5:60630`). The ephemeral port is assigned by the client's OS and changes on each reconnection -- it identifies a specific TCP connection, not a stable client identity. Use the peer name (e.g., `peer=clients/1`) to track clients across reconnections.

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

**Single-peer mode:** Set `remote_address` and `remote_port`. The socket is "connected" to one destination. `is_multi_peer() = false`. In message logs, `remote=` shows the configured `remote_address:remote_port`.

**Multi-peer mode:** Leave `remote_address` empty. Inbound datagrams from different source addresses create distinct peers. `is_multi_peer() = true`. In message logs, `remote=` shows the sender's IP and port.

Unlike TCP, UDP does expose local port configuration via `bind_port`. Set it to a specific value when the remote side needs a known port to send to, or leave it as 0 for an OS-assigned ephemeral port.

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

In message logs, `remote=` shows the device path (e.g., `remote=COM3` or `remote=/dev/ttyUSB0`).

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

## Remote Endpoint in Logs

Each transport reports a `remote=` field in message logs identifying the other side of the connection:

| Transport | `remote=` Shows | Example | Stable? |
|-----------|-----------------|---------|---------|
| TCP Client | Configured server address | `remote=10.0.1.5:5000` | Yes (from config) |
| TCP Server | Client's IP + ephemeral port | `remote=192.168.1.5:60630` | No (changes per connection) |
| UDP (single) | Configured remote address | `remote=10.0.1.5:4000` | Yes (from config) |
| UDP (multi) | Sender's IP + port | `remote=10.0.1.5:50123` | Depends on sender |
| Serial | Device path | `remote=COM3` | Yes (from config) |

For TCP server, the ephemeral port identifies a specific TCP connection, not a stable client identity. If a client disconnects and reconnects, it will get a new ephemeral port.

## See Also

- [Error Handling](error-handling.md) -- `VoidResult` returned by `ITransport::start()`, `send()`, and `validate_serial_config()`
- [Stream Framing](stream-framing.md) -- How stream-oriented transports (TCP, Serial) are reassembled into frames
- [Transceiver](transceiver.md) -- The orchestrator that owns and manages transports
- [Configuration](configuration.md) -- `TransportConfig` variant and `PeerConfig` for declarative setup
