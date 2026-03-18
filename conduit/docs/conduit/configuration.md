# Configuration

[Back to index](index.md)

All `Transceiver` behavior is controlled through `TransceiverConfig` and its nested structs. Configuration is set once before calling `start()`.

```cpp
#include <conduit/transceiver/transceiver_config.hpp>
```

## TransceiverConfig

```cpp
struct TransceiverConfig {
    QueueConfig rx_queue;                            // dispatch queue settings
    WorkerConfig worker;                             // worker thread settings
    std::vector<PeerConfig> peers;                   // declarative peer list
    std::chrono::milliseconds shutdown_timeout{0};   // 0 = wait indefinitely
    MessageLogConfig message_log;                    // message logging settings

    TransceiverConfig& add_peer(std::string name,
                                SessionFactory session_factory,
                                TransportConfig transport);
};
```

### add_peer

Fluent helper that appends a `PeerConfig` and returns `*this`. This is the **config-driven** approach -- you describe peers declaratively, and the `Transceiver` constructor creates the transports automatically. For imperative peer management with custom transport instances, see [`Transceiver::add_peer()`](transceiver.md).

```cpp
TransceiverConfig config;
config.add_peer("radar", asterix::create_frame_session,
                UdpConfig{.bind_port = 5000})
      .add_peer("link", sentry::create_frame_session,
                TcpClientConfig{.host = "10.0.0.1", .port = 9100});
```

## QueueConfig

Controls the internal dispatch queue between the I/O thread and worker threads.

