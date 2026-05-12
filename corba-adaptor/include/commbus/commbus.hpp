// SPDX-License-Identifier: MIT
//
// commbus/commbus.hpp
// ===================
//
// Umbrella header — `#include "commbus/commbus.hpp"` to get the full
// API surface.  The individual headers can also be included directly:
//
//   commbus/error.hpp          - error codes + Result aliases
//   commbus/slot.hpp           - Slot<T> cross-thread rendezvous
//   commbus/context.hpp        - Context (per-task handle)
//   commbus/wait_registry.hpp  - WaitRegistry<T> + WaitGuard<T>
//   commbus/bus.hpp            - CommBus + Config + Stats
//
// See the per-header docs for details.  See examples/commbus/*.cpp for
// end-to-end usage patterns.

#ifndef COMMBUS_HPP
#define COMMBUS_HPP

#include "error.hpp"
#include "slot.hpp"
#include "inbox.hpp"
#include "context.hpp"
#include "wait_registry.hpp"
#include "bus.hpp"

#endif
