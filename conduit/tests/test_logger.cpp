// SPDX-License-Identifier: MIT
// Conduit - Logger Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/logging/logger.hpp>
#include <vector>

using namespace conduit::logging;

// ============================================================================
// Test sink that captures log entries
// ============================================================================

class TestSink : public ILogSink {
public:
    void write(const LogEntry& entry) override {
        entries.push_back(entry);
    }
    void flush() override { flushed = true; }

    std::vector<LogEntry> entries;
    bool flushed = false;
};

// Helper: configure logger for each test
struct LoggerFixture {
    LoggerFixture() {
        auto& logger = Logger::instance();
        logger.clearSinks();
        logger.setLevel(Level::Trace);
        sink = std::make_shared<TestSink>();
        logger.addSink(sink);
    }

    ~LoggerFixture() {
        Logger::instance().clearSinks();
        Logger::instance().setLevel(Level::Info);
    }

    std::shared_ptr<TestSink> sink;
};

// ============================================================================
// Level filtering
// ============================================================================

TEST_CASE("Logger level filtering", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    SECTION("All levels pass when set to Trace") {
        logger.setLevel(Level::Trace);
        logger.trace("t");
        logger.debug("d");
        logger.info("i");
        logger.warn("w");
        logger.error("e");
        logger.fatal("f");
        CHECK(fix.sink->entries.size() == 6);
    }

    SECTION("Only Warn and above pass when set to Warn") {
        logger.setLevel(Level::Warn);
        logger.trace("t");
        logger.debug("d");
        logger.info("i");
        logger.warn("w");
        logger.error("e");
        logger.fatal("f");
        CHECK(fix.sink->entries.size() == 3);
        CHECK(fix.sink->entries[0].level == Level::Warn);
        CHECK(fix.sink->entries[1].level == Level::Error);
        CHECK(fix.sink->entries[2].level == Level::Fatal);
    }

    SECTION("Nothing passes when set to Off") {
        logger.setLevel(Level::Off);
        logger.trace("t");
        logger.debug("d");
        logger.info("i");
        logger.warn("w");
        logger.error("e");
        logger.fatal("f");
        CHECK(fix.sink->entries.empty());
    }
}

// ============================================================================
// isEnabled
// ============================================================================

TEST_CASE("Logger isEnabled reflects level", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.setLevel(Level::Warn);
    CHECK_FALSE(logger.isEnabled(Level::Trace));
    CHECK_FALSE(logger.isEnabled(Level::Debug));
    CHECK_FALSE(logger.isEnabled(Level::Info));
    CHECK(logger.isEnabled(Level::Warn));
    CHECK(logger.isEnabled(Level::Error));
    CHECK(logger.isEnabled(Level::Fatal));
}

// ============================================================================
// Sink management
// ============================================================================

TEST_CASE("Logger clearSinks removes all sinks", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.info("before clear");
    CHECK(fix.sink->entries.size() == 1);

    logger.clearSinks();
    logger.info("after clear");
    // Sink was removed, so no new entries
    CHECK(fix.sink->entries.size() == 1);
}

TEST_CASE("Logger multiple sinks", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    auto sink2 = std::make_shared<TestSink>();
    logger.addSink(sink2);

    logger.info("test");
    CHECK(fix.sink->entries.size() == 1);
    CHECK(sink2->entries.size() == 1);
}

// ============================================================================
// Message content
// ============================================================================

TEST_CASE("Logger message content preserved", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.info("hello world");
    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message == "hello world");
    CHECK(fix.sink->entries[0].level == Level::Info);
}

// ============================================================================
// Formatted logging
// ============================================================================

TEST_CASE("Logger formatted logging", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.infof("count={} name={}", 42, "test");
    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message == "count=42 name=test");
}

// ============================================================================
// Category
// ============================================================================

TEST_CASE("Logger category in entry", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.log(Level::Info, "test msg", "net");
    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].category == "net");
}

// ============================================================================
// CategoryLogger
// ============================================================================

TEST_CASE("CategoryLogger routes to global logger", "[logger]") {
    LoggerFixture fix;

    CategoryLogger cat("codec");
    cat.info("decode ok");

    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message == "decode ok");
    CHECK(fix.sink->entries[0].category == "codec");
}

// ============================================================================
// Flush
// ============================================================================

TEST_CASE("Logger flush propagates to sinks", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    CHECK_FALSE(fix.sink->flushed);
    logger.flush();
    CHECK(fix.sink->flushed);
}

// ============================================================================
// Hex dump
// ============================================================================

