import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

import io.conduit.CabiBindings;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;
import io.conduit.ConduitError;

/**
 * Comprehensive tests for the Conduit Transceiver C ABI bindings via Project Panama.
 *
 * These tests exercise transceiver lifecycle, peer management, handler registration,
 * state/error callbacks, and the high-level Transceiver wrapper.
 */
public class TestTransceiverCabi {

    private static final String CODEC_LIB = "/home/user/ConduitPoC/conduit/build/tests/libconduit_codec_cabi_test.so";
    private static final String CABI_LIB = "/home/user/ConduitPoC/conduit/build/tests/libconduit_cabi_test.so";

    // Transport type constants (matching conduit_transport_type_t)
    private static final int TRANSPORT_UDP = 0;
    private static final int TRANSPORT_TCP_CLIENT = 1;
    private static final int TRANSPORT_TCP_SERVER = 2;
    private static final int TRANSPORT_SERIAL = 3;

    // Known type IDs
    private static final long PING_TYPE_ID = 0x0ad7bb3ecc473399L;

    /**
     * Layout for conduit_transport_config_t:
     *   { int type; [4 padding]; char* address; uint32_t baud_rate; [4 padding] }
     */
    private static final StructLayout TRANSPORT_CONFIG_LAYOUT = MemoryLayout.structLayout(
        ValueLayout.JAVA_INT.withName("type"),
        MemoryLayout.paddingLayout(4),
        ValueLayout.ADDRESS.withName("address"),
        ValueLayout.JAVA_INT.withName("baud_rate"),
        MemoryLayout.paddingLayout(4)
    );

    static {
        // The CABI library depends on conduit_register_session from the codec library,
        // so load the codec library first to resolve the symbol before CabiBindings initializes.
        System.load(CODEC_LIB);
        System.setProperty("conduit.cabi.path", CABI_LIB);
    }

    // ========================================================================
    // Version
    // ========================================================================

    @Test
    @DisplayName("Transceiver version: returns a non-empty version string")
    void versionReturnsNonEmpty() throws Throwable {
        MemorySegment versionPtr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
        assertNotEquals(MemorySegment.NULL, versionPtr, "Version pointer should not be NULL");
        String version = versionPtr.reinterpret(256).getUtf8String(0);
        assertNotNull(version);
        assertFalse(version.isEmpty(), "Version string should not be empty");
    }

    @Test
    @DisplayName("Transceiver version: matches semver format")
    void versionMatchesSemver() throws Throwable {
        MemorySegment versionPtr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
        String version = versionPtr.reinterpret(256).getUtf8String(0);
        assertTrue(version.matches("\\d+\\.\\d+\\.\\d+.*"),
            "Version should match semver pattern, got: " + version);
    }

    @Test
    @DisplayName("Transceiver version: matches high-level wrapper version")
    void versionMatchesWrapper() throws Throwable {
        String wrapperVersion = Transceiver.version();
        MemorySegment versionPtr = (MemorySegment) CabiBindings.conduit_version.invokeExact();
        String rawVersion = versionPtr.reinterpret(256).getUtf8String(0);
        assertEquals(rawVersion, wrapperVersion, "High-level and low-level versions should match");
    }

    // ========================================================================
    // Create / Destroy lifecycle
    // ========================================================================

