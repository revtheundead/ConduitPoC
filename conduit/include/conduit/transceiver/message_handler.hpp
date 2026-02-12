// SPDX-License-Identifier: MIT
// Conduit - MessageHandler (User-Facing Handler Builder)

#pragma once

#include <conduit/transceiver/handler.hpp>
#include <conduit/traits/codec_traits.hpp>
#include <any>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

namespace conduit::transceiver {

// ============================================================================
// MessageHandler: Fluent builder for typed message handlers
//
// Collects per-type, group, and catch-all handlers, then installed into
// the HandlerRegistry via set_handler() on the Transceiver.
//
// Dispatch priority: typed > group > catch-all.
// ============================================================================

class MessageHandler {
public:
    // Register a typed handler for a specific message type.
    template<traits::Message T>
    MessageHandler& on(std::function<void(const T&)> cb) {
        handlers_.emplace_back(std::move(cb));
        return *this;
    }

    // Register a handler for a set of type_ids (group handler).
    // The callback receives the type_id and the raw std::any payload.
    MessageHandler& on_group(std::initializer_list<uint64_t> type_ids,
                             std::function<void(uint64_t, const std::any&)> cb) {
        for (auto tid : type_ids) {
            handlers_.emplace_back(tid,
                [cb, tid](const std::any& p) { cb(tid, p); });
        }
        return *this;
    }

    // Register a catch-all handler for types without a typed or group handler.
    MessageHandler& on_any(std::function<void(uint64_t, const std::any&)> cb) {
        catch_all_ = std::move(cb);
        return *this;
    }

    [[nodiscard]] const std::vector<ErasedHandler>& handlers() const { return handlers_; }
    [[nodiscard]] const std::function<void(uint64_t, const std::any&)>& catch_all() const {
        return catch_all_;
    }

private:
    std::vector<ErasedHandler> handlers_;
    std::function<void(uint64_t, const std::any&)> catch_all_;
};

} // namespace conduit::transceiver