TEST_CASE("Logger hexDump produces output", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    logger.hexDump(Level::Info, "test", data);

    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message.find("de") != std::string::npos);
    CHECK(fix.sink->entries[0].message.find("4 bytes") != std::string::npos);
}

// ============================================================================
// levelToString
// ============================================================================

TEST_CASE("levelToString covers all levels", "[logger]") {
    CHECK(levelToString(Level::Trace) == "TRACE");
    CHECK(levelToString(Level::Debug) == "DEBUG");
    CHECK(levelToString(Level::Info) == "INFO");
    CHECK(levelToString(Level::Warn) == "WARN");
    CHECK(levelToString(Level::Error) == "ERROR");
    CHECK(levelToString(Level::Fatal) == "FATAL");
    CHECK(levelToString(Level::Off) == "OFF");
}

TEST_CASE("levelToShortString covers all levels", "[logger]") {
    CHECK(levelToShortString(Level::Trace) == "T");
    CHECK(levelToShortString(Level::Debug) == "D");
    CHECK(levelToShortString(Level::Info) == "I");
    CHECK(levelToShortString(Level::Warn) == "W");
    CHECK(levelToShortString(Level::Error) == "E");
    CHECK(levelToShortString(Level::Fatal) == "F");
    CHECK(levelToShortString(Level::Off) == "-");
}

// ============================================================================
// LogEntry formatting
// ============================================================================

TEST_CASE("LogEntry format produces output", "[logger]") {
    LogEntry entry{
        .level = Level::Info,
        .timestamp = std::chrono::system_clock::now(),
        .message = "test message",
        .category = "cat",
        .location = std::source_location::current()
    };

    auto full = entry.format(false);
    CHECK(full.find("[INFO]") != std::string::npos);
    CHECK(full.find("test message") != std::string::npos);
    CHECK(full.find("[cat]") != std::string::npos);

    auto compact = entry.formatCompact(false);
    CHECK(compact.find("I") != std::string::npos);
    CHECK(compact.find("test message") != std::string::npos);
}

// ============================================================================
// levelFromString
// ============================================================================

TEST_CASE("levelFromString covers all levels", "[logger]") {
    // Full names
    CHECK(levelFromString("TRACE") == Level::Trace);
    CHECK(levelFromString("DEBUG") == Level::Debug);
    CHECK(levelFromString("INFO") == Level::Info);
    CHECK(levelFromString("WARN") == Level::Warn);
    CHECK(levelFromString("ERROR") == Level::Error);
    CHECK(levelFromString("FATAL") == Level::Fatal);
    CHECK(levelFromString("OFF") == Level::Off);

    // Lowercase
    CHECK(levelFromString("trace") == Level::Trace);
    CHECK(levelFromString("debug") == Level::Debug);
    CHECK(levelFromString("info") == Level::Info);
    CHECK(levelFromString("warn") == Level::Warn);
    CHECK(levelFromString("error") == Level::Error);
    CHECK(levelFromString("fatal") == Level::Fatal);
    CHECK(levelFromString("off") == Level::Off);

    // Short codes
    CHECK(levelFromString("T") == Level::Trace);
    CHECK(levelFromString("D") == Level::Debug);
    CHECK(levelFromString("I") == Level::Info);
    CHECK(levelFromString("W") == Level::Warn);
    CHECK(levelFromString("E") == Level::Error);
    CHECK(levelFromString("F") == Level::Fatal);
    CHECK(levelFromString("-") == Level::Off);

    // Invalid
    CHECK_FALSE(levelFromString("INVALID").has_value());
    CHECK_FALSE(levelFromString("").has_value());
    CHECK_FALSE(levelFromString("info ").has_value());
}

// ============================================================================
// Logger default category
// ============================================================================

TEST_CASE("Logger default category applied when no per-message category", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.setCategory("default-cat");
    logger.info("msg1");

    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].category == "default-cat");

    // Per-message category overrides default
    logger.log(Level::Info, "msg2", "specific");
    REQUIRE(fix.sink->entries.size() == 2);
    CHECK(fix.sink->entries[1].category == "specific");

    // Restore
    logger.setCategory("");
}

// ============================================================================
// Logger addSink with null
// ============================================================================

TEST_CASE("Logger addSink ignores nullptr", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.addSink(nullptr);
    logger.info("test");
    // Only the fixture sink should have the entry
    CHECK(fix.sink->entries.size() == 1);
}

// ============================================================================
// CategoryLogger all levels
// ============================================================================

