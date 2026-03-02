// PoC ASTERIX Transceiver Application — TCP Client
//
// Connects to a peer, sends random Cat007Uplink/Cat021/Cat048/Cat253 messages,
// and logs all received messages to stdout.
//
// Usage: poc_app [host] [port] [--interval-ms N] [--log-dir DIR] [--log-prefix PREFIX] [--log-filename PATTERN]

#include "random_asterix.hpp"
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

int main(int argc, char* argv[]) {
    // Parse args
    std::string host = "127.0.0.1";
    uint16_t port = 5000;
    int interval_ms = 1000;
    std::string log_dir = "./logs";
    std::string log_prefix = "poc";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--interval-ms" && i + 1 < argc) {
            interval_ms = std::atoi(argv[++i]);
        } else if (arg == "--log-dir" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (arg == "--log-prefix" && i + 1 < argc) {
            log_prefix = argv[++i];
        } else if (port == 5000 && i >= 2) {
            port = static_cast<uint16_t>(std::atoi(argv[i]));
        } else if (host == "127.0.0.1" && i == 1) {
            host = arg;
        }
    }

    install_signal_handler();

    std::cout << "[poc_app] Connecting to " << host << ":" << port
              << " (interval=" << interval_ms << "ms)\n";

    // Build transceiver config
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

    // Simple stdout handlers for received messages
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

    // Connection state logging
    (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
        std::cout << "[STATE] peer=" << peer.value() << " -> "
                  << conduit::net::to_string(state) << "\n";
    });

    // Structured error reporting
    (void)tx.on_error([](const ErrorEvent& event) {
        std::cerr << "[ERROR] peer=" << event.peer_name
                  << " " << event.error.format_short() << "\n";
    });

    // Start
    auto result = tx.start();
    if (!result) {
        std::cerr << "[ERROR] Failed to start: " << result.error().format_short() << "\n";
        return 1;
    }

    std::cout << "[poc_app] Started. Press Ctrl+C to stop.\n";

    // Send loop
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> type_dist(0, 3);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        if (!g_running) break;

        conduit::VoidResult send_result;
        int choice = type_dist(rng);
        switch (choice) {
        case 0: {
            auto msg = random_asterix::random_cat007_uplink(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_result = tx.send(msg);
            break;
        }
        case 1: {
            auto msg = random_asterix::random_cat021(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_result = tx.send(msg);
            break;
        }
        case 2: {
            auto msg = random_asterix::random_cat048(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_result = tx.send(msg);
            break;
        }
        case 3: {
            auto msg = random_asterix::random_cat253(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_result = tx.send(msg);
            break;
        }
        }

        if (!send_result) {
            auto code = send_result.error().code();
            if (code == conduit::ErrorCode::DirectionViolation)
                std::cerr << "[SEND BLOCKED] " << send_result.error().format_short() << "\n";
            else if (code == conduit::ErrorCode::EncodeConstraintViolation)
                std::cerr << "[SEND REJECTED] " << send_result.error().format_short() << "\n";
            else
                std::cerr << "[SEND ERROR] " << send_result.error().format_short() << "\n";
        }
    }

    std::cout << "[poc_app] Stopping...\n";
    tx.stop();

    auto s = tx.stats().snapshot();
    std::cout << "[STATS] received=" << s.messages_received << "\n"
              << " dispatched=" << s.messages_dispatched << "\n"
              << " dropped=" << s.messages_dropped << "\n"
              << " decode_errors=" << s.decode_errors << "\n"
              << " handler_errors=" << s.handler_errors << "\n"
              << " handler_timeouts=" << s.handler_timeouts << "\n"
              << " bytes_rx=" << s.bytes_received << "\n"
              << " bytes_tx=" << s.bytes_sent << "\n\n";

    std::cout << "[poc_app] Done.\n";
    return 0;
}
