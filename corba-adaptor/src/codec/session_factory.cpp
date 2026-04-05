// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Session Factory Implementation
//
// TODO: Once bgen generates the real session classes from the BMDL files,
//       replace the NullSession placeholders with:
//
//   #include <tcp-peer/sessions.hpp>
//   #include <corba-peer/sessions.hpp>
//
//   return std::make_unique<tcp_peer::TcpPeerFrameSession>();
//   return std::make_unique<corba_peer::CorbaPeerFrameSession>();

#include <adaptor/codec/session_factory.hpp>

#include <ace/Log_Msg.h>

#include <span>
#include <string_view>
#include <vector>

namespace adaptor::codec {

// ============================================================================
// NullSession — placeholder until bgen generates real sessions
// ============================================================================

namespace {

class NullSession : public conduit::traits::ISession {
public:
    explicit NullSession(std::string_view name) : name_(name) {}

    conduit::Result<std::vector<conduit::traits::DecodedMessage>>
    decode_frame(std::span<const uint8_t>) override {
        ACE_DEBUG((LM_WARNING, "NullSession(%s): decode_frame called on placeholder\n",
                   name_.c_str()));
        return std::vector<conduit::traits::DecodedMessage>{};
    }

    conduit::Result<conduit::traits::EncodeResult>
    encode_wrap(uint64_t, const std::any&) override {
        ACE_DEBUG((LM_WARNING, "NullSession(%s): encode_wrap called on placeholder\n",
                   name_.c_str()));
        return std::unexpected(conduit::Error(
            conduit::ErrorCode::NotSupported,
            "placeholder session — run bgen to generate real codec"));
    }

    std::span<const uint8_t> sync_pattern() const override {
        return {};
    }

    size_t min_frame_header_size() const override {
        return 0;
    }

    size_t extract_frame_length(std::span<const uint8_t>) const override {
        return 0;
    }

    std::span<const uint64_t> leaf_type_ids() const override {
        return {};
    }

    std::string_view type_name(uint64_t) const override {
        return "unknown";
    }

private:
    std::string name_;
};

} // anonymous namespace

// ============================================================================
// Factory functions
// ============================================================================

std::unique_ptr<conduit::traits::ISession> create_tcp_peer_session() {
    // TODO: Replace with bgen-generated session:
    //   return std::make_unique<tcp_peer::TcpPeerFrameSession>();
    return std::make_unique<NullSession>("tcp-peer");
}

std::unique_ptr<conduit::traits::ISession> create_corba_peer_session() {
    // TODO: Replace with bgen-generated session:
    //   return std::make_unique<corba_peer::CorbaPeerFrameSession>();
    return std::make_unique<NullSession>("corba-peer");
}

} // namespace adaptor::codec
