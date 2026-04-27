// SPDX-License-Identifier: MIT
// Conduit - MessageLog Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/transceiver/message_log.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace conduit::transceiver;

namespace {

// Create a unique temp directory for each test
std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / "conduit_log_test" /
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(dir);
    return dir;
}

// Read entire file contents
std::string read_file(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// Count files in a directory
int count_files(const std::filesystem::path& dir) {
    int n = 0;
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) ++n;
    }
    return n;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() { std::filesystem::remove_all(path); }
};

} // namespace

// ============================================================================
// Combined mode
// ============================================================================

TEST_CASE("MessageLog: combined mode creates single file", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";
    cfg.include_message_content = true;

    {
        MessageLog log(cfg);
        log.log_send("peer1", "10.0.0.1:5000", "Heartbeat", 12, "Heartbeat{seq=1}", "proto", "tcp-client");
        log.log_recv("peer1", "10.0.0.1:5000", "Status", 8, "Status{ok=true}", "proto", "tcp-client");
    }

    auto content = read_file(tmp.path / "test_messages.log");
    CHECK(content.find("SEND") != std::string::npos);
    CHECK(content.find("RECV") != std::string::npos);
    CHECK(content.find("peer=peer1") != std::string::npos);
    CHECK(content.find("remote=10.0.0.1:5000") != std::string::npos);
    CHECK(content.find("type=Heartbeat") != std::string::npos);
    CHECK(content.find("type=Status") != std::string::npos);
    CHECK(content.find("bytes=12") != std::string::npos);
    CHECK(content.find("Heartbeat{seq=1}") != std::string::npos);
    CHECK(content.find("Status{ok=true}") != std::string::npos);
    CHECK(count_files(tmp.path) == 1);
}

// ============================================================================
// SeparateDirection mode
// ============================================================================

TEST_CASE("MessageLog: separate direction creates two files", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::SeparateDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "udp");
        log.log_recv("peer1", "", "Status", 8, "", "proto", "udp");
    }

    auto sent = read_file(tmp.path / "test_sent.log");
    auto recv = read_file(tmp.path / "test_received.log");
    CHECK(sent.find("SEND") != std::string::npos);
    CHECK(recv.find("RECV") != std::string::npos);
    CHECK(sent.find("RECV") == std::string::npos);
    CHECK(recv.find("SEND") == std::string::npos);
    CHECK(count_files(tmp.path) == 2);
}

// ============================================================================
// PerPeer mode
// ============================================================================

TEST_CASE("MessageLog: per-peer creates one file per peer", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeer;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";

    {
        MessageLog log(cfg);
        log.log_send("radar1", "10.0.0.1:5000", "Cat048Record", 37, "", "asterix", "tcp-client");
        log.log_recv("radar2", "10.0.0.2:5000", "Cat048Record", 37, "", "asterix", "tcp-client");
    }

    auto r1 = read_file(tmp.path / "test_radar1.log");
    auto r2 = read_file(tmp.path / "test_radar2.log");
    CHECK(r1.find("SEND") != std::string::npos);
    CHECK(r1.find("peer=radar1") != std::string::npos);
    CHECK(r2.find("RECV") != std::string::npos);
    CHECK(r2.find("peer=radar2") != std::string::npos);
    CHECK(count_files(tmp.path) == 2);
}

// ============================================================================
// PerPeerDirection mode
// ============================================================================