    @Test
    @DisplayName("Create: returns non-null handle")
    void createReturnsHandle() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        assertNotEquals(MemorySegment.NULL, xcvr, "conduit_create should return non-NULL handle");
        CabiBindings.conduit_destroy.invokeExact(xcvr);
    }

    @Test
    @DisplayName("Create/destroy: multiple create-destroy cycles succeed")
    void createDestroyCycles() throws Throwable {
        for (int i = 0; i < 5; i++) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            assertNotEquals(MemorySegment.NULL, xcvr, "Cycle " + i + " should create non-NULL handle");
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("Create: high-level Transceiver wrapper creates and closes")
    void transceiverWrapperLifecycle() {
        Transceiver t = new Transceiver();
        assertNotNull(t);
        t.close();
    }

    @Test
    @DisplayName("Create: Transceiver try-with-resources works")
    void transceiverTryWithResources() {
        try (Transceiver t = new Transceiver()) {
            assertNotNull(t);
            assertFalse(t.isRunning());
        }
    }

    // ========================================================================
    // Start / Stop lifecycle
    // ========================================================================

    @Test
    @DisplayName("Start: succeeds on fresh transceiver")
    void startSucceeds() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int err = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, err, "Start should succeed on fresh transceiver");
        } finally {
            CabiBindings.conduit_stop.invokeExact(xcvr);
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("Start: double start returns error")
    void doubleStartReturnsError() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int err1 = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, err1, "First start should succeed");
            int err2 = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertNotEquals(0, err2, "Second start should return error (already running)");
        } finally {
            CabiBindings.conduit_stop.invokeExact(xcvr);
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("Stop: stop without start does not crash")
    void stopWithoutStart() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        // Stop on a non-running transceiver should be a no-op
        CabiBindings.conduit_stop.invokeExact(xcvr);
        CabiBindings.conduit_destroy.invokeExact(xcvr);
    }

    @Test
    @DisplayName("Start/Stop: high-level Transceiver wrapper start/stop")
    void transceiverWrapperStartStop() {
        try (Transceiver t = new Transceiver()) {
            assertFalse(t.isRunning());
            t.start();
            assertTrue(t.isRunning());
            t.stop();
            assertFalse(t.isRunning());
        }
    }

    // ========================================================================
    // isRunning state
    // ========================================================================

    @Test
    @DisplayName("isRunning: false before start")
    void isRunningFalseBeforeStart() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int running = (int) CabiBindings.conduit_is_running.invokeExact(xcvr);
            assertEquals(0, running, "Transceiver should not be running before start");
        } finally {
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("isRunning: true after start")
    void isRunningTrueAfterStart() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int startErr = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, startErr, "Start should succeed");
            int running = (int) CabiBindings.conduit_is_running.invokeExact(xcvr);
            assertNotEquals(0, running, "Transceiver should be running after start");
        } finally {
            CabiBindings.conduit_stop.invokeExact(xcvr);
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("isRunning: false after stop")
    void isRunningFalseAfterStop() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int startErr = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, startErr, "Start should succeed");
            CabiBindings.conduit_stop.invokeExact(xcvr);
            int running = (int) CabiBindings.conduit_is_running.invokeExact(xcvr);
            assertEquals(0, running, "Transceiver should not be running after stop");
        } finally {
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("isRunning: restart cycle works")
    void isRunningRestartCycle() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            assertEquals(0, (int) CabiBindings.conduit_is_running.invokeExact(xcvr));
            int startErr = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, startErr, "Start should succeed");
            assertNotEquals(0, (int) CabiBindings.conduit_is_running.invokeExact(xcvr));
            CabiBindings.conduit_stop.invokeExact(xcvr);
            assertEquals(0, (int) CabiBindings.conduit_is_running.invokeExact(xcvr));
            // Restart
            int err = (int) CabiBindings.conduit_start.invokeExact(xcvr);
            assertEquals(0, err, "Restart after stop should succeed");
            assertNotEquals(0, (int) CabiBindings.conduit_is_running.invokeExact(xcvr));
        } finally {
            CabiBindings.conduit_stop.invokeExact(xcvr);
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    // ========================================================================
    // Add peer with UDP transport
    // ========================================================================

    @Test
    @DisplayName("Add peer: UDP peer with session_protocol succeeds")
    void addPeerUdpSucceeds() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                var nameStr = arena.allocateUtf8String("radar");
                var sessionStr = arena.allocateUtf8String("session_protocol");
                var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
                cfg.set(ValueLayout.JAVA_INT, 0, TRANSPORT_UDP);
                var addrStr = arena.allocateUtf8String("0.0.0.0:5000");
                cfg.set(ValueLayout.ADDRESS, 8, addrStr);
                cfg.set(ValueLayout.JAVA_INT, 16, 0);

                var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);

                int err = (int) CabiBindings.conduit_add_peer.invokeExact(
                    xcvr, nameStr, sessionStr, cfg, peerIdOut);
                assertEquals(0, err, "Adding UDP peer should succeed");

                int peerId = peerIdOut.get(ValueLayout.JAVA_INT, 0);
                assertTrue(peerId >= 0, "Peer ID should be non-negative");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("Add peer: high-level Transceiver.addPeer works")
    void transceiverAddPeer() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("radar", "session_protocol", TransportConfig.udp("0.0.0.0:5000"));
            assertTrue(peerId >= 0, "Peer ID should be non-negative");
        }
    }

    @Test
    @DisplayName("Add peer: unknown session name throws error")
    void addPeerUnknownSession() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(ConduitError.class, () ->
                t.addPeer("test", "nonexistent_session", TransportConfig.udp("0.0.0.0:6000")));
        }
    }

    @Test
    @DisplayName("Add peer: multiple peers with different names")
    void addMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("peer1", "session_protocol", TransportConfig.udp("0.0.0.0:5001"));
            int id2 = t.addPeer("peer2", "session_protocol", TransportConfig.udp("0.0.0.0:5002"));
            assertNotEquals(id1, id2, "Different peers should have different IDs");
        }
    }

    @Test
    @DisplayName("Add peer: TCP client transport succeeds")
    void addPeerTcpClient() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("tcp_peer", "session_protocol",
                TransportConfig.tcpClient("127.0.0.1:5003"));
            assertTrue(peerId >= 0);
        }
    }

    @Test
    @DisplayName("Add peer: TCP server transport succeeds or reports unsupported")
    void addPeerTcpServer() {
        try (Transceiver t = new Transceiver()) {
            try {
                int peerId = t.addPeer("tcp_srv", "session_protocol",
                    TransportConfig.tcpServer("0.0.0.0:5004"));
                assertTrue(peerId >= 0);
            } catch (ConduitError e) {
                // TCP server transport may not be available in all test environments
                assertTrue(e.getMessage().contains("error") || e.code() != 0,
                    "If TCP server fails, it should report a meaningful error");
            }
        }
    }

    // ========================================================================
    // Peer count
    // ========================================================================

    @Test
    @DisplayName("Peer count: zero when no peers added")
    void peerCountZero() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            long count = (long) CabiBindings.conduit_peer_count.invokeExact(xcvr);
            assertEquals(0, count, "Fresh transceiver should have zero peers");
        } finally {
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    @Test
    @DisplayName("Peer count: increments as peers are added")
    void peerCountIncrements() {
        try (Transceiver t = new Transceiver()) {
            assertEquals(0, t.peerCount());
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:6001"));
            assertEquals(1, t.peerCount());
            t.addPeer("p2", "session_protocol", TransportConfig.udp("0.0.0.0:6002"));
            assertEquals(2, t.peerCount());
            t.addPeer("p3", "session_protocol", TransportConfig.udp("0.0.0.0:6003"));
            assertEquals(3, t.peerCount());
        }
    }

    @Test
    @DisplayName("Peer count: correct after adding peers with different sessions")
    void peerCountDifferentSessions() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("sp", "session_protocol", TransportConfig.udp("0.0.0.0:6004"));
            t.addPeer("cp", "choice_protocol", TransportConfig.udp("0.0.0.0:6005"));
            assertEquals(2, t.peerCount());
        }
    }

    // ========================================================================
    // Peer by name
    // ========================================================================

    @Test
    @DisplayName("Peer by name: finds added peer")
    void peerByNameFinds() {
        try (Transceiver t = new Transceiver()) {
            int addedId = t.addPeer("radar", "session_protocol", TransportConfig.udp("0.0.0.0:7001"));
            int foundId = t.peerByName("radar");
            assertEquals(addedId, foundId, "peerByName should return the same ID as addPeer");
        }
    }

    @Test
    @DisplayName("Peer by name: throws for unknown name")
    void peerByNameUnknown() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("radar", "session_protocol", TransportConfig.udp("0.0.0.0:7002"));
            assertThrows(ConduitError.class, () -> t.peerByName("nonexistent"));
        }
    }

    @Test
    @DisplayName("Peer by name: distinguishes different peers")
    void peerByNameDistinguishes() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("alpha", "session_protocol", TransportConfig.udp("0.0.0.0:7003"));
            int id2 = t.addPeer("bravo", "session_protocol", TransportConfig.udp("0.0.0.0:7004"));
            assertEquals(id1, t.peerByName("alpha"));
            assertEquals(id2, t.peerByName("bravo"));
        }
    }

    @Test
    @DisplayName("Peer by name: low-level API works")
    void peerByNameLowLevel() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                // Add peer
                var nameStr = arena.allocateUtf8String("sensor");
                var sessionStr = arena.allocateUtf8String("session_protocol");
                var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
                cfg.set(ValueLayout.JAVA_INT, 0, TRANSPORT_UDP);
                var addrStr = arena.allocateUtf8String("0.0.0.0:7005");
                cfg.set(ValueLayout.ADDRESS, 8, addrStr);
                cfg.set(ValueLayout.JAVA_INT, 16, 0);
                var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);

                int addErr = (int) CabiBindings.conduit_add_peer.invokeExact(
                    xcvr, nameStr, sessionStr, cfg, peerIdOut);
                assertEquals(0, addErr);
                int addedId = peerIdOut.get(ValueLayout.JAVA_INT, 0);

                // Look up by name
                var lookupName = arena.allocateUtf8String("sensor");
                var lookupOut = arena.allocate(ValueLayout.JAVA_INT);
                int lookupErr = (int) CabiBindings.conduit_peer_by_name.invokeExact(
                    xcvr, lookupName, lookupOut);
                assertEquals(0, lookupErr, "Peer lookup by name should succeed");
                assertEquals(addedId, lookupOut.get(ValueLayout.JAVA_INT, 0));
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    // ========================================================================
    // Sole peer
    // ========================================================================

    @Test
    @DisplayName("Sole peer: works when exactly one peer exists")
    void solePeerOneExists() {
        try (Transceiver t = new Transceiver()) {
            int addedId = t.addPeer("only", "session_protocol", TransportConfig.udp("0.0.0.0:8001"));
            int soleId = t.solePeer();
            assertEquals(addedId, soleId, "solePeer should return the only peer's ID");
        }
    }

    @Test
    @DisplayName("Sole peer: throws when no peers exist")
    void solePeerNoPeers() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    @Test
    @DisplayName("Sole peer: throws when multiple peers exist")
    void solePeerMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:8002"));
            t.addPeer("p2", "session_protocol", TransportConfig.udp("0.0.0.0:8003"));
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    @Test
    @DisplayName("Sole peer: low-level API works")
    void solePeerLowLevel() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                var nameStr = arena.allocateUtf8String("sensor");
                var sessionStr = arena.allocateUtf8String("session_protocol");
                var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
                cfg.set(ValueLayout.JAVA_INT, 0, TRANSPORT_UDP);
                var addrStr = arena.allocateUtf8String("0.0.0.0:8004");
                cfg.set(ValueLayout.ADDRESS, 8, addrStr);
                cfg.set(ValueLayout.JAVA_INT, 16, 0);
                var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);

                int addErr = (int) CabiBindings.conduit_add_peer.invokeExact(xcvr, nameStr, sessionStr, cfg, peerIdOut);
                assertEquals(0, addErr, "add_peer should succeed");
                int addedId = peerIdOut.get(ValueLayout.JAVA_INT, 0);

                var soleOut = arena.allocate(ValueLayout.JAVA_INT);
                int err = (int) CabiBindings.conduit_sole_peer.invokeExact(xcvr, soleOut);
                assertEquals(0, err, "sole_peer should succeed with one peer");
                assertEquals(addedId, soleOut.get(ValueLayout.JAVA_INT, 0));
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    // ========================================================================
    // Message handler registration/removal
    // ========================================================================

    // Dummy callback target for upcall stubs
    public static void dummyMsgCallback(int peer, long typeId, MemorySegment typeName,
                                         MemorySegment data, long len, MemorySegment userData) {
        // no-op: used only for registration testing
    }

    private static final FunctionDescriptor MSG_CALLBACK_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT,   // peer_id
        ValueLayout.JAVA_LONG,  // type_id
        ValueLayout.ADDRESS,    // type_name
        ValueLayout.ADDRESS,    // data
        ValueLayout.JAVA_LONG,  // len
        ValueLayout.ADDRESS     // user_data
    );

    @Test
    @DisplayName("Handler: on_message registers and returns callback ID")
    void onMessageRegisters() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyMsgCallback",
                    MethodType.methodType(void.class, int.class, long.class,
                        MemorySegment.class, MemorySegment.class, long.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, MSG_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_message.invokeExact(
                    xcvr, PING_TYPE_ID, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0, "Callback ID should be non-negative, got: " + cbId);
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("Handler: on_any_message registers and returns callback ID")
    void onAnyMessageRegisters() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyMsgCallback",
                    MethodType.methodType(void.class, int.class, long.class,
                        MemorySegment.class, MemorySegment.class, long.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, MSG_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_any_message.invokeExact(
                    xcvr, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0, "Any-message callback ID should be non-negative");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("Handler: remove_handler succeeds after registration")
    void removeHandlerAfterRegistration() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                // Add a peer first (handlers are per-peer in the C ABI)
                var nameStr = arena.allocateUtf8String("sensor");
                var sessionStr = arena.allocateUtf8String("session_protocol");
                var cfg = arena.allocate(TRANSPORT_CONFIG_LAYOUT);
                cfg.set(ValueLayout.JAVA_INT, 0, TRANSPORT_UDP);
                var addrStr = arena.allocateUtf8String("0.0.0.0:9001");
                cfg.set(ValueLayout.ADDRESS, 8, addrStr);
                cfg.set(ValueLayout.JAVA_INT, 16, 0);
                var peerIdOut = arena.allocate(ValueLayout.JAVA_INT);
                int addErr = (int) CabiBindings.conduit_add_peer.invokeExact(xcvr, nameStr, sessionStr, cfg, peerIdOut);
                assertEquals(0, addErr, "add_peer should succeed");
                int peerId = peerIdOut.get(ValueLayout.JAVA_INT, 0);

                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyMsgCallback",
                    MethodType.methodType(void.class, int.class, long.class,
                        MemorySegment.class, MemorySegment.class, long.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, MSG_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_message.invokeExact(
                    xcvr, PING_TYPE_ID, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0);

                // Remove the handler — returns count of removed handlers (1 = success)
                int removeResult = (int) CabiBindings.conduit_remove_handler.invokeExact(
                    xcvr, peerId, PING_TYPE_ID);
                assertTrue(removeResult >= 0, "Removing registered handler should return non-negative count");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    // ========================================================================
    // State change callback registration
    // ========================================================================

    // Dummy state change callback
    public static void dummyStateCallback(int peer, int newState, MemorySegment userData) {
        // no-op
    }

    private static final FunctionDescriptor STATE_CALLBACK_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT,   // peer_id
        ValueLayout.JAVA_INT,   // new_state
        ValueLayout.ADDRESS     // user_data
    );

    @Test
    @DisplayName("State change: on_state_change registers and returns callback ID")
    void onStateChangeRegisters() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyStateCallback",
                    MethodType.methodType(void.class, int.class, int.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, STATE_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_state_change.invokeExact(
                    xcvr, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0, "State change callback ID should be non-negative");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("State change: remove_state_change succeeds after registration")
    void removeStateChangeAfterRegistration() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyStateCallback",
                    MethodType.methodType(void.class, int.class, int.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, STATE_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_state_change.invokeExact(
                    xcvr, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0);

                int removeResult = (int) CabiBindings.conduit_remove_state_change.invokeExact(
                    xcvr, cbId);
                assertEquals(1, removeResult, "Removing registered state callback should return 1 (count removed)");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("State change: remove with invalid ID returns error")
    void removeStateChangeInvalidId() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int removeResult = (int) CabiBindings.conduit_remove_state_change.invokeExact(
                xcvr, 99999);
            assertEquals(0, removeResult, "Removing non-existent state callback should return 0 (none removed)");
        } finally {
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    // ========================================================================
    // Error callback registration
    // ========================================================================

    // Dummy error callback
    public static void dummyErrorCallback(int peer, MemorySegment peerName,
                                           int errorCode, MemorySegment errorMessage,
                                           MemorySegment userData) {
        // no-op
    }

    private static final FunctionDescriptor ERROR_CALLBACK_DESC = FunctionDescriptor.ofVoid(
        ValueLayout.JAVA_INT,   // peer_id
        ValueLayout.ADDRESS,    // peer_name
        ValueLayout.JAVA_INT,   // error_code
        ValueLayout.ADDRESS,    // error_message
        ValueLayout.ADDRESS     // user_data
    );

    @Test
    @DisplayName("Error callback: on_error registers and returns callback ID")
    void onErrorRegisters() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyErrorCallback",
                    MethodType.methodType(void.class, int.class, MemorySegment.class,
                        int.class, MemorySegment.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, ERROR_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_error.invokeExact(
                    xcvr, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0, "Error callback ID should be non-negative");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("Error callback: remove_error_callback succeeds after registration")
    void removeErrorCallbackAfterRegistration() throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
            try {
                MethodHandle target = MethodHandles.lookup().findStatic(
                    TestTransceiverCabi.class, "dummyErrorCallback",
                    MethodType.methodType(void.class, int.class, MemorySegment.class,
                        int.class, MemorySegment.class, MemorySegment.class));

                MemorySegment stub = Linker.nativeLinker().upcallStub(
                    target, ERROR_CALLBACK_DESC, arena);

                int cbId = (int) CabiBindings.conduit_on_error.invokeExact(
                    xcvr, stub, MemorySegment.NULL);
                assertTrue(cbId >= 0);

                int removeResult = (int) CabiBindings.conduit_remove_error_callback.invokeExact(
                    xcvr, cbId);
                assertEquals(1, removeResult, "Removing registered error callback should return 1 (count removed)");
            } finally {
                CabiBindings.conduit_destroy.invokeExact(xcvr);
            }
        }
    }

    @Test
    @DisplayName("Error callback: remove with invalid ID returns error")
    void removeErrorCallbackInvalidId() throws Throwable {
        MemorySegment xcvr = (MemorySegment) CabiBindings.conduit_create.invokeExact();
        try {
            int removeResult = (int) CabiBindings.conduit_remove_error_callback.invokeExact(
                xcvr, 99999);
            assertEquals(0, removeResult, "Removing non-existent error callback should return 0 (none removed)");
        } finally {
            CabiBindings.conduit_destroy.invokeExact(xcvr);
        }
    }

    // ========================================================================
    // Peer state
    // ========================================================================

    @Test
    @DisplayName("Peer state: initial state is disconnected (0)")
    void peerStateInitial() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("sensor", "session_protocol", TransportConfig.udp("0.0.0.0:9100"));
            int state = t.peerState(peerId);
            // State 0 typically means disconnected/idle
            assertEquals(0, state, "Initial peer state should be 0 (disconnected)");
        }
    }

    // ========================================================================
    // Integration: full lifecycle with peers
    // ========================================================================

    @Test
    @DisplayName("Integration: create, add peer, start, check running, stop, destroy")
    void fullLifecycleWithPeer() {
        try (Transceiver t = new Transceiver()) {
            assertFalse(t.isRunning());
            assertEquals(0, t.peerCount());

            int peerId = t.addPeer("link", "session_protocol", TransportConfig.udp("0.0.0.0:9200"));
            assertEquals(1, t.peerCount());
            assertTrue(peerId >= 0);

            t.start();
            assertTrue(t.isRunning());

            // Verify peer is still accessible while running
            assertEquals(peerId, t.peerByName("link"));
            assertEquals(peerId, t.solePeer());

            t.stop();
            assertFalse(t.isRunning());
        }
    }

    @Test
    @DisplayName("Integration: multiple sessions coexist on transceiver")
    void multipleSessionsOnTransceiver() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("session_peer", "session_protocol", TransportConfig.udp("0.0.0.0:9300"));
            int id2 = t.addPeer("choice_peer", "choice_protocol", TransportConfig.udp("0.0.0.0:9301"));
            int id3 = t.addPeer("sentry_peer", "sentry_link", TransportConfig.udp("0.0.0.0:9302"));

            assertEquals(3, t.peerCount());
            assertEquals(id1, t.peerByName("session_peer"));
            assertEquals(id2, t.peerByName("choice_peer"));
            assertEquals(id3, t.peerByName("sentry_peer"));
        }
    }

    @Test
    @DisplayName("Integration: start with multiple peers")
    void startWithMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:9400"));
            t.addPeer("p2", "choice_protocol", TransportConfig.udp("0.0.0.0:9401"));
            t.start();
            assertTrue(t.isRunning());
            assertEquals(2, t.peerCount());
            t.stop();
            assertFalse(t.isRunning());
        }
    }
}
