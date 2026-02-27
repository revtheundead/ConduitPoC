// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;

/**
 * Java wrapper for the Conduit Transceiver.
 * <p>
 * Provides a Java API over libconduit_cabi that mirrors the C++ experience.
 * Users work with typed message objects — never raw bytes or type IDs.
 * Uses the Java Foreign Function and Memory API (JDK 21+, Panama FFI).
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
    public record StatsSnapshot(
        long messagesReceived,
        long messagesDispatched,
        long messagesDropped,
        long decodeErrors,
        long handlerErrors,
        long handlerTimeouts,
        long bytesReceived,
        long bytesSent
    ) {}

    // ================================================================
    // Callback FunctionDescriptors matching C ABI callback signatures
    // ================================================================

    // void conduit_msg_callback_t(uint32_t peer, uint64_t type_id, const char* type_name,
    //                             const uint8_t* data, size_t len, void* user_data)
    private static final FunctionDescriptor MSG_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);

    // void conduit_state_callback_t(uint32_t peer, int32_t new_state, void* user_data)
    private static final FunctionDescriptor STATE_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS);

    // void conduit_error_callback_t(uint32_t peer, const char* peer_name,
    //                               int32_t error_code, const char* error_msg, void* user_data)
    private static final FunctionDescriptor ERROR_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private static final Linker LINKER = Linker.nativeLinker();

    // ================================================================
    // Instance state
    // ================================================================

    private volatile MemorySegment handle;
    private final Arena arena;
    // Keep upcall stubs alive to prevent GC while the transceiver is active
    private final List<MemorySegment> callbackStubs = new ArrayList<>();

    /**
     * Layout for conduit_transport_config_t with correct padding for 64-bit.
     * C struct: { int type; [4 bytes padding]; char* address; uint32_t baud_rate; [4 bytes padding] }
     */
    private static final StructLayout TRANSPORT_CONFIG_LAYOUT = MemoryLayout.structLayout(
        ValueLayout.JAVA_INT.withName("type"),
        MemoryLayout.paddingLayout(4),
        ValueLayout.ADDRESS.withName("address"),
        ValueLayout.JAVA_INT.withName("baud_rate"),
        MemoryLayout.paddingLayout(4)
    );

    public Transceiver() {
        this.arena = Arena.ofShared();
        try {
            this.handle = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            if (handle == MemorySegment.NULL) {
                arena.close();
                throw new ConduitError(-99, "Failed to create Transceiver");
            }
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            arena.close();
            throw new RuntimeException("Failed to create Transceiver", e);
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
        try {
            var nameStr = arena.allocateUtf8String(name);
            var sessionStr = arena.allocateUtf8String(sessionName);

            // Allocate transport config struct with correct layout
            var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
            cfg.set(ValueLayout.JAVA_INT, 0, transport.type().value());
            var addrStr = arena.allocateUtf8String(transport.address());
            cfg.set(ValueLayout.ADDRESS, 8, addrStr);
            cfg.set(ValueLayout.JAVA_INT, 16, transport.baudRate());

            var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);

            int err = (int) CabiBindings.conduit_add_peer.invokeExact(
                handle, nameStr, sessionStr, cfg, peerIdOut);
            if (err != 0) {
                throw new ConduitError(err, "Failed to add peer '" + name + "'");
            }
            return peerIdOut.get(ValueLayout.JAVA_INT, 0);
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("addPeer failed", e);
        }
    }

    /** Start the transceiver. */
    public void start() {
        try {
            int err = (int) CabiBindings.conduit_start.invokeExact(handle);
            if (err != 0) {
                throw new ConduitError(err, "Failed to start transceiver");
            }
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("start failed", e);
        }
    }

    /** Stop the transceiver. */
    public void stop() {
        try {
            CabiBindings.conduit_stop.invokeExact(handle);
        } catch (Throwable e) {
            throw new RuntimeException("stop failed", e);
        }
    }

    /** Check if the transceiver is running. */
    public boolean isRunning() {
        try {
            return (int) CabiBindings.conduit_is_running.invokeExact(handle) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("isRunning failed", e);
        }
    }

    /** Get the number of peers. */
    public long peerCount() {
        try {
            return (long) CabiBindings.conduit_peer_count.invokeExact(handle);
        } catch (Throwable e) {
            throw new RuntimeException("peerCount failed", e);
        }
    }

    /** Get the connection state of a peer. */
    public int peerState(int peerId) {
        try {
            return (int) CabiBindings.conduit_peer_state.invokeExact(handle, peerId);
        } catch (Throwable e) {
            throw new RuntimeException("peerState failed", e);
        }
    }

    /** Get the sole peer ID (when only one exists). */
    public int solePeer() {
        try {
            var pidOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_sole_peer.invokeExact(handle, pidOut);
            if (err != 0) {
                throw new ConduitError(err, "sole_peer failed");
            }
            return pidOut.get(ValueLayout.JAVA_INT, 0);
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("solePeer failed", e);
        }
    }

    /**
     * Look up a peer by name.
     *
     * @param name  The peer name
     * @return Peer ID
     */
    public int peerByName(String name) {
        try {
            var nameStr = arena.allocateUtf8String(name);
            var pidOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_peer_by_name.invokeExact(handle, nameStr, pidOut);
            if (err != 0) {
                throw new ConduitError(err, "Peer not found: '" + name + "'");
            }
            return pidOut.get(ValueLayout.JAVA_INT, 0);
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("peerByName failed", e);
        }
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
            var cls = msg.getClass();
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
        try (var sendArena = Arena.ofConfined()) {
            var buf = sendArena.allocateArray(ValueLayout.JAVA_BYTE, data);
            int err = (int) CabiBindings.conduit_send.invokeExact(
                handle, peerId, typeId, buf, (long) data.length);
            if (err != 0) {
                throw new ConduitError(err, "send failed");
            }
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("send failed", e);
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
        try (var batchArena = Arena.ofConfined()) {
            int count = payloads.size();
            // Allocate array of pointers (one per payload)
            var ptrs = batchArena.allocate(
                ValueLayout.ADDRESS.byteSize() * count,
                ValueLayout.ADDRESS.byteAlignment());
            // Allocate array of sizes (one per payload)
            var lens = batchArena.allocate(
                ValueLayout.JAVA_LONG.byteSize() * count,
                ValueLayout.JAVA_LONG.byteAlignment());

            for (int i = 0; i < count; i++) {
                byte[] payload = payloads.get(i);
                var buf = batchArena.allocateArray(ValueLayout.JAVA_BYTE, payload);
                ptrs.setAtIndex(ValueLayout.ADDRESS, i, buf);
                lens.setAtIndex(ValueLayout.JAVA_LONG, i, (long) payload.length);
            }

            int err = (int) CabiBindings.conduit_send_batch.invokeExact(
                handle, peerId, typeId, ptrs, lens, (long) count);
            if (err != 0) {
                throw new ConduitError(err, "sendBatch failed");
            }
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("sendBatch failed", e);
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
        try {
            var stub = createMsgUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_message.invokeExact(
                handle, typeId, stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onMessage failed", e);
        }
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
            var decodeMethod = msgClass.getMethod("decodeBytes", byte[].class);

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
        try {
            var stub = createMsgUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_any_message.invokeExact(
                handle, stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onAnyMessage failed", e);
        }
    }

    /**
     * Remove a message handler.
     *
     * @param peerId  Peer ID
     * @param typeId  Message type ID
     * @return true if a handler was removed
     */
    public boolean removeHandler(int peerId, long typeId) {
        try {
            return (int) CabiBindings.conduit_remove_handler.invokeExact(
                handle, peerId, typeId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeHandler failed", e);
        }
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
        try {
            var stub = createStateUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_state_change.invokeExact(
                handle, stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onStateChange failed", e);
        }
    }

    /**
     * Remove a state change callback.
     *
     * @param callbackId  The ID returned by {@link #onStateChange}
     * @return true if a callback was removed
     */
    public boolean removeStateChange(int callbackId) {
        try {
            return (int) CabiBindings.conduit_remove_state_change.invokeExact(
                handle, callbackId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeStateChange failed", e);
        }
    }

    /**
     * Register an error callback.
     *
     * @param callback  Handler invoked on errors
     * @return Callback ID (can be used for removal)
     */
    public int onError(ErrorCallback callback) {
        try {
            var stub = createErrorUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_error.invokeExact(
                handle, stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onError failed", e);
        }
    }

    /**
     * Remove an error callback.
     *
     * @param callbackId  The ID returned by {@link #onError}
     * @return true if a callback was removed
     */
    public boolean removeErrorCallback(int callbackId) {
        try {
            return (int) CabiBindings.conduit_remove_error_callback.invokeExact(
                handle, callbackId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeErrorCallback failed", e);
        }
    }

    // ================================================================
    // Statistics
    // ================================================================

    /** Layout for conduit_stats_snapshot_t: 8 uint64_t fields */
    private static final StructLayout STATS_LAYOUT = MemoryLayout.structLayout(
        ValueLayout.JAVA_LONG.withName("messages_received"),
        ValueLayout.JAVA_LONG.withName("messages_dispatched"),
        ValueLayout.JAVA_LONG.withName("messages_dropped"),
        ValueLayout.JAVA_LONG.withName("decode_errors"),
        ValueLayout.JAVA_LONG.withName("handler_errors"),
        ValueLayout.JAVA_LONG.withName("handler_timeouts"),
        ValueLayout.JAVA_LONG.withName("bytes_received"),
        ValueLayout.JAVA_LONG.withName("bytes_sent")
    );

    /**
     * Get a snapshot of transceiver statistics.
     *
     * @return Statistics snapshot
     */
    public StatsSnapshot stats() {
        try (var statsArena = Arena.ofConfined()) {
            var snap = statsArena.allocate(STATS_LAYOUT);
            int err = (int) CabiBindings.conduit_stats.invokeExact(handle, snap);
            if (err != 0) {
                throw new ConduitError(err, "stats failed");
            }
            return new StatsSnapshot(
                snap.get(ValueLayout.JAVA_LONG, 0),   // messages_received
                snap.get(ValueLayout.JAVA_LONG, 8),   // messages_dispatched
                snap.get(ValueLayout.JAVA_LONG, 16),  // messages_dropped
                snap.get(ValueLayout.JAVA_LONG, 24),  // decode_errors
                snap.get(ValueLayout.JAVA_LONG, 32),  // handler_errors
                snap.get(ValueLayout.JAVA_LONG, 40),  // handler_timeouts
                snap.get(ValueLayout.JAVA_LONG, 48),  // bytes_received
                snap.get(ValueLayout.JAVA_LONG, 56)   // bytes_sent
            );
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("stats failed", e);
        }
    }

    /** Reset all statistics counters to zero. */
    public void statsReset() {
        try {
            int err = (int) CabiBindings.conduit_stats_reset.invokeExact(handle);
            if (err != 0) {
                throw new ConduitError(err, "statsReset failed");
            }
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("statsReset failed", e);
        }
    }

    // ================================================================
    // Utility
    // ================================================================

    /** Get the library version string. */
    public static String version() {
        try {
            var ptr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
            if (ptr == MemorySegment.NULL) return "";
            return ptr.reinterpret(256).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("version failed", e);
        }
    }

    @Override
    public void close() {
        var h = handle;
        if (h != null && h != MemorySegment.NULL) {
            handle = MemorySegment.NULL;
            try {
                // Stop before destroying to avoid undefined behavior with running I/O threads
                if ((int) CabiBindings.conduit_is_running.invokeExact(h) != 0) {
                    CabiBindings.conduit_stop.invokeExact(h);
                }
            } catch (Throwable ignored) {
                // Best-effort stop; proceed with destroy
            }
            try {
                CabiBindings.conduit_destroy.invokeExact(h);
            } catch (Throwable e) {
                throw new RuntimeException("destroy failed", e);
            }
        }
        callbackStubs.clear();
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }

    // ================================================================
    // Upcall stub creation (Panama FFI)
    // ================================================================

    /**
     * Create a native upcall stub for a MessageCallback.
     * The C ABI calls: void(uint32_t peer, uint64_t type_id, const char* name,
     *                       const uint8_t* data, size_t len, void* user_data)
     */
    private MemorySegment createMsgUpcallStub(MessageCallback callback) {
        try {
            MethodHandle target = MethodHandles.lookup().bind(
                new MsgCallbackDispatcher(callback), "dispatch",
                MethodType.methodType(void.class,
                    int.class, long.class, MemorySegment.class,
                    MemorySegment.class, long.class, MemorySegment.class));
            return LINKER.upcallStub(target, MSG_CB_DESC, arena);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new RuntimeException("Failed to create message upcall stub", e);
        }
    }

    /**
     * Create a native upcall stub for a StateCallback.
     * The C ABI calls: void(uint32_t peer, int32_t new_state, void* user_data)
     */
    private MemorySegment createStateUpcallStub(StateCallback callback) {
        try {
            MethodHandle target = MethodHandles.lookup().bind(
                new StateCallbackDispatcher(callback), "dispatch",
                MethodType.methodType(void.class,
                    int.class, int.class, MemorySegment.class));
            return LINKER.upcallStub(target, STATE_CB_DESC, arena);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new RuntimeException("Failed to create state upcall stub", e);
        }
    }

    /**
     * Create a native upcall stub for an ErrorCallback.
     * The C ABI calls: void(uint32_t peer, const char* peer_name,
     *                       int32_t error_code, const char* error_msg, void* user_data)
     */
    private MemorySegment createErrorUpcallStub(ErrorCallback callback) {
        try {
            MethodHandle target = MethodHandles.lookup().bind(
                new ErrorCallbackDispatcher(callback), "dispatch",
                MethodType.methodType(void.class,
                    int.class, MemorySegment.class, int.class,
                    MemorySegment.class, MemorySegment.class));
            return LINKER.upcallStub(target, ERROR_CB_DESC, arena);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new RuntimeException("Failed to create error upcall stub", e);
        }
    }

    // ================================================================
    // Callback dispatchers — bridge native C calls to Java lambdas
    // ================================================================

    private static final class MsgCallbackDispatcher {
        private final MessageCallback callback;

        MsgCallbackDispatcher(MessageCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused") // Called via MethodHandle from native code
        public void dispatch(int peerId, long typeId, MemorySegment typeNamePtr,
                             MemorySegment dataPtr, long dataLen, MemorySegment userData) {
            String typeName = "";
            if (typeNamePtr != MemorySegment.NULL) {
                typeName = typeNamePtr.reinterpret(256).getUtf8String(0);
            }
            byte[] data = new byte[0];
            if (dataPtr != MemorySegment.NULL && dataLen > 0) {
                data = dataPtr.reinterpret(dataLen).toArray(ValueLayout.JAVA_BYTE);
            }
            callback.onMessage(peerId, typeId, typeName, data);
        }
    }

    private static final class StateCallbackDispatcher {
        private final StateCallback callback;

        StateCallbackDispatcher(StateCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused") // Called via MethodHandle from native code
        public void dispatch(int peerId, int newState, MemorySegment userData) {
            callback.onStateChange(peerId, newState);
        }
    }

    private static final class ErrorCallbackDispatcher {
        private final ErrorCallback callback;

        ErrorCallbackDispatcher(ErrorCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused") // Called via MethodHandle from native code
        public void dispatch(int peerId, MemorySegment peerNamePtr,
                             int errorCode, MemorySegment errorMsgPtr,
                             MemorySegment userData) {
            String peerName = "";
            if (peerNamePtr != MemorySegment.NULL) {
                peerName = peerNamePtr.reinterpret(256).getUtf8String(0);
            }
            String errorMsg = "";
            if (errorMsgPtr != MemorySegment.NULL) {
                errorMsg = errorMsgPtr.reinterpret(1024).getUtf8String(0);
            }
            callback.onError(peerId, peerName, errorCode, errorMsg);
        }
    }
}
