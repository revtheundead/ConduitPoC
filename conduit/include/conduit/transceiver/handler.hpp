// SPDX-License-Identifier: MIT
// Conduit - Handler Registry

#pragma once

#include <conduit/transceiver/peer.hpp>
#include <conduit/traits/codec_traits.hpp>
#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <span>
#include <unordered_map>

namespace conduit::transceiver {

class MessageHandler;  // forward declaration

enum class DispatchResult {
    Handled,     // Handler found and invoked successfully
    NotFound,    // No handler registered for this type_id/peer
    Error        // Handler found but threw an exception
};

// ============================================================================
// ErasedHandler: Type-erased message handler
// ============================================================================

class ErasedHandler {
public:
    // Typed handler (any_cast to T inside invoke).
    template<traits::Message T>
    explicit ErasedHandler(std::function<void(const T&)> cb)
        : invoke_([cb = std::move(cb)](const std::any& payload) {
              cb(std::any_cast<const T&>(payload));
          })
        , type_id_(T::TYPE_ID) {}

    // Raw handler for group/catch-all (no any_cast, passes payload directly).
    ErasedHandler(uint64_t type_id, std::function<void(const std::any&)> invoke)
        : invoke_(std::move(invoke))
        , type_id_(type_id) {}

    void invoke(const std::any& payload) const { invoke_(payload); }
    [[nodiscard]] uint64_t type_id() const noexcept { return type_id_; }

private:
    std::function<void(const std::any&)> invoke_;
    uint64_t type_id_;
};

// ============================================================================
// HandlerRegistry: Thread-safe handler lookup and dispatch
//
// Maps (type_id, peer) -> handler. Per-peer handlers override global handlers.
// Supports catch-all handlers for unmatched type_ids.
// Thread-safe: shared lock for dispatch, exclusive lock for registration.
// ============================================================================

class HandlerRegistry {
public:
    // Register a handler for a specific peer and message type.
    void register_handler(PeerId peer, ErasedHandler handler);

    // Register a global handler (applies to any peer without a per-peer override).
    void register_handler_all(ErasedHandler handler);

    // Unregister a per-peer handler for a given type_id.
    // Returns true if a handler was found and removed.
    bool unregister_handler(PeerId peer, uint64_t type_id);

    // Unregister a global handler for a given type_id.
    // Returns true if a handler was found and removed.
    bool unregister_handler_all(uint64_t type_id);

    // Set a per-peer catch-all handler (fires for unmatched type_ids from this peer).
    void set_catch_all(PeerId peer, std::function<void(uint64_t, const std::any&)> cb);

    // Set a global catch-all handler (fires for unmatched type_ids from any peer).
    void set_catch_all(std::function<void(uint64_t, const std::any&)> cb);

    // Install all handlers from a MessageHandler for a specific peer.
    void install_handler(PeerId peer, const MessageHandler& handler);

    // Install all handlers from a MessageHandler as global handlers.
    void install_handler(const MessageHandler& handler);

    // Remove all per-peer handlers and catch-all for a disconnected peer.
    // Called during peer cleanup to prevent unbounded handler map growth.
    void remove_peer(PeerId peer);

    // Dispatch a decoded message to the matching handler.
    // Returns Handled if a handler was found and invoked successfully,
    // NotFound if no handler matched, or Error if a handler threw.
    DispatchResult dispatch(PeerId peer, uint64_t type_id, const std::any& payload);

    // Dispatch with raw bytes available for catch-all handlers.
    // Typed handlers still receive the typed payload; only the raw catch-all
    // receives the raw frame bytes alongside the payload.
    DispatchResult dispatch(PeerId peer, uint64_t type_id,
                            const std::any& payload,
                            std::span<const uint8_t> raw);

    // Set a global raw catch-all that receives (peer, type_id, payload, raw_bytes).
    // This is used by the C ABI to forward raw bytes across the FFI boundary.
    using RawCatchAllFn = std::function<void(PeerId, uint64_t, const std::any&, std::span<const uint8_t>)>;
    void set_raw_catch_all(RawCatchAllFn cb);

private:
    struct HandlerKey {
        PeerId peer;
        uint64_t type_id;

        bool operator==(const HandlerKey&) const noexcept = default;
    };

    struct HandlerKeyHash {
        size_t operator()(const HandlerKey& k) const noexcept {
            auto h1 = std::hash<uint32_t>{}(k.peer.value());
            auto h2 = std::hash<uint64_t>{}(k.type_id);
            // Boost-style hash combine with size_t-appropriate golden ratio
            if constexpr (sizeof(size_t) >= 8) {
                h1 ^= h2 * size_t{0x9e3779b97f4a7c15} + (h1 << 6) + (h1 >> 2);
            } else {
                h1 ^= h2 * size_t{0x9e3779b9} + (h1 << 6) + (h1 >> 2);
            }
            return h1;
        }
    };

    using CatchAllFn = std::function<void(uint64_t, const std::any&)>;

    mutable std::shared_mutex mutex_;
    std::unordered_map<HandlerKey, std::shared_ptr<const ErasedHandler>, HandlerKeyHash> per_peer_handlers_;
    std::unordered_map<uint64_t, std::shared_ptr<const ErasedHandler>> global_handlers_;
    std::unordered_map<uint32_t, std::shared_ptr<const CatchAllFn>> per_peer_catch_all_;  // keyed by peer id
    std::shared_ptr<const CatchAllFn> catch_all_;
    std::shared_ptr<const RawCatchAllFn> raw_catch_all_;
};

} // namespace conduit::transceiver
