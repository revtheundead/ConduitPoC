// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * Low-level bindings to libconduit_cabi using the Java Foreign Function API (JDK 21+).
 * <p>
 * This class maps C ABI functions to Java method handles using the Panama FFI.
 * Users should use the higher-level {@link Transceiver} class instead.
 */
public final class CabiBindings {

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LIB;

    static {
        String libPath = System.getProperty("conduit.cabi.path");
        if (libPath != null) {
            System.load(libPath);
            LIB = SymbolLookup.loaderLookup();
        } else {
            try {
                System.loadLibrary("conduit_cabi");
                LIB = SymbolLookup.loaderLookup();
            } catch (UnsatisfiedLinkError e) {
                throw new RuntimeException(
                    "Cannot load libconduit_cabi. Set -Dconduit.cabi.path or add to java.library.path", e);
            }
        }
    }

    // Lifecycle
    public static final MethodHandle conduit_create;
    public static final MethodHandle conduit_destroy;
    public static final MethodHandle conduit_start;
    public static final MethodHandle conduit_stop;
    public static final MethodHandle conduit_is_running;

    // Peer management
    public static final MethodHandle conduit_add_peer;
    public static final MethodHandle conduit_sole_peer;
    public static final MethodHandle conduit_peer_by_name;

    // Query
    public static final MethodHandle conduit_peer_count;
    public static final MethodHandle conduit_peer_state;

    // Messaging
    public static final MethodHandle conduit_send;
    public static final MethodHandle conduit_send_batch;

    // Message logging (passthrough)
    public static final MethodHandle conduit_log_recv_message;
    public static final MethodHandle conduit_log_send_message;

    // Handler registration
    public static final MethodHandle conduit_on_message;
    public static final MethodHandle conduit_on_any_message;
    public static final MethodHandle conduit_remove_handler;

    // State & error callbacks
    public static final MethodHandle conduit_on_state_change;
    public static final MethodHandle conduit_remove_state_change;
    public static final MethodHandle conduit_on_error;
    public static final MethodHandle conduit_remove_error_callback;

    // Stats
    public static final MethodHandle conduit_stats;
    public static final MethodHandle conduit_stats_reset;

    // Configuration
    public static final MethodHandle conduit_set_queue_config;
    public static final MethodHandle conduit_set_worker_config;
    public static final MethodHandle conduit_set_shutdown_timeout;
    public static final MethodHandle conduit_set_message_log_config;

    // Session registration
    public static final MethodHandle conduit_register_passthrough_session;

    // Logger configuration
    public static final MethodHandle conduit_set_log_level;
    public static final MethodHandle conduit_get_log_level;
    public static final MethodHandle conduit_log_add_console_sink;
    public static final MethodHandle conduit_log_add_file_sink;
    public static final MethodHandle conduit_log_clear_sinks;

    // Version
    public static final MethodHandle conduit_version;

    static {
        conduit_create = lookup("conduit_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
        conduit_destroy = lookup("conduit_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
        conduit_start = lookup("conduit_start",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        conduit_stop = lookup("conduit_stop",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
        conduit_is_running = lookup("conduit_is_running",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

        conduit_add_peer = lookup("conduit_add_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_sole_peer = lookup("conduit_sole_peer",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_peer_by_name = lookup("conduit_peer_by_name",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));

        conduit_peer_count = lookup("conduit_peer_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
        conduit_peer_state = lookup("conduit_peer_state",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

        // Messaging: conduit_send(xcvr, peer, type_id, data, len) -> error
        conduit_send = lookup("conduit_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
        // conduit_send_batch(xcvr, peer, type_id, payloads, lens, count) -> error
        conduit_send_batch = lookup("conduit_send_batch",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));

        // conduit_log_recv_message(xcvr, peer, type_name, byte_count, content) -> error
        conduit_log_recv_message = lookup("conduit_log_recv_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
        // conduit_log_send_message(xcvr, peer, type_name, byte_count, content) -> error
        conduit_log_send_message = lookup("conduit_log_send_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

        // Handler registration: conduit_on_message(xcvr, type_id, callback, user_data) -> callback_id
        conduit_on_message = lookup("conduit_on_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_on_any_message = lookup("conduit_on_any_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        // conduit_remove_handler(xcvr, peer, type_id) -> int
        conduit_remove_handler = lookup("conduit_remove_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));

        // State & error callbacks
        conduit_on_state_change = lookup("conduit_on_state_change",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_remove_state_change = lookup("conduit_remove_state_change",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        conduit_on_error = lookup("conduit_on_error",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_remove_error_callback = lookup("conduit_remove_error_callback",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

        // Stats: conduit_stats(xcvr, &snapshot) -> error
        conduit_stats = lookup("conduit_stats",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_stats_reset = lookup("conduit_stats_reset",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

        // conduit_set_queue_config(xcvr, capacity, drop_policy, back_pressure_threshold) -> error
        conduit_set_queue_config = lookup("conduit_set_queue_config",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT,
                ValueLayout.JAVA_DOUBLE));
        // conduit_set_worker_config(xcvr, thread_count, handler_timeout_ms) -> error
        conduit_set_worker_config = lookup("conduit_set_worker_config",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_LONG));
        // conduit_set_shutdown_timeout(xcvr, timeout_ms) -> error
        conduit_set_shutdown_timeout = lookup("conduit_set_shutdown_timeout",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
        // conduit_set_message_log_config(xcvr, config*) -> error
        conduit_set_message_log_config = lookup("conduit_set_message_log_config",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));

        // conduit_register_passthrough_session(name, frame_config, type_ids, type_names, receive_only, count) -> error
        conduit_register_passthrough_session = lookup("conduit_register_passthrough_session",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));

        // conduit_set_log_level(level) -> void
        conduit_set_log_level = lookup("conduit_set_log_level",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT));
        // conduit_get_log_level() -> int
        conduit_get_log_level = lookup("conduit_get_log_level",
            FunctionDescriptor.of(ValueLayout.JAVA_INT));
        // conduit_log_add_console_sink(use_stderr, colorize) -> void
        conduit_log_add_console_sink = lookup("conduit_log_add_console_sink",
            FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
        // conduit_log_add_file_sink(path, append) -> void
        conduit_log_add_file_sink = lookup("conduit_log_add_file_sink",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
        // conduit_log_clear_sinks() -> void
        conduit_log_clear_sinks = lookup("conduit_log_clear_sinks",
            FunctionDescriptor.ofVoid());

        conduit_version = lookup("conduit_version",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    }

    private static MethodHandle lookup(String name, FunctionDescriptor desc) {
        var sym = LIB.find(name)
            .orElseThrow(() -> new RuntimeException("Symbol not found: " + name));
        return LINKER.downcallHandle(sym, desc);
    }

    private CabiBindings() {}
}