```cpp
struct QueueConfig {
    size_t capacity = 1024;
    queue::DropPolicy drop_policy = queue::DropPolicy::DropOldest;
    double back_pressure_threshold = 0.0;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `capacity` | 1024 | Maximum number of messages in the dispatch queue |
| `drop_policy` | `DropOldest` | What happens when the queue is full |
| `back_pressure_threshold` | 0.0 (disabled) | Pause transport reading when fill exceeds this ratio (e.g., 0.8 = 80%). Resumes when fill drops below threshold x 0.8 (multiplicative hysteresis). |

### Drop Policies

| Policy | Behavior |
|--------|----------|
| `DropOldest` | Remove the oldest message to make room for the new one |
| `DropNewest` | Reject the new message (oldest messages preserved) |
| `Block` | Block the I/O thread until space is available |

> **Pitfall:** `DropPolicy::Block` stalls the transport I/O thread when the queue is full, blocking all receive processing across all peers on that transport. Prefer `DropOldest` or `DropNewest` for the dispatch queue.

## WorkerConfig

Controls the worker threads that dispatch messages to handlers.

```cpp
struct WorkerConfig {
    size_t thread_count = 1;
    std::chrono::milliseconds handler_timeout{0};
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `thread_count` | 1 | Number of worker threads for message dispatch |
| `handler_timeout` | 0 (disabled) | If > 0, log a warning when any handler takes longer than this. Warning only -- does not interrupt the handler. |

## MessageLogConfig

Controls built-in message logging. See [Message Logging](transceiver.md#message-logging) for full details.

```cpp
struct MessageLogConfig {
    bool enabled = false;
    MessageLogMode mode = MessageLogMode::Combined;
    MessageLogOutput output = MessageLogOutput::File;
    std::string directory = ".";
    std::string prefix = "conduit";
    std::string filename;                  // custom filename pattern (overrides prefix)
    std::string sent_filename;             // per-direction override for sends
    std::string received_filename;         // per-direction override for receives
    bool include_message_content = true;
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `enabled` | `false` | Enable/disable message logging |
| `mode` | `Combined` | File splitting strategy: `Combined`, `SeparateDirection`, `PerPeer`, `PerPeerDirection` |
| `output` | `File` | Where to write: `File`, `Stdout`, `Both` |
| `directory` | `"."` | Directory for log files |
| `prefix` | `"conduit"` | File name prefix (ignored when `filename` is set) |
| `filename` | `""` (empty) | Custom filename pattern with `{peer}` and `{direction}` placeholders. When set, overrides prefix-based naming. |
| `sent_filename` | `""` (empty) | Override filename for sent messages. Takes priority over `filename`. Supports `{peer}` placeholder. |
| `received_filename` | `""` (empty) | Override filename for received messages. Takes priority over `filename`. Supports `{peer}` placeholder. |
| `include_message_content` | `true` | Include `to_string()` output in log entries (has performance cost) |

## PeerConfig

Declarative peer definition used by `add_peer()`.

```cpp
struct PeerConfig {
    std::string name;
    SessionFactory session_factory;
    TransportConfig transport;
};
```

## TransportConfig

A variant over all transport config types:

```cpp
using TransportConfig = std::variant<
    transport::UdpConfig,
    transport::TcpClientConfig,
    transport::TcpServerConfig,
    transport::SerialConfig
>;
```

## SessionFactory

```cpp
using SessionFactory = std::function<std::unique_ptr<traits::ISession>()>;
```

A callable that creates a new `ISession` instance. Generated by bgen as `create_<lower_snake_case(name)>_session()`. For single-peer transports, it is called once. For multi-peer transports, it is called for each connecting client.

### Config Struct

When a frame has `auto="config(key)"` fields, the generated session requires a `Config` struct. Use a lambda to capture the config:

```cpp
MyFrameSession::Config cfg;
cfg.system_id = 42;

config.add_peer("link",
    [cfg]() { return std::make_unique<MyFrameSession>(cfg); },
    UdpConfig{.bind_port = 5000});
```

Alternatively, use the generated factory function which accepts the Config:

```cpp
config.add_peer("link",
    [cfg]() { return my_protocol::create_my_frame_session(cfg); },
    UdpConfig{.bind_port = 5000});
```

## Tuning Guidelines

### Queue Capacity

- **Low-throughput protocols** (< 100 msg/s): Default 1024 is plenty
- **High-throughput protocols** (> 10k msg/s): Increase to 8192 or 16384
- **Bursty traffic**: Size to absorb the largest expected burst

### Drop Policy

- **Real-time data** (sensors, radar): `DropOldest` -- stale data is useless
- **Reliable delivery** (commands, configs): `DropNewest` or `Block` -- preserve order
- **Back-pressure needed**: Enable `back_pressure_threshold` (e.g., 0.8) with `DropOldest`

### Worker Threads

- **Single protocol, simple handlers**: 1 thread (default) is sufficient
- **Multiple peers with independent handlers**: Match thread count to peer count
- **CPU-intensive handlers**: Increase threads, but consider offloading to a separate pool

### Back-Pressure

Set `back_pressure_threshold` to a value between 0.0 and 1.0 to automatically pause transport reading when the queue fills beyond that ratio. The threshold uses multiplicative hysteresis: transports are paused when fill exceeds the threshold, and **all** transports resume when fill drops below `threshold × 0.8`. For example, with `threshold = 0.8` and `capacity = 1024`, transports pause at 820+ messages and resume when the queue drops below 656 messages (fill ratio 0.64).

When back-pressure activates, only the transport that triggered the threshold crossing is paused. On resume (when fill drops below `threshold x 0.8`), **all** transports are resumed, since multiple transports may have been individually paused.

### Shutdown

`shutdown_timeout` controls how long `stop()` waits for worker threads to drain the dispatch queue:

- **0 (default)**: Wait indefinitely until all queued messages are processed
- **> 0**: Wait up to the specified duration, then force-stop workers even if messages remain

## Example: Full Configuration

```cpp
using namespace conduit::transceiver;
using namespace conduit::transceiver::transport;

TransceiverConfig config;

// Dispatch queue: 4096 capacity, drop oldest on overflow,
// pause reading at 80% full
config.rx_queue.capacity = 4096;
config.rx_queue.drop_policy = queue::DropPolicy::DropOldest;
config.rx_queue.back_pressure_threshold = 0.8;

// Workers: 2 threads, warn if handler takes > 500ms
config.worker.thread_count = 2;
config.worker.handler_timeout = std::chrono::milliseconds{500};

// Graceful shutdown: wait up to 5 seconds for workers to drain
config.shutdown_timeout = std::chrono::milliseconds{5000};

// Message logging: separate files for sent/received
config.message_log.enabled = true;
config.message_log.mode = MessageLogMode::SeparateDirection;
config.message_log.output = MessageLogOutput::File;
config.message_log.directory = "./logs";
config.message_log.prefix = "myapp";
// Creates: ./logs/myapp_sent.log, ./logs/myapp_received.log

// Add peers
config.add_peer("radar-feed",
                asterix::create_frame_session,
                UdpConfig{.bind_port = 5000})
      .add_peer("control-link",
                sentry::create_frame_session,
                TcpClientConfig{.host = "10.0.0.1", .port = 9100});

Transceiver xcvr(std::move(config));
```

## Java Configuration

In Java, configuration is set via method calls on the `Transceiver` before `start()`. There is no separate config struct -- each setting is applied directly.

```java
try (Transceiver tx = new Transceiver()) {
    // Queue config
    tx.setQueueConfig(
        /* capacity */               4096,
        /* dropPolicy */             0,    // 0=DropOldest, 1=DropNewest, 2=Block
        /* backPressureThreshold */  0.8);

    // Worker config
    tx.setWorkerConfig(
        /* threadCount */        2,
        /* handlerTimeoutMs */   500);

    // Shutdown timeout
    tx.setShutdownTimeout(5000);  // milliseconds

    // Message logging
    Transceiver.MessageLogConfig logCfg = new Transceiver.MessageLogConfig();
    logCfg.enabled = true;
    logCfg.mode = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
    logCfg.output = Transceiver.MessageLogOutput.FILE;
    logCfg.directory = "./logs";
    logCfg.prefix = "myapp";
    tx.setMessageLogConfig(logCfg);

    // Register session and add peers
    tx.registerSession("asterix", new AsterixDataBlockSession());
    tx.addPeer("radar-feed", "asterix",
               new TransportConfig.UdpConfig().bindPort(5000));
    tx.addPeer("control-link", "asterix",
               TransportConfig.tcpClient("10.0.0.1:9100"));

    tx.start();
    // ...
}
```

## Python Configuration

Python follows the same pattern, using keyword arguments for configuration methods.

```python
with Transceiver() as tx:
    # Queue config
    tx.set_queue_config(capacity=4096, drop_policy=0,
                        back_pressure_threshold=0.8)

    # Worker config
    tx.set_worker_config(thread_count=2, handler_timeout_ms=500)

    # Shutdown timeout
    tx.set_shutdown_timeout(timeout_ms=5000)

    # Message logging
    tx.set_message_log_config(
        enabled=True,
        mode=MessageLogMode.SEPARATE_DIRECTION,
        output=MessageLogOutput.FILE,
        directory="./logs",
        prefix="myapp",
    )

    # Register session and add peers
    tx.register_session("asterix", AsterixDataBlockSession())
    tx.add_peer("radar-feed", "asterix",
                UdpConfig(bind_port=5000))
    tx.add_peer("control-link", "asterix",
                TcpClientConfig("10.0.0.1:9100"))

    tx.start()
    # ...
```

## See Also

- [Transceiver](transceiver.md) -- The orchestrator configured by `TransceiverConfig`; also provides imperative `Transceiver::add_peer()` for custom transports
- [Transports](transports.md) -- Full documentation for `UdpConfig`, `TcpClientConfig`, `TcpServerConfig`, `SerialConfig`
- [Message Handlers](handlers.md) -- Handler registration and `handler_timeout` behavior
- [BoundedQueue](bounded-queue.md) -- Underlying queue implementation and `DropPolicy` details
- [Sessions & Generated Code](sessions-and-codegen.md) -- `SessionFactory` and session creation
