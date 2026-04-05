// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Session Factory
//
// Factory functions that create ISession instances for each peer.
// These wrap the bgen-generated session classes.
//
// When the BMDL files are finalized and bgen generates the real sessions,
// uncomment the includes and return the generated session instances.
// Until then, a NullSession stub is used as a placeholder.

#pragma once

#include <conduit/traits/session_traits.hpp>

#include <memory>

namespace adaptor::codec {

/// Create the codec session for the TCP peer protocol.
/// Uses the bgen-generated TcpPeerFrameSession.
[[nodiscard]] std::unique_ptr<conduit::traits::ISession> create_tcp_peer_session();

/// Create the codec session for the CORBA peer protocol.
/// Uses the bgen-generated CorbaPeerFrameSession.
[[nodiscard]] std::unique_ptr<conduit::traits::ISession> create_corba_peer_session();

} // namespace adaptor::codec
