// SPDX-License-Identifier: MIT
// Conduit - Handler Registry (implementation)

#include <conduit/transceiver/handler.hpp>
#include <conduit/transceiver/message_handler.hpp>
#include <conduit/logging/logger.hpp>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace conduit::transceiver {

void HandlerRegistry::register_handler(PeerId peer, ErasedHandler handler) {
    std::unique_lock lock(mutex_);
    HandlerKey key{peer, handler.type_id()};
    per_peer_handlers_.insert_or_assign(key, std::make_shared<const ErasedHandler>(std::move(handler)));
}

void HandlerRegistry::register_handler_all(ErasedHandler handler) {
    std::unique_lock lock(mutex_);
    auto type_id = handler.type_id();
    global_handlers_.insert_or_assign(type_id, std::make_shared<const ErasedHandler>(std::move(handler)));
}

bool HandlerRegistry::unregister_handler(PeerId peer, uint64_t type_id) {
    std::unique_lock lock(mutex_);
    return per_peer_handlers_.erase(HandlerKey{peer, type_id}) > 0;
}

bool HandlerRegistry::unregister_handler_all(uint64_t type_id) {
    std::unique_lock lock(mutex_);
    return global_handlers_.erase(type_id) > 0;
}

void HandlerRegistry::set_catch_all(PeerId peer,
                                     std::function<void(uint64_t, const std::any&)> cb) {
    std::unique_lock lock(mutex_);
    per_peer_catch_all_.insert_or_assign(peer.value(), std::make_shared<const CatchAllFn>(std::move(cb)));
}

void HandlerRegistry::set_catch_all(std::function<void(uint64_t, const std::any&)> cb) {
    std::unique_lock lock(mutex_);
    catch_all_ = std::make_shared<const CatchAllFn>(std::move(cb));
}

void HandlerRegistry::install_handler(PeerId peer, const MessageHandler& handler) {
    std::unique_lock lock(mutex_);
    for (const auto& h : handler.handlers()) {
        HandlerKey key{peer, h.type_id()};
        per_peer_handlers_.insert_or_assign(key, std::make_shared<const ErasedHandler>(h));
    }
    if (handler.catch_all()) {
        per_peer_catch_all_.insert_or_assign(peer.value(), std::make_shared<const CatchAllFn>(handler.catch_all()));
    }
}

void HandlerRegistry::install_handler(const MessageHandler& handler) {
    std::unique_lock lock(mutex_);
    for (const auto& h : handler.handlers()) {
        global_handlers_.insert_or_assign(h.type_id(), std::make_shared<const ErasedHandler>(h));
    }
    if (handler.catch_all()) {
        catch_all_ = std::make_shared<const CatchAllFn>(handler.catch_all());
    }
}

DispatchResult HandlerRegistry::dispatch(PeerId peer, uint64_t type_id, const std::any& payload) {
    std::shared_ptr<const ErasedHandler> handler_ptr;
    std::shared_ptr<const CatchAllFn> catch_all_ptr;

    {
        std::shared_lock lock(mutex_);

        auto it = per_peer_handlers_.find(HandlerKey{peer, type_id});
        if (it != per_peer_handlers_.end()) {
            handler_ptr = it->second;
        } else {
            auto git = global_handlers_.find(type_id);
            if (git != global_handlers_.end()) {
                handler_ptr = git->second;
            } else {
                auto cit = per_peer_catch_all_.find(peer.value());
                if (cit != per_peer_catch_all_.end()) {
                    catch_all_ptr = cit->second;
                } else if (catch_all_) {
                    catch_all_ptr = catch_all_;
                }
            }
        }
    }

    try {
        if (handler_ptr) {
            handler_ptr->invoke(payload);
            return DispatchResult::Handled;
        }
        if (catch_all_ptr) {
            (*catch_all_ptr)(type_id, payload);
            return DispatchResult::Handled;
        }
    } catch (const std::bad_any_cast& e) {
        LOG_ERRORF("Handler type mismatch for type_id={}: {}", type_id, e.what());
        return DispatchResult::Error;
    } catch (const std::exception& e) {
        LOG_ERRORF("Handler threw exception for type_id={}: {}", type_id, e.what());
        return DispatchResult::Error;
    } catch (...) {
        LOG_ERRORF("Handler threw unknown exception for type_id={}", type_id);
        return DispatchResult::Error;
    }

    return DispatchResult::NotFound;
}

} // namespace conduit::transceiver
