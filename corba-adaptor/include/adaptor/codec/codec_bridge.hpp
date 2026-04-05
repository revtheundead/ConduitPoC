// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Codec Bridge
//
// Bridges the Conduit codec layer (bgen-generated sessions) with the
// adaptor's raw byte streams.  Each peer gets a CodecPipeline that
// decodes inbound bytes into typed messages and encodes outbound
// messages into wire bytes.
//
// For the TCP peer:  full transport + codec (the TCP connection carries
//   framed binary data that Conduit decodes/encodes).
//
// For the CORBA peer:  codec only (CORBA handles transport; we decode/
//   encode the raw bytes that flow through RawDataChannel).

#pragma once

#include <adaptor/types.hpp>

#include <conduit/traits/session_traits.hpp>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace adaptor::codec {

// ============================================================================
// Decoded message (adaptor-level, wraps conduit::traits::DecodedMessage)
// ============================================================================

struct AdaptorMessage {
    PeerId          source{};
    uint64_t        type_id{};
    std::string     type_name;
    std::any        payload;        // Typed message (e.g. tcp_peer::Heartbeat)
    ByteBuffer      raw_bytes;      // Original wire bytes
};

// ============================================================================
// Callbacks
// ============================================================================

/// Called when the codec produces a decoded message.
using MessageDecodedCallback = std::function<void(AdaptorMessage)>;

/// Called when a codec error occurs.
using CodecErrorCallback = std::function<void(PeerId, const std::string& error)>;

// ============================================================================
// CodecPipeline — per-peer codec instance
// ============================================================================

class CodecPipeline {
public:
    /// Construct a pipeline for a specific peer using its session factory.
    CodecPipeline(PeerId peer_id,
                  std::unique_ptr<conduit::traits::ISession> session);
    ~CodecPipeline();

    CodecPipeline(const CodecPipeline&) = delete;
    CodecPipeline& operator=(const CodecPipeline&) = delete;

    /// Set callbacks.
    void set_message_callback(MessageDecodedCallback cb);
    void set_error_callback(CodecErrorCallback cb);

    /// Feed raw inbound bytes into the codec.  Decoded messages are
    /// delivered via the MessageDecodedCallback.
    void on_bytes_received(std::span<const uint8_t> data);

    /// Encode a typed message into wire bytes for outbound transmission.
    /// @param type_id  The message type ID (from generated TYPE_ID constant).
    /// @param payload  The typed message object (e.g. tcp_peer::CommandRequest).
    /// @return Encoded wire bytes, or empty on failure.
    [[nodiscard]] ByteBuffer encode(uint64_t type_id, const std::any& payload);

    /// Encode a raw byte buffer (pass-through, no codec processing).
    [[nodiscard]] ByteBuffer encode_raw(std::span<const uint8_t> data);

    /// Get the session (for type introspection, format_message, etc.).
    [[nodiscard]] conduit::traits::ISession& session() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// CodecBridge — coordinates codecs for both peers
// ============================================================================

class CodecBridge {
public:
    CodecBridge();
    ~CodecBridge();

    CodecBridge(const CodecBridge&) = delete;
    CodecBridge& operator=(const CodecBridge&) = delete;

    /// Set the pipeline for a specific peer.
    void set_pipeline(PeerId peer_id, std::unique_ptr<CodecPipeline> pipeline);

    /// Get the pipeline for a specific peer (may be null if not set).
    [[nodiscard]] CodecPipeline* pipeline(PeerId peer_id) noexcept;

    /// Set global message callback (receives decoded messages from all peers).
    void set_message_callback(MessageDecodedCallback cb);

    /// Set global error callback.
    void set_error_callback(CodecErrorCallback cb);

    /// Feed raw bytes from a specific peer into its codec pipeline.
    void on_bytes_received(PeerId peer_id, std::span<const uint8_t> data);

    /// Encode a message for a specific peer.
    [[nodiscard]] ByteBuffer encode(PeerId peer_id,
                                     uint64_t type_id,
                                     const std::any& payload);

private:
    std::unique_ptr<CodecPipeline> tcp_pipeline_;
    std::unique_ptr<CodecPipeline> corba_pipeline_;

    MessageDecodedCallback  message_cb_;
    CodecErrorCallback      error_cb_;
};

} // namespace adaptor::codec
