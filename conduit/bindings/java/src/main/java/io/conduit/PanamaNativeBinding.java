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
     * Layout for the extended conduit_transport_config_t (64-bit LP64 ABI).
     * <p>
     * Offsets (all 64-bit systems):
     *   0  int    type
     *   4  (pad4)
     *   8  char*  address
     *  16  u32    baud_rate
     *  20  (pad4)
     *  24  size_t recv_buffer_size
     *  32  u32    connect_timeout_ms
     *  36  int    reconnect_enabled
     *  40  u32    reconnect_initial_delay_ms
     *  44  u32    reconnect_max_delay_ms
     *  48  double reconnect_backoff_multiplier
     *  56  u32    reconnect_max_attempts
     *  60  (pad4)
     *  64  char*  bind_address
     *  72  u16    bind_port
     *  74  u16    remote_port
     *  76  (pad4)
     *  80  size_t send_buffer_size
     *  88  size_t max_datagram_size
     *  96  size_t max_peers
     * 104  u32    peer_timeout_s
     * 108  (pad4)
     * 112  char*  multicast_group
     * 120  char*  multicast_interface
     * 128  u8     multicast_ttl
     * 129  u8     multicast_loop
     * 130  (pad6)
     * 136  size_t max_clients
     * 144  u8     data_bits
     * 145  (pad3)
     * 148  int    parity
     * 152  int    stop_bits
     * 156  int    flow_control
     * Total: 160 bytes
     */
    private static final int TRANSPORT_CONFIG_SIZE = 160;
    // field byte offsets
    private static final long TC_OFF_TYPE                    =   0;
    private static final long TC_OFF_ADDRESS                 =   8;
    private static final long TC_OFF_BAUD_RATE               =  16;
    private static final long TC_OFF_RECV_BUFFER_SIZE        =  24;
    private static final long TC_OFF_CONNECT_TIMEOUT_MS      =  32;
    private static final long TC_OFF_RECONNECT_ENABLED       =  36;
    private static final long TC_OFF_RECONNECT_INIT_DELAY_MS =  40;
    private static final long TC_OFF_RECONNECT_MAX_DELAY_MS  =  44;
    private static final long TC_OFF_RECONNECT_BACKOFF_MUL   =  48;
    private static final long TC_OFF_RECONNECT_MAX_ATTEMPTS  =  56;
    private static final long TC_OFF_BIND_ADDRESS            =  64;
    private static final long TC_OFF_BIND_PORT               =  72;
    private static final long TC_OFF_REMOTE_PORT             =  74;
    private static final long TC_OFF_SEND_BUFFER_SIZE        =  80;
    private static final long TC_OFF_MAX_DATAGRAM_SIZE       =  88;
    private static final long TC_OFF_MAX_PEERS               =  96;
    private static final long TC_OFF_PEER_TIMEOUT_S          = 104;
    private static final long TC_OFF_MULTICAST_GROUP         = 112;
    private static final long TC_OFF_MULTICAST_INTERFACE     = 120;
    private static final long TC_OFF_MULTICAST_TTL           = 128;
    private static final long TC_OFF_MULTICAST_LOOP          = 129;
    private static final long TC_OFF_MAX_CLIENTS             = 136;
    private static final long TC_OFF_DATA_BITS               = 144;
    private static final long TC_OFF_PARITY                  = 148;
    private static final long TC_OFF_STOP_BITS               = 152;
    private static final long TC_OFF_FLOW_CONTROL            = 156;

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
                       TransportConfig transport) {
        try {
            var nameStr    = arena.allocateUtf8String(name);
            var sessionStr = arena.allocateUtf8String(sessionName);
            // Allocate zero-filled config struct (0 fields = use C++ defaults)
            var cfg = arena.allocate(TRANSPORT_CONFIG_SIZE, 8);
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_TYPE,      transport.type().value());
            var addrStr = arena.allocateUtf8String(transport.address());
            cfg.set(ValueLayout.ADDRESS,     TC_OFF_ADDRESS,   addrStr);
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_BAUD_RATE, (int) transport.baudRate());
            cfg.set(ValueLayout.JAVA_LONG,   TC_OFF_RECV_BUFFER_SIZE,        transport.recvBufferSize());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_CONNECT_TIMEOUT_MS,      (int) transport.connectTimeoutMs());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_RECONNECT_ENABLED,       transport.reconnectEnabled());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_RECONNECT_INIT_DELAY_MS, (int) transport.reconnectInitialDelayMs());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_RECONNECT_MAX_DELAY_MS,  (int) transport.reconnectMaxDelayMs());
            cfg.set(ValueLayout.JAVA_DOUBLE, TC_OFF_RECONNECT_BACKOFF_MUL,   transport.reconnectBackoffMul());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_RECONNECT_MAX_ATTEMPTS,  (int) transport.reconnectMaxAttempts());
            if (transport.bindAddress() != null) {
                var bindAddrStr = arena.allocateUtf8String(transport.bindAddress());
                cfg.set(ValueLayout.ADDRESS, TC_OFF_BIND_ADDRESS, bindAddrStr);
            }
            cfg.set(ValueLayout.JAVA_SHORT,  TC_OFF_BIND_PORT,          (short) transport.bindPort());
            cfg.set(ValueLayout.JAVA_SHORT,  TC_OFF_REMOTE_PORT,        (short) transport.remotePort());
            cfg.set(ValueLayout.JAVA_LONG,   TC_OFF_SEND_BUFFER_SIZE,   0L); // TODO: expose send_buffer_size
            cfg.set(ValueLayout.JAVA_LONG,   TC_OFF_MAX_DATAGRAM_SIZE,  transport.maxDatagramSize());
            cfg.set(ValueLayout.JAVA_LONG,   TC_OFF_MAX_PEERS,          transport.maxPeers());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_PEER_TIMEOUT_S,     (int) transport.peerTimeoutS());
            if (transport.multicastGroup() != null) {
                var mcastGroupStr = arena.allocateUtf8String(transport.multicastGroup());
                cfg.set(ValueLayout.ADDRESS, TC_OFF_MULTICAST_GROUP, mcastGroupStr);
            }
            if (transport.multicastInterface() != null) {
                var mcastIfaceStr = arena.allocateUtf8String(transport.multicastInterface());
                cfg.set(ValueLayout.ADDRESS, TC_OFF_MULTICAST_INTERFACE, mcastIfaceStr);
            }
            cfg.set(ValueLayout.JAVA_BYTE,   TC_OFF_MULTICAST_TTL,     (byte) transport.multicastTtl());
            cfg.set(ValueLayout.JAVA_BYTE,   TC_OFF_MULTICAST_LOOP,    (byte) transport.multicastLoop());
            cfg.set(ValueLayout.JAVA_LONG,   TC_OFF_MAX_CLIENTS,        transport.maxClients());
            cfg.set(ValueLayout.JAVA_BYTE,   TC_OFF_DATA_BITS,          (byte) transport.dataBits());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_PARITY,             transport.parity());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_STOP_BITS,          transport.stopBits());
            cfg.set(ValueLayout.JAVA_INT,    TC_OFF_FLOW_CONTROL,       transport.flowControl());

            var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);
            int err = (int) CabiBindings.conduit_add_peer.invokeExact(
                MemorySegment.ofAddress(handle), nameStr, sessionStr, cfg, peerIdOut);
            if (err != 0) return err;
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
        if (payloads.isEmpty()) {
            // C++ returns InvalidArgument for empty batch; pass count=0 to C ABI
            try {
                return (int) CabiBindings.conduit_send_batch.invokeExact(
                    MemorySegment.ofAddress(handle), peerId, typeId,
                    MemorySegment.NULL, MemorySegment.NULL, 0L);
            } catch (Throwable e) {
                throw new RuntimeException("sendBatch failed", e);
            }
        }
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
    // Message logging (passthrough)
    // ================================================================

    @Override
    public int logRecvMessage(long handle, int peerId, String typeName,
                              long byteCount, String content) {
        try (var logArena = Arena.ofConfined()) {
            var cTypeName = logArena.allocateUtf8String(typeName);
            var cContent = (content != null)
                ? logArena.allocateUtf8String(content) : MemorySegment.NULL;
            return (int) CabiBindings.conduit_log_recv_message.invokeExact(
                MemorySegment.ofAddress(handle), peerId, cTypeName, byteCount, cContent);
        } catch (Throwable e) {
            throw new RuntimeException("logRecvMessage failed", e);
        }
    }

    @Override
    public int logSendMessage(long handle, int peerId, String typeName,
                              long byteCount, String content) {
        try (var logArena = Arena.ofConfined()) {
            var cTypeName = logArena.allocateUtf8String(typeName);
            var cContent = (content != null)
                ? logArena.allocateUtf8String(content) : MemorySegment.NULL;
            return (int) CabiBindings.conduit_log_send_message.invokeExact(
                MemorySegment.ofAddress(handle), peerId, cTypeName, byteCount, cContent);
        } catch (Throwable e) {
            throw new RuntimeException("logSendMessage failed", e);
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
    // Configuration
    // ================================================================

    @Override
    public int setQueueConfig(long handle, long capacity, int dropPolicy,
                              double backPressureThreshold) {
        try {
            return (int) CabiBindings.conduit_set_queue_config.invokeExact(
                MemorySegment.ofAddress(handle), capacity, dropPolicy, backPressureThreshold);
        } catch (Throwable e) {
            throw new RuntimeException("setQueueConfig failed", e);
        }
    }

    @Override
    public int setWorkerConfig(long handle, long threadCount, long handlerTimeoutMs) {
        try {
            return (int) CabiBindings.conduit_set_worker_config.invokeExact(
                MemorySegment.ofAddress(handle), threadCount, handlerTimeoutMs);
        } catch (Throwable e) {
            throw new RuntimeException("setWorkerConfig failed", e);
        }
    }

    @Override
    public int setShutdownTimeout(long handle, long timeoutMs) {
        try {
            return (int) CabiBindings.conduit_set_shutdown_timeout.invokeExact(
                MemorySegment.ofAddress(handle), timeoutMs);
        } catch (Throwable e) {
            throw new RuntimeException("setShutdownTimeout failed", e);
        }
    }

    /**
     * Layout for conduit_message_log_config_t (64-bit LP64 ABI):
     *   0  int    enabled
     *   4  int    mode
     *   8  int    output
     *  12  (pad4)
     *  16  char*  directory
     *  24  char*  prefix
     *  32  char*  filename          (nullable)
     *  40  char*  sent_filename     (nullable)
     *  48  char*  received_filename (nullable)
     *  56  int    include_message_content
     *  60  (pad4)
     * Total: 64 bytes
     */
    private static final int MSG_LOG_CONFIG_SIZE = 64;

    @Override
    public int setMessageLogConfig(long handle, boolean enabled, int mode, int output,
                                   String directory, String prefix, String filename,
                                   String sentFilename, String receivedFilename,
                                   boolean includeMessageContent, boolean includeRawBytes) {
        try {
            var cfg = arena.allocate(MSG_LOG_CONFIG_SIZE, 8);
            cfg.set(ValueLayout.JAVA_INT,  0, enabled ? 1 : 0);
            cfg.set(ValueLayout.JAVA_INT,  4, mode);
            cfg.set(ValueLayout.JAVA_INT,  8, output);
            cfg.set(ValueLayout.ADDRESS,  16, directory        != null ? arena.allocateUtf8String(directory)        : MemorySegment.NULL);
            cfg.set(ValueLayout.ADDRESS,  24, prefix           != null ? arena.allocateUtf8String(prefix)           : MemorySegment.NULL);
            cfg.set(ValueLayout.ADDRESS,  32, filename         != null ? arena.allocateUtf8String(filename)         : MemorySegment.NULL);
            cfg.set(ValueLayout.ADDRESS,  40, sentFilename     != null ? arena.allocateUtf8String(sentFilename)     : MemorySegment.NULL);
            cfg.set(ValueLayout.ADDRESS,  48, receivedFilename != null ? arena.allocateUtf8String(receivedFilename) : MemorySegment.NULL);
            cfg.set(ValueLayout.JAVA_INT, 56, includeMessageContent ? 1 : 0);
            cfg.set(ValueLayout.JAVA_INT, 60, includeRawBytes ? 1 : 0);
            return (int) CabiBindings.conduit_set_message_log_config.invokeExact(
                MemorySegment.ofAddress(handle), cfg);
        } catch (Throwable e) {
            throw new RuntimeException("setMessageLogConfig failed", e);
        }
    }

    // ================================================================
    // Logger configuration
    // ================================================================

    @Override
    public void setLogLevel(int level) {
        try {
            CabiBindings.conduit_set_log_level.invokeExact(level);
        } catch (Throwable e) {
            throw new RuntimeException("setLogLevel failed", e);
        }
    }

    @Override
    public int getLogLevel() {
        try {
            return (int) CabiBindings.conduit_get_log_level.invokeExact();
        } catch (Throwable e) {
            throw new RuntimeException("getLogLevel failed", e);
        }
    }

    @Override
    public void logAddConsoleSink(boolean useStderr, boolean colorize) {
        try {
            CabiBindings.conduit_log_add_console_sink.invokeExact(
                useStderr ? 1 : 0, colorize ? 1 : 0);
        } catch (Throwable e) {
            throw new RuntimeException("logAddConsoleSink failed", e);
        }
    }

    @Override
    public void logAddFileSink(String path, boolean append) {
        try {
            var pathStr = arena.allocateUtf8String(path);
            CabiBindings.conduit_log_add_file_sink.invokeExact(pathStr, append ? 1 : 0);
        } catch (Throwable e) {
            throw new RuntimeException("logAddFileSink failed", e);
        }
    }

    @Override
    public void logClearSinks() {
        try {
            CabiBindings.conduit_log_clear_sinks.invokeExact();
        } catch (Throwable e) {
            throw new RuntimeException("logClearSinks failed", e);
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
            if (syncPattern.length > 0) {
                var syncBuf = arena.allocateArray(ValueLayout.JAVA_BYTE, syncPattern);
                cfg.set(ValueLayout.ADDRESS, 0, syncBuf);
                cfg.set(ValueLayout.JAVA_LONG, 8, (long) syncPattern.length);
            } else {
                cfg.set(ValueLayout.ADDRESS, 0, MemorySegment.NULL);
                cfg.set(ValueLayout.JAVA_LONG, 8, 0L);
            }
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
            callback.onStateChange(peerId, Transceiver.ConnectionState.fromValue(newState));
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
