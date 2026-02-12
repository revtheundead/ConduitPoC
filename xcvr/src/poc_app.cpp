// PoC ASTERIX Transceiver Application — TCP Client
//
// Connects to a peer, sends random Cat007Uplink/Cat021/Cat048/Cat253 messages,
// and logs all received messages to stdout.
//
// Usage: poc_app [host] [port] [--interval-ms N]

#include "random_asterix.hpp"
#include "message_logger.hpp"
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

int main(int argc, char* argv[]) {
    // Parse args
    std::string host = "127.0.0.1";
    uint16_t port = 5000;
    int interval_ms = 1000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--interval-ms" && i + 1 < argc) {
            interval_ms = std::atoi(argv[++i]);
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
    cfg.add_peer("server",
                 asterix::create_asterix_data_block_session,
                 transport::TcpClientConfig{.host = host, .port = port, .reconnect = {}});

    Transceiver tx(std::move(cfg));

    // File loggers for sent/received messages
    auto send_log = std::make_shared<MessageLogger>("poc_sent.log");
    auto recv_log = std::make_shared<MessageLogger>("poc_received.log");

    // Register receive handlers.
    // Note: The frame decoder always decodes CAT 7 as Cat007DownlinkRecord
    // since uplink/downlink share the same wire format. Direction is
    // application-level context. As the client, incoming CAT 7 = downlink.
    tx.on<asterix::Cat007DownlinkRecord>([recv_log](const asterix::Cat007DownlinkRecord& msg) {
        std::cout << "[RECV] Cat007DownlinkRecord\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix::Cat021Record>([recv_log](const asterix::Cat021Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix::Cat048Record>([recv_log](const asterix::Cat048Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix::Cat253Record>([recv_log](const asterix::Cat253Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });

    // Connection state logging
    (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
        std::cout << "[STATE] peer=" << peer.value() << " -> "
                  << static_cast<int>(state) << "\n";
    });

    // Start
    auto result = tx.start();
    if (!result) {
        std::cerr << "[ERROR] Failed to start: " << result.error().message() << "\n";
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
            send_log->log("SEND", msg);
            send_result = tx.send(msg);
            break;
        }
        case 1: {
            auto msg = random_asterix::random_cat021(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_log->log("SEND", msg);
            send_result = tx.send(msg);
            break;
        }
        case 2: {
            auto msg = random_asterix::random_cat048(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_log->log("SEND", msg);
            send_result = tx.send(msg);
            break;
        }
        case 3: {
            auto msg = random_asterix::random_cat253(rng);
            std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
            send_log->log("SEND", msg);
            send_result = tx.send(msg);
            break;
        }
        }

        if (!send_result) {
            std::cerr << "[SEND ERROR] " << send_result.error().message() << "\n";
        }
    }

    std::cout << "[poc_app] Stopping...\n";
    tx.stop();
    std::cout << "[poc_app] Done.\n";
    return 0;
}
