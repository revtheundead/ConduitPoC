// SPDX-License-Identifier: MIT
// Bgen - Logging utilities

#pragma once

#include <iostream>
#include <string>

namespace bgen {

enum class LogLevel { Normal, Verbose };

class Logger {
public:
    static void set_level(LogLevel l) { level_() = l; }
    static LogLevel level() { return level_(); }

    // info: shown only at Verbose (phase messages, stats)
    static void info(const std::string& msg) {
        if (level_() >= LogLevel::Verbose) {
            std::cerr << "bgen: " << msg << "\n";
        }
    }

    // verbose: shown only at Verbose (extra detail)
    static void verbose(const std::string& msg) {
        if (level_() >= LogLevel::Verbose) {
            std::cerr << "bgen: " << msg << "\n";
        }
    }

    // warn: always shown (except errors are more severe), prefixed "bgen: warning: "
    static void warn(const std::string& msg) {
        std::cerr << "bgen: warning: " << msg << "\n";
    }

    // error: always shown, prefixed "bgen: error: "
    static void error(const std::string& msg) {
        std::cerr << "bgen: error: " << msg << "\n";
    }

private:
    static LogLevel& level_() {
        static LogLevel lvl = LogLevel::Normal;
        return lvl;
    }
};

} // namespace bgen
