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
     * Add a peer.
     *
     * @param handle       Transceiver handle
     * @param name         Human-readable peer name
     * @param sessionName  Registered session type name
     * @param transportType  Transport type ordinal (UDP=0, TCP_CLIENT=1, TCP_SERVER=2, SERIAL=3)
     * @param address      Transport address string
     * @param baudRate     Baud rate (serial only, 0 otherwise)
     * @return peer ID, or negative error code
     */
    int addPeer(long handle, String name, String sessionName,
                int transportType, String address, int baudRate);

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
    // Cleanup
    // ================================================================

    /** Release any resources held by this binding (e.g., arenas). */
    @Override
    void close();
}
