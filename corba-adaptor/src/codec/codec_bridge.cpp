// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Codec Bridge Implementation

#include <adaptor/codec/codec_bridge.hpp>

#include <ace/Log_Msg.h>

namespace adaptor::codec {

// ============================================================================
// CodecPipeline::Impl
// ============================================================================

struct CodecPipeline::Impl {
    PeerId                                      peer_id;
    std::unique_ptr<conduit::traits::ISession>  session;
    MessageDecodedCallback                      message_cb;
    CodecErrorCallback                          error_cb;

    // Stream framing buffer for stream-oriented protocols (TCP).
    // For message-oriented protocols (CORBA) we decode whole messages.
    ByteBuffer                                  frame_buf;

    Impl(PeerId id, std::unique_ptr<conduit::traits::ISession> sess)
        : peer_id(id)
        , session(std::move(sess))
    {
    }

    void on_bytes_received(std::span<const uint8_t> data) {
        // Append to frame buffer
        frame_buf.insert(frame_buf.end(), data.begin(), data.end());

        // Try to extract complete frames
        while (frame_buf.size() >= session->min_frame_header_size()) {
            auto frame_len = session->extract_frame_length(
                std::span<const uint8_t>(frame_buf));

            if (frame_len == 0) {
                // Not enough data for length extraction — wait for more
                break;
            }

            if (frame_buf.size() < frame_len) {
                // Incomplete frame — wait for more data
                break;
            }

            // Full frame available — decode it
            auto frame_data = std::span<const uint8_t>(
                frame_buf.data(), frame_len);

            auto result = session->decode_frame(frame_data);
            if (result) {
                for (auto& msg : *result) {
                    if (message_cb) {
                        AdaptorMessage am;
                        am.source    = peer_id;
                        am.type_id   = msg.type_id;
                        am.type_name = std::string(msg.type_name);
                        am.payload   = std::move(msg.payload);
                        am.raw_bytes = ByteBuffer(frame_data.begin(),
                                                   frame_data.end());
                        message_cb(std::move(am));
                    }
                }
            } else {
                if (error_cb) {
                    error_cb(peer_id, result.error().message());
                }
            }

            // Remove consumed bytes
            frame_buf.erase(frame_buf.begin(),
                           frame_buf.begin() + static_cast<ptrdiff_t>(frame_len));
        }
    }

    ByteBuffer encode(uint64_t type_id, const std::any& payload) {
        auto result = session->encode_wrap(type_id, payload);
        if (result) {
            return ByteBuffer(result->data.begin(), result->data.end());
        }

        if (error_cb) {
            error_cb(peer_id, result.error().message());
        }
        return {};
    }
};

// ============================================================================
// CodecPipeline
// ============================================================================

CodecPipeline::CodecPipeline(
    PeerId peer_id,
    std::unique_ptr<conduit::traits::ISession> session)
    : impl_(std::make_unique<Impl>(peer_id, std::move(session)))
{
}

CodecPipeline::~CodecPipeline() = default;

void CodecPipeline::set_message_callback(MessageDecodedCallback cb) {
    impl_->message_cb = std::move(cb);
}

void CodecPipeline::set_error_callback(CodecErrorCallback cb) {
    impl_->error_cb = std::move(cb);
}

void CodecPipeline::on_bytes_received(std::span<const uint8_t> data) {
    impl_->on_bytes_received(data);
}

ByteBuffer CodecPipeline::encode(uint64_t type_id, const std::any& payload) {
    return impl_->encode(type_id, payload);
}

ByteBuffer CodecPipeline::encode_raw(std::span<const uint8_t> data) {
    return ByteBuffer(data.begin(), data.end());
}

conduit::traits::ISession& CodecPipeline::session() noexcept {
    return *impl_->session;
}

// ============================================================================
// CodecBridge
// ============================================================================

CodecBridge::CodecBridge() = default;
CodecBridge::~CodecBridge() = default;

void CodecBridge::set_pipeline(PeerId peer_id,
                                std::unique_ptr<CodecPipeline> pipeline) {
    // Wire up the bridge-level callbacks
    if (pipeline) {
        pipeline->set_message_callback([this](AdaptorMessage msg) {
            if (message_cb_) message_cb_(std::move(msg));
        });
        pipeline->set_error_callback([this](PeerId id, const std::string& err) {
            if (error_cb_) error_cb_(id, err);
        });
    }

    switch (peer_id) {
        case PeerId::tcp_peer:   tcp_pipeline_ = std::move(pipeline); break;
        case PeerId::corba_peer: corba_pipeline_ = std::move(pipeline); break;
    }
}

CodecPipeline* CodecBridge::pipeline(PeerId peer_id) noexcept {
    switch (peer_id) {
        case PeerId::tcp_peer:   return tcp_pipeline_.get();
        case PeerId::corba_peer: return corba_pipeline_.get();
    }
    return nullptr;
}

void CodecBridge::set_message_callback(MessageDecodedCallback cb) {
    message_cb_ = std::move(cb);
}

void CodecBridge::set_error_callback(CodecErrorCallback cb) {
    error_cb_ = std::move(cb);
}

void CodecBridge::on_bytes_received(PeerId peer_id,
                                     std::span<const uint8_t> data) {
    if (auto* p = pipeline(peer_id)) {
        p->on_bytes_received(data);
    } else {
        ACE_DEBUG((LM_WARNING,
                   "CodecBridge: no pipeline for peer '%s', dropping %u bytes\n",
                   to_string(peer_id), static_cast<unsigned>(data.size())));
    }
}

ByteBuffer CodecBridge::encode(PeerId peer_id,
                                uint64_t type_id,
                                const std::any& payload) {
    if (auto* p = pipeline(peer_id)) {
        return p->encode(type_id, payload);
    }
    return {};
}

} // namespace adaptor::codec