TEST_CASE("MessageLog: per-peer-direction creates four files for two peers", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeerDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";

    {
        MessageLog log(cfg);
        log.log_send("radar1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("radar1", "", "Status", 8, "", "proto", "tcp-client");
        log.log_send("radar2", "", "Heartbeat", 12, "", "proto", "udp");
        log.log_recv("radar2", "", "Status", 8, "", "proto", "udp");
    }

    CHECK(std::filesystem::exists(tmp.path / "test_radar1_sent.log"));
    CHECK(std::filesystem::exists(tmp.path / "test_radar1_received.log"));
    CHECK(std::filesystem::exists(tmp.path / "test_radar2_sent.log"));
    CHECK(std::filesystem::exists(tmp.path / "test_radar2_received.log"));
    CHECK(count_files(tmp.path) == 4);
}

// ============================================================================
// include_message_content = false
// ============================================================================

TEST_CASE("MessageLog: content excluded when disabled", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";
    cfg.include_message_content = false;

    {
        MessageLog log(cfg);
        log.log_send("peer1", "10.0.0.1:5000", "Heartbeat", 12, "Heartbeat{seq=1}", "proto", "tcp-client");
    }

    auto content = read_file(tmp.path / "test_messages.log");
    CHECK(content.find("SEND") != std::string::npos);
    CHECK(content.find("Heartbeat{seq=1}") == std::string::npos);
}

// ============================================================================
// Timestamp format
// ============================================================================

TEST_CASE("MessageLog: log entries contain ISO timestamp", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";
    cfg.include_message_content = false;

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "tcp-client");
    }

    auto content = read_file(tmp.path / "test_messages.log");
    // Expect ISO-8601-ish timestamp: [2026-02-15T10:24:53.486]
    // Check for the pattern [YYYY-MM-DDT
    CHECK(content.find("[20") != std::string::npos);
    CHECK(content.find("T") != std::string::npos);
}

// ============================================================================
// Protocol and transport metadata
// ============================================================================

TEST_CASE("MessageLog: log entries contain protocol and transport", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";
    cfg.include_message_content = false;

    {
        MessageLog log(cfg);
        log.log_send("peer1", "10.0.0.5:9100", "Cat048Record", 37, "", "asterix", "tcp-client");
    }

    auto content = read_file(tmp.path / "test_messages.log");
    CHECK(content.find("proto=asterix") != std::string::npos);
    CHECK(content.find("transport=tcp-client") != std::string::npos);
}

// ============================================================================
// Custom filename pattern
// ============================================================================

TEST_CASE("MessageLog: custom filename (no placeholders)", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.filename = "my_app.log";

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "my_app.log"));
    auto content = read_file(tmp.path / "my_app.log");
    CHECK(content.find("SEND") != std::string::npos);
    CHECK(count_files(tmp.path) == 1);
}

TEST_CASE("MessageLog: filename with {direction} placeholder", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::SeparateDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.filename = "app_{direction}.log";

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("peer1", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "app_sent.log"));
    CHECK(std::filesystem::exists(tmp.path / "app_received.log"));
    auto sent = read_file(tmp.path / "app_sent.log");
    auto recv = read_file(tmp.path / "app_received.log");
    CHECK(sent.find("SEND") != std::string::npos);
    CHECK(recv.find("RECV") != std::string::npos);
}

