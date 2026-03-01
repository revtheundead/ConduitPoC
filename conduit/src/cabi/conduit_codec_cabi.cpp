// SPDX-License-Identifier: MIT
// Conduit Codec-Only C ABI Implementation

#define CONDUIT_CODEC_CABI_EXPORTS

#include <conduit/cabi/conduit_codec_cabi.h>
#include <conduit/traits/session_traits.hpp>
#include <conduit/transceiver/stream_framer.hpp>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Session registry — maps string names to session factory functions
// ============================================================================

namespace {

struct SessionRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, conduit_session_factory_fn> factories;

    static SessionRegistry& instance() {
        static SessionRegistry reg;
        return reg;
    }
};

} // namespace

// ============================================================================
// Opaque handle wrappers
// ============================================================================

struct conduit_session {
    std::unique_ptr<conduit::traits::ISession> session;
    // Cache for leaf type IDs (returned as pointer from introspection)
    std::vector<uint64_t> cached_leaf_ids;
    // Cache for type name strings (need stable storage for C string pointers)
    std::unordered_map<uint64_t, std::string> cached_type_names;
    std::string cached_protocol_name;
};

struct conduit_framer {
    conduit::transceiver::StreamFramer framer;
    // Storage for extracted frames (kept alive until next call or free)
    std::vector<std::vector<uint8_t>> extracted_frames;

    explicit conduit_framer(conduit::traits::ISession& session)
        : framer(session) {}
};

// ============================================================================
// Helper: map C++ Error to conduit_error_t
// ============================================================================

namespace {

conduit_error_t map_error(const conduit::Error& err) {
    using EC = conduit::ErrorCode;
    switch (err.code()) {
        case EC::UnknownTypeId:
        case EC::UnknownEnumValue:
        case EC::UnknownDiscriminator:
            return CONDUIT_ERR_UNKNOWN_TYPE;
        case EC::BatchNotSupported:
            return CONDUIT_ERR_BATCH_NOT_SUPPORTED;
        default:
            if (err.is_decode_error()) return CONDUIT_ERR_DECODE_FAILED;
            if (err.is_encode_error()) return CONDUIT_ERR_ENCODE_FAILED;
            return CONDUIT_ERR_UNKNOWN;
    }
}

} // namespace

// ============================================================================
// Session lifecycle
// ============================================================================

