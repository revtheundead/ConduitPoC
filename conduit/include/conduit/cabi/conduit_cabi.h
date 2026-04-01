/* SPDX-License-Identifier: MIT */
/* Conduit Full Transceiver C ABI — lifecycle, peers, transports, handlers */

#ifndef CONDUIT_CABI_H
#define CONDUIT_CABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef CONDUIT_CABI_EXPORTS
#    define CONDUIT_CABI_API __declspec(dllexport)
#  else
#    define CONDUIT_CABI_API __declspec(dllimport)
#  endif
#else
#  define CONDUIT_CABI_API __attribute__((visibility("default")))
#endif

/* ================================================================
 * Opaque handles
 * ================================================================ */
typedef struct conduit_transceiver conduit_transceiver_t;
typedef uint32_t conduit_peer_id;
typedef uint32_t conduit_callback_id;

/* ================================================================
 * Error codes (mirrors conduit::ErrorCode categories)
 * ================================================================ */
typedef int32_t conduit_xcvr_error_t;
#define CONDUIT_XCVR_OK                      0
#define CONDUIT_XCVR_ERR_INVALID_ARGUMENT  (-1)
#define CONDUIT_XCVR_ERR_ALREADY_RUNNING   (-2)
#define CONDUIT_XCVR_ERR_NOT_RUNNING       (-3)
#define CONDUIT_XCVR_ERR_PEER_NOT_FOUND    (-4)
#define CONDUIT_XCVR_ERR_SEND_FAILED       (-5)
#define CONDUIT_XCVR_ERR_ENCODE_FAILED     (-6)
#define CONDUIT_XCVR_ERR_BATCH_NOT_SUPPORTED (-7)
#define CONDUIT_XCVR_ERR_DECODE_FAILED     (-8)
#define CONDUIT_XCVR_ERR_UNKNOWN           (-99)

/* ================================================================
 * Transport configuration
 * ================================================================ */
typedef enum {
    CONDUIT_TRANSPORT_UDP,
    CONDUIT_TRANSPORT_TCP_CLIENT,
    CONDUIT_TRANSPORT_TCP_SERVER,
    CONDUIT_TRANSPORT_SERIAL
} conduit_transport_type_t;

/* Serial parity values for conduit_transport_config_t.parity */
#define CONDUIT_SERIAL_PARITY_NONE  0
#define CONDUIT_SERIAL_PARITY_ODD   1
#define CONDUIT_SERIAL_PARITY_EVEN  2

/* Serial stop-bits values for conduit_transport_config_t.stop_bits */
#define CONDUIT_SERIAL_STOP_BITS_ONE 0
#define CONDUIT_SERIAL_STOP_BITS_TWO 1

/* Serial flow-control values for conduit_transport_config_t.flow_control */
#define CONDUIT_SERIAL_FLOW_NONE     0
#define CONDUIT_SERIAL_FLOW_HARDWARE 1
#define CONDUIT_SERIAL_FLOW_SOFTWARE 2

