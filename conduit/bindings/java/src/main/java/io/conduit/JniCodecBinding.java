// SPDX-License-Identifier: MIT
package io.conduit;

/**
 * JNI-based implementation of {@link NativeCodecBinding} for JDK 11+.
 * <p>
 * Delegates to native methods implemented in {@code conduit_codec_jni.c}
 * which bridge to the same C ABI functions used by the Panama codec bindings.
 */
public final class JniCodecBinding implements NativeCodecBinding {

    // ================================================================
    // Library loading
    // ================================================================

    private static volatile boolean loaded = false;

    static {
        loadNativeLibrary();
    }

    private static void loadNativeLibrary() {
        if (loaded) return;
        String libPath = System.getProperty("conduit.codec.jni.path");
        if (libPath != null) {
            System.load(libPath);
        } else {
            try {
                System.loadLibrary("conduit_codec_jni");
            } catch (UnsatisfiedLinkError e) {
                throw new RuntimeException(
                    "Cannot load libconduit_codec_jni. Set -Dconduit.codec.jni.path or add to java.library.path", e);
            }
        }
        loaded = true;
    }

    // ================================================================
    // Native method declarations
    // ================================================================

    private static native long nSessionCreate(String sessionType);
    private static native void nSessionDestroy(long session);
    private static native void nSessionReset(long session);

    // Returns flattened array: [count, typeId0, nameLen0, ...name0 bytes..., dataLen0, ...data0 bytes..., ...]
    // For simplicity, returns Object[]: DecodedMessage objects built on C side
    private static native Object[] nDecodeFrame(long session, byte[] data, int len);

    private static native byte[] nEncodeMessage(long session, long typeId, byte[] payload, int payloadLen);
    private static native byte[] nEncodeBatch(long session, long typeId, byte[][] payloads, int[] lens, int count);

    private static native String nSessionTypeName(long session, long typeId);
    private static native long nSessionLeafTypeCount(long session);
    private static native long[] nSessionLeafTypeIds(long session);
    private static native int nSessionIsReceiveOnly(long session, long typeId);
    private static native String nSessionProtocolName(long session);

    private static native String nFormatMessage(long session, long typeId, byte[] payload, int payloadLen);

    private static native long nFramerCreate(long session);
    private static native void nFramerDestroy(long framer);
    private static native byte[][] nFramerFeed(long framer, byte[] data, int len);

    private static native String nCodecVersion();

    // ================================================================
    // NativeCodecBinding implementation
    // ================================================================

    @Override
    public long sessionCreate(String sessionType) {
        return nSessionCreate(sessionType);
    }

    @Override
    public void sessionDestroy(long session) {
        nSessionDestroy(session);
    }

    @Override
    public void sessionReset(long session) {
        nSessionReset(session);
    }

    @Override
    public DecodedMessage[] decodeFrame(long session, byte[] data) {
        Object[] raw = nDecodeFrame(session, data, data.length);
        if (raw == null) return new DecodedMessage[0];
        // The native side returns an array of DecodedMessage objects
        DecodedMessage[] result = new DecodedMessage[raw.length];
        for (int i = 0; i < raw.length; i++) {
            result[i] = (DecodedMessage) raw[i];
        }
        return result;
    }

    @Override
    public byte[] encodeMessage(long session, long typeId, byte[] payload) {
        return nEncodeMessage(session, typeId, payload, payload.length);
    }

    @Override
    public byte[] encodeBatch(long session, long typeId, byte[][] payloads) {
        int count = payloads.length;
        int[] lens = new int[count];
        for (int i = 0; i < count; i++) {
            lens[i] = payloads[i].length;
        }
        return nEncodeBatch(session, typeId, payloads, lens, count);
    }

    @Override
    public String sessionTypeName(long session, long typeId) {
        String name = nSessionTypeName(session, typeId);
        return name != null ? name : "";
    }

    @Override
    public long sessionLeafTypeCount(long session) {
        return nSessionLeafTypeCount(session);
    }

    @Override
    public long[] sessionLeafTypeIds(long session) {
        return nSessionLeafTypeIds(session);
    }

    @Override
    public boolean sessionIsReceiveOnly(long session, long typeId) {
        return nSessionIsReceiveOnly(session, typeId) != 0;
    }

    @Override
    public String sessionProtocolName(long session) {
        String name = nSessionProtocolName(session);
        return name != null ? name : "";
    }

    @Override
    public String formatMessage(long session, long typeId, byte[] payload) {
        return nFormatMessage(session, typeId, payload, payload.length);
    }

    @Override
    public long framerCreate(long session) {
        return nFramerCreate(session);
    }

    @Override
    public void framerDestroy(long framer) {
        nFramerDestroy(framer);
    }

    @Override
    public byte[][] framerFeed(long framer, byte[] data) {
        byte[][] result = nFramerFeed(framer, data, data.length);
        return result != null ? result : new byte[0][];
    }

    @Override
    public String codecVersion() {
        String v = nCodecVersion();
        return v != null ? v : "";
    }

    @Override
    public void close() {
        // No persistent resources
    }
}
