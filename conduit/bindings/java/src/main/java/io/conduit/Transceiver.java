// SPDX-License-Identifier: MIT
package io.conduit;

import java.util.List;

/**
 * Java wrapper for the Conduit Transceiver.
 * <p>
 * Provides a Java API over libconduit_cabi that mirrors the C++ experience.
 * Users work with typed message objects — never raw bytes or type IDs.
 * <p>
 * Supports two native backends:
 * <ul>
 *   <li><b>Panama FFI</b> (JDK 21+) — the default when available</li>
 *   <li><b>JNI</b> (JDK 11+) — fallback for older JDKs</li>
 * </ul>
 * Use {@link ConduitNative#setBackend} to override the auto-detected choice.
 *
 * <pre>{@code
 * try (var t = new Transceiver()) {
 *     t.addPeer("radar", "my_session", TransportConfig.udp("0.0.0.0:5000"));
 *     t.onMessage(PingBody.class, (peerId, msg) ->
 *         System.out.println("Got: " + msg));
 *     t.start();
 *     t.send(peerId, pingMsg);
 * }
 * }</pre>
 */
public class Transceiver implements AutoCloseable {

    // ================================================================
    // Callback functional interfaces
    // ================================================================

    /** Raw message callback (receives raw bytes). */
    @FunctionalInterface
    public interface MessageCallback {
        void onMessage(int peerId, long typeId, String typeName, byte[] data);
    }

    /** Typed message callback (receives decoded message object). */
    @FunctionalInterface
    public interface TypedMessageCallback<T> {
        void onMessage(int peerId, T msg);
    }

    @FunctionalInterface
    public interface StateCallback {
        void onStateChange(int peerId, int newState);
    }

    @FunctionalInterface
    public interface ErrorCallback {
        void onError(int peerId, String peerName, int errorCode, String errorMessage);
    }

    /** Statistics snapshot matching C++ TransceiverStats::Snapshot. */
    public static final class StatsSnapshot {
        private final long messagesReceived;
        private final long messagesDispatched;
        private final long messagesDropped;
        private final long decodeErrors;
        private final long handlerErrors;
        private final long handlerTimeouts;
        private final long bytesReceived;
        private final long bytesSent;

        public StatsSnapshot(long messagesReceived, long messagesDispatched,
                             long messagesDropped, long decodeErrors,
                             long handlerErrors, long handlerTimeouts,
                             long bytesReceived, long bytesSent) {
            this.messagesReceived = messagesReceived;
            this.messagesDispatched = messagesDispatched;
            this.messagesDropped = messagesDropped;
            this.decodeErrors = decodeErrors;
            this.handlerErrors = handlerErrors;
            this.handlerTimeouts = handlerTimeouts;
            this.bytesReceived = bytesReceived;
            this.bytesSent = bytesSent;
        }

        public long messagesReceived() { return messagesReceived; }
        public long messagesDispatched() { return messagesDispatched; }
        public long messagesDropped() { return messagesDropped; }
        public long decodeErrors() { return decodeErrors; }
        public long handlerErrors() { return handlerErrors; }
        public long handlerTimeouts() { return handlerTimeouts; }
        public long bytesReceived() { return bytesReceived; }
        public long bytesSent() { return bytesSent; }

