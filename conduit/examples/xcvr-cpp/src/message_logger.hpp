// SPDX-License-Identifier: MIT
// MessageLogger — Timestamped file logger for sent/received messages

#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

class MessageLogger {
public:
    explicit MessageLogger(const std::string& filepath)
        : file_(filepath, std::ios::out | std::ios::trunc) {
        if (!file_.is_open()) {
            std::cerr << "[LOG] Failed to open log file: " << filepath << "\n";
        }
    }

    template <typename T>
    void log(const std::string& direction, const T& msg) {
        if (!file_.is_open()) return;
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;
        struct tm tm_buf {};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        file_ << "[" << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
              << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
              << direction << " " << msg.TYPE_NAME << ": "
              << msg.to_string() << "\n\n";
        file_.flush();
    }

    ~MessageLogger() {
        if (file_.is_open()) file_.close();
    }

    MessageLogger(const MessageLogger&) = delete;
    MessageLogger& operator=(const MessageLogger&) = delete;

private:
    std::ofstream file_;
};
