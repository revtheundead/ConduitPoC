// Dummy ASTERIX Peer — TCP Server or Client mode
//
// Server mode: listens for connections, sends Cat007Downlink/Cat021/Cat048/Cat253
//              Uses asterix-alt namespace (server perspective: Downlink=send, Uplink=receive)
// Client mode: identical to poc_app (sends uplink types)
//              Uses asterix namespace (client perspective: Downlink=receive, Uplink=send)
//
// Usage:
//   dummy_peer server [--port N]       (default port 5000)
//   dummy_peer client [host] [port]    (default 127.0.0.1 5000)

#include "random_asterix.hpp"
#include "random_asterix_alt.hpp"
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

// ── Handlers ────────────────────────────────────────────────────────────────

static void register_server_handlers(Transceiver& tx, std::shared_ptr<MessageLogger> recv_log) {
    // Server receives Cat007UplinkRecord (asterix_alt: receive-only)
    tx.on<asterix_alt::Cat007UplinkRecord>([recv_log](const asterix_alt::Cat007UplinkRecord& msg) {
        std::cout << "[RECV] Cat007UplinkRecord\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix_alt::Cat021Record>([recv_log](const asterix_alt::Cat021Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix_alt::Cat048Record>([recv_log](const asterix_alt::Cat048Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });
    tx.on<asterix_alt::Cat253Record>([recv_log](const asterix_alt::Cat253Record& msg) {
        std::cout << "[RECV] " << msg.TYPE_NAME << "\n";
        recv_log->log("RECV", msg);
    });
}

static void register_client_handlers(Transceiver& tx, std::shared_ptr<MessageLogger> recv_log) {
    // Client receives Cat007DownlinkRecord (asterix: receive-only)
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
}

// ── Send helpers ────────────────────────────────────────────────────────────

// Server sends: Cat007Downlink, Cat021, Cat048, Cat253 (asterix_alt namespace)
static void send_server_message(Transceiver& tx, std::mt19937& rng, MessageLogger& send_log) {
    std::uniform_int_distribution<int> dist(0, 3);
    conduit::VoidResult result;
    switch (dist(rng)) {
    case 0: {
        auto msg = random_asterix_alt::random_cat007_downlink(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 1: {
        auto msg = random_asterix_alt::random_cat021(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 2: {
        auto msg = random_asterix_alt::random_cat048(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 3: {
        auto msg = random_asterix_alt::random_cat253(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    }
    if (!result) {
        std::cerr << "[SEND ERROR] " << result.error().message() << "\n";
    }
}

// Client sends: Cat007Uplink, Cat021, Cat048, Cat253 (asterix namespace)
static void send_client_message(Transceiver& tx, std::mt19937& rng, MessageLogger& send_log) {
    std::uniform_int_distribution<int> dist(0, 3);
    conduit::VoidResult result;
    switch (dist(rng)) {
    case 0: {
        auto msg = random_asterix::random_cat007_uplink(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 1: {
        auto msg = random_asterix::random_cat021(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 2: {
        auto msg = random_asterix::random_cat048(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    case 3: {
        auto msg = random_asterix::random_cat253(rng);
        std::cout << "[SEND] " << msg.TYPE_NAME << "\n";
        send_log.log("SEND", msg);
        result = tx.send(msg);
        break;
    }
    }
    if (!result) {
        std::cerr << "[SEND ERROR] " << result.error().message() << "\n";
    }
}

// ── Main ────────────────────────────────────────────────────────────────────

static void print_usage() {
    std::cerr << "Usage:\n"
              << "  dummy_peer server [--port N]           (default port 5000)\n"
              << "  dummy_peer client [host] [port]        (default 127.0.0.1 5000)\n";
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

    if (is_server) {
        uint16_t port = 5000;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (std::string(argv[i]) == "--interval-ms" && i + 1 < argc) {
                interval_ms = std::atoi(argv[++i]);
            }
        }

        std::cout << "[dummy_peer] Server mode on port " << port
                  << " (interval=" << interval_ms << "ms)\n";

        TransceiverConfig cfg;
        cfg.add_peer("clients",
                     asterix_alt::create_asterix_data_block_session,
                     transport::TcpServerConfig{.bind_address = "0.0.0.0", .port = port});

        Transceiver tx(std::move(cfg));
        auto recv_log = std::make_shared<MessageLogger>("server_received.log");
        MessageLogger send_log("server_sent.log");
        register_server_handlers(tx, recv_log);

        (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
            std::cout << "[STATE] peer=" << peer.value() << " -> "
                      << static_cast<int>(state) << "\n";
        });

        auto result = tx.start();
        if (!result) {
            std::cerr << "[ERROR] Failed to start: " << result.error().message() << "\n";
            return 1;
        }

        std::cout << "[dummy_peer] Listening. Press Ctrl+C to stop.\n";

        std::mt19937 rng(std::random_device{}());
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (!g_running) break;
            send_server_message(tx, rng, send_log);
        }

        std::cout << "[dummy_peer] Stopping...\n";
        tx.stop();

    } else {
        // Client mode (same perspective as poc_app)
        std::string host = "127.0.0.1";
        uint16_t port = 5000;

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--interval-ms" && i + 1 < argc) {
                interval_ms = std::atoi(argv[++i]);
            } else if (i == 2) {
                host = arg;
            } else if (i == 3) {
                port = static_cast<uint16_t>(std::atoi(argv[i]));
            }
        }

        std::cout << "[dummy_peer] Client mode connecting to " << host << ":" << port
                  << " (interval=" << interval_ms << "ms)\n";

        TransceiverConfig cfg;
        cfg.add_peer("server",
                     asterix::create_asterix_data_block_session,
                     transport::TcpClientConfig{.host = host, .port = port, .reconnect = {}});

        Transceiver tx(std::move(cfg));
        auto recv_log = std::make_shared<MessageLogger>("client_received.log");
        MessageLogger send_log("client_sent.log");
        register_client_handlers(tx, recv_log);

        (void)tx.on_state_change([](PeerId peer, conduit::net::ConnectionState state) {
            std::cout << "[STATE] peer=" << peer.value() << " -> "
                      << static_cast<int>(state) << "\n";
        });

        auto result = tx.start();
        if (!result) {
            std::cerr << "[ERROR] Failed to start: " << result.error().message() << "\n";
            return 1;
        }

        std::cout << "[dummy_peer] Started. Press Ctrl+C to stop.\n";

        std::mt19937 rng(std::random_device{}());
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (!g_running) break;
            send_client_message(tx, rng, send_log);
        }

        std::cout << "[dummy_peer] Stopping...\n";
        tx.stop();
    }

    std::cout << "[dummy_peer] Done.\n";
    return 0;
}
