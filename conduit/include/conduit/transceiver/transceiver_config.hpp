// SPDX-License-Identifier: MIT
// Conduit - Transceiver Configuration

#pragma once

#include <conduit/queue/bounded_queue.hpp>
#include <conduit/traits/session_traits.hpp>
#include <conduit/transceiver/transport/udp.hpp>
#include <conduit/transceiver/transport/tcp_client.hpp>
#include <conduit/transceiver/transport/tcp_server.hpp>
#include <conduit/transceiver/transport/serial.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace conduit::transceiver {

// ============================================================================
// Message Logging Configuration
// ============================================================================

enum class MessageLogMode {
    Combined,          // Single file for all messages
    SeparateDirection, // One file for sends, one for receives
    PerPeer,           // One file per peer (both directions)
    PerPeerDirection   // Separate send/recv files per peer
};

enum class MessageLogOutput { File, Stdout, Both };

struct MessageLogConfig {
    bool enabled = false;
    MessageLogMode mode = MessageLogMode::Combined;
    MessageLogOutput output = MessageLogOutput::File;
    std::string directory = ".";       // Where log files are created
    std::string prefix = "conduit";    // File name prefix (used when filename is empty)

    // Log filename pattern. Supports placeholders:
    //   {peer}      — peer name (e.g., "radar-1")
    //   {direction} — "sent" or "received"
    //
    // When empty, defaults are derived from prefix + mode:
    //   Combined:          "{prefix}_messages.log"
    //   SeparateDirection: "{prefix}_{direction}.log"
    //   PerPeer:           "{prefix}_{peer}.log"
    //   PerPeerDirection:  "{prefix}_{peer}_{direction}.log"
    //
    // Examples:
    //   "my_log.log"               — single file (Combined)
    //   "app_{direction}.log"      — "app_sent.log", "app_received.log"
    //   "{peer}_{direction}.log"   — "radar1_sent.log", "radar1_received.log"
    std::string filename;

    // Per-direction filename overrides. When set, these take priority over
    // 'filename' for the corresponding direction. Supports {peer} placeholder.
    // Useful with SeparateDirection or PerPeerDirection modes:
    //   sent_filename = "outbound.log";      received_filename = "inbound.log";
    //   sent_filename = "{peer}_out.log";     received_filename = "{peer}_in.log";
    std::string sent_filename;
    std::string received_filename;

    bool include_message_content = true; // Include to_string() output (has perf cost)
    bool include_raw_bytes = false;      // Include hex dump of wire bytes (opt-in)
};

// ============================================================================
// Queue Configuration
// ============================================================================

struct QueueConfig {
    size_t capacity = 1024;

    // Note: Using DropPolicy::Block for the rx_queue will cause the transport
    // I/O thread to stall when the queue is full, which can block all receive
    // processing. Prefer DropOldest or DropNewest for the rx_queue.
    queue::DropPolicy drop_policy = queue::DropPolicy::DropOldest;

    // Back-pressure: pause transport reading when queue fill exceeds threshold.
    // 0.0 = disabled (default), 0.8 = pause at 80% full.
    // Resumes when fill drops below (threshold * 0.8).
    double back_pressure_threshold = 0.0;
};

// ============================================================================
// Worker Configuration
// ============================================================================

struct WorkerConfig {
    size_t thread_count = 1;

    // If > 0, log a warning when any handler takes longer than this.
    // Does not interrupt the handler — warning only.
    std::chrono::milliseconds handler_timeout{0};
};

// ============================================================================
// Transport Configuration Variant
// ============================================================================

using TransportConfig = std::variant<
    transport::UdpConfig,
    transport::TcpClientConfig,
    transport::TcpServerConfig,
    transport::SerialConfig
>;

// ============================================================================
// SessionFactory: Creates ISession instances (for multi-peer transports)
// ============================================================================

using SessionFactory = std::function<std::unique_ptr<traits::ISession>()>;

// ============================================================================
// PeerConfig: Declarative peer definition
// ============================================================================

struct PeerConfig {
    std::string name;
    SessionFactory session_factory;
    TransportConfig transport;
};

// ============================================================================
// Transceiver Configuration
// ============================================================================

struct TransceiverConfig {
    QueueConfig rx_queue;
    WorkerConfig worker;
    std::vector<PeerConfig> peers;

    // Graceful shutdown: max time to wait for workers to drain.
    // 0 = wait indefinitely (current behavior).
    std::chrono::milliseconds shutdown_timeout{0};

    MessageLogConfig message_log;

    TransceiverConfig& add_peer(std::string name,
                                SessionFactory session_factory,
                                TransportConfig transport) {
        peers.push_back(PeerConfig{
            std::move(name),
            std::move(session_factory),
            std::move(transport)
        });
        return *this;
    }
};

} // namespace conduit::transceiver
