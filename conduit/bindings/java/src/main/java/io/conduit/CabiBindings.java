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
