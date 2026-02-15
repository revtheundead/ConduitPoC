// SPDX-License-Identifier: MIT
// Conduit - Logging System

#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace conduit::logging {

// ============================================================================
// Log Levels
// ============================================================================

enum class Level {
    Trace = 0,   // Very detailed debugging
    Debug = 1,   // Debugging information
    Info = 2,    // General information
    Warn = 3,    // Warning conditions
    Error = 4,   // Error conditions
    Fatal = 5,   // Fatal errors
    Off = 6      // Logging disabled
};

[[nodiscard]] constexpr std::string_view levelToString(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
        case Level::Off:   return "OFF";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr std::optional<Level> levelFromString(std::string_view str) noexcept {
    if (str == "TRACE" || str == "trace" || str == "T") return Level::Trace;
    if (str == "DEBUG" || str == "debug" || str == "D") return Level::Debug;
    if (str == "INFO"  || str == "info"  || str == "I") return Level::Info;
    if (str == "WARN"  || str == "warn"  || str == "W") return Level::Warn;
    if (str == "ERROR" || str == "error" || str == "E") return Level::Error;
    if (str == "FATAL" || str == "fatal" || str == "F") return Level::Fatal;
    if (str == "OFF"   || str == "off"   || str == "-") return Level::Off;
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view levelToShortString(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "T";
        case Level::Debug: return "D";
        case Level::Info:  return "I";
        case Level::Warn:  return "W";
        case Level::Error: return "E";
        case Level::Fatal: return "F";
        case Level::Off:   return "-";
    }
    return "?";
}

// ============================================================================
// ANSI Color Support
// ============================================================================

namespace colors {

// Check if terminal supports colors (simplified check).
// use_stderr selects which stream to check (false = stdout, true = stderr).
[[nodiscard]] bool supportsColor(bool use_stderr = false) noexcept;

// ANSI escape codes
constexpr std::string_view Reset     = "\033[0m";
constexpr std::string_view Bold      = "\033[1m";
constexpr std::string_view Dim       = "\033[2m";

constexpr std::string_view Black     = "\033[30m";
constexpr std::string_view Red       = "\033[31m";
constexpr std::string_view Green     = "\033[32m";
constexpr std::string_view Yellow    = "\033[33m";
constexpr std::string_view Blue      = "\033[34m";
constexpr std::string_view Magenta   = "\033[35m";
constexpr std::string_view Cyan      = "\033[36m";
constexpr std::string_view White     = "\033[37m";

constexpr std::string_view BrightRed    = "\033[91m";
constexpr std::string_view BrightGreen  = "\033[92m";
constexpr std::string_view BrightYellow = "\033[93m";
constexpr std::string_view BrightBlue   = "\033[94m";
constexpr std::string_view BrightCyan   = "\033[96m";

// Get color for log level
[[nodiscard]] constexpr std::string_view levelColor(Level level) noexcept {
    switch (level) {
        case Level::Trace: return Dim;
        case Level::Debug: return Cyan;
        case Level::Info:  return Green;
        case Level::Warn:  return Yellow;
        case Level::Error: return Red;
        case Level::Fatal: return BrightRed;
        default: return Reset;
    }
}

} // namespace colors

// ============================================================================
// Log Entry
// ============================================================================

struct LogEntry {
    Level level = Level::Info;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string category;  // e.g., "net", "codec", "framing"
    std::source_location location;

    [[nodiscard]] std::string format(bool with_color = true) const;
    [[nodiscard]] std::string formatCompact(bool with_color = true) const;
};

// ============================================================================
// Log Sink Interface
// ============================================================================

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

// ============================================================================
// Console Sink
// ============================================================================

class ConsoleSink : public ILogSink {
public:
    explicit ConsoleSink(bool use_stderr = false, bool colorize = true);

    void write(const LogEntry& entry) override;
    void flush() override;

    void setColorEnabled(bool enabled) { std::lock_guard lock(mutex_); colorize_ = enabled; }
    void setCompactFormat(bool compact) { std::lock_guard lock(mutex_); compact_ = compact; }

private:
    std::ostream& stream_;
    bool colorize_;
    bool compact_ = false;
    std::mutex mutex_;
};

// ============================================================================
// File Sink
// ============================================================================

class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& path, bool append = true);
    ~FileSink() override;

    void write(const LogEntry& entry) override;
    void flush() override;

    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

// ============================================================================
// Logger Class
// ============================================================================

class Logger {
public:
    // Singleton access
    static Logger& instance();

    // Configuration
    void setLevel(Level level) noexcept { level_.store(level, std::memory_order_relaxed); }
    [[nodiscard]] Level level() const noexcept { return level_.load(std::memory_order_relaxed); }

    void setCategory(std::string_view category) {
        std::lock_guard lock(mutex_);
        default_category_ = category;
    }

    // Sink management
    void addSink(std::shared_ptr<ILogSink> sink);
    void clearSinks();

    // Check if level is enabled
    [[nodiscard]] bool isEnabled(Level level) const noexcept {
        return level >= level_.load(std::memory_order_relaxed);
    }

