/* SPDX-License-Identifier: MIT */
/* Conduit Codec-Only C ABI — stateless encode/decode/framing, no transport */

#ifndef CONDUIT_CODEC_CABI_H
#define CONDUIT_CODEC_CABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef CONDUIT_CODEC_CABI_EXPORTS
#    define CONDUIT_CODEC_API __declspec(dllexport)
#  else
#    define CONDUIT_CODEC_API __declspec(dllimport)
#  endif
#else
#  define CONDUIT_CODEC_API __attribute__((visibility("default")))
#endif

/* ================================================================
 * Opaque handles
 * ================================================================ */
typedef struct conduit_session conduit_session_t;
typedef struct conduit_framer conduit_framer_t;

/* ================================================================
 * Error codes
 * ================================================================ */
typedef int32_t conduit_error_t;
#define CONDUIT_OK                  0
#define CONDUIT_ERR_DECODE_FAILED  -1
#define CONDUIT_ERR_ENCODE_FAILED  -2
#define CONDUIT_ERR_UNKNOWN_TYPE   -3
#define CONDUIT_ERR_UNKNOWN_SESSION -4
#define CONDUIT_ERR_BUFFER_TOO_SMALL -5
#define CONDUIT_ERR_BATCH_NOT_SUPPORTED -6
#define CONDUIT_ERR_UNKNOWN        -99

/* ================================================================
 * Decoded message (returned from decode)
 * ================================================================ */
typedef struct {
    uint64_t type_id;
    const char* type_name;
    const uint8_t* data;
    size_t data_len;
} conduit_decoded_msg_t;

/* ================================================================
 * Encode result
 * ================================================================ */
typedef struct {
    uint8_t* data;
    size_t data_len;
} conduit_encode_result_t;

/* ================================================================
 * Frame extracted by framer
 * ================================================================ */
typedef struct {
    const uint8_t* data;
    size_t data_len;
} conduit_frame_t;

/* ================================================================
 * Session lifecycle
 * ================================================================ */
CONDUIT_CODEC_API conduit_session_t* conduit_session_create(const char* session_type);
CONDUIT_CODEC_API void conduit_session_destroy(conduit_session_t* session);
CONDUIT_CODEC_API void conduit_session_reset(conduit_session_t* session);

/* ================================================================
 * Decode: raw bytes -> structured messages
 * ================================================================ */
CONDUIT_CODEC_API conduit_error_t conduit_decode_frame(
    conduit_session_t* session,
    const uint8_t* data, size_t len,
    conduit_decoded_msg_t** out_msgs, size_t* out_count);

CONDUIT_CODEC_API void conduit_free_decoded_msgs(conduit_decoded_msg_t* msgs, size_t count);

/* ================================================================
 * Encode: type_id + field bytes -> wire bytes
 * ================================================================ */
CONDUIT_CODEC_API conduit_error_t conduit_encode_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    conduit_encode_result_t* out_result);

CONDUIT_CODEC_API void conduit_free_encode_result(conduit_encode_result_t* result);

/* ================================================================
 * Batch encode (array-payload protocols)
 * ================================================================ */
CONDUIT_CODEC_API conduit_error_t conduit_encode_batch(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* payload_lens, size_t count,
    conduit_encode_result_t* out_result);

/* ================================================================
 * Introspection
 * ================================================================ */
CONDUIT_CODEC_API const char* conduit_session_type_name(
    conduit_session_t* session, uint64_t type_id);
CONDUIT_CODEC_API size_t conduit_session_leaf_type_count(
    conduit_session_t* session);
CONDUIT_CODEC_API const uint64_t* conduit_session_leaf_type_ids(
    conduit_session_t* session);
CONDUIT_CODEC_API int conduit_session_is_receive_only(
    conduit_session_t* session, uint64_t type_id);
CONDUIT_CODEC_API const char* conduit_session_protocol_name(
    conduit_session_t* session);

/* ================================================================
 * Human-readable formatting
 * ================================================================ */
CONDUIT_CODEC_API conduit_error_t conduit_format_message(
    conduit_session_t* session,
    uint64_t type_id,
    const uint8_t* payload, size_t payload_len,
    char* buf, size_t buf_len, size_t* out_written);

/* ================================================================
 * Stream framing (for users doing their own I/O)
 * ================================================================ */
CONDUIT_CODEC_API conduit_framer_t* conduit_framer_create(
    conduit_session_t* session);
CONDUIT_CODEC_API void conduit_framer_destroy(conduit_framer_t* framer);
CONDUIT_CODEC_API conduit_error_t conduit_framer_feed(
    conduit_framer_t* framer,
    const uint8_t* data, size_t len,
    conduit_frame_t** out_frames, size_t* out_count);
CONDUIT_CODEC_API void conduit_free_frames(conduit_frame_t* frames, size_t count);

/* ================================================================
 * Session registry
 * ================================================================ */
typedef void* (*conduit_session_factory_fn)(void);
CONDUIT_CODEC_API void conduit_register_session(
    const char* name, conduit_session_factory_fn factory);

/* ================================================================
 * Version
 * ================================================================ */
CONDUIT_CODEC_API const char* conduit_codec_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CONDUIT_CODEC_CABI_H */