typedef struct {
    conduit_transport_type_t type;

    /* ------------------------------------------------------------------
     * Common fields (all transport types)
     * ------------------------------------------------------------------ */
    const char* address;        /* "host:port" for TCP/UDP; device path for serial */
    uint32_t baud_rate;         /* Serial: baud rate (0 = use default 9600) */
    size_t recv_buffer_size;    /* 0 = use transport default (~65536 TCP/UDP, 4096 serial) */

    /* ------------------------------------------------------------------
     * TCP client: connect timeout and reconnect policy
     * reconnect_enabled: >0 = enabled, 0 = use C++ default (enabled),
     *                    <0 = disabled
     * All delay/multiplier fields: 0 = use C++ default
     * ------------------------------------------------------------------ */
    uint32_t connect_timeout_ms;           /* 0 = use default (10000 ms) */
    int      reconnect_enabled;            /* >0=on, 0=default(on), <0=off */
    uint32_t reconnect_initial_delay_ms;   /* 0 = use default (1000 ms) */
    uint32_t reconnect_max_delay_ms;       /* 0 = use default (30000 ms) */
    double   reconnect_backoff_multiplier; /* 0.0 = use default (2.0) */
    uint32_t reconnect_max_attempts;       /* 0 = unlimited (C++ default) */

    /* ------------------------------------------------------------------
     * UDP: explicit bind/remote split and multi-peer tuning.
     * When bind_address/bind_port are set, address is treated as remote.
     * ------------------------------------------------------------------ */
    const char* bind_address;   /* NULL = "0.0.0.0" or infer from address */
    uint16_t    bind_port;      /* 0 = ephemeral or port from address */
    uint16_t    remote_port;    /* 0 = port from address */
    size_t      send_buffer_size;  /* 0 = use default (65536) */
    size_t      max_datagram_size; /* 0 = use default (65507) */
    size_t      max_peers;         /* 0 = use default (1024) */
    uint32_t    peer_timeout_s;    /* 0 = no timeout (C++ default) */
    /* Multicast (all zero/NULL = unicast, C++ defaults) */
    const char* multicast_group;     /* NULL = no multicast */
    const char* multicast_interface; /* NULL = "0.0.0.0" (OS default route) */
    uint8_t     multicast_ttl;       /* 0 = use default (1) */
    uint8_t     multicast_loop;      /* 0 = use default (true), 1 = on, 2 = off */

    /* ------------------------------------------------------------------
     * TCP server
     * ------------------------------------------------------------------ */
    size_t max_clients;         /* 0 = use default (64) */

    /* ------------------------------------------------------------------
     * Serial: frame parameters (0 = use C++ defaults)
     * ------------------------------------------------------------------ */
    uint8_t data_bits;          /* 0 = use default (8) */
    int     parity;             /* CONDUIT_SERIAL_PARITY_* (0=None, C++ default) */
    int     stop_bits;          /* CONDUIT_SERIAL_STOP_BITS_* (0=One, C++ default) */
    int     flow_control;       /* CONDUIT_SERIAL_FLOW_* (0=None, C++ default) */
} conduit_transport_config_t;

/* ================================================================
 * Callbacks
 * ================================================================ */
typedef void (*conduit_msg_callback_t)(
    conduit_peer_id peer,
    uint64_t type_id,
    const char* type_name,
    const uint8_t* data, size_t len,
    void* user_data);

typedef void (*conduit_state_callback_t)(
    conduit_peer_id peer,
    int32_t new_state,
    void* user_data);

typedef void (*conduit_error_callback_t)(
    conduit_peer_id peer,
    const char* peer_name,
    int32_t error_code,
    const char* error_message,
    void* user_data);

/* Session names — string-based lookup into the session registry. */
typedef const char* conduit_session_name_t;

/* ================================================================
 * Lifecycle
 * ================================================================ */
CONDUIT_CABI_API conduit_transceiver_t* conduit_create(void);
CONDUIT_CABI_API void conduit_destroy(conduit_transceiver_t* xcvr);
CONDUIT_CABI_API conduit_xcvr_error_t conduit_start(conduit_transceiver_t* xcvr);
CONDUIT_CABI_API void conduit_stop(conduit_transceiver_t* xcvr);
CONDUIT_CABI_API int conduit_is_running(const conduit_transceiver_t* xcvr);

/* ================================================================
 * Peer management
 * ================================================================ */
CONDUIT_CABI_API conduit_xcvr_error_t conduit_add_peer(
    conduit_transceiver_t* xcvr,
    const char* name,
    conduit_session_name_t session_name,
    const conduit_transport_config_t* transport,
    conduit_peer_id* out_peer_id);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_peer_by_name(
    const conduit_transceiver_t* xcvr,
    const char* name,
    conduit_peer_id* out_peer_id);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_sole_peer(
    const conduit_transceiver_t* xcvr,
    conduit_peer_id* out_peer_id);

/* ================================================================
 * Messaging
 * ================================================================ */
CONDUIT_CABI_API conduit_xcvr_error_t conduit_send(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t* data, size_t len);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_send_batch(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id,
    const uint8_t** payloads, const size_t* lens, size_t count);

