// SPDX-License-Identifier: MIT
// SENTRIX - Logger Implementation

#include <conduit/logging/logger.hpp>
#include <cstdlib>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

namespace conduit::logging {

// ============================================================================
// Color Support Detection
// ============================================================================

namespace colors {

// Portable getenv that avoids MSVC C4996 deprecation warning.
static bool has_env(const char* name) noexcept {
#ifdef _MSC_VER
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        free(buf);
        return true;
    }
    return false;
#else
    return std::getenv(name) != nullptr;
#endif
}

bool supportsColor(bool use_stderr) noexcept {
    // Cache both results to avoid repeated system calls.
    static const bool stdout_supports = []() {
        if (has_env("NO_COLOR")) return false;
        if (has_env("FORCE_COLOR")) return true;
#ifdef _WIN32
        if (has_env("WT_SESSION") || has_env("ConEmuANSI")) return true;
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }();

    static const bool stderr_supports = []() {
        if (has_env("NO_COLOR")) return false;
        if (has_env("FORCE_COLOR")) return true;
#ifdef _WIN32
        if (has_env("WT_SESSION") || has_env("ConEmuANSI")) return true;
        return _isatty(_fileno(stderr)) != 0;
#else
        return isatty(fileno(stderr)) != 0;
#endif
    }();

    return use_stderr ? stderr_supports : stdout_supports;
}

} // namespace colors

// ============================================================================
// LogEntry Formatting
// ============================================================================

std::string LogEntry::format(bool with_color) const {
    std::ostringstream oss;

    // Timestamp
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    // Level (callers pre-decide colorize_; trust their answer instead of
    // re-checking supportsColor() against stdout, which would disable colors
    // on a stderr sink whose stdout is redirected).
    if (with_color) {
        oss << " " << colors::levelColor(level)
            << "[" << levelToString(level) << "]"
            << colors::Reset;
    } else {
        oss << " [" << levelToString(level) << "]";
    }

    // Category
    if (!category.empty()) {
        if (with_color) {
            oss << " " << colors::Cyan << "[" << category << "]" << colors::Reset;
        } else {
            oss << " [" << category << "]";
        }
    }

    // Message
    oss << " " << message;

    // Source location (for debug/trace)
    if (level <= Level::Debug) {
        oss << " (" << location.file_name() << ":" << location.line() << ")";
    }

    return oss.str();
}

std::string LogEntry::formatCompact(bool with_color) const {
    std::ostringstream oss;

    // Short timestamp (time only)
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << std::put_time(&tm_buf, "%H:%M:%S");

    // Level indicator (trust the caller's with_color decision; see format()).
    if (with_color) {
        oss << " " << colors::levelColor(level)
            << levelToShortString(level)
            << colors::Reset;
    } else {
        oss << " " << levelToShortString(level);
    }

    // Message
    oss << " " << message;

    return oss.str();
}

// ============================================================================
// ConsoleSink
// ============================================================================

ConsoleSink::ConsoleSink(bool use_stderr, bool colorize)
    : stream_(use_stderr ? std::cerr : std::cout)
    , colorize_(colorize && colors::supportsColor(use_stderr)) {}

void ConsoleSink::write(const LogEntry& entry) {
    std::lock_guard lock(mutex_);

    if (compact_) {
        stream_ << entry.formatCompact(colorize_) << '\n';
    } else {
        stream_ << entry.format(colorize_) << '\n';
    }
}

void ConsoleSink::flush() {
    std::lock_guard lock(mutex_);
    stream_.flush();
}

// ============================================================================
// FileSink
// ============================================================================

struct FileSink::Impl {
    std::ofstream file;
};

FileSink::FileSink(const std::string& path, bool append)
    : impl_(std::make_unique<Impl>()) {
    auto mode = std::ios::out;
    if (append) {
        mode |= std::ios::app;
    }
    impl_->file.open(path, mode);
    if (!impl_->file.is_open()) {
        // Write to stderr so the failure is visible even before sinks are set up.
        // Callers should check isOpen() after construction to detect this.
        std::cerr << "FileSink: failed to open log file: " << path
                  << " (check isOpen() after construction)" << std::endl;
    }
}

FileSink::~FileSink() {
    if (impl_ && impl_->file.is_open()) {
        impl_->file.flush();
        impl_->file.close();
    }
}

void FileSink::write(const LogEntry& entry) {
    if (!impl_ || !impl_->file.is_open()) return;

    std::lock_guard lock(mutex_);
    impl_->file << entry.format(false) << '\n';  // No colors in files
}

void FileSink::flush() {
    if (!impl_ || !impl_->file.is_open()) return;

    std::lock_guard lock(mutex_);
    impl_->file.flush();
}

bool FileSink::isOpen() const noexcept {
    return impl_ && impl_->file.is_open();
}

// ============================================================================
// Logger
// ============================================================================

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    // Default: no sinks (user must add them)
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) return;
    // Warn if a FileSink was added that failed to open
    if (auto* fs = dynamic_cast<FileSink*>(sink.get()); fs && !fs->isOpen()) {
        std::cerr << "Logger::addSink: adding a FileSink that is not open" << std::endl;
    }
    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard lock(mutex_);
    sinks_.clear();
}

void Logger::log(Level level,
                 std::string_view message,
                 std::string_view category,
                 std::source_location location) {
    if (!isEnabled(level)) return;

    LogEntry entry{
        .level = level,
        .timestamp = std::chrono::system_clock::now(),
        .message = std::string(message),
        .category = {},
        .location = location
    };

    // Copy sinks under lock, then write without holding it to avoid
    // blocking other threads during potentially slow sink I/O.
    std::vector<std::shared_ptr<ILogSink>> sinks_copy;
    {
        std::lock_guard lock(mutex_);
        entry.category = category.empty() ? default_category_ : std::string(category);
        sinks_copy = sinks_;
    }

    for (auto& sink : sinks_copy) {
        sink->write(entry);
    }
}

void Logger::hexDump(Level level,
                     std::string_view prefix,
                     std::span<const uint8_t> data,
                     size_t max_bytes) {
    if (!isEnabled(level)) return;

    std::ostringstream oss;
    oss << prefix << " (" << data.size() << " bytes): ";

    size_t dump_size = std::min(data.size(), max_bytes);
    oss << std::hex << std::setfill('0');

    for (size_t i = 0; i < dump_size; ++i) {
        if (i > 0 && i % 16 == 0) {
            oss << "\n" << std::string(prefix.size() + 2, ' ');
        } else if (i > 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }

    if (data.size() > max_bytes) {
        oss << std::dec << " ... (" << (data.size() - max_bytes) << " more bytes)";
    }

    log(level, oss.str(), "");
}

void Logger::flush() {
    // Copy sinks under lock, then flush without holding it to avoid
    // blocking other threads during potentially slow sink I/O.
    std::vector<std::shared_ptr<ILogSink>> sinks_copy;
    {
        std::lock_guard lock(mutex_);
        sinks_copy = sinks_;
    }
    for (auto& sink : sinks_copy) {
        sink->flush();
    }
}

} // namespace conduit::logging
