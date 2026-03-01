// SPDX-License-Identifier: MIT
package io.conduit;

import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * JNI-based implementation of {@link NativeBinding} for JDK 11+.
 * <p>
 * Delegates to native methods implemented in {@code conduit_jni.c} which
 * bridge to the same C ABI functions used by the Panama implementation.
 * <p>
 * Callbacks are dispatched from C via static dispatch methods on this class.
 * Each callback is registered with an integer key so the C side can identify
 * which Java callback to invoke.
 */
public final class JniNativeBinding implements NativeBinding {

    // ================================================================
    // Library loading
    // ================================================================

    private static volatile boolean loaded = false;

    static {
        loadNativeLibrary();
    }

    private static void loadNativeLibrary() {
        if (loaded) return;
        String libPath = System.getProperty("conduit.jni.path");
        if (libPath != null) {
            System.load(libPath);
        } else {
            try {
                System.loadLibrary("conduit_jni");
            } catch (UnsatisfiedLinkError e) {
                throw new RuntimeException(
                    "Cannot load libconduit_jni. Set -Dconduit.jni.path or add to java.library.path", e);
            }
        }
        loaded = true;
    }

    // ================================================================
    // Callback registries
    // ================================================================

    private static final AtomicInteger callbackIdGen = new AtomicInteger(1);
    private static final ConcurrentHashMap<Integer, Transceiver.MessageCallback> msgCallbacks = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Transceiver.StateCallback> stateCallbacks = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Transceiver.ErrorCallback> errorCallbacks = new ConcurrentHashMap<>();

    // ================================================================
    // Native method declarations
    // ================================================================

    private static native long nCreate();
    private static native void nDestroy(long handle);
    private static native int nStart(long handle);
    private static native void nStop(long handle);
    private static native int nIsRunning(long handle);

    private static native int nAddPeer(long handle, String name, String sessionName,
                                       int transportType, String address, int baudRate);
    private static native int nSolePeer(long handle);
    private static native int nPeerByName(long handle, String name);
    private static native long nPeerCount(long handle);
    private static native int nPeerState(long handle, int peerId);

    private static native int nSend(long handle, int peerId, long typeId, byte[] data, int len);
    private static native int nSendBatch(long handle, int peerId, long typeId,
                                          byte[][] payloads, int[] lengths, int count);

    private static native int nOnMessage(long handle, long typeId, int callbackKey);
    private static native int nOnAnyMessage(long handle, int callbackKey);
    private static native int nRemoveHandler(long handle, int peerId, long typeId);

    private static native int nOnStateChange(long handle, int callbackKey);
    private static native int nRemoveStateChange(long handle, int callbackId);
    private static native int nOnError(long handle, int callbackKey);
    private static native int nRemoveErrorCallback(long handle, int callbackId);

    private static native long[] nStats(long handle);
    private static native int nStatsReset(long handle);

    private static native String nVersion();

    // ================================================================
    // Callback dispatch (called from C via JNI)
    // ================================================================

    /** Called from JNI when a message callback fires. */
    @SuppressWarnings("unused") // Called from native code
    private static void dispatchMessage(int callbackKey, int peerId, long typeId,
                                        String typeName, byte[] data) {
        Transceiver.MessageCallback cb = msgCallbacks.get(callbackKey);
        if (cb != null) {
            cb.onMessage(peerId, typeId, typeName, data);
        }
    }

    /** Called from JNI when a state change callback fires. */
    @SuppressWarnings("unused") // Called from native code
    private static void dispatchStateChange(int callbackKey, int peerId, int newState) {
        Transceiver.StateCallback cb = stateCallbacks.get(callbackKey);
        if (cb != null) {
            cb.onStateChange(peerId, newState);
        }
    }

    /** Called from JNI when an error callback fires. */
    @SuppressWarnings("unused") // Called from native code
    private static void dispatchError(int callbackKey, int peerId, String peerName,
                                       int errorCode, String errorMessage) {
        Transceiver.ErrorCallback cb = errorCallbacks.get(callbackKey);
        if (cb != null) {
            cb.onError(peerId, peerName, errorCode, errorMessage);
        }
    }

    // ================================================================
    // NativeBinding implementation
    // ================================================================

    @Override
    public long create() {
        return nCreate();
    }

    @Override
    public void destroy(long handle) {
        nDestroy(handle);
    }

    @Override
    public int start(long handle) {
        return nStart(handle);
    }

    @Override
    public void stop(long handle) {
        nStop(handle);
    }

    @Override
    public boolean isRunning(long handle) {
        return nIsRunning(handle) != 0;
    }

    @Override
    public int addPeer(long handle, String name, String sessionName,
                       int transportType, String address, int baudRate) {
        return nAddPeer(handle, name, sessionName, transportType, address, baudRate);
    }

    @Override
    public int solePeer(long handle) {
        return nSolePeer(handle);
    }

    @Override
    public int peerByName(long handle, String name) {
        return nPeerByName(handle, name);
    }

    @Override
    public long peerCount(long handle) {
        return nPeerCount(handle);
    }

    @Override
    public int peerState(long handle, int peerId) {
        return nPeerState(handle, peerId);
    }

    @Override
    public int send(long handle, int peerId, long typeId, byte[] data) {
        return nSend(handle, peerId, typeId, data, data.length);
    }

    @Override
    public int sendBatch(long handle, int peerId, long typeId, List<byte[]> payloads) {
        if (payloads.isEmpty()) return 0;
        int count = payloads.size();
        byte[][] arrays = payloads.toArray(new byte[0][]);
        int[] lengths = new int[count];
        for (int i = 0; i < count; i++) {
            lengths[i] = arrays[i].length;
        }
        return nSendBatch(handle, peerId, typeId, arrays, lengths, count);
    }

    @Override
    public int onMessage(long handle, long typeId, Transceiver.MessageCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        msgCallbacks.put(key, callback);
        return nOnMessage(handle, typeId, key);
    }

    @Override
    public int onAnyMessage(long handle, Transceiver.MessageCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        msgCallbacks.put(key, callback);
        return nOnAnyMessage(handle, key);
    }

    @Override
    public boolean removeHandler(long handle, int peerId, long typeId) {
        return nRemoveHandler(handle, peerId, typeId) != 0;
    }

    @Override
    public int onStateChange(long handle, Transceiver.StateCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        stateCallbacks.put(key, callback);
        return nOnStateChange(handle, key);
    }

    @Override
    public boolean removeStateChange(long handle, int callbackId) {
        return nRemoveStateChange(handle, callbackId) != 0;
    }

    @Override
    public int onError(long handle, Transceiver.ErrorCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        errorCallbacks.put(key, callback);
        return nOnError(handle, key);
    }

    @Override
    public boolean removeErrorCallback(long handle, int callbackId) {
        return nRemoveErrorCallback(handle, callbackId) != 0;
    }

    @Override
    public long[] stats(long handle) {
        return nStats(handle);
    }

    @Override
    public int statsReset(long handle) {
        return nStatsReset(handle);
    }

    @Override
    public String version() {
        return nVersion();
    }

    @Override
    public void close() {
        // No persistent resources to release — callbacks are per-transceiver
    }

    /** Remove all callbacks associated with this binding (for cleanup). */
    void clearCallbacks() {
        msgCallbacks.clear();
        stateCallbacks.clear();
        errorCallbacks.clear();
    }
}
