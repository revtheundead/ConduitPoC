// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - TCP Peer Implementation

#include <adaptor/tcp_peer.hpp>

#include <ace/Log_Msg.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace adaptor {

namespace {

conduit::transceiver::transport::TcpClientConfig
make_tcp_client_config(const TcpPeerConfig& cfg) {
    conduit::transceiver::transport::TcpClientConfig out;
    out.host             = cfg.host;
    out.port             = cfg.port;
    out.recv_buffer_size = cfg.recv_buffer_size;
    out.connect_timeout  = cfg.connect_timeout;

    out.reconnect.enabled            = cfg.auto_reconnect;
    out.reconnect.initial_delay      = cfg.initial_delay;
    out.reconnect.max_delay          = cfg.max_delay;
    out.reconnect.backoff_multiplier = cfg.backoff_multiplier;
    out.reconnect.max_attempts       = cfg.max_attempts;
    return out;
}

} // namespace

// ============================================================================
// TcpPeer
// ============================================================================

TcpPeer::TcpPeer(TcpPeerConfig config)
    : config_(std::move(config))
    , tx_(conduit::transceiver::TransceiverConfig{})
{}

TcpPeer::~TcpPeer() {
    stop();
}

void TcpPeer::on_state_change(StateCallback cb) {
    (void)tx_.on_state_change(
        [cb = std::move(cb)](conduit::transceiver::PeerId,
                             conduit::net::ConnectionState state) {
            cb(state);
        });
}

void TcpPeer::start() {
    if (started_) return;

    auto session   = std::make_unique<tcp_peer::TcpPeerFrameSession>();
    auto transport = std::make_shared<
        conduit::transceiver::transport::TcpClientTransport>(
            make_tcp_client_config(config_));

    auto peer_result = tx_.add_peer(
        config_.name, std::move(session), std::move(transport));
    if (!peer_result) {
        throw std::runtime_error(
            "TcpPeer: add_peer failed: "
            + peer_result.error().format_short());
    }
    peer_id_ = *peer_result;

    auto start_result = tx_.start();
    if (!start_result) {
        throw std::runtime_error(
            "TcpPeer: transceiver start failed: "
            + start_result.error().format_short());
    }

    started_ = true;

    ACE_DEBUG((LM_INFO,
        "TcpPeer: started (host=%s port=%u peer_id=%u)\n",
        config_.host.c_str(),
        static_cast<unsigned>(config_.port),
        peer_id_.value()));
}

void TcpPeer::stop() {
    if (!started_) return;
    tx_.stop();
    started_ = false;
    ACE_DEBUG((LM_INFO, "TcpPeer: stopped\n"));
}

bool TcpPeer::is_connected() const noexcept {
    return state() == conduit::net::ConnectionState::Connected;
}

conduit::net::ConnectionState TcpPeer::state() const noexcept {
    if (!peer_id_.valid()) {
        return conduit::net::ConnectionState::Disconnected;
    }
    return tx_.peer_state(peer_id_);
}

} // namespace adaptor
