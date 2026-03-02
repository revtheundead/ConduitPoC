// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.List;

/**
 * Panama FFI (JDK 21+) implementation of {@link NativeBinding}.
 * <p>
 * Wraps the existing {@link CabiBindings} MethodHandles and manages
 * memory arenas and upcall stubs for callbacks.
 */
public final class PanamaNativeBinding implements NativeBinding {

    private static final Linker LINKER = Linker.nativeLinker();

    // Callback FunctionDescriptors matching C ABI callback signatures
    private static final FunctionDescriptor MSG_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);

    private static final FunctionDescriptor STATE_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS);

    private static final FunctionDescriptor ERROR_CB_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS);

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

    private final Arena arena;
    private final List<MemorySegment> callbackStubs = new ArrayList<>();

    public PanamaNativeBinding() {
        this.arena = Arena.ofShared();
    }

    // ================================================================
    // Lifecycle
    // ================================================================

    @Override
    public long create() {
        try {
            MemorySegment handle = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            return handle.address();
        } catch (Throwable e) {
            throw new RuntimeException("create failed", e);
        }
    }

    @Override
    public void destroy(long handle) {
        try {
            CabiBindings.conduit_destroy.invokeExact(MemorySegment.ofAddress(handle));
        } catch (Throwable e) {
            throw new RuntimeException("destroy failed", e);
        }
    }

    @Override
    public int start(long handle) {
        try {
            return (int) CabiBindings.conduit_start.invokeExact(MemorySegment.ofAddress(handle));
        } catch (Throwable e) {
            throw new RuntimeException("start failed", e);
        }
    }

    @Override
    public void stop(long handle) {
        try {
            CabiBindings.conduit_stop.invokeExact(MemorySegment.ofAddress(handle));
        } catch (Throwable e) {
            throw new RuntimeException("stop failed", e);
        }
    }

    @Override
    public boolean isRunning(long handle) {
        try {
            return (int) CabiBindings.conduit_is_running.invokeExact(MemorySegment.ofAddress(handle)) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("isRunning failed", e);
        }
    }

    // ================================================================
    // Peer management
    // ================================================================

    @Override
    public int addPeer(long handle, String name, String sessionName,
                       int transportType, String address, int baudRate) {
        try {
            var nameStr = arena.allocateUtf8String(name);
            var sessionStr = arena.allocateUtf8String(sessionName);
            var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
            cfg.set(ValueLayout.JAVA_INT, 0, transportType);
            var addrStr = arena.allocateUtf8String(address);
            cfg.set(ValueLayout.ADDRESS, 8, addrStr);
            cfg.set(ValueLayout.JAVA_INT, 16, baudRate);

            var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_add_peer.invokeExact(
                MemorySegment.ofAddress(handle), nameStr, sessionStr, cfg, peerIdOut);
            if (err != 0) return err; // negative error code
            return peerIdOut.get(ValueLayout.JAVA_INT, 0);
        } catch (Throwable e) {
            throw new RuntimeException("addPeer failed", e);
        }
    }

    @Override
    public int solePeer(long handle) {
        try {
            var pidOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_sole_peer.invokeExact(
                MemorySegment.ofAddress(handle), pidOut);
            if (err != 0) return err;
            return pidOut.get(ValueLayout.JAVA_INT, 0);
        } catch (Throwable e) {
            throw new RuntimeException("solePeer failed", e);
        }
    }

    @Override
    public int peerByName(long handle, String name) {
        try {
            var nameStr = arena.allocateUtf8String(name);
            var pidOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_peer_by_name.invokeExact(
                MemorySegment.ofAddress(handle), nameStr, pidOut);
            if (err != 0) return err;
            return pidOut.get(ValueLayout.JAVA_INT, 0);
        } catch (Throwable e) {
            throw new RuntimeException("peerByName failed", e);
        }
    }

    @Override
    public long peerCount(long handle) {
        try {
            return (long) CabiBindings.conduit_peer_count.invokeExact(MemorySegment.ofAddress(handle));
        } catch (Throwable e) {
            throw new RuntimeException("peerCount failed", e);
        }
    }

    @Override
    public int peerState(long handle, int peerId) {
        try {
            return (int) CabiBindings.conduit_peer_state.invokeExact(
                MemorySegment.ofAddress(handle), peerId);
        } catch (Throwable e) {
            throw new RuntimeException("peerState failed", e);
        }
    }

    // ================================================================
    // Messaging
    // ================================================================

    @Override
    public int send(long handle, int peerId, long typeId, byte[] data) {
        try (var sendArena = Arena.ofConfined()) {
            var buf = sendArena.allocateArray(ValueLayout.JAVA_BYTE, data);
            return (int) CabiBindings.conduit_send.invokeExact(
                MemorySegment.ofAddress(handle), peerId, typeId, buf, (long) data.length);
        } catch (Throwable e) {
            throw new RuntimeException("send failed", e);
        }
    }

    @Override
    public int sendBatch(long handle, int peerId, long typeId, List<byte[]> payloads) {
        if (payloads.isEmpty()) return 0;
        try (var batchArena = Arena.ofConfined()) {
            int count = payloads.size();
            var ptrs = batchArena.allocate(
                ValueLayout.ADDRESS.byteSize() * count,
                ValueLayout.ADDRESS.byteAlignment());
            var lens = batchArena.allocate(
                ValueLayout.JAVA_LONG.byteSize() * count,
                ValueLayout.JAVA_LONG.byteAlignment());

            for (int i = 0; i < count; i++) {
                byte[] payload = payloads.get(i);
                var buf = batchArena.allocateArray(ValueLayout.JAVA_BYTE, payload);
                ptrs.setAtIndex(ValueLayout.ADDRESS, i, buf);
                lens.setAtIndex(ValueLayout.JAVA_LONG, i, (long) payload.length);
            }

            return (int) CabiBindings.conduit_send_batch.invokeExact(
                MemorySegment.ofAddress(handle), peerId, typeId, ptrs, lens, (long) count);
        } catch (Throwable e) {
            throw new RuntimeException("sendBatch failed", e);
        }
    }

    // ================================================================
    // Handler registration
    // ================================================================

    @Override
    public int onMessage(long handle, long typeId, Transceiver.MessageCallback callback) {
        try {
            var stub = createMsgUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_message.invokeExact(
                MemorySegment.ofAddress(handle), typeId, stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onMessage failed", e);
        }
    }

    @Override
    public int onAnyMessage(long handle, Transceiver.MessageCallback callback) {
        try {
            var stub = createMsgUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_any_message.invokeExact(
                MemorySegment.ofAddress(handle), stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onAnyMessage failed", e);
        }
    }

    @Override
    public boolean removeHandler(long handle, int peerId, long typeId) {
        try {
            return (int) CabiBindings.conduit_remove_handler.invokeExact(
                MemorySegment.ofAddress(handle), peerId, typeId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeHandler failed", e);
        }
    }

    // ================================================================
    // State & error callbacks
    // ================================================================

    @Override
    public int onStateChange(long handle, Transceiver.StateCallback callback) {
        try {
            var stub = createStateUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_state_change.invokeExact(
                MemorySegment.ofAddress(handle), stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onStateChange failed", e);
        }
    }

    @Override
    public boolean removeStateChange(long handle, int callbackId) {
        try {
            return (int) CabiBindings.conduit_remove_state_change.invokeExact(
                MemorySegment.ofAddress(handle), callbackId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeStateChange failed", e);
        }
    }

    @Override
    public int onError(long handle, Transceiver.ErrorCallback callback) {
        try {
            var stub = createErrorUpcallStub(callback);
            callbackStubs.add(stub);
            return (int) CabiBindings.conduit_on_error.invokeExact(
                MemorySegment.ofAddress(handle), stub, MemorySegment.NULL);
        } catch (Throwable e) {
            throw new RuntimeException("onError failed", e);
        }
    }

    @Override
    public boolean removeErrorCallback(long handle, int callbackId) {
        try {
            return (int) CabiBindings.conduit_remove_error_callback.invokeExact(
                MemorySegment.ofAddress(handle), callbackId) != 0;
        } catch (Throwable e) {
            throw new RuntimeException("removeErrorCallback failed", e);
        }
    }

    // ================================================================
    // Statistics
    // ================================================================

    @Override
    public long[] stats(long handle) {
        try (var statsArena = Arena.ofConfined()) {
            var snap = statsArena.allocate(STATS_LAYOUT);
            int err = (int) CabiBindings.conduit_stats.invokeExact(
                MemorySegment.ofAddress(handle), snap);
            if (err != 0) {
                throw new ConduitError(err, "stats failed");
            }
            return new long[] {
                snap.get(ValueLayout.JAVA_LONG, 0),   // messages_received
                snap.get(ValueLayout.JAVA_LONG, 8),   // messages_dispatched
                snap.get(ValueLayout.JAVA_LONG, 16),  // messages_dropped
                snap.get(ValueLayout.JAVA_LONG, 24),  // decode_errors
                snap.get(ValueLayout.JAVA_LONG, 32),  // handler_errors
                snap.get(ValueLayout.JAVA_LONG, 40),  // handler_timeouts
                snap.get(ValueLayout.JAVA_LONG, 48),  // bytes_received
                snap.get(ValueLayout.JAVA_LONG, 56)   // bytes_sent
            };
        } catch (ConduitError e) {
            throw e;
        } catch (Throwable e) {
            throw new RuntimeException("stats failed", e);
        }
    }

    @Override
    public int statsReset(long handle) {
        try {
            return (int) CabiBindings.conduit_stats_reset.invokeExact(MemorySegment.ofAddress(handle));
        } catch (Throwable e) {
            throw new RuntimeException("statsReset failed", e);
        }
    }

    // ================================================================
    // Version
    // ================================================================

    @Override
    public String version() {
        try {
            var ptr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
            if (ptr == MemorySegment.NULL) return "";
            return ptr.reinterpret(256).getUtf8String(0);
        } catch (Throwable e) {
            throw new RuntimeException("version failed", e);
        }
    }

    // ================================================================
    // Session registration
    // ================================================================

    /**
     * Layout for conduit_frame_config_t (6 fields, 64-bit platform):
     * { uint8_t* sync_pattern, size_t sync_pattern_len, size_t min_header_size,
     *   size_t length_skip_bits, size_t length_field_bits, int length_big_endian }
     */
    private static final StructLayout FRAME_CONFIG_LAYOUT = MemoryLayout.structLayout(
        ValueLayout.ADDRESS.withName("sync_pattern"),
        ValueLayout.JAVA_LONG.withName("sync_pattern_len"),
        ValueLayout.JAVA_LONG.withName("min_header_size"),
        ValueLayout.JAVA_LONG.withName("length_skip_bits"),
        ValueLayout.JAVA_LONG.withName("length_field_bits"),
        ValueLayout.JAVA_INT.withName("length_big_endian"),
        MemoryLayout.paddingLayout(4)
    );

    @Override
    public int registerPassthroughSession(
            String name, byte[] syncPattern,
            int minHeaderSize, int lengthSkipBits, int lengthFieldBits,
            boolean lengthBigEndian,
            long[] typeIds, String[] typeNames, int[] receiveOnly) {
        try {
            int count = typeIds.length;
            var nameStr = arena.allocateUtf8String(name);

            // Build frame config struct
            var cfg = arena.allocate(FRAME_CONFIG_LAYOUT);
            var syncBuf = arena.allocateArray(ValueLayout.JAVA_BYTE, syncPattern);
            cfg.set(ValueLayout.ADDRESS, 0, syncBuf);
            cfg.set(ValueLayout.JAVA_LONG, 8, (long) syncPattern.length);
            cfg.set(ValueLayout.JAVA_LONG, 16, (long) minHeaderSize);
            cfg.set(ValueLayout.JAVA_LONG, 24, (long) lengthSkipBits);
            cfg.set(ValueLayout.JAVA_LONG, 32, (long) lengthFieldBits);
            cfg.set(ValueLayout.JAVA_INT, 40, lengthBigEndian ? 1 : 0);

            // Build arrays
            var ids = arena.allocateArray(ValueLayout.JAVA_LONG, typeIds);
            var names = arena.allocate(
                ValueLayout.ADDRESS.byteSize() * count,
                ValueLayout.ADDRESS.byteAlignment());
            for (int i = 0; i < count; i++) {
                names.setAtIndex(ValueLayout.ADDRESS, i,
                    arena.allocateUtf8String(typeNames[i]));
            }
            var recvOnly = arena.allocateArray(ValueLayout.JAVA_INT, receiveOnly);

            return (int) CabiBindings.conduit_register_passthrough_session.invokeExact(
                nameStr, cfg, ids, names, recvOnly, (long) count);
        } catch (Throwable e) {
            throw new RuntimeException("registerPassthroughSession failed", e);
        }
    }

    // ================================================================
    // Cleanup
    // ================================================================

    @Override
    public void close() {
        callbackStubs.clear();
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }

    // ================================================================
    // Upcall stub creation (Panama FFI)
    // ================================================================

    private MemorySegment createMsgUpcallStub(Transceiver.MessageCallback callback) {
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

    private MemorySegment createStateUpcallStub(Transceiver.StateCallback callback) {
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

    private MemorySegment createErrorUpcallStub(Transceiver.ErrorCallback callback) {
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
    // Callback dispatchers
    // ================================================================

    private static final class MsgCallbackDispatcher {
        private final Transceiver.MessageCallback callback;

        MsgCallbackDispatcher(Transceiver.MessageCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused")
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
        private final Transceiver.StateCallback callback;

        StateCallbackDispatcher(Transceiver.StateCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused")
        public void dispatch(int peerId, int newState, MemorySegment userData) {
            callback.onStateChange(peerId, newState);
        }
    }

    private static final class ErrorCallbackDispatcher {
        private final Transceiver.ErrorCallback callback;

        ErrorCallbackDispatcher(Transceiver.ErrorCallback cb) { this.callback = cb; }

        @SuppressWarnings("unused")
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
