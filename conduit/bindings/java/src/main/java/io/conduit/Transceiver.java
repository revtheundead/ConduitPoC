// SPDX-License-Identifier: MIT
package io.conduit;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * Java wrapper for the Conduit Transceiver.
 * <p>
 * Provides a Java API over libconduit_cabi with AutoCloseable lifecycle management.
 * Uses the Java Foreign Function and Memory API (JDK 21+, Panama FFI).
 *
 * <pre>{@code
 * try (var t = new Transceiver()) {
 *     t.addPeer("radar", "my_session", TransportConfig.udp("0.0.0.0:5000"));
 *     t.start();
 *     // ...
 * }
 * }</pre>
 */
public class Transceiver implements AutoCloseable {

    private volatile MemorySegment handle;
    private final Arena arena;

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
            var nameStr = arena.allocateFrom(name);
            var sessionStr = arena.allocateFrom(sessionName);

            // Allocate transport config struct with correct layout
            var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
            cfg.set(ValueLayout.JAVA_INT, 0, transport.type().value());
            var addrStr = arena.allocateFrom(transport.address());
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
            var nameStr = arena.allocateFrom(name);
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

    /**
     * Send raw bytes as a message.
     *
     * @param peerId  Target peer ID
     * @param typeId  Message type ID
     * @param data    Raw message payload
     */
    public void send(int peerId, long typeId, byte[] data) {
        try (var sendArena = Arena.ofConfined()) {
            var buf = sendArena.allocateFrom(ValueLayout.JAVA_BYTE, data);
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

    /** Get the library version string. */
    public static String version() {
        try {
            var ptr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
            if (ptr == MemorySegment.NULL) return "";
            return ptr.reinterpret(256).getString(0);
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
        if (arena.scope().isAlive()) {
            arena.close();
        }
    }
}
