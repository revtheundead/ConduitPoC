// SPDX-License-Identifier: MIT
// Conduit - Connection State

#pragma once

#include <string_view>

namespace conduit::net {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed
};

[[nodiscard]] constexpr std::string_view to_string(ConnectionState state) noexcept {
    switch (state) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Connecting:   return "Connecting";
        case ConnectionState::Connected:    return "Connected";
        case ConnectionState::Reconnecting: return "Reconnecting";
        case ConnectionState::Failed:       return "Failed";
    }
    return "Unknown";
}

} // namespace conduit::net
