// SPDX-License-Identifier: MIT
package io.conduit;

import java.util.ArrayList;
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
            // NativeLoader tries java.library.path first, then extracts from
            // bundled JAR resources (native/<os>-<arch>/libconduit_jni.so|.dll|.dylib)
            NativeLoader.load("conduit_jni");
        }
        loaded = true;
    }

    // ================================================================
    // Callback registries (global, keyed by callback ID)
    // ================================================================

    private static final AtomicInteger callbackIdGen = new AtomicInteger(1);
    private static final ConcurrentHashMap<Integer, Transceiver.MessageCallback> msgCallbacks = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Transceiver.StateCallback> stateCallbacks = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Transceiver.ErrorCallback> errorCallbacks = new ConcurrentHashMap<>();

    // Per-instance tracking of registered callback keys for cleanup
    private final List<Integer> ownedMsgKeys = new ArrayList<>();
    private final List<Integer> ownedStateKeys = new ArrayList<>();
    private final List<Integer> ownedErrorKeys = new ArrayList<>();

    // ================================================================
    // Native method declarations
    // ================================================================

    private static native long nCreate();
    private static native void nDestroy(long handle);
    private static native int nStart(long handle);
    private static native void nStop(long handle);
    private static native int nIsRunning(long handle);

    private static native int nAddPeer(long handle, String name, String sessionName,
                                       int transportType, String address, long baudRate,
                                       long recvBufferSize, long connectTimeoutMs,
                                       int reconnectEnabled,
                                       long reconnectInitialDelayMs, long reconnectMaxDelayMs,
                                       double reconnectBackoffMul, long reconnectMaxAttempts,
                                       String bindAddress, int bindPort, int remotePort,
                                       long maxDatagramSize, long maxPeers, long peerTimeoutS,
                                       String multicastGroup, String multicastInterface,
                                       int multicastTtl, int multicastLoop,
                                       long maxClients,
                                       int dataBits, int parity, int stopBits, int flowControl);
    private static native int nSolePeer(long handle);
    private static native int nPeerByName(long handle, String name);
    private static native long nPeerCount(long handle);
    private static native int nPeerState(long handle, int peerId);

    private static native int nSend(long handle, int peerId, long typeId, byte[] data, int len);
    private static native int nSendBatch(long handle, int peerId, long typeId,
                                          byte[][] payloads, int[] lengths, int count);

    private static native int nLogRecvMessage(long handle, int peerId, String typeName,
                                               long byteCount, String content);
    private static native int nLogSendMessage(long handle, int peerId, String typeName,
                                               long byteCount, String content);

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

    private static native int nSetQueueConfig(long handle, long capacity, int dropPolicy,
                                                double backPressureThreshold);
    private static native int nSetWorkerConfig(long handle, long threadCount, long handlerTimeoutMs);
    private static native int nSetShutdownTimeout(long handle, long timeoutMs);
    private static native int nSetMessageLogConfig(long handle, int enabled, int mode, int output,
                                                    String directory, String prefix, String filename,
                                                    String sentFilename, String receivedFilename,
                                                    int includeMessageContent);

    private static native int nRegisterPassthroughSession(
        String name, byte[] syncPattern,
        int minHeaderSize, int lengthSkipBits, int lengthFieldBits,
        int lengthBigEndian,
        long[] typeIds, String[] typeNames, int[] receiveOnly);

    // Logger configuration
    private static native void nSetLogLevel(int level);
    private static native int nGetLogLevel();
    private static native void nLogAddConsoleSink(int useStderr, int colorize);
    private static native void nLogAddFileSink(String path, int append);
    private static native void nLogClearSinks();

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
            cb.onStateChange(peerId, Transceiver.ConnectionState.fromValue(newState));
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
                       TransportConfig transport) {
        return nAddPeer(handle, name, sessionName,
            transport.type().value(), transport.address(), transport.baudRate(),
            transport.recvBufferSize(), transport.connectTimeoutMs(),
            transport.reconnectEnabled(),
            transport.reconnectInitialDelayMs(), transport.reconnectMaxDelayMs(),
            transport.reconnectBackoffMul(), transport.reconnectMaxAttempts(),
            transport.bindAddress(), transport.bindPort(), transport.remotePort(),
            transport.maxDatagramSize(), transport.maxPeers(), transport.peerTimeoutS(),
            transport.multicastGroup(), transport.multicastInterface(),
            transport.multicastTtl(), transport.multicastLoop(),
            transport.maxClients(),
            transport.dataBits(), transport.parity(), transport.stopBits(), transport.flowControl());
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
        if (payloads.isEmpty()) {
            return nSendBatch(handle, peerId, typeId, new byte[0][], new int[0], 0);
        }
        int count = payloads.size();
        byte[][] arrays = payloads.toArray(new byte[0][]);
        int[] lengths = new int[count];
        for (int i = 0; i < count; i++) {
            lengths[i] = arrays[i].length;
        }
        return nSendBatch(handle, peerId, typeId, arrays, lengths, count);
    }

    @Override
    public int logRecvMessage(long handle, int peerId, String typeName,
                              long byteCount, String content) {
        return nLogRecvMessage(handle, peerId, typeName, byteCount, content);
    }

    @Override
    public int logSendMessage(long handle, int peerId, String typeName,
                              long byteCount, String content) {
        return nLogSendMessage(handle, peerId, typeName, byteCount, content);
    }

    @Override
    public int onMessage(long handle, long typeId, Transceiver.MessageCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        msgCallbacks.put(key, callback);
        ownedMsgKeys.add(key);
        return nOnMessage(handle, typeId, key);
    }

    @Override
    public int onAnyMessage(long handle, Transceiver.MessageCallback callback) {
        int key = callbackIdGen.getAndIncrement();
        msgCallbacks.put(key, callback);
        ownedMsgKeys.add(key);
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
        ownedStateKeys.add(key);
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
        ownedErrorKeys.add(key);
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
    public int setQueueConfig(long handle, long capacity, int dropPolicy,
                              double backPressureThreshold) {
        return nSetQueueConfig(handle, capacity, dropPolicy, backPressureThreshold);
    }

    @Override
    public int setWorkerConfig(long handle, long threadCount, long handlerTimeoutMs) {
        return nSetWorkerConfig(handle, threadCount, handlerTimeoutMs);
    }

    @Override
    public int setShutdownTimeout(long handle, long timeoutMs) {
        return nSetShutdownTimeout(handle, timeoutMs);
    }

    @Override
    public int setMessageLogConfig(long handle, boolean enabled, int mode, int output,
                                   String directory, String prefix, String filename,
                                   String sentFilename, String receivedFilename,
                                   boolean includeMessageContent) {
        return nSetMessageLogConfig(handle, enabled ? 1 : 0, mode, output,
            directory, prefix, filename,
            sentFilename, receivedFilename,
            includeMessageContent ? 1 : 0);
    }

    @Override
    public int registerPassthroughSession(
            String name, byte[] syncPattern,
            int minHeaderSize, int lengthSkipBits, int lengthFieldBits,
            boolean lengthBigEndian,
            long[] typeIds, String[] typeNames, int[] receiveOnly) {
        return nRegisterPassthroughSession(
            name, syncPattern,
            minHeaderSize, lengthSkipBits, lengthFieldBits,
            lengthBigEndian ? 1 : 0,
            typeIds, typeNames, receiveOnly);
    }

    @Override
    public void setLogLevel(int level) { nSetLogLevel(level); }

    @Override
    public int getLogLevel() { return nGetLogLevel(); }

    @Override
    public void logAddConsoleSink(boolean useStderr, boolean colorize) {
        nLogAddConsoleSink(useStderr ? 1 : 0, colorize ? 1 : 0);
    }

    @Override
    public void logAddFileSink(String path, boolean append) {
        nLogAddFileSink(path, append ? 1 : 0);
    }

    @Override
    public void logClearSinks() { nLogClearSinks(); }

    @Override
    public void close() {
        // Remove only this instance's callbacks from the global registries
        for (Integer key : ownedMsgKeys) {
            msgCallbacks.remove(key);
        }
        for (Integer key : ownedStateKeys) {
            stateCallbacks.remove(key);
        }
        for (Integer key : ownedErrorKeys) {
            errorCallbacks.remove(key);
        }
        ownedMsgKeys.clear();
        ownedStateKeys.clear();
        ownedErrorKeys.clear();
    }
}
