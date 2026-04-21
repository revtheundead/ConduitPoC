// SPDX-License-Identifier: MIT
// Adaptor - TCP Peer implementation (C++11)

#include "adaptor/tcp_peer.hpp"
#include "adaptor/ace_tcp_client.hpp"
#include "adaptor/ace_tcp_client_standalone.hpp"

namespace adaptor {

TcpPeer::TcpPeer(const TcpPeerConfig& config, ACE_Reactor* reactor)
    : config_(config), framer_(session_), started_(false) {
    client_.reset(new AceTcpClient(reactor, config_));
    client_->set_bytes_callback(
        std::bind(&TcpPeer::on_bytes, this,
                  std::placeholders::_1, std::placeholders::_2));
    client_->set_state_callback([this](bool connected) {
        on_state_change_internal(connected ? Connected : Disconnected);
    });
}

TcpPeer::TcpPeer(const TcpPeerConfig& config)
    : config_(config), framer_(session_), started_(false) {
    client_.reset(new AceTcpClientStandalone(config_));
    client_->set_bytes_callback(
        std::bind(&TcpPeer::on_bytes, this,
                  std::placeholders::_1, std::placeholders::_2));
    client_->set_state_callback([this](bool connected) {
        on_state_change_internal(connected ? Connected : Disconnected);
    });
}

TcpPeer::TcpPeer(const TcpPeerConfig& config,
                 std::unique_ptr<ITcpClient> client)
    : config_(config), framer_(session_),
      client_(std::move(client)), started_(false) {
    if (client_) {
        client_->set_bytes_callback(
            std::bind(&TcpPeer::on_bytes, this,
                      std::placeholders::_1, std::placeholders::_2));
        client_->set_state_callback([this](bool connected) {
            on_state_change_internal(connected ? Connected : Disconnected);
        });
    }
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
    if (client_) client_->connect();
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
    // TCP is a byte stream: the chunk delivered by the client backend
    // is not guaranteed to start or end on a frame boundary.  The
    // StreamFramer buffers across calls and emits only complete frames.
    bgen11::Result<std::vector<std::vector<uint8_t> > > frames_r =
        framer_.push_data(cpp11::span<const uint8_t>(data, size));
    if (!frames_r) {
        // Framing error (buffer overflow).  Reset and drop; the TCP
        // peer will have its state propagated separately if the link
        // itself is unhealthy.
        framer_.reset();
        return;
    }
    const std::vector<std::vector<uint8_t> >& frames = *frames_r;
    for (std::size_t fi = 0; fi < frames.size(); ++fi) {
        const std::vector<uint8_t>& frame = frames[fi];
        bgen11::Result<std::vector<bgen11::traits::DecodedMessage> > dec =
            session_.decode_frame(
                cpp11::span<const uint8_t>(frame.data(), frame.size()));
        if (!dec) continue;  // malformed frame; skip, keep framing

        for (std::size_t i = 0; i < dec->size(); ++i) {
            const bgen11::traits::DecodedMessage& dm = (*dec)[i];
            std::map<uint64_t, TypedHandler>::iterator it =
                handlers_.find(dm.type_id);
            if (it != handlers_.end()) {
                it->second(dm.payload);
            }
        }
    }
}

void TcpPeer::on_state_change_internal(ConnectionState s) {
    for (std::size_t i = 0; i < state_callbacks_.size(); ++i) {
        state_callbacks_[i](s);
    }
}

} // namespace adaptor
