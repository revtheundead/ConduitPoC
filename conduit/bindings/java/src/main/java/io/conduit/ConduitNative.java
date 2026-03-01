// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * Factory for selecting the native backend used by Conduit bindings.
 * <p>
 * By default, the library auto-detects the best available backend:
 * <ul>
 *   <li><b>Panama FFI</b> (JDK 21+) — preferred when available</li>
 *   <li><b>JNI</b> (JDK 11+) — fallback for older JDKs</li>
 * </ul>
 * <p>
 * Users can override the choice before creating any {@link Transceiver}:
 * <pre>{@code
 * ConduitNative.setBackend(ConduitNative.Backend.JNI);
 * // or
 * ConduitNative.setBackend(ConduitNative.Backend.PANAMA);
 * }</pre>
 */
public final class ConduitNative {

    /** Available native FFI backends. */
    public enum Backend {
        /** Auto-detect: prefer Panama on JDK 21+, fall back to JNI. */
        AUTO,
        /** Force Panama FFI (JDK 21+). Throws if unavailable. */
        PANAMA,
        /** Force JNI (JDK 11+). Requires conduit_jni / conduit_codec_jni native libraries. */
        JNI
    }

    private static volatile Backend selectedBackend = Backend.AUTO;
    private static volatile Boolean panamaAvailable;

    private ConduitNative() {}

    /**
     * Set the native backend. Must be called before any binding is created.
     *
     * @param backend the backend to use
     */
    public static void setBackend(Backend backend) {
        if (backend == null) throw new NullPointerException("backend must not be null");
        selectedBackend = backend;
    }

    /** Get the currently selected backend preference. */
    public static Backend getBackend() {
        return selectedBackend;
    }

    /**
     * Check if Panama FFI is available on the current JVM.
     * <p>
     * Tests for the presence of {@code java.lang.foreign.Linker} (JDK 21+).
     */
    public static boolean isPanamaAvailable() {
        if (panamaAvailable == null) {
            try {
                Class.forName("java.lang.foreign.Linker");
                panamaAvailable = true;
            } catch (ClassNotFoundException e) {
                panamaAvailable = false;
            }
        }
        return panamaAvailable;
    }

    /**
     * Create a {@link NativeBinding} for the transceiver, based on the
     * selected backend.
     */
    public static NativeBinding createBinding() {
        Backend b = resolveBackend();
        switch (b) {
            case PANAMA:
                return createPanamaBinding();
            case JNI:
                return new JniNativeBinding();
            default:
                throw new IllegalStateException("Unknown backend: " + b);
        }
    }

    /**
     * Create a {@link NativeCodecBinding} for codec-only operations,
     * based on the selected backend.
     */
    public static NativeCodecBinding createCodecBinding() {
        Backend b = resolveBackend();
        switch (b) {
            case PANAMA:
                return createPanamaCodecBinding();
            case JNI:
                return new JniCodecBinding();
            default:
                throw new IllegalStateException("Unknown backend: " + b);
        }
    }

    /**
     * Get a human-readable name for the backend that would be used.
     */
    public static String resolvedBackendName() {
        return resolveBackend().name();
    }

    // ================================================================
    // Internal
    // ================================================================

    private static Backend resolveBackend() {
        Backend b = selectedBackend;
        if (b == Backend.AUTO) {
            return isPanamaAvailable() ? Backend.PANAMA : Backend.JNI;
        }
        if (b == Backend.PANAMA && !isPanamaAvailable()) {
            throw new UnsupportedOperationException(
                "Panama FFI backend requested but java.lang.foreign.Linker is not available. " +
                "Requires JDK 21+.");
        }
        return b;
    }

    private static NativeBinding createPanamaBinding() {
        try {
            return (NativeBinding) Class.forName("io.conduit.PanamaNativeBinding")
                .getDeclaredConstructor()
                .newInstance();
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException("Failed to create Panama native binding", e);
        }
    }

    private static NativeCodecBinding createPanamaCodecBinding() {
        try {
            return (NativeCodecBinding) Class.forName("io.conduit.PanamaCodecBinding")
                .getDeclaredConstructor()
                .newInstance();
        } catch (ReflectiveOperationException e) {
            throw new RuntimeException("Failed to create Panama codec binding", e);
        }
    }
}