extern "C" {

CONDUIT_CODEC_API conduit_session_t* conduit_session_create(const char* session_type) {
    if (!session_type) return nullptr;

    auto& reg = SessionRegistry::instance();
    std::lock_guard lock(reg.mutex);

    auto it = reg.factories.find(session_type);
    if (it == reg.factories.end()) return nullptr;

    auto* raw = it->second();
    if (!raw) return nullptr;

    auto* wrapper = new (std::nothrow) conduit_session{};
    if (!wrapper) {
        delete static_cast<conduit::traits::ISession*>(raw);
        return nullptr;
    }

    wrapper->session.reset(static_cast<conduit::traits::ISession*>(raw));

    // Cache leaf type IDs
    auto ids = wrapper->session->leaf_type_ids();
    wrapper->cached_leaf_ids.assign(ids.begin(), ids.end());

    // Cache type names
    for (auto id : wrapper->cached_leaf_ids) {
        auto name = wrapper->session->type_name(id);
        wrapper->cached_type_names[id] = std::string(name);
    }

    // Cache protocol name
    wrapper->cached_protocol_name = std::string(wrapper->session->protocol_name());

    return wrapper;
}

CONDUIT_CODEC_API void conduit_session_destroy(conduit_session_t* session) {
    delete session;
}

CONDUIT_CODEC_API void conduit_session_reset(conduit_session_t* session) {
    if (session && session->session) {
        session->session->reset();
    }
}

// ============================================================================
// Decode
// ============================================================================

CONDUIT_CODEC_API conduit_error_t conduit_decode_frame(
    conduit_session_t* session,
    const uint8_t* data, size_t len,
    conduit_decoded_msg_t** out_msgs, size_t* out_count) {

    if (!session || !session->session || !data || !out_msgs || !out_count)
        return CONDUIT_ERR_UNKNOWN;

    *out_msgs = nullptr;
    *out_count = 0;

    auto result = session->session->decode_frame({data, len});
    if (!result) {
        return map_error(result.error());
    }

    auto& msgs = *result;
    if (msgs.empty()) {
        return CONDUIT_OK;
    }

    // Allocate output array
    auto* out = static_cast<conduit_decoded_msg_t*>(
        std::malloc(msgs.size() * sizeof(conduit_decoded_msg_t)));
    if (!out) return CONDUIT_ERR_UNKNOWN;

    for (size_t i = 0; i < msgs.size(); i++) {
        out[i].type_id = msgs[i].type_id;

        // Use cached type name for stable pointer
        auto name_it = session->cached_type_names.find(msgs[i].type_id);
        if (name_it != session->cached_type_names.end()) {
            out[i].type_name = name_it->second.c_str();
        } else {
            out[i].type_name = "";
        }

        // Copy raw bytes
        if (!msgs[i].raw.empty()) {
            auto* raw_copy = static_cast<uint8_t*>(std::malloc(msgs[i].raw.size()));
            if (!raw_copy) {
                // Free previously allocated raw copies
                for (size_t j = 0; j < i; j++) {
                    std::free(const_cast<uint8_t*>(out[j].data));
                }
                std::free(out);
                return CONDUIT_ERR_UNKNOWN;
            }
            std::memcpy(raw_copy, msgs[i].raw.data(), msgs[i].raw.size());
            out[i].data = raw_copy;
            out[i].data_len = msgs[i].raw.size();
        } else {
            out[i].data = nullptr;
            out[i].data_len = 0;
        }
    }

    *out_msgs = out;
    *out_count = msgs.size();
    return CONDUIT_OK;
}

CONDUIT_CODEC_API void conduit_free_decoded_msgs(conduit_decoded_msg_t* msgs, size_t count) {
    if (!msgs) return;
    for (size_t i = 0; i < count; i++) {
        std::free(const_cast<uint8_t*>(msgs[i].data));
    }
    std::free(msgs);
}

// ============================================================================
// Encode
// ============================================================================

CONDUIT_CODEC_API conduit_error_t conduit_encode_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    conduit_encode_result_t* out_result) {

    if (!session || !session->session || !out_result)
        return CONDUIT_ERR_UNKNOWN;

    out_result->data = nullptr;
    out_result->data_len = 0;

    // The payload bytes are the raw serialized message fields.
    // We wrap them via encode_wrap which handles frame wrapping.
    std::any any_payload;

    // For the codec C ABI, we pass raw bytes through encode_wrap.
    // The session's encode_wrap expects a typed payload wrapped in std::any.
    // Since we receive raw bytes, we need to first decode, then re-encode.
    // This is the codec-only path: decode the payload bytes to get the typed
    // message, then pass that to encode_wrap.

    // First, try to decode the raw payload bytes as the message type
    // by calling decode_frame with just the payload.
    // Actually, for the C ABI, the simplest approach is to wrap the raw
    // bytes directly. The session's decode_frame handles frame bytes.
    // For encode, we need the typed payload.

    // Since we can't construct typed messages from raw bytes without
    // knowing the type, we pass the raw bytes as a vector<uint8_t> any.
    // The session's encode_wrap must handle this case, or we provide
    // a raw-bytes encode path.

    // For now, wrap the raw bytes in a vector<uint8_t> any:
    std::vector<uint8_t> raw_payload(payload, payload + payload_len);
    auto result = session->session->encode_wrap(type_id, std::any(raw_payload));

    if (!result) {
        return map_error(result.error());
    }

    auto& enc = *result;
    if (!enc.bytes.empty()) {
        auto* data_copy = static_cast<uint8_t*>(std::malloc(enc.bytes.size()));
        if (!data_copy) return CONDUIT_ERR_UNKNOWN;
        std::memcpy(data_copy, enc.bytes.data(), enc.bytes.size());
        out_result->data = data_copy;
        out_result->data_len = enc.bytes.size();
    }

    return CONDUIT_OK;
}

CONDUIT_CODEC_API void conduit_free_encode_result(conduit_encode_result_t* result) {
    if (result && result->data) {
        std::free(result->data);
        result->data = nullptr;
        result->data_len = 0;
    }
}

// ============================================================================
// Batch encode
// ============================================================================

CONDUIT_CODEC_API conduit_error_t conduit_encode_batch(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* payload_lens, size_t count,
    conduit_encode_result_t* out_result) {

    if (!session || !session->session || !out_result)
        return CONDUIT_ERR_UNKNOWN;
    if (count > 0 && (!payloads || !payload_lens))
        return CONDUIT_ERR_UNKNOWN;

    out_result->data = nullptr;
    out_result->data_len = 0;

    std::vector<std::any> any_payloads;
    any_payloads.reserve(count);
    for (size_t i = 0; i < count; i++) {
        std::vector<uint8_t> raw(payloads[i], payloads[i] + payload_lens[i]);
        any_payloads.emplace_back(std::move(raw));
    }

    auto result = session->session->encode_batch(type_id, any_payloads);
    if (!result) {
        return map_error(result.error());
    }

    auto& enc = *result;
    if (!enc.bytes.empty()) {
        auto* data_copy = static_cast<uint8_t*>(std::malloc(enc.bytes.size()));
        if (!data_copy) return CONDUIT_ERR_UNKNOWN;
        std::memcpy(data_copy, enc.bytes.data(), enc.bytes.size());
        out_result->data = data_copy;
        out_result->data_len = enc.bytes.size();
    }

    return CONDUIT_OK;
}

