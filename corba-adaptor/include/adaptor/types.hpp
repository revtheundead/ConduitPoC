// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Common Types (C++11)

#ifndef ADAPTOR_TYPES_HPP
#define ADAPTOR_TYPES_HPP

#include <cstdint>
#include <functional>

#include "compat11/span.hpp"

namespace adaptor {

typedef std::function<void(cpp11::span<const std::uint8_t>)> RawBytesCallback;
typedef std::function<void(bool /*connected*/)> PeerStateCallback;

} // namespace adaptor

#endif