TEST_CASE("CategoryLogger routes all levels correctly", "[logger]") {
    LoggerFixture fix;

    CategoryLogger cat("test-cat");

    cat.trace("trace msg");
    cat.debug("debug msg");
    cat.info("info msg");
    cat.warn("warn msg");
    cat.error("error msg");
    cat.fatal("fatal msg");

    REQUIRE(fix.sink->entries.size() == 6);

    CHECK(fix.sink->entries[0].level == Level::Trace);
    CHECK(fix.sink->entries[0].message == "trace msg");
    CHECK(fix.sink->entries[0].category == "test-cat");

    CHECK(fix.sink->entries[1].level == Level::Debug);
    CHECK(fix.sink->entries[2].level == Level::Info);
    CHECK(fix.sink->entries[3].level == Level::Warn);
    CHECK(fix.sink->entries[4].level == Level::Error);

    CHECK(fix.sink->entries[5].level == Level::Fatal);
    CHECK(fix.sink->entries[5].message == "fatal msg");
    CHECK(fix.sink->entries[5].category == "test-cat");
}

// ============================================================================
// CategoryLogger formatted methods
// ============================================================================

TEST_CASE("CategoryLogger formatted methods", "[logger]") {
    LoggerFixture fix;

    CategoryLogger cat("fmt");

    cat.tracef("t={}", 1);
    cat.debugf("d={}", 2);
    cat.infof("i={}", 3);
    cat.warnf("w={}", 4);
    cat.errorf("e={}", 5);
    cat.fatalf("f={}", 6);

    REQUIRE(fix.sink->entries.size() == 6);
    CHECK(fix.sink->entries[0].message == "t=1");
    CHECK(fix.sink->entries[0].level == Level::Trace);
    CHECK(fix.sink->entries[0].category == "fmt");

    CHECK(fix.sink->entries[1].message == "d=2");
    CHECK(fix.sink->entries[2].message == "i=3");
    CHECK(fix.sink->entries[3].message == "w=4");
    CHECK(fix.sink->entries[4].message == "e=5");

    CHECK(fix.sink->entries[5].message == "f=6");
    CHECK(fix.sink->entries[5].level == Level::Fatal);
}

TEST_CASE("CategoryLogger formatted methods respect level filter", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();
    logger.setLevel(Level::Warn);

    CategoryLogger cat("filt");
    cat.tracef("t={}", 1);
    cat.debugf("d={}", 2);
    cat.infof("i={}", 3);
    cat.warnf("w={}", 4);
    cat.errorf("e={}", 5);
    cat.fatalf("f={}", 6);

    CHECK(fix.sink->entries.size() == 3);
    CHECK(fix.sink->entries[0].level == Level::Warn);
    CHECK(fix.sink->entries[1].level == Level::Error);
    CHECK(fix.sink->entries[2].level == Level::Fatal);
}

// ============================================================================
// Logger formatted methods at all levels
// ============================================================================

TEST_CASE("Logger formatted methods all levels", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    logger.tracef("t={}", 10);
    logger.debugf("d={}", 20);
    logger.infof("i={}", 30);
    logger.warnf("w={}", 40);
    logger.errorf("e={}", 50);
    logger.fatalf("f={}", 60);

    REQUIRE(fix.sink->entries.size() == 6);
    CHECK(fix.sink->entries[0].message == "t=10");
    CHECK(fix.sink->entries[0].level == Level::Trace);
    CHECK(fix.sink->entries[5].message == "f=60");
    CHECK(fix.sink->entries[5].level == Level::Fatal);
}

TEST_CASE("Logger formatted methods respect level filter", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();
    logger.setLevel(Level::Error);

    logger.tracef("t={}", 1);
    logger.debugf("d={}", 2);
    logger.infof("i={}", 3);
    logger.warnf("w={}", 4);
    logger.errorf("e={}", 5);
    logger.fatalf("f={}", 6);

    CHECK(fix.sink->entries.size() == 2);
    CHECK(fix.sink->entries[0].level == Level::Error);
    CHECK(fix.sink->entries[1].level == Level::Fatal);
}

// ============================================================================
// LOG_*F macros (tests __VA_OPT__ fix with zero and multiple args)
// ============================================================================

TEST_CASE("LOG_*F macros with zero variadic args", "[logger]") {
    LoggerFixture fix;

    LOG_INFOF("no args here");
    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message == "no args here");
}