        @Override
        public String toString() {
            return "StatsSnapshot{received=" + messagesReceived +
                ", dispatched=" + messagesDispatched +
                ", dropped=" + messagesDropped +
                ", decodeErrors=" + decodeErrors +
                ", handlerErrors=" + handlerErrors +
                ", handlerTimeouts=" + handlerTimeouts +
                ", bytesReceived=" + bytesReceived +
                ", bytesSent=" + bytesSent + "}";
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof StatsSnapshot)) return false;
            StatsSnapshot s = (StatsSnapshot) o;
            return messagesReceived == s.messagesReceived
                && messagesDispatched == s.messagesDispatched
                && messagesDropped == s.messagesDropped
                && decodeErrors == s.decodeErrors
                && handlerErrors == s.handlerErrors
                && handlerTimeouts == s.handlerTimeouts
                && bytesReceived == s.bytesReceived
                && bytesSent == s.bytesSent;
        }

        @Override
        public int hashCode() {
            long h = messagesReceived * 31 + messagesDispatched;
            h = h * 31 + messagesDropped;
            h = h * 31 + decodeErrors;
            h = h * 31 + handlerErrors;
            h = h * 31 + handlerTimeouts;
            h = h * 31 + bytesReceived;
            h = h * 31 + bytesSent;
            return Long.hashCode(h);
        }
    }

    // ================================================================
    // Instance state
    // ================================================================

    private final NativeBinding binding;
    private volatile long handle;

    /**
     * Create a Transceiver using the auto-detected or user-selected backend.
     *
     * @see ConduitNative#setBackend
     */
    public Transceiver() {
        this(ConduitNative.createBinding());
    }

    /**
     * Create a Transceiver with a specific native binding.
     *
     * @param binding  The native binding to use
     */
    public Transceiver(NativeBinding binding) {
        this.binding = binding;
        this.handle = binding.create();
        if (handle == 0) {
            binding.close();
            throw new ConduitError(-99, "Failed to create Transceiver");
        }
    }

    /**
     * Add a peer to the transceiver.
     *
     * @param name         Human-readable peer name
     * @param sessionName  Registered session type name
     * @param transport    Transport configuration
     * @return Peer ID
     */
    public int addPeer(String name, String sessionName, TransportConfig transport) {
        int result = binding.addPeer(handle, name, sessionName,
            transport.type().value(), transport.address(), transport.baudRate());
        if (result < 0) {
            throw new ConduitError(result, "Failed to add peer '" + name + "'");
        }
        return result;
    }

    /** Start the transceiver. */
    public void start() {
        int err = binding.start(handle);
        if (err != 0) {
            throw new ConduitError(err, "Failed to start transceiver");
        }
    }

    /** Stop the transceiver. */
    public void stop() {
        binding.stop(handle);
    }

    /** Check if the transceiver is running. */
    public boolean isRunning() {
        return binding.isRunning(handle);
    }

    /** Get the number of peers. */
    public long peerCount() {
        return binding.peerCount(handle);
    }

    /** Get the connection state of a peer. */
    public int peerState(int peerId) {
        return binding.peerState(handle, peerId);
    }

    /** Get the sole peer ID (when only one exists). */
    public int solePeer() {
        int result = binding.solePeer(handle);
        if (result < 0) {
            throw new ConduitError(result, "sole_peer failed");
        }
        return result;
    }

    /**
     * Look up a peer by name.
     *
     * @param name  The peer name
     * @return Peer ID
     */
    public int peerByName(String name) {
        int result = binding.peerByName(handle, name);
        if (result < 0) {
            throw new ConduitError(result, "Peer not found: '" + name + "'");
        }
        return result;
    }

    // ================================================================
    // Messaging
    // ================================================================

    /**
     * Send a typed message to a specific peer.
     * <p>
     * The message object must be a bgen-generated class with static fields
     * {@code TYPE_ID} (long) and method {@code encodeBytes()} (byte[]).
     * This mirrors the C++ {@code transceiver.send<T>(peer, msg)} API.
     *
     * @param peerId  Target peer ID
     * @param msg     Typed message object
     */
    public void send(int peerId, Object msg) {
        if (msg == null) throw new NullPointerException("msg must not be null");
        try {
            Class<?> cls = msg.getClass();
            long typeId = cls.getField("TYPE_ID").getLong(null);
            byte[] data = (byte[]) cls.getMethod("encodeBytes").invoke(msg);
            sendRaw(peerId, typeId, data);
        } catch (ConduitError e) {
            throw e;
        } catch (NoSuchFieldException | NoSuchMethodException e) {
            throw new IllegalArgumentException(
                "Message class " + msg.getClass().getName() +
                " must have static TYPE_ID field and encodeBytes() method", e);
        } catch (Throwable e) {
            throw new RuntimeException("send failed", e);
        }
    }

    /**
     * Send a typed message to the sole peer (convenience).
     *
     * @param msg  Typed message object
     */
    public void send(Object msg) {
        send(solePeer(), msg);
    }

    /**
     * Send raw bytes as a message (low-level API).
     *
     * @param peerId  Target peer ID
     * @param typeId  Message type ID
     * @param data    Raw message payload
     */
    public void sendRaw(int peerId, long typeId, byte[] data) {
        int err = binding.send(handle, peerId, typeId, data);
        if (err != 0) {
            throw new ConduitError(err, "send failed");
        }
    }

    /**
     * Send a batch of messages to a peer.
     *
     * @param peerId    Target peer ID
     * @param typeId    Message type ID
     * @param payloads  List of raw message payloads
     */
    public void sendBatch(int peerId, long typeId, List<byte[]> payloads) {
        if (payloads.isEmpty()) return;
        int err = binding.sendBatch(handle, peerId, typeId, payloads);
        if (err != 0) {
            throw new ConduitError(err, "sendBatch failed");
        }
    }

    // ================================================================
    // Handler registration / removal
    // ================================================================

    /**
     * Register a raw message handler for a specific type ID.
     *
     * @param typeId    Message type ID to listen for
     * @param callback  Handler invoked on receipt (receives raw bytes)
     * @return Callback ID (can be used for removal)
     */
    public int onMessage(long typeId, MessageCallback callback) {
        return binding.onMessage(handle, typeId, callback);
    }

    /**
     * Register a typed message handler with auto-deserialization.
     * <p>
     * The message class must have {@code TYPE_ID} (long), and
     * {@code decodeBytes(byte[])} static method. This mirrors the C++
     * {@code transceiver.on<T>([](const T& msg) { ... })} API.
     *
     * @param msgClass  The bgen-generated message class
     * @param callback  Handler invoked with the decoded message
     * @return Callback ID
     */
    public <T> int onMessage(Class<T> msgClass, TypedMessageCallback<T> callback) {
        try {
            long typeId = msgClass.getField("TYPE_ID").getLong(null);
            java.lang.reflect.Method decodeMethod = msgClass.getMethod("decodeBytes", byte[].class);

            return onMessage(typeId, (peerId, tid, typeName, data) -> {
                try {
                    @SuppressWarnings("unchecked")
                    T msg = (T) decodeMethod.invoke(null, data);
                    callback.onMessage(peerId, msg);
                } catch (Exception e) {
                    // Decode failed — skip silently (error fires on C++ side)
                }
            });
        } catch (NoSuchFieldException | NoSuchMethodException e) {
            throw new IllegalArgumentException(
                "Class " + msgClass.getName() +
                " must have static TYPE_ID field and decodeBytes(byte[]) method", e);
        } catch (Throwable e) {
            throw new RuntimeException("onMessage failed", e);
        }
    }

    /**
     * Register a catch-all message handler.
     *
     * @param callback  Handler invoked for every received message
     * @return Callback ID
     */
    public int onAnyMessage(MessageCallback callback) {
        return binding.onAnyMessage(handle, callback);
    }

    /**
     * Remove a message handler.
     *
     * @param peerId  Peer ID
     * @param typeId  Message type ID
     * @return true if a handler was removed
     */
    public boolean removeHandler(int peerId, long typeId) {
        return binding.removeHandler(handle, peerId, typeId);
    }

    // ================================================================
    // State & error callbacks
    // ================================================================

    /**
     * Register a connection state change callback.
     *
     * @param callback  Handler invoked when peer state changes
     * @return Callback ID (can be used for removal)
     */
    public int onStateChange(StateCallback callback) {
        return binding.onStateChange(handle, callback);
    }

    /**
     * Remove a state change callback.
     *
     * @param callbackId  The ID returned by {@link #onStateChange}
     * @return true if a callback was removed
     */
    public boolean removeStateChange(int callbackId) {
        return binding.removeStateChange(handle, callbackId);
    }

    /**
     * Register an error callback.
     *
     * @param callback  Handler invoked on errors
     * @return Callback ID (can be used for removal)
     */
    public int onError(ErrorCallback callback) {
        return binding.onError(handle, callback);
    }

    /**
     * Remove an error callback.
     *
     * @param callbackId  The ID returned by {@link #onError}
     * @return true if a callback was removed
     */
    public boolean removeErrorCallback(int callbackId) {
        return binding.removeErrorCallback(handle, callbackId);
    }

    // ================================================================
    // Statistics
    // ================================================================

    /**
     * Get a snapshot of transceiver statistics.
     *
     * @return Statistics snapshot
     */
    public StatsSnapshot stats() {
        long[] s = binding.stats(handle);
        return new StatsSnapshot(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    }

    /** Reset all statistics counters to zero. */
    public void statsReset() {
        int err = binding.statsReset(handle);
        if (err != 0) {
            throw new ConduitError(err, "statsReset failed");
        }
    }

    // ================================================================
    // Utility
    // ================================================================

    /** Get the library version string. */
    public static String version() {
        try (NativeBinding b = ConduitNative.createBinding()) {
            return b.version();
        }
    }

    /** Get the native backend in use by this transceiver. */
    public String backendName() {
        return binding.getClass().getSimpleName();
    }

    @Override
    public void close() {
        long h = handle;
        if (h != 0) {
            handle = 0;
            try {
                if (binding.isRunning(h)) {
                    binding.stop(h);
                }
            } catch (Throwable ignored) {
                // Best-effort stop; proceed with destroy
            }
            binding.destroy(h);
            binding.close();
        }
    }
}
