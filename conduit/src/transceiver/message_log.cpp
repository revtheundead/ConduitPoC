// SPDX-License-Identifier: MIT
// Conduit - Message Log Implementation

#include <conduit/transceiver/message_log.hpp>
#include <conduit/logging/logger.hpp>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace conduit::transceiver {

namespace {

std::string timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace

MessageLog::MessageLog(const MessageLogConfig& config)
    : config_(config), dir_created_(false) {}

MessageLog::~MessageLog() = default;

void MessageLog::log_send(const std::string& peer_name, std::string_view remote_endpoint,
                          std::string_view type_name, size_t byte_count,
                          const std::string& message_content,
                          std::string_view protocol, std::string_view transport,
                          std::span<const uint8_t> raw_bytes) {
    write_entry("SEND", peer_name, remote_endpoint, type_name, byte_count,
                message_content, protocol, transport, raw_bytes);
}

void MessageLog::log_recv(const std::string& peer_name, std::string_view remote_endpoint,
                          std::string_view type_name, size_t byte_count,
                          const std::string& message_content,
                          std::string_view protocol, std::string_view transport,
                          std::span<const uint8_t> raw_bytes) {
    write_entry("RECV", peer_name, remote_endpoint, type_name, byte_count,
                message_content, protocol, transport, raw_bytes);
}

void MessageLog::write_entry(const std::string& direction,
                             const std::string& peer_name, std::string_view remote_endpoint,
                             std::string_view type_name, size_t byte_count,
                             const std::string& message_content,
                             std::string_view protocol, std::string_view transport,
                             std::span<const uint8_t> raw_bytes) {
    std::string ts = timestamp_now();
    std::string key = resolve_file_key(direction, peer_name);

    std::ostringstream line;
    line << '[' << ts << "] " << direction
         << " peer=" << peer_name;
    if (!remote_endpoint.empty()) {
        line << " remote=" << remote_endpoint;
    }
    line << " proto=" << protocol
         << " transport=" << transport
         << " type=" << type_name
         << " bytes=" << byte_count;

    // Pre-format hex dump outside lock if needed
    std::string hex_dump;
    if (config_.include_raw_bytes && !raw_bytes.empty()) {
        static constexpr char hex_chars[] = "0123456789ABCDEF";
        std::ostringstream hex;
        hex << "  hex:";
        for (size_t i = 0; i < raw_bytes.size(); ++i) {
            if (i % 32 == 0) {
                hex << (i == 0 ? " " : "\n       ");
            } else {
                hex << ' ';
            }
            hex << hex_chars[raw_bytes[i] >> 4] << hex_chars[raw_bytes[i] & 0xF];
        }
        hex_dump = hex.str();
    }

    std::lock_guard lock(mutex_);

    auto write_to = [&](std::ostream& out) {
        out << line.str() << '\n';
        if (!hex_dump.empty()) {
            out << hex_dump << '\n';
        }
        if (config_.include_message_content && !message_content.empty()) {
            out << '\n' << message_content << "\n\n";
        } else {
            out << '\n';
        }
        out.flush();
    };

    if (config_.output == MessageLogOutput::Stdout || config_.output == MessageLogOutput::Both) {
        write_to(std::cout);
    }
    if (config_.output == MessageLogOutput::File || config_.output == MessageLogOutput::Both) {
        write_to(get_stream(key));
    }
}

std::ostream& MessageLog::get_stream(const std::string& key) {
    auto it = files_.find(key);
    if (it != files_.end()) return *it->second;

    if (!dir_created_) {
        std::filesystem::create_directories(config_.directory);
        dir_created_ = true;
    }

    std::string path = (std::filesystem::path(config_.directory) / key).string();
    auto file = std::make_unique<std::ofstream>(path, std::ios::app);
    if (!file->is_open()) {
        LOG_WARN("MessageLog: failed to open log file: " + path);
    }
    auto& ref = *file;
    files_.emplace(key, std::move(file));
    return ref;
}

std::string MessageLog::expand_filename(const std::string& pattern,
                                         const std::string& direction_label,
                                         const std::string& peer_name) {
    std::string result = pattern;
    // Replace {direction} placeholder
    for (std::string::size_type pos = 0;
         (pos = result.find("{direction}", pos)) != std::string::npos; ) {
        result.replace(pos, 11, direction_label);
        pos += direction_label.size();
    }
    // Replace {peer} placeholder
    for (std::string::size_type pos = 0;
         (pos = result.find("{peer}", pos)) != std::string::npos; ) {
        result.replace(pos, 6, peer_name);
        pos += peer_name.size();
    }
    return result;
}

std::string MessageLog::resolve_file_key(const std::string& direction,
                                         const std::string& peer_name) const {
    std::string dir_label = (direction == "SEND") ? "sent" : "received";

    // Per-direction filename overrides take highest priority
    const auto& dir_filename = (direction == "SEND") ? config_.sent_filename
                                                     : config_.received_filename;
    if (!dir_filename.empty()) {
        return expand_filename(dir_filename, dir_label, peer_name);
    }

    if (!config_.filename.empty()) {
        return expand_filename(config_.filename, dir_label, peer_name);
    }

    // Default prefix-based naming
    switch (config_.mode) {
        case MessageLogMode::Combined:
            return config_.prefix + "_messages.log";
        case MessageLogMode::SeparateDirection:
            return config_.prefix + "_" + dir_label + ".log";
        case MessageLogMode::PerPeer:
            return config_.prefix + "_" + peer_name + ".log";
        case MessageLogMode::PerPeerDirection:
            return config_.prefix + "_" + peer_name + "_" + dir_label + ".log";
    }
    return config_.prefix + "_messages.log";
}

} // namespace conduit::transceiver