// ============================================================================
// Introspection
// ============================================================================

CONDUIT_CODEC_API const char* conduit_session_type_name(
    conduit_session_t* session, uint64_t type_id) {
    if (!session) return "";
    auto it = session->cached_type_names.find(type_id);
    if (it != session->cached_type_names.end()) return it->second.c_str();
    return "";
}

CONDUIT_CODEC_API size_t conduit_session_leaf_type_count(conduit_session_t* session) {
    if (!session) return 0;
    return session->cached_leaf_ids.size();
}

CONDUIT_CODEC_API const uint64_t* conduit_session_leaf_type_ids(conduit_session_t* session) {
    if (!session || session->cached_leaf_ids.empty()) return nullptr;
    return session->cached_leaf_ids.data();
}

CONDUIT_CODEC_API int conduit_session_is_receive_only(
    conduit_session_t* session, uint64_t type_id) {
    if (!session || !session->session) return 0;
    return session->session->is_receive_only(type_id) ? 1 : 0;
}

CONDUIT_CODEC_API const char* conduit_session_protocol_name(conduit_session_t* session) {
    if (!session) return "";
    return session->cached_protocol_name.c_str();
}

// ============================================================================
// Human-readable formatting
// ============================================================================

CONDUIT_CODEC_API conduit_error_t conduit_format_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    char* buf, size_t buf_len, size_t* out_written) {

    if (!session || !session->session || !buf || !out_written || buf_len == 0)
        return CONDUIT_ERR_UNKNOWN;

    *out_written = 0;

    // First decode the raw bytes to get a typed payload
    auto decode_result = session->session->decode_frame({payload, payload_len});
    if (!decode_result || decode_result->empty()) {
        return CONDUIT_ERR_DECODE_FAILED;
    }

    // Find the message with matching type_id
    for (const auto& msg : *decode_result) {
        if (msg.type_id == type_id) {
            auto formatted = session->session->format_message(type_id, msg.payload);
            size_t copy_len = std::min(formatted.size(), buf_len - 1);
            std::memcpy(buf, formatted.c_str(), copy_len);
            buf[copy_len] = '\0';
            *out_written = copy_len;
            return CONDUIT_OK;
        }
    }

    return CONDUIT_ERR_UNKNOWN_TYPE;
}

// ============================================================================
// Stream framing
// ============================================================================

CONDUIT_CODEC_API conduit_framer_t* conduit_framer_create(conduit_session_t* session) {
    if (!session || !session->session) return nullptr;
    return new (std::nothrow) conduit_framer(*session->session);
}

CONDUIT_CODEC_API void conduit_framer_destroy(conduit_framer_t* framer) {
    delete framer;
}

CONDUIT_CODEC_API conduit_error_t conduit_framer_feed(
    conduit_framer_t* framer,
    const uint8_t* data, size_t len,
    conduit_frame_t** out_frames, size_t* out_count) {

    if (!framer || !out_frames || !out_count)
        return CONDUIT_ERR_UNKNOWN;
    if (!data && len > 0)
        return CONDUIT_ERR_UNKNOWN;

    *out_frames = nullptr;
    *out_count = 0;

    auto result = framer->framer.push_data({data, len});
    if (!result) {
        return CONDUIT_ERR_DECODE_FAILED;
    }

    // Store extracted frames in the framer (keeps data alive)
    framer->extracted_frames = std::move(*result);

    if (framer->extracted_frames.empty()) {
        return CONDUIT_OK;
    }

    auto* out = static_cast<conduit_frame_t*>(
        std::malloc(framer->extracted_frames.size() * sizeof(conduit_frame_t)));
    if (!out) return CONDUIT_ERR_UNKNOWN;

    for (size_t i = 0; i < framer->extracted_frames.size(); i++) {
        out[i].data = framer->extracted_frames[i].data();
        out[i].data_len = framer->extracted_frames[i].size();
    }

    *out_frames = out;
    *out_count = framer->extracted_frames.size();
    return CONDUIT_OK;
}

CONDUIT_CODEC_API void conduit_free_frames(conduit_frame_t* frames, size_t /*count*/) {
    std::free(frames);
}

// ============================================================================
// Session registry
// ============================================================================

CONDUIT_CODEC_API void conduit_register_session(
    const char* name, conduit_session_factory_fn factory) {
    if (!name || !factory) return;
    auto& reg = SessionRegistry::instance();
    std::lock_guard lock(reg.mutex);
    reg.factories[name] = factory;
}

// ============================================================================
// Version
// ============================================================================

CONDUIT_CODEC_API const char* conduit_codec_version(void) {
    return "0.1.0";
}

} // extern "C"
