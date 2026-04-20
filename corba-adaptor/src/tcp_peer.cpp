// SPDX-License-Identifier: MIT
// Adaptor - TCP Peer implementation (ACE-backed, C++11)

#include "adaptor/tcp_peer.hpp"
#include "adaptor/ace_tcp_client.hpp"

namespace adaptor {

TcpPeer::TcpPeer(const TcpPeerConfig& config, ACE_Reactor* reactor)
    : config_(config), reactor_(reactor), started_(false) {
    AceTcpClientConfig c;
    c.host = config_.host;
    c.port = config_.port;
    c.recv_buffer_size = config_.recv_buffer_size;
    c.connect_timeout_ms = config_.connect_timeout_ms;
    c.auto_reconnect = config_.auto_reconnect;
    c.initial_delay_ms = config_.initial_delay_ms;
    c.max_delay_ms = config_.max_delay_ms;
    c.backoff_multiplier = config_.backoff_multiplier;
    c.max_attempts = config_.max_attempts;
    client_.reset(new AceTcpClient(reactor_, c));
    client_->set_bytes_callback(
        std::bind(&TcpPeer::on_bytes, this,
                  std::placeholders::_1, std::placeholders::_2));
    client_->set_state_callback([this](bool connected) {
        on_state_change_internal(connected ? Connected : Disconnected);
    });
}

TcpPeer::~TcpPeer() {
    stop();
}

void TcpPeer::on_state_change(StateCallback cb) {
    state_callbacks_.push_back(cb);
}

void TcpPeer::start() {
    if (started_) return;
    started_ = true;
    on_state_change_internal(Connecting);
    client_->connect();
}

void TcpPeer::stop() {
    if (!started_) return;
    started_ = false;
    if (client_) client_->close();
}

bool TcpPeer::is_connected() const {
    return client_ && client_->is_connected();
}

ConnectionState TcpPeer::state() const {
    if (!started_) return Disconnected;
    if (client_ && client_->is_connected()) return Connected;
    return Connecting;
}

bool TcpPeer::send_bytes(const std::vector<uint8_t>& bytes) {
    if (!client_ || !client_->is_connected()) return false;
    return client_->send(bytes.data(), bytes.size()) == 0;
}

void TcpPeer::on_bytes(const uint8_t* data, std::size_t size) {
    bgen11::Result<std::vector<bgen11::traits::DecodedMessage> > result =
        session_.decode_frame(cpp11::span<const uint8_t>(data, size));
    if (!result) return;
    for (std::size_t i = 0; i < result->size(); ++i) {
        const bgen11::traits::DecodedMessage& dm = (*result)[i];
        std::map<uint64_t, TypedHandler>::iterator it = handlers_.find(dm.type_id);
        if (it != handlers_.end()) {
            it->second(dm.payload);
        }
    }
}

void TcpPeer::on_state_change_internal(ConnectionState s) {
    for (std::size_t i = 0; i < state_callbacks_.size(); ++i) {
        state_callbacks_[i](s);
    }
}

} // namespace adaptor