TEST_CASE("MessageLog: filename with {peer} and {direction} placeholders", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeerDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.filename = "{peer}_{direction}.log";

    {
        MessageLog log(cfg);
        log.log_send("radar1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("radar1", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "radar1_sent.log"));
    CHECK(std::filesystem::exists(tmp.path / "radar1_received.log"));
}

TEST_CASE("MessageLog: per-direction filename overrides", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::SeparateDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.sent_filename = "outbound.log";
    cfg.received_filename = "inbound.log";

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("peer1", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "outbound.log"));
    CHECK(std::filesystem::exists(tmp.path / "inbound.log"));
    auto sent = read_file(tmp.path / "outbound.log");
    auto recv = read_file(tmp.path / "inbound.log");
    CHECK(sent.find("SEND") != std::string::npos);
    CHECK(recv.find("RECV") != std::string::npos);
    CHECK(count_files(tmp.path) == 2);
}

TEST_CASE("MessageLog: per-direction filenames with {peer} placeholder", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeerDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.sent_filename = "{peer}_out.log";
    cfg.received_filename = "{peer}_in.log";

    {
        MessageLog log(cfg);
        log.log_send("radar1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("radar1", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "radar1_out.log"));
    CHECK(std::filesystem::exists(tmp.path / "radar1_in.log"));
    auto sent = read_file(tmp.path / "radar1_out.log");
    auto recv = read_file(tmp.path / "radar1_in.log");
    CHECK(sent.find("SEND") != std::string::npos);
    CHECK(recv.find("RECV") != std::string::npos);
    CHECK(count_files(tmp.path) == 2);
}

TEST_CASE("MessageLog: sent_filename only overrides send direction", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::SeparateDirection;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "test";
    cfg.sent_filename = "custom_sent.log";
    // received_filename not set — falls back to prefix-based default

    {
        MessageLog log(cfg);
        log.log_send("peer1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("peer1", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "custom_sent.log"));
    CHECK(std::filesystem::exists(tmp.path / "test_received.log"));
    CHECK(count_files(tmp.path) == 2);
}

TEST_CASE("MessageLog: filename with {peer} only", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeer;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.filename = "log_{peer}.txt";

    {
        MessageLog log(cfg);
        log.log_send("r1", "", "Heartbeat", 12, "", "proto", "tcp-client");
        log.log_recv("r2", "", "Status", 8, "", "proto", "tcp-client");
    }

    CHECK(std::filesystem::exists(tmp.path / "log_r1.txt"));
    CHECK(std::filesystem::exists(tmp.path / "log_r2.txt"));
    CHECK(count_files(tmp.path) == 2);
}

// ============================================================================
// Peer name sanitization
//
// Multi-peer transports (TCP server) name dynamic peers "<base>/<index>".
// Without sanitization the '/' would create a subdirectory (best case) or
// escape the configured directory (worst case). Confirm that path separators
// are stripped from filenames whether they come via PerPeer mode or via the
// {peer} placeholder.
// ============================================================================

TEST_CASE("MessageLog: PerPeer mode sanitizes path separators in peer name", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeer;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "log";

    {
        MessageLog log(cfg);
        // Mimic a TCP-server child peer name with '/'.
        log.log_send("server/0", "", "Heartbeat", 12, "", "proto", "tcp-server");
        log.log_recv("server/1", "", "Status", 8, "", "proto", "tcp-server");
    }

    // No subdirectory should be created; the '/' is replaced with '_'.
    CHECK(std::filesystem::exists(tmp.path / "log_server_0.log"));
    CHECK(std::filesystem::exists(tmp.path / "log_server_1.log"));
    CHECK_FALSE(std::filesystem::exists(tmp.path / "server"));
    CHECK(count_files(tmp.path) == 2);
}

TEST_CASE("MessageLog: {peer} placeholder sanitizes path separators", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::Combined;  // mode ignored when filename is set
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.filename = "{peer}.log";

    {
        MessageLog log(cfg);
        log.log_send("group/child", "", "T", 1, "", "p", "tcp-server");
    }

    CHECK(std::filesystem::exists(tmp.path / "group_child.log"));
    CHECK_FALSE(std::filesystem::exists(tmp.path / "group"));
}

TEST_CASE("MessageLog: backslash and colon in peer name are sanitized", "[message_log]") {
    TempDir tmp;
    MessageLogConfig cfg;
    cfg.enabled = true;
    cfg.mode = MessageLogMode::PerPeer;
    cfg.output = MessageLogOutput::File;
    cfg.directory = tmp.path.string();
    cfg.prefix = "log";

    {
        MessageLog log(cfg);
        log.log_send("a\\b", "", "T", 1, "", "p", "tcp-server");
        log.log_send("ip:5000", "", "T", 1, "", "p", "tcp-server");
    }

    CHECK(std::filesystem::exists(tmp.path / "log_a_b.log"));
    CHECK(std::filesystem::exists(tmp.path / "log_ip_5000.log"));
}