/* ================================================================
 * Message logging (for passthrough sessions)
 *
 * Passthrough sessions defer message logging to the caller because the
 * C++ side cannot identify message types or format content.  These
 * functions let Java/Python log individual decoded messages through
 * the same message log infrastructure.
 * ================================================================ */
CONDUIT_CABI_API conduit_xcvr_error_t conduit_log_recv_message(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    const char* type_name,
    size_t byte_count,
    const char* content);         /* nullable — omitted when include_message_content is off */

CONDUIT_CABI_API conduit_xcvr_error_t conduit_log_send_message(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    const char* type_name,
    size_t byte_count,
    const char* content);         /* nullable */

/* ================================================================
 * Handler registration/removal
 * ================================================================ */
CONDUIT_CABI_API conduit_callback_id conduit_on_message(
    conduit_transceiver_t* xcvr,
    uint64_t type_id,
    conduit_msg_callback_t callback,
    void* user_data);

CONDUIT_CABI_API conduit_callback_id conduit_on_any_message(
    conduit_transceiver_t* xcvr,
    conduit_msg_callback_t callback,
    void* user_data);

/**
 * Remove a message handler for the given type_id.
 * Note: The peer parameter is reserved for future use and currently ignored.
 * Handlers are removed transceiver-wide regardless of the peer value.
 */
CONDUIT_CABI_API int conduit_remove_handler(
    conduit_transceiver_t* xcvr,
    conduit_peer_id peer,
    uint64_t type_id);

/* ================================================================
 * State & error callbacks
 * ================================================================ */
CONDUIT_CABI_API conduit_callback_id conduit_on_state_change(
    conduit_transceiver_t* xcvr,
    conduit_state_callback_t callback,
    void* user_data);

CONDUIT_CABI_API int conduit_remove_state_change(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id);

CONDUIT_CABI_API conduit_callback_id conduit_on_error(
    conduit_transceiver_t* xcvr,
    conduit_error_callback_t callback,
    void* user_data);

CONDUIT_CABI_API int conduit_remove_error_callback(
    conduit_transceiver_t* xcvr,
    conduit_callback_id id);

/* ================================================================
 * Query
 * ================================================================ */
CONDUIT_CABI_API size_t conduit_peer_count(const conduit_transceiver_t* xcvr);
CONDUIT_CABI_API int32_t conduit_peer_state(
    const conduit_transceiver_t* xcvr, conduit_peer_id peer);

/* ================================================================
 * Statistics
 * ================================================================ */
typedef struct {
    uint64_t messages_received;
    uint64_t messages_dispatched;
    uint64_t messages_dropped;
    uint64_t decode_errors;
    uint64_t handler_errors;
    uint64_t handler_timeouts;
    uint64_t bytes_received;
    uint64_t bytes_sent;
} conduit_stats_snapshot_t;

CONDUIT_CABI_API conduit_xcvr_error_t conduit_stats(
    const conduit_transceiver_t* xcvr,
    conduit_stats_snapshot_t* out);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_stats_reset(
    conduit_transceiver_t* xcvr);

/* ================================================================
 * Session registry (shared with codec CABI)
 * ================================================================ */
typedef void* (*conduit_session_factory_t)(void);
CONDUIT_CABI_API void conduit_xcvr_register_session(
    const char* name, conduit_session_factory_t factory);

/* ================================================================
 * Pre-start configuration (call before conduit_start)
 * ================================================================ */

/* Queue drop policies. */
#define CONDUIT_DROP_OLDEST 0
#define CONDUIT_DROP_NEWEST 1
#define CONDUIT_DROP_BLOCK  2

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_queue_config(
    conduit_transceiver_t* xcvr,
    size_t capacity,
    int drop_policy,            /* CONDUIT_DROP_* */
    double back_pressure_threshold);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_worker_config(
    conduit_transceiver_t* xcvr,
    size_t thread_count,
    uint64_t handler_timeout_ms);

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_shutdown_timeout(
    conduit_transceiver_t* xcvr,
    uint64_t timeout_ms);

