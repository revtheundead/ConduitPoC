// Dummy ASTERIX Peer — TCP Server or Client mode
//
// Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
//              Uses asterix-alt namespace (server perspective: Downlink=send, Uplink=receive)
// Client mode: identical to poc_app (sends uplink types)
//              Uses asterix namespace (client perspective: Downlink=receive, Uplink=send)
//
// Usage:
//   dummy_peer server [--port N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]
//   dummy_peer client [host] [port] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]

#include "random_asterix.hpp"
#include "random_asterix_alt.hpp"
#include <conduit/transceiver/transceiver_all.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

using namespace conduit::transceiver;
using namespace std::chrono_literals;

static std::atomic<bool> g_running{true};

#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD) {
    g_running = false;
    return TRUE;
}
#else
static void signal_handler(int) { g_running = false; }
#endif

static void install_signal_handler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

// ── Handlers ────────────────────────────────────────────────────────────────

static void register_server_handlers(Transceiver& tx) {
    tx.on<asterix_alt::Cat007UplinkRecord>([](const asterix_alt::Cat007UplinkRecord&) {
        std::cout << "[RECV] Cat007UplinkRecord\n";
    });
    tx.on<asterix_alt::Cat021Record>([](const asterix_alt::Cat021Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
    tx.on<asterix_alt::Cat048Record>([](const asterix_alt::Cat048Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
    tx.on<asterix_alt::Cat253Record>([](const asterix_alt::Cat253Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
}

static void register_client_handlers(Transceiver& tx) {
    tx.on<asterix::Cat007DownlinkRecord>([](const asterix::Cat007DownlinkRecord&) {
        std::cout << "[RECV] Cat007DownlinkRecord\n";
    });
    tx.on<asterix::Cat021Record>([](const asterix::Cat021Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
    tx.on<asterix::Cat048Record>([](const asterix::Cat048Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
    tx.on<asterix::Cat253Record>([](const asterix::Cat253Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
    });
}

// ── Send helpers ────────────────────────────────────────────────────────────

// Server sends: Cat007Downlink, Cat021, Cat048, Cat253 (asterix_alt namespace)
static void send_server_message(Transceiver& tx, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 3);
    conduit::VoidResult result;
    switch (dist(rng)) {
    case 0: {
        auto msg = random_asterix_alt::random_cat007_downlink(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 1: {
        auto msg = random_asterix_alt::random_cat021(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 2: {
        auto msg = random_asterix_alt::random_cat048(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 3: {
        auto msg = random_asterix_alt::random_cat253(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    }
    if (!result) {
        auto code = result.error().code();
        if (code == conduit::ErrorCode::DirectionViolation)
            std::cerr << "[SEND BLOCKED] " << result.error().format_short() << "\n";
        else if (code == conduit::ErrorCode::EncodeConstraintViolation)
            std::cerr << "[SEND REJECTED] " << result.error().format_short() << "\n";
        else
            std::cerr << "[SEND ERROR] " << result.error().format_short() << "\n";
    }
}

// Client sends: Cat007Uplink, Cat021, Cat048, Cat253 (asterix namespace)
static void send_client_message(Transceiver& tx, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 3);
    conduit::VoidResult result;
    switch (dist(rng)) {
    case 0: {
        auto msg = random_asterix::random_cat007_uplink(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 1: {
        auto msg = random_asterix::random_cat021(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 2: {
        auto msg = random_asterix::random_cat048(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    case 3: {
        auto msg = random_asterix::random_cat253(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        result = tx.send(msg);
        break;
    }
    }
    if (!result) {
        auto code = result.error().code();
        if (code == conduit::ErrorCode::DirectionViolation)
            std::cerr << "[SEND BLOCKED] " << result.error().format_short() << "\n";
        else if (code == conduit::ErrorCode::EncodeConstraintViolation)
            std::cerr << "[SEND REJECTED] " << result.error().format_short() << "\n";
        else
            std::cerr << "[SEND ERROR] " << result.error().format_short() << "\n";
    }
}

// ── Stats ────────────────────────────────────────────────────────────────────

static void print_stats(const Transceiver& tx) {
    auto s = tx.stats().snapshot();
    std::cout << "[STATS] received=" << s.messages_received << "\n"
              << " dispatched=" << s.messages_dispatched << "\n"
              << " dropped=" << s.messages_dropped << "\n"
              << " decode_errors=" << s.decode_errors << "\n"
              << " handler_errors=" << s.handler_errors << "\n"
              << " handler_timeouts=" << s.handler_timeouts << "\n"
              << " bytes_rx=" << s.bytes_received << "\n"
              << " bytes_tx=" << s.bytes_sent << "\n\n";
}

// ── Main ────────────────────────────────────────────────────────────────────

static void print_usage() {
    std::cerr << "Usage:\n"
              << "  dummy_peer server [--port N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]\n"
              << "  dummy_peer client [host] [port] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string mode = argv[1];
    bool is_server = (mode == "server");
    bool is_client = (mode == "client");

    if (!is_server && !is_client) {
        std::cerr << "Unknown mode: " << mode << "\n";
        print_usage();
        return 1;
    }

    install_signal_handler();

    int interval_ms = 1000;
    std::string log_dir = "./logs";
    std::string log_prefix;  // default set per mode below

    // Parse common options from all args
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--interval-ms" && i + 1 < argc) {
            interval_ms = std::atoi(argv[++i]);
        } else if (a == "--log-dir" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (a == "--log-prefix" && i + 1 < argc) {
            log_prefix = argv[++i];
        }
    }

    if (is_server) {
        uint16_t port = 5000;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::atoi(argv[++i]));
            }
        }
        if (log_prefix.empty()) log_prefix = "server";

        std::cout << "[dummy_peer] Server mode on port " << port
                  << " (interval=" << interval_ms << "ms)\n";

        TransceiverConfig cfg;
        cfg.message_log.enabled = true;
        cfg.message_log.mode = MessageLogMode::SeparateDirection;
        cfg.message_log.output = MessageLogOutput::File;
        cfg.message_log.directory = log_dir;
        cfg.message_log.prefix = log_prefix;
        cfg.add_peer("clients",
                     asterix_alt::create_asterix_data_block_session,
                     transport::TcpServerConfig{.bind_address = "0.0.0.0", .port = port});

        Transceiver tx(std::move(cfg));
        register_server_handlers(tx);

        (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
            std::cout << "[STATE] peer=" << peer.value() << " -> "
                      << conduit::net::to_string(state) << "\n";
        });
        (void)tx.on_error([](const ErrorEvent& event) {
            std::cerr << "[ERROR] peer=" << event.peer_name
                      << " " << event.error.format_short() << "\n";
        });

        auto result = tx.start();
        if (!result) {
            std::cerr << "[ERROR] Failed to start: " << result.error().format_short() << "\n";
            return 1;
        }

        std::cout << "[dummy_peer] Listening. Press Ctrl+C to stop.\n";

        std::mt19937 rng(std::random_device{}());
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (!g_running) break;
            send_server_message(tx, rng);
        }

        std::cout << "[dummy_peer] Stopping...\n";
        tx.stop();
        print_stats(tx);

    } else {
        // Client mode (same perspective as poc_app)
        std::string host = "127.0.0.1";
        uint16_t port = 5000;
        if (log_prefix.empty()) log_prefix = "client";

        // Parse positional args (host, port) — skip flag args
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.starts_with("--")) {
                ++i;  // skip flag value
                continue;
            }
            if (host == "127.0.0.1") {
                host = arg;
            } else if (port == 5000) {
                port = static_cast<uint16_t>(std::atoi(argv[i]));
            }
        }

        std::cout << "[dummy_peer] Client mode connecting to " << host << ":" << port
                  << " (interval=" << interval_ms << "ms)\n";

        TransceiverConfig cfg;
        cfg.message_log.enabled = true;
        cfg.message_log.mode = MessageLogMode::SeparateDirection;
        cfg.message_log.output = MessageLogOutput::File;
        cfg.message_log.directory = log_dir;
        cfg.message_log.prefix = log_prefix;
        cfg.add_peer("server",
                     asterix::create_asterix_data_block_session,
                     transport::TcpClientConfig{.host = host, .port = port});

        Transceiver tx(std::move(cfg));
        register_client_handlers(tx);

        (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
            std::cout << "[STATE] peer=" << peer.value() << " -> "
                      << conduit::net::to_string(state) << "\n";
        });
        (void)tx.on_error([](const ErrorEvent& event) {
            std::cerr << "[ERROR] peer=" << event.peer_name
                      << " " << event.error.format_short() << "\n";
        });

        auto result = tx.start();
        if (!result) {
            std::cerr << "[ERROR] Failed to start: " << result.error().format_short() << "\n";
            return 1;
        }

        std::cout << "[dummy_peer] Started. Press Ctrl+C to stop.\n";

        std::mt19937 rng(std::random_device{}());
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (!g_running) break;
            send_client_message(tx, rng);
        }

        std::cout << "[dummy_peer] Stopping...\n";
        tx.stop();
        print_stats(tx);
    }

    std::cout << "[dummy_peer] Done.\n";
    return 0;
}