    // Log methods
    void log(Level level,
             std::string_view message,
             std::string_view category = "",
             std::source_location location = std::source_location::current());

    // Convenience methods
    void trace(std::string_view message,
               std::source_location location = std::source_location::current()) {
        log(Level::Trace, message, "", location);
    }

    void debug(std::string_view message,
               std::source_location location = std::source_location::current()) {
        log(Level::Debug, message, "", location);
    }

    void info(std::string_view message,
              std::source_location location = std::source_location::current()) {
        log(Level::Info, message, "", location);
    }

    void warn(std::string_view message,
              std::source_location location = std::source_location::current()) {
        log(Level::Warn, message, "", location);
    }

    void error(std::string_view message,
               std::source_location location = std::source_location::current()) {
        log(Level::Error, message, "", location);
    }

    void fatal(std::string_view message,
               std::source_location location = std::source_location::current()) {
        log(Level::Fatal, message, "", location);
    }

    // Formatted logging with std::format.
    // Note: source_location is captured from logf(), not the ultimate caller.
    // For accurate source_location, use the LOG_*F macros instead which
    // expand at the call site.
    template<typename... Args>
    void logf(Level level,
              std::format_string<Args...> fmt,
              Args&&... args) {
        if (isEnabled(level)) {
            log(level, std::format(fmt, std::forward<Args>(args)...), "");
        }
    }

    template<typename... Args>
    void tracef(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Trace, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debugf(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Debug, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void infof(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Info, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warnf(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Warn, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void errorf(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Error, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void fatalf(std::format_string<Args...> fmt, Args&&... args) {
        logf(Level::Fatal, fmt, std::forward<Args>(args)...);
    }

    // Hex dump helper
    void hexDump(Level level,
                 std::string_view prefix,
                 std::span<const uint8_t> data,
                 size_t max_bytes = 64);

    // Flush all sinks
    void flush();

private:
    Logger();

    std::atomic<Level> level_ = Level::Info;
    std::string default_category_;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    std::mutex mutex_;
};

// ============================================================================
// Global Convenience Functions
// ============================================================================

inline Logger& log() { return Logger::instance(); }

// Category-specific logger (returns reference to global logger with category set)
class CategoryLogger {
public:
    explicit CategoryLogger(std::string_view category) : category_(category) {}

    void trace(std::string_view msg,
               std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Trace, msg, category_, loc);
    }

    void debug(std::string_view msg,
               std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Debug, msg, category_, loc);
    }

    void info(std::string_view msg,
              std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Info, msg, category_, loc);
    }

    void warn(std::string_view msg,
              std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Warn, msg, category_, loc);
    }

    void error(std::string_view msg,
               std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Error, msg, category_, loc);
    }

    void fatal(std::string_view msg,
               std::source_location loc = std::source_location::current()) {
        Logger::instance().log(Level::Fatal, msg, category_, loc);
    }

    template<typename... Args>
    void tracef(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Trace)) {
            trace(std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template<typename... Args>
    void debugf(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Debug)) {
            debug(std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template<typename... Args>
    void infof(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Info)) {
            info(std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template<typename... Args>
    void warnf(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Warn)) {
            warn(std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template<typename... Args>
    void errorf(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Error)) {
            error(std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template<typename... Args>
    void fatalf(std::format_string<Args...> fmt, Args&&... args) {
        if (Logger::instance().isEnabled(Level::Fatal)) {
            fatal(std::format(fmt, std::forward<Args>(args)...));
        }
    }

private:
    std::string category_;
};

// ============================================================================
// Logging Macros (optional, for source location capture)
// ============================================================================

#define LOG_TRACE(msg) ::conduit::logging::log().trace(msg)
#define LOG_DEBUG(msg) ::conduit::logging::log().debug(msg)
#define LOG_INFO(msg)  ::conduit::logging::log().info(msg)
#define LOG_WARN(msg)  ::conduit::logging::log().warn(msg)
#define LOG_ERROR(msg) ::conduit::logging::log().error(msg)
#define LOG_FATAL(msg) ::conduit::logging::log().fatal(msg)

#define LOG_TRACEF(fmt, ...) do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Trace)) \
    ::conduit::logging::log().trace(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)
#define LOG_DEBUGF(fmt, ...) do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Debug)) \
    ::conduit::logging::log().debug(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)
#define LOG_INFOF(fmt, ...)  do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Info))  \
    ::conduit::logging::log().info(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)
#define LOG_WARNF(fmt, ...)  do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Warn))  \
    ::conduit::logging::log().warn(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)
#define LOG_ERRORF(fmt, ...) do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Error)) \
    ::conduit::logging::log().error(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)
#define LOG_FATALF(fmt, ...) do { if (::conduit::logging::log().isEnabled(::conduit::logging::Level::Fatal)) \
    ::conduit::logging::log().fatal(std::format(fmt __VA_OPT__(,) __VA_ARGS__)); } while(0)

} // namespace conduit::logging