/* Message log modes. */
#define CONDUIT_LOG_MODE_COMBINED           0
#define CONDUIT_LOG_MODE_SEPARATE_DIRECTION 1
#define CONDUIT_LOG_MODE_PER_PEER           2
#define CONDUIT_LOG_MODE_PER_PEER_DIRECTION 3

/* Message log output targets. */
#define CONDUIT_LOG_OUTPUT_FILE   0
#define CONDUIT_LOG_OUTPUT_STDOUT 1
#define CONDUIT_LOG_OUTPUT_BOTH   2

typedef struct {
    int            enabled;
    int            mode;              /* CONDUIT_LOG_MODE_* */
    int            output;            /* CONDUIT_LOG_OUTPUT_* */
    const char*    directory;
    const char*    prefix;
    const char*    filename;          /* nullable */
    const char*    sent_filename;     /* nullable */
    const char*    received_filename; /* nullable */
    int            include_message_content;
} conduit_message_log_config_t;

CONDUIT_CABI_API conduit_xcvr_error_t conduit_set_message_log_config(
    conduit_transceiver_t* xcvr,
    const conduit_message_log_config_t* config);

/* ================================================================
 * Passthrough session registration (no protocol-specific .so needed)
 *
 * Registers a session that handles framing only.  Encode/decode of
 * individual messages is done on the caller side (Java/Python).
 * The passthrough session:
 *   - Uses the provided framing parameters for StreamFramer
 *   - encode_wrap(): returns input bytes as-is (already framed)
 *   - decode_frame(): returns raw frame bytes with type_id = 0
 * ================================================================ */
typedef struct {
    const uint8_t* sync_pattern;
    size_t         sync_pattern_len;
    size_t         min_header_size;
    /* Frame-length extraction: skip `skip_bits` from frame start,
       then read `field_bits` (8/16/32) in given endianness. */
    size_t         length_skip_bits;
    size_t         length_field_bits;    /* 8, 16, or 32 */
    int            length_big_endian;    /* 1 = big-endian */
} conduit_frame_config_t;

CONDUIT_CABI_API conduit_xcvr_error_t conduit_register_passthrough_session(
    const char* name,
    const conduit_frame_config_t* frame_config,
    const uint64_t* type_ids,
    const char** type_names,
    const int* receive_only,     /* 1 = receive-only, 0 = bidirectional */
    size_t type_count);

/* ================================================================
 * Logger configuration (global — controls internal Conduit logging)
 *
 * These configure the conduit::logging::Logger singleton which all
 * Transceiver internals use for diagnostic output (DEBUG, WARN, etc.).
 * Distinct from message_log_config which records message traffic.
 * ================================================================ */

/* Log levels (mirrors conduit::logging::Level). */
#define CONDUIT_LOG_LEVEL_TRACE 0
#define CONDUIT_LOG_LEVEL_DEBUG 1
#define CONDUIT_LOG_LEVEL_INFO  2
#define CONDUIT_LOG_LEVEL_WARN  3
#define CONDUIT_LOG_LEVEL_ERROR 4
#define CONDUIT_LOG_LEVEL_FATAL 5
#define CONDUIT_LOG_LEVEL_OFF   6

/** Set the global log level. Messages below this level are suppressed. */
CONDUIT_CABI_API void conduit_set_log_level(int level);

/** Get the current global log level. */
CONDUIT_CABI_API int conduit_get_log_level(void);

/** Add a console sink (stdout or stderr, with optional color).
 *  @param use_stderr  1 = log to stderr, 0 = stdout
 *  @param colorize    1 = ANSI color output, 0 = plain text
 */
CONDUIT_CABI_API void conduit_log_add_console_sink(int use_stderr, int colorize);

/** Add a file sink.
 *  @param path    File path for log output
 *  @param append  1 = append to existing file, 0 = overwrite
 */
CONDUIT_CABI_API void conduit_log_add_file_sink(const char* path, int append);

/** Remove all log sinks. */
CONDUIT_CABI_API void conduit_log_clear_sinks(void);

/* ================================================================
 * Version
 * ================================================================ */
CONDUIT_CABI_API const char* conduit_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CONDUIT_CABI_H */
