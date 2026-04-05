// SPDX-License-Identifier: MIT
// Conduit - Message Log

#pragma once

#include <conduit/transceiver/transceiver_config.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace conduit::transceiver {

class MessageLog {
public:
    explicit MessageLog(const MessageLogConfig& config);
    ~MessageLog();

    void log_send(const std::string& peer_name, std::string_view remote_endpoint,
                  std::string_view type_name, size_t byte_count,
                  const std::string& message_content,
                  std::string_view protocol, std::string_view transport,
                  std::span<const uint8_t> raw_bytes = {});

    void log_recv(const std::string& peer_name, std::string_view remote_endpoint,
                  std::string_view type_name, size_t byte_count,
                  const std::string& message_content,
                  std::string_view protocol, std::string_view transport,
                  std::span<const uint8_t> raw_bytes = {});

private:
    void write_entry(const std::string& direction,
                     const std::string& peer_name, std::string_view remote_endpoint,
                     std::string_view type_name, size_t byte_count,
                     const std::string& message_content,
                     std::string_view protocol, std::string_view transport,
                     std::span<const uint8_t> raw_bytes);

    std::ostream& get_stream(const std::string& key);

    std::string resolve_file_key(const std::string& direction,
                                 const std::string& peer_name) const;

    static std::string expand_filename(const std::string& pattern,
                                       const std::string& direction_label,
                                       const std::string& peer_name);

    MessageLogConfig config_;
    std::mutex mutex_;
    bool dir_created_;
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> files_;
};

} // namespace conduit::transceiver
