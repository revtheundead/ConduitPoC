// SPDX-License-Identifier: MIT
package io.conduit;

import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

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

    /** Connection state enum (mirrors C++ {@code conduit::net::ConnectionState}). */
    public enum ConnectionState {
        DISCONNECTED(0),
        CONNECTING(1),
        CONNECTED(2),
        RECONNECTING(3),
        FAILED(4);

        public final int value;
        ConnectionState(int v) { this.value = v; }

        /** Convert a raw integer state to the enum. */
        public static ConnectionState fromValue(int v) {
            switch (v) {
                case 0: return DISCONNECTED;
                case 1: return CONNECTING;
                case 2: return CONNECTED;
                case 3: return RECONNECTING;
                case 4: return FAILED;
                default: return DISCONNECTED;
            }
        }
    }

    @FunctionalInterface
    public interface StateCallback {
        void onStateChange(int peerId, ConnectionState newState);
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
    // Message logging enums and config (mirrors C++ MessageLogMode / MessageLogOutput)
    // ================================================================

    /** Message-log grouping mode (mirrors C++ {@code MessageLogMode}). */
    public enum MessageLogMode {
        /** All messages in a single file. */
        COMBINED(0),
        /** Separate files for sent and received. */
        SEPARATE_DIRECTION(1),
        /** One file per peer (both directions). */
        PER_PEER(2),
        /** Separate sent/received files per peer. */
        PER_PEER_DIRECTION(3);

        public final int value;
        MessageLogMode(int v) { this.value = v; }
    }

    /** Message-log output destination (mirrors C++ {@code MessageLogOutput}). */
    public enum MessageLogOutput {
        /** Write to log files. */
        FILE(0),
        /** Write to stdout. */
        STDOUT(1),
        /** Write to both files and stdout. */
        BOTH(2);

        public final int value;
        MessageLogOutput(int v) { this.value = v; }
    }

    /**
     * Message logging configuration (mirrors C++ {@code MessageLogConfig}).
     * <p>
     * Use with {@link #setMessageLogConfig(MessageLogConfig)}.
     *
     * <pre>{@code
     * var cfg = new Transceiver.MessageLogConfig();
     * cfg.enabled = true;
     * cfg.mode    = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
     * cfg.output  = Transceiver.MessageLogOutput.FILE;
     * cfg.directory = "./logs";
     * cfg.prefix    = "poc";
     * tx.setMessageLogConfig(cfg);
     * }</pre>
     */
    public static final class MessageLogConfig {
        public boolean        enabled                = false;
        public MessageLogMode mode                   = MessageLogMode.COMBINED;
        public MessageLogOutput output               = MessageLogOutput.FILE;
        public String         directory              = ".";
        public String         prefix                 = "conduit";
        /** Filename pattern; supports {@code {peer}} and {@code {direction}}. */
        public String         filename               = null;
        public String         sentFilename           = null;
        public String         receivedFilename       = null;
        public boolean        includeMessageContent  = true;
        /** Include hex dump of raw wire bytes (opt-in). */
        public boolean        includeRawBytes        = false;
    }

    // ================================================================
    // Instance state
    // ================================================================

    private final NativeBinding binding;
    private volatile long handle;

    // Java-side session for passthrough mode (no protocol-specific native .so)
    private Object javaSession;
    private java.lang.reflect.Method sessionEncodeWrap;
    private java.lang.reflect.Method sessionDecodeFrame;
    private java.lang.reflect.Method sessionFormatMessage;
    private java.lang.reflect.Method sessionFormatOutbound;
    private java.lang.reflect.Method sessionTypeName;
    private volatile boolean logIncludeContent;
    private final ConcurrentHashMap<Long, CopyOnWriteArrayList<TypedMessageCallback<?>>>
        sessionHandlers = new ConcurrentHashMap<>();

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

    // ================================================================
    // Pre-start configuration (call before start())
    // ================================================================

    /**
     * Configure the receive queue. Must be called before {@link #start()}.
     *
     * @param capacity               Queue capacity (default 1024)
     * @param dropPolicy             0=DropOldest, 1=DropNewest, 2=Block
     * @param backPressureThreshold  0.0=disabled, 0.8=pause at 80%
     */
    public void setQueueConfig(long capacity, int dropPolicy,
                               double backPressureThreshold) {
        int err = binding.setQueueConfig(handle, capacity, dropPolicy,
                                         backPressureThreshold);
        if (err != 0) throw new ConduitError(err, "Failed to set queue config");
    }

    /**
     * Configure worker threads. Must be called before {@link #start()}.
     *
     * @param threadCount      Number of dispatch threads (default 1)
     * @param handlerTimeoutMs Log warning if handler exceeds this (0=off)
     */
    public void setWorkerConfig(long threadCount, long handlerTimeoutMs) {
        int err = binding.setWorkerConfig(handle, threadCount, handlerTimeoutMs);
        if (err != 0) throw new ConduitError(err, "Failed to set worker config");
    }

    /**
     * Set the graceful shutdown timeout.
     *
     * @param timeoutMs Max time to wait for workers to drain (0=indefinite)
     */
    public void setShutdownTimeout(long timeoutMs) {
        int err = binding.setShutdownTimeout(handle, timeoutMs);
        if (err != 0) throw new ConduitError(err, "Failed to set shutdown timeout");
    }

    /**
     * Configure message logging using a {@link MessageLogConfig} object.
     * Must be called before {@link #start()}.
     *
     * <pre>{@code
     * var cfg = new Transceiver.MessageLogConfig();
     * cfg.enabled = true;
     * cfg.mode    = Transceiver.MessageLogMode.SEPARATE_DIRECTION;
     * cfg.output  = Transceiver.MessageLogOutput.FILE;
     * cfg.directory = "./logs";
     * cfg.prefix    = "poc";
     * tx.setMessageLogConfig(cfg);
     * }</pre>
     */
    public void setMessageLogConfig(MessageLogConfig cfg) {
        int err = binding.setMessageLogConfig(handle, cfg.enabled,
                cfg.mode.value, cfg.output.value,
                cfg.directory, cfg.prefix,
                cfg.filename, cfg.sentFilename, cfg.receivedFilename,
                cfg.includeMessageContent, cfg.includeRawBytes);
        if (err != 0) throw new ConduitError(err, "Failed to set message log config");
        this.logIncludeContent = cfg.includeMessageContent;
    }

    // ================================================================
    // Logger configuration (global — controls internal Conduit logging)
    // ================================================================

    /**
     * Set the global Conduit log level.
     * <p>
     * Controls internal diagnostic output (debug, warn, error, etc.).
     * This is distinct from message log config which records message traffic.
     *
     * @param level  0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Off
     */
    public void setLogLevel(int level) {
        binding.setLogLevel(level);
    }

    /**
     * Get the current global Conduit log level.
     *
     * @return Current log level (0=Trace through 6=Off)
     */
    public int getLogLevel() {
        return binding.getLogLevel();
    }

    /**
     * Add a console log sink for Conduit internal logging.
     *
     * @param useStderr  If true, log to stderr; otherwise stdout
     * @param colorize   If true, use ANSI color codes
     */
    public void logAddConsoleSink(boolean useStderr, boolean colorize) {
        binding.logAddConsoleSink(useStderr, colorize);
    }

    /**
     * Add a file log sink for Conduit internal logging.
     *
     * @param path    File path for log output
     * @param append  If true, append to existing file; otherwise overwrite
     */
    public void logAddFileSink(String path, boolean append) {
        binding.logAddFileSink(path, append);
    }

    /** Remove all Conduit internal log sinks. */
    public void logClearSinks() {
        binding.logClearSinks();
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
        int result = binding.addPeer(handle, name, sessionName, transport);
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
    public ConnectionState peerState(int peerId) {
        return ConnectionState.fromValue(binding.peerState(handle, peerId));
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
    // Session registration (passthrough — no protocol-specific .so)
    // ================================================================

    /**
     * Register a passthrough session using a bgen-generated Java session class.
     * <p>
     * This eliminates the need for protocol-specific native shared libraries.
     * The native transceiver handles transport and framing only; all message
     * encode/decode is done on the Java side via the generated session.
     *
     * @param name     Session name (e.g., "asterix", "asterix_alt")
     * @param session  Bgen-generated session object
     */
    public void registerSession(String name, Object session) {
        try {
            Class<?> cls = session.getClass();

            byte[] syncPattern = (byte[]) cls.getMethod("syncPattern").invoke(session);
            int minHeaderSize = (int) cls.getMethod("minFrameHeaderSize").invoke(session);
            long[] typeIds = (long[]) cls.getMethod("leafTypeIds").invoke(session);

            java.lang.reflect.Method typeNameMethod = cls.getMethod("typeName", long.class);
            java.lang.reflect.Method isReceiveOnlyMethod = cls.getMethod("isReceiveOnly", long.class);
            java.lang.reflect.Method extractMethod = cls.getMethod("extractFrameLength", byte[].class);

            String[] typeNames = new String[typeIds.length];
            int[] receiveOnly = new int[typeIds.length];
            for (int i = 0; i < typeIds.length; i++) {
                typeNames[i] = (String) typeNameMethod.invoke(session, typeIds[i]);
                receiveOnly[i] = (boolean) isReceiveOnlyMethod.invoke(session, typeIds[i]) ? 1 : 0;
            }

            // Probe the frame length extraction parameters
            int skipBits = probeFrameSkipBits(extractMethod, session, minHeaderSize);
            int fieldBits = (minHeaderSize * 8) - skipBits;
            boolean bigEndian = probeFrameEndianness(extractMethod, session, skipBits, fieldBits);

            int err = binding.registerPassthroughSession(
                name, syncPattern, minHeaderSize, skipBits, fieldBits,
                bigEndian, typeIds, typeNames, receiveOnly);
            if (err != 0) {
                throw new ConduitError(err, "Failed to register session '" + name + "'");
            }

            // Store session for Java-side encode/decode
            this.javaSession = session;
            this.sessionEncodeWrap = cls.getMethod("encodeWrap", long.class, Object.class);
            this.sessionDecodeFrame = cls.getMethod("decodeFrame", byte[].class);
            this.sessionFormatMessage = cls.getMethod("formatMessage", long.class, Object.class);
            this.sessionFormatOutbound = cls.getMethod("formatOutbound", long.class, Object.class, List.class);
            this.sessionTypeName = cls.getMethod("typeName", long.class);

            // Install a raw frame dispatcher: PassthroughSession delivers raw
            // frames with type_id=0.  We decode them here and fan out to the
            // Java-side typed handler registry.
            onAnyMessage((peerId, typeId, typeName, data) -> {
                try {
                    @SuppressWarnings("unchecked")
                    List<Map<String, Object>> messages =
                        (List<Map<String, Object>>)
                            sessionDecodeFrame.invoke(javaSession, data);
                    for (Map<String, Object> dm : messages) {
                        long tid = (Long) dm.get("type_id");
                        Object payload = dm.get("payload");

                        // Log each decoded message with its real type name
                        logDecodedRecv(peerId, tid, payload, data);

                        CopyOnWriteArrayList<TypedMessageCallback<?>> handlers =
                            sessionHandlers.get(tid);
                        if (handlers != null) {
                            for (TypedMessageCallback<?> h : handlers) {
                                @SuppressWarnings("unchecked")
                                TypedMessageCallback<Object> typed =
                                    (TypedMessageCallback<Object>) h;
                                typed.onMessage(peerId, payload);
                            }
                        }
                    }
                } catch (Exception e) {
                    // Frame decode failed — silently skip
                }
            });
        } catch (ConduitError e) {
            throw e;
        } catch (Exception e) {
            throw new RuntimeException("Failed to register session: " + e.getMessage(), e);
        }
    }

    private static int probeFrameSkipBits(java.lang.reflect.Method extractMethod,
                                           Object session, int headerSize) throws Exception {
        for (int skipBytes = 0; skipBytes < headerSize; skipBytes++) {
            byte[] header = new byte[headerSize];
            int testLen = 42;
            if (headerSize - skipBytes >= 2) {
                header[skipBytes] = (byte)((testLen >> 8) & 0xFF);
                header[skipBytes + 1] = (byte)(testLen & 0xFF);
                int result = (int) extractMethod.invoke(session, (Object) header);
                if (result == testLen) return skipBytes * 8;
                header[skipBytes] = (byte)(testLen & 0xFF);
                header[skipBytes + 1] = (byte)((testLen >> 8) & 0xFF);
                result = (int) extractMethod.invoke(session, (Object) header);
                if (result == testLen) return skipBytes * 8;
            }
        }
        return (headerSize - 2) * 8;
    }

    private static boolean probeFrameEndianness(java.lang.reflect.Method extractMethod,
                                                 Object session,
                                                 int skipBits, int fieldBits) throws Exception {
        int skipBytes = skipBits / 8;
        int fieldBytes = (fieldBits + 7) / 8;
        byte[] header = new byte[skipBytes + fieldBytes];
        int testLen = 0x0102;
        if (fieldBytes >= 2) {
            header[skipBytes] = (byte) 0x01;
            header[skipBytes + 1] = (byte) 0x02;
        }
        int result = (int) extractMethod.invoke(session, (Object) header);
        return result == testLen;
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

            if (sessionEncodeWrap != null) {
                // Use Java session to produce fully-framed bytes.
                // PassthroughSession passes them through to the transport as-is.
                @SuppressWarnings("unchecked")
                Map<String, Object> result =
                    (Map<String, Object>) sessionEncodeWrap.invoke(javaSession, typeId, msg);
                if (result == null) {
                    throw new RuntimeException(
                        "Session encodeWrap returned null for type " + cls.getSimpleName());
                }
                byte[] frameBytes = (byte[]) result.get("bytes");
                @SuppressWarnings("unchecked")
                List<String[]> autoFields = (List<String[]>) result.get("auto_fields");
                // Record the send attempt before invoking the transport so the
                // message log captures it even if the peer is disconnected and
                // sendRaw throws.  Mirrors the C++ Transceiver, which calls
                // message_log_->log_send before transport->send.
                logDecodedSend(peerId, typeId, msg, frameBytes, autoFields);
                sendRaw(peerId, typeId, frameBytes);
            } else {
                // Original path: encode message bytes, let C++ session wrap them.
                byte[] data = (byte[]) cls.getMethod("encodeBytes").invoke(msg);
                sendRaw(peerId, typeId, data);
            }
        } catch (ConduitError e) {
            throw e;
        } catch (NoSuchFieldException e) {
            throw new IllegalArgumentException(
                "Message class " + msg.getClass().getName() +
                " must have static TYPE_ID field", e);
        } catch (Throwable e) {
            throw new RuntimeException("send failed", e);
        }
    }

    /**
     * Send a typed message to the sole peer (convenience).
     * <p>
     * If no sole peer exists (e.g. a TCP server with no clients connected
     * yet, or a UDP listener that hasn't seen a remote), the underlying
     * {@link #solePeer()} lookup fails — but the message log still records
     * the attempt under {@code peer=<no-peer>} before the error propagates.
     * Mirrors the C++ Transceiver's "log before transport" contract.
     *
     * @param msg  Typed message object
     */
    public void send(Object msg) {
        if (msg == null) throw new NullPointerException("msg must not be null");
        int peerId;
        try {
            peerId = solePeer();
        } catch (ConduitError e) {
            // No usable peer ID — encode and log the attempt so the message
            // log captures it, then propagate the original error.
            logSendAttemptNoPeer(msg);
            throw e;
        }
        send(peerId, msg);
    }

    /**
     * Encode the outbound message and write a passthrough log entry tagged
     * with peer_id=0 so the C++ helper falls back to {@code peer=<no-peer>}.
     * Used when {@link #solePeer()} fails before the regular send path can
     * call {@link #logDecodedSend}.  Best-effort: any failure here is
     * swallowed because the caller is already about to throw the real
     * sole-peer error and we don't want to mask it.
     */
    private void logSendAttemptNoPeer(Object msg) {
        if (sessionEncodeWrap == null) return;  // not in passthrough mode
        try {
            long typeId = msg.getClass().getField("TYPE_ID").getLong(null);
            @SuppressWarnings("unchecked")
            Map<String, Object> result =
                (Map<String, Object>) sessionEncodeWrap.invoke(javaSession, typeId, msg);
            if (result == null) return;
            byte[] frameBytes = (byte[]) result.get("bytes");
            @SuppressWarnings("unchecked")
            List<String[]> autoFields = (List<String[]>) result.get("auto_fields");
            // peer_id=0 → find_peer misses → C++ helper logs peer="<no-peer>"
            logDecodedSend(0, typeId, msg, frameBytes, autoFields);
        } catch (Throwable ignored) {
            // best-effort — never let log failures hide the real send error
        }
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
        int err = binding.sendBatch(handle, peerId, typeId, payloads);
        if (err != 0) {
            throw new ConduitError(err, "sendBatch failed");
        }
    }

    // ================================================================
    // Message logging helpers (passthrough mode)
    // ================================================================

    private void logDecodedRecv(int peerId, long typeId, Object payload,
                                byte[] rawBytes) {
        if (sessionFormatMessage == null) return;
        try {
            String tname = (String) sessionTypeName.invoke(javaSession, typeId);
            if (tname == null) tname = "unknown";
            String content = logIncludeContent
                ? (String) sessionFormatMessage.invoke(javaSession, typeId, payload)
                : null;
            binding.logRecvMessage(handle, peerId, tname, rawBytes.length, content, rawBytes);
        } catch (Exception e) {
            System.err.println("[conduit] message log error: " + e.getMessage());
        }
    }

    private void logDecodedSend(int peerId, long typeId, Object payload,
                                byte[] rawBytes, List<String[]> autoFields) {
        if (sessionFormatMessage == null) return;
        try {
            String tname = (String) sessionTypeName.invoke(javaSession, typeId);
            if (tname == null) tname = "unknown";
            String content = null;
            if (logIncludeContent) {
                if (sessionFormatOutbound != null && autoFields != null) {
                    content = (String) sessionFormatOutbound.invoke(
                        javaSession, typeId, payload, autoFields);
                } else {
                    content = (String) sessionFormatMessage.invoke(
                        javaSession, typeId, payload);
                }
            }
            binding.logSendMessage(handle, peerId, tname, rawBytes.length, content, rawBytes);
        } catch (Exception e) {
            System.err.println("[conduit] message log error: " + e.getMessage());
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

            if (sessionDecodeFrame != null) {
                // Java-side dispatch: the onAnyMessage frame dispatcher
                // decodes frames and fans out to these typed handlers.
                sessionHandlers
                    .computeIfAbsent(typeId, k -> new CopyOnWriteArrayList<>())
                    .add(callback);
                return 0;
            }

            // Original path: register with native binding for per-message decode.
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
        } catch (NoSuchFieldException e) {
            throw new IllegalArgumentException(
                "Class " + msgClass.getName() +
                " must have static TYPE_ID field", e);
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
     * Remove a message handler for the given type_id.
     * <p>
     * The {@code peerId} parameter is reserved for future use and currently
     * ignored — handlers are scoped transceiver-wide and a removal affects
     * all peers.
     *
     * @param peerId  Peer ID (currently ignored)
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
        // Use the instance binding if available, otherwise create a temporary one
        try (NativeBinding b = ConduitNative.createBinding()) {
            return b.version();
        } catch (Exception e) {
            return "";
        }
    }

    /** Get the library version string using this transceiver's binding. */
    public String versionInstance() {
        return binding.version();
    }

    /** Get the native backend in use by this transceiver. */
    public String backendName() {
        return binding.getClass().getSimpleName();
    }

    @Override
    public synchronized void close() {
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
