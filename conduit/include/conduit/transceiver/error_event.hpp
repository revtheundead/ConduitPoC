// SPDX-License-Identifier: MIT
// Conduit - Error Event for on_error() callback

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/transceiver/peer.hpp>
#include <functional>
#include <string>

namespace conduit::transceiver {

struct ErrorEvent {
    PeerId peer;                    // Which peer (may be invalid for global errors)
    std::string peer_name;          // Human-readable peer name
    std::string remote_endpoint;    // "ip:port" or serial device
    Error error;                    // The full conduit Error
};

using ErrorCallback = std::function<void(const ErrorEvent&)>;

} // namespace conduit::transceiver
