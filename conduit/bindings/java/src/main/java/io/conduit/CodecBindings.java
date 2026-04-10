// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * Low-level bindings to libconduit_codec_cabi using the Java Foreign Function API (JDK 21+).
 * <p>
 * Provides codec-only operations (encode, decode, framing) without transport/threading.
 */
public final class CodecBindings {

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LIB;

    static {
        String libPath = System.getProperty("conduit.codec.path");
        if (libPath != null) {
            System.load(libPath);
        } else {
            try {
                System.loadLibrary("conduit_codec_cabi");
            } catch (UnsatisfiedLinkError e) {
                // NativeLoader tries java.library.path first, then extracts from
                // the fat JAR resource at /native/<os>-<arch>/libconduit_codec_cabi.*
                NativeLoader.load("conduit_codec_cabi");
            }
        }
        LIB = SymbolLookup.loaderLookup();
    }

    // Session lifecycle
    public static final MethodHandle conduit_session_create;
    public static final MethodHandle conduit_session_destroy;
    public static final MethodHandle conduit_session_reset;

    // Decode
    public static final MethodHandle conduit_decode_frame;
    public static final MethodHandle conduit_free_decoded_msgs;

    // Encode
    public static final MethodHandle conduit_encode_message;
    public static final MethodHandle conduit_free_encode_result;

    // Batch encode
    public static final MethodHandle conduit_encode_batch;

    // Human-readable formatting
    public static final MethodHandle conduit_format_message;

    // Introspection
    public static final MethodHandle conduit_session_type_name;
    public static final MethodHandle conduit_session_leaf_type_count;
    public static final MethodHandle conduit_session_leaf_type_ids;
    public static final MethodHandle conduit_session_is_receive_only;
    public static final MethodHandle conduit_session_protocol_name;

    // Framing
    public static final MethodHandle conduit_framer_create;
    public static final MethodHandle conduit_framer_destroy;
    public static final MethodHandle conduit_framer_feed;
    public static final MethodHandle conduit_free_frames;

    // Version
    public static final MethodHandle conduit_codec_version;

    static {
        conduit_session_create = lookup("conduit_session_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_session_destroy = lookup("conduit_session_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
        conduit_session_reset = lookup("conduit_session_reset",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

        conduit_decode_frame = lookup("conduit_decode_frame",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_free_decoded_msgs = lookup("conduit_free_decoded_msgs",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));

        conduit_encode_message = lookup("conduit_encode_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS));
        conduit_free_encode_result = lookup("conduit_free_encode_result",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

        // conduit_encode_batch(session, type_id, payloads, payload_lens, count, out_result) -> error
        conduit_encode_batch = lookup("conduit_encode_batch",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS));

        // conduit_format_message(session, type_id, payload, len, buf, buf_len, out_written) -> error
        conduit_format_message = lookup("conduit_format_message",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS));

        conduit_session_type_name = lookup("conduit_session_type_name",
            FunctionDescriptor.of(ValueLayout.ADDRESS,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
        conduit_session_leaf_type_count = lookup("conduit_session_leaf_type_count",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
        conduit_session_leaf_type_ids = lookup("conduit_session_leaf_type_ids",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_session_is_receive_only = lookup("conduit_session_is_receive_only",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
        conduit_session_protocol_name = lookup("conduit_session_protocol_name",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));

        conduit_framer_create = lookup("conduit_framer_create",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_framer_destroy = lookup("conduit_framer_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
        conduit_framer_feed = lookup("conduit_framer_feed",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        conduit_free_frames = lookup("conduit_free_frames",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));

        conduit_codec_version = lookup("conduit_codec_version",
            FunctionDescriptor.of(ValueLayout.ADDRESS));
    }

    private static MethodHandle lookup(String name, FunctionDescriptor desc) {
        var sym = LIB.find(name)
            .orElseThrow(() -> new RuntimeException("Symbol not found: " + name));
        return LINKER.downcallHandle(sym, desc);
    }

    private CodecBindings() {}
}
