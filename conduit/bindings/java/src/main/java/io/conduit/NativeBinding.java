// SPDX-License-Identifier: MIT
package io.conduit;

import java.util.List;

/**
 * Abstraction over the native Conduit Transceiver C ABI.
 * <p>
 * Two implementations exist:
 * <ul>
 *   <li>{@code PanamaNativeBinding} — uses the Panama FFI (JDK 21+)</li>
 *   <li>{@code JniNativeBinding} — uses JNI (JDK 11+)</li>
 * </ul>
 * Users select a backend via {@link ConduitNative#setBackend} or let the
 * library auto-detect the best available option.
 */
public interface NativeBinding extends AutoCloseable {

    // ================================================================
    // Lifecycle
    // ================================================================

    /** Create a new transceiver, returning an opaque handle. */
    long create();

    /** Destroy a transceiver. */
    void destroy(long handle);

    /** Start the transceiver. Returns 0 on success or an error code. */
    int start(long handle);

    /** Stop the transceiver. */
    void stop(long handle);

    /** Check if the transceiver is running. */
    boolean isRunning(long handle);

    // ================================================================
    // Peer management
    // ================================================================

    /**
     * Add a peer with full transport configuration.
     *
     * @param handle     Transceiver handle
     * @param name       Human-readable peer name
     * @param sessionName Registered session type name
     * @param transport  Full transport configuration
     * @return peer ID, or negative error code
     */
    int addPeer(long handle, String name, String sessionName, TransportConfig transport);

    /**
     * Get the sole peer ID (when only one peer exists).
     *
     * @return peer ID, or negative error code
     */
    int solePeer(long handle);

    /**
     * Look up a peer by name.
     *
     * @return peer ID, or negative error code
     */
    int peerByName(long handle, String name);

    /** Get the number of peers. */
    long peerCount(long handle);

    /** Get the connection state of a peer. */
    int peerState(long handle, int peerId);

    // ================================================================
    // Messaging
    // ================================================================

    /**
     * Send raw bytes as a message.
     *
     * @return 0 on success or error code
     */
    int send(long handle, int peerId, long typeId, byte[] data);

    /**
     * Send a batch of messages.
     *
     * @return 0 on success or error code
     */
    int sendBatch(long handle, int peerId, long typeId, List<byte[]> payloads);

    // ================================================================
    // Message logging (passthrough)
    // ================================================================

    /** Log a received message (for passthrough sessions). */
    int logRecvMessage(long handle, int peerId, String typeName,
                       long byteCount, String content, byte[] rawBytes);

    /** Log a sent message (for passthrough sessions). */
    int logSendMessage(long handle, int peerId, String typeName,
                       long byteCount, String content, byte[] rawBytes);

    // ================================================================
    // Handler registration
    // ================================================================

    /**
     * Register a message handler for a specific type ID.
     * <p>
     * The callback receives (peerId, typeId, typeName, data).
     *
     * @return callback ID
     */
    int onMessage(long handle, long typeId, Transceiver.MessageCallback callback);

    /**
     * Register a catch-all message handler.
     *
     * @return callback ID
     */
    int onAnyMessage(long handle, Transceiver.MessageCallback callback);

    /**
     * Remove a message handler.
     *
     * @return true if a handler was removed
     */
    boolean removeHandler(long handle, int peerId, long typeId);

    // ================================================================
    // State & error callbacks
    // ================================================================

    /** Register a state change callback. Returns callback ID. */
    int onStateChange(long handle, Transceiver.StateCallback callback);

    /** Remove a state change callback. Returns true if removed. */
    boolean removeStateChange(long handle, int callbackId);

    /** Register an error callback. Returns callback ID. */
    int onError(long handle, Transceiver.ErrorCallback callback);

    /** Remove an error callback. Returns true if removed. */
    boolean removeErrorCallback(long handle, int callbackId);

    // ================================================================
    // Statistics
    // ================================================================

    /**
     * Get a statistics snapshot.
     *
     * @return 8-element long array: [messagesReceived, messagesDispatched,
     *         messagesDropped, decodeErrors, handlerErrors, handlerTimeouts,
     *         bytesReceived, bytesSent]
     */
    long[] stats(long handle);

    /** Reset all statistics counters. Returns 0 on success. */
    int statsReset(long handle);

    // ================================================================
    // Version
    // ================================================================

    /** Get the library version string. */
    String version();

    // ================================================================
    // Pre-start configuration
    // ================================================================

    /** Configure the receive queue. Must be called before start(). */
    int setQueueConfig(long handle, long capacity, int dropPolicy,
                       double backPressureThreshold);

    /** Configure worker threads. Must be called before start(). */
    int setWorkerConfig(long handle, long threadCount,
                        long handlerTimeoutMs);

    /** Set the graceful shutdown timeout. */
    int setShutdownTimeout(long handle, long timeoutMs);

    /**
     * Configure message logging. Must be called before start().
     *
     * @param mode   0=Combined, 1=SeparateDirection, 2=PerPeer, 3=PerPeerDirection
     * @param output 0=File, 1=Stdout, 2=Both
     */
    int setMessageLogConfig(long handle, boolean enabled, int mode, int output,
                            String directory, String prefix, String filename,
                            String sentFilename, String receivedFilename,
                            boolean includeMessageContent, boolean includeRawBytes);

    /**
     * Register a passthrough session (framing-only, no protocol .so needed).
     * Encode/decode of individual messages is handled on the Java side.
     */
    int registerPassthroughSession(
        String name, byte[] syncPattern,
        int minHeaderSize, int lengthSkipBits, int lengthFieldBits,
        boolean lengthBigEndian,
        long[] typeIds, String[] typeNames, int[] receiveOnly);

    // ================================================================
    // Logger configuration (global — controls internal Conduit logging)
    // ================================================================

    /** Set the global log level. 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Off. */
    void setLogLevel(int level);

    /** Get the current global log level. */
    int getLogLevel();

    /** Add a console log sink. */
    void logAddConsoleSink(boolean useStderr, boolean colorize);

    /** Add a file log sink. */
    void logAddFileSink(String path, boolean append);

    /** Remove all log sinks. */
    void logClearSinks();

    // ================================================================
    // Cleanup
    // ================================================================

    /** Release any resources held by this binding (e.g., arenas). */
    @Override
    void close();
}