TEST_CASE("LOG_*F macros with arguments", "[logger]") {
    LoggerFixture fix;

    LOG_TRACEF("val={}", 42);
    LOG_DEBUGF("a={} b={}", 1, 2);
    LOG_INFOF("name={}", "test");
    LOG_WARNF("count={}", 99);
    LOG_ERRORF("code={}", 500);
    LOG_FATALF("crash={}", true);

    REQUIRE(fix.sink->entries.size() == 6);
    CHECK(fix.sink->entries[0].message == "val=42");
    CHECK(fix.sink->entries[1].message == "a=1 b=2");
    CHECK(fix.sink->entries[2].message == "name=test");
    CHECK(fix.sink->entries[3].message == "count=99");
    CHECK(fix.sink->entries[4].message == "code=500");
    CHECK(fix.sink->entries[5].message == "crash=true");
}

TEST_CASE("LOG_*F macros respect level filter", "[logger]") {
    LoggerFixture fix;
    Logger::instance().setLevel(Level::Fatal);

    LOG_INFOF("should not appear");
    LOG_FATALF("should appear");

    CHECK(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].level == Level::Fatal);
}

// ============================================================================
// LogEntry format with source location for debug/trace
// ============================================================================

TEST_CASE("LogEntry format includes source location for debug level", "[logger]") {
    LogEntry entry{
        .level = Level::Debug,
        .timestamp = std::chrono::system_clock::now(),
        .message = "debug msg",
        .category = {},
        .location = std::source_location::current()
    };

    auto text = entry.format(false);
    // Debug level should include file:line
    CHECK(text.find("test_logger.cpp") != std::string::npos);
}

TEST_CASE("LogEntry format excludes source location for info level", "[logger]") {
    LogEntry entry{
        .level = Level::Info,
        .timestamp = std::chrono::system_clock::now(),
        .message = "info msg",
        .category = {},
        .location = std::source_location::current()
    };

    auto text = entry.format(false);
    // Info level should NOT include file:line
    CHECK(text.find("test_logger.cpp") == std::string::npos);
}

// ============================================================================
// Multiple flushes
// ============================================================================

TEST_CASE("Logger flush with multiple sinks", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    auto sink2 = std::make_shared<TestSink>();
    logger.addSink(sink2);

    logger.flush();
    CHECK(fix.sink->flushed);
    CHECK(sink2->flushed);
}

// ============================================================================
// hexDump truncation
// ============================================================================

TEST_CASE("Logger hexDump truncates long data", "[logger]") {
    LoggerFixture fix;
    auto& logger = Logger::instance();

    std::vector<uint8_t> data(128, 0xAB);
    logger.hexDump(Level::Info, "big", data, 16);

    REQUIRE(fix.sink->entries.size() == 1);
    CHECK(fix.sink->entries[0].message.find("128 bytes") != std::string::npos);
    CHECK(fix.sink->entries[0].message.find("112 more bytes") != std::string::npos);
}

// ============================================================================
// LogEntry colorize honored regardless of stdout terminal state
//
// Regression: format() previously ANDed `with_color` with supportsColor()
// which always checks stdout. Under test the test harness has stdout
// redirected to a non-tty, so colors got disabled even when callers had
// already decided they wanted ANSI escapes (e.g., a stderr-attached sink).
// ============================================================================

TEST_CASE("LogEntry format honors with_color=true even when stdout is not a tty", "[logger]") {
    LogEntry entry{
        .level = Level::Error,
        .timestamp = std::chrono::system_clock::now(),
        .message = "boom",
        .category = "cat",
        .location = std::source_location::current()
    };

    auto with_color = entry.format(true);
    auto without_color = entry.format(false);

    // With color, the formatted string must contain at least one ANSI escape
    // sequence (the level prefix or the category brackets).
    CHECK(with_color.find("\033[") != std::string::npos);
    CHECK(without_color.find("\033[") == std::string::npos);
}

TEST_CASE("LogEntry formatCompact honors with_color=true regardless of stdout", "[logger]") {
    LogEntry entry{
        .level = Level::Warn,
        .timestamp = std::chrono::system_clock::now(),
        .message = "hi",
        .category = {},
        .location = std::source_location::current()
    };

    auto with_color = entry.formatCompact(true);
    auto without_color = entry.formatCompact(false);

    CHECK(with_color.find("\033[") != std::string::npos);
    CHECK(without_color.find("\033[") == std::string::npos);
}

// ============================================================================
// Logger with no sinks does not crash
// ============================================================================

TEST_CASE("Logger with no sinks is a no-op (does not crash)", "[logger]") {
    auto& logger = Logger::instance();
    logger.clearSinks();
    logger.setLevel(Level::Trace);

    // None of these should throw or crash with no sinks installed.
    logger.info("nope");
    logger.error("nope");
    logger.flush();

    // Restore for other tests
    logger.setLevel(Level::Info);
}
