# Logging

[Back to index](index.md)

Conduit provides a built-in logging system with configurable sinks, log levels, category filtering, and ANSI color support.

```cpp
#include <conduit/logging/logger.hpp>
```

## Log Levels

```cpp
enum class Level {
    Trace = 0,   // Very detailed debugging
    Debug = 1,   // Debugging information
    Info  = 2,   // General information (default)
    Warn  = 3,   // Warning conditions
    Error = 4,   // Error conditions
    Fatal = 5,   // Fatal errors
    Off   = 6    // Logging disabled
};
```

Helper functions:

| Function | Signature | Description |
|----------|-----------|-------------|
| `levelToString` | `constexpr string_view levelToString(Level)` | `Level::Warn` → `"WARN"` |
| `levelToShortString` | `constexpr string_view levelToShortString(Level)` | `Level::Warn` → `"W"` |
| `levelFromString` | `constexpr optional<Level> levelFromString(string_view)` | `"WARN"`, `"warn"`, or `"W"` → `Level::Warn` |

## Logger

Singleton logger accessed via `Logger::instance()` or the convenience function `log()`.

### Configuration

```cpp
auto& logger = conduit::logging::Logger::instance();

logger.setLevel(Level::Debug);       // set minimum log level
Level lvl = logger.level();          // query current level
bool on = logger.isEnabled(Level::Trace);  // check if level is active

logger.setCategory("my-app");        // set default category
```

### Sink Management

```cpp
logger.addSink(std::make_shared<ConsoleSink>());           // stdout, color
logger.addSink(std::make_shared<ConsoleSink>(true, true)); // stderr, color
logger.addSink(std::make_shared<FileSink>("app.log"));     // file, append
logger.clearSinks();                                        // remove all sinks
```

### Logging Methods

```cpp
logger.trace("detailed debug info");
logger.debug("connection attempt");
logger.info("server started on port 5000");
logger.warn("queue 80% full");
logger.error("decode failed: buffer underrun");
logger.fatal("out of memory");

// With explicit level
logger.log(Level::Info, "message", "category");
```

### Formatted Logging

Uses `std::format` syntax:

```cpp
logger.tracef("read {} bytes from peer {}", count, peer_id);
logger.infof("listening on {}:{}", host, port);
logger.warnf("queue fill at {}%", fill_pct);
logger.errorf("decode error at offset {:#x}: {}", offset, msg);
logger.fatalf("unrecoverable: {}", reason);

// With explicit level
logger.logf(Level::Warn, "queue fill at {}%", fill_pct);
```

### Hex Dump

```cpp
logger.hexDump(Level::Debug, "received:", data_span, 64);
// max_bytes defaults to 64; data beyond this is truncated
```

### Flush

```cpp
logger.flush();  // flush all sinks
```

## Built-in Sinks

### ConsoleSink

Writes to stdout (default) or stderr. Supports ANSI color and compact format.

```cpp
ConsoleSink(bool use_stderr = false, bool colorize = true);

sink->setColorEnabled(false);   // disable ANSI colors
sink->setCompactFormat(true);   // use short format
```

> **Note:** Even with `colorize = true`, color output is automatically disabled if the terminal does not support ANSI colors (detected via `isatty()` and environment variables).

### FileSink

Writes to a file. Appends by default.

```cpp
FileSink(const std::string& path, bool append = true);

sink->isOpen();  // check if file is open
```

## ILogSink Interface

Implement custom sinks by inheriting from `ILogSink`:

```cpp
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};
```

Example: network sink

```cpp
class NetworkSink : public conduit::logging::ILogSink {
public:
    void write(const LogEntry& entry) override {
        send_to_server(entry.format(false));  // no color
    }
    void flush() override { /* flush network buffer */ }
};

logger.addSink(std::make_shared<NetworkSink>());
```

## LogEntry

```cpp
struct LogEntry {
    Level level = Level::Info;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string category;           // e.g., "net", "codec", "framing"
    std::source_location location;

    std::string format(bool with_color = true) const;
    std::string formatCompact(bool with_color = true) const;
};
```

## CategoryLogger

Scoped logger that always tags messages with a fixed category:

```cpp
conduit::logging::CategoryLogger net_log("net");

net_log.trace("socket created");
net_log.info("connected to 10.0.0.1:5000");
net_log.errorf("send failed: {}", err.message());
```

`CategoryLogger` delegates to the global `Logger::instance()` with the category set. It supports trace/debug/info/warn/error/fatal and their formatted variants tracef/debugf/infof/warnf/errorf/fatalf.

## Convenience Function

```cpp
conduit::logging::log().info("shorthand for Logger::instance()");
```

## Macros

Source-location-capturing macros (automatically capture file/line):

| Macro | Equivalent |
|-------|-----------|
| `LOG_TRACE(msg)` | `log().trace(msg)` |
| `LOG_DEBUG(msg)` | `log().debug(msg)` |
| `LOG_INFO(msg)` | `log().info(msg)` |
| `LOG_WARN(msg)` | `log().warn(msg)` |
| `LOG_ERROR(msg)` | `log().error(msg)` |
| `LOG_FATAL(msg)` | `log().fatal(msg)` |

Formatted variants:

| Macro | Example |
|-------|---------|
| `LOG_TRACEF(fmt, ...)` | `LOG_TRACEF("read {} bytes", n)` |
| `LOG_DEBUGF(fmt, ...)` | `LOG_DEBUGF("peer {} connected", id)` |
| `LOG_INFOF(fmt, ...)` | `LOG_INFOF("listening on port {}", port)` |
| `LOG_WARNF(fmt, ...)` | `LOG_WARNF("queue {}% full", pct)` |
| `LOG_ERRORF(fmt, ...)` | `LOG_ERRORF("error: {}", msg)` |
| `LOG_FATALF(fmt, ...)` | `LOG_FATALF("out of memory")` |

All formatted macros check `isEnabled()` before formatting, avoiding the cost of `std::format` when the level is disabled.

## ANSI Color Support

Colors are controlled per-sink. The `colors` namespace provides constants:

```cpp
namespace conduit::logging::colors {
    bool supportsColor(bool use_stderr = false) noexcept;  // runtime check

    // Reset / style
    constexpr string_view Reset, Bold, Dim;

    // Colors
    constexpr string_view Black, Red, Green, Yellow, Blue, Magenta, Cyan, White;

    // Bright variants
    constexpr string_view BrightRed, BrightGreen, BrightYellow, BrightBlue, BrightCyan;

    // Level → color mapping
    constexpr string_view levelColor(Level) noexcept;
    // Trace=Dim, Debug=Cyan, Info=Green, Warn=Yellow, Error=Red, Fatal=BrightRed
}
```

## See Also

- [Error Handling](error-handling.md) -- `ErrorSeverity` is a separate enum from the logging `Level` (note: `ErrorSeverity::Warning` vs `Level::Warn`)
- [Transceiver](transceiver.md) -- The transceiver logs internally using conduit's logging system
- [Configuration](configuration.md) -- `WorkerConfig::handler_timeout` triggers warning-level log messages
