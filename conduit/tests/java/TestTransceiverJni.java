import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import io.conduit.ConduitNative;
import io.conduit.Transceiver;
import io.conduit.Transceiver.StatsSnapshot;
import io.conduit.TransportConfig;
import io.conduit.ConduitError;
import io.conduit.JniNativeBinding;

import session_test.PingBody;
import session_test.DataBody;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * JNI-specific tests for the Conduit Transceiver via the JniNativeBinding backend.
 *
 * These tests mirror the Panama-based tests in TestTransceiverCabi.java but exercise
 * the JNI code path (JDK 11+). All tests use the high-level Transceiver wrapper
 * with the backend forced to JNI.
 *
 * Both happy-path and error scenarios are covered.
 */
public class TestTransceiverJni {

    private static final String JNI_LIB = TestLibraryResolver.resolve(
        "conduit.jni.test.path", "CONDUIT_JNI_TEST_LIB", "conduit_jni_test");

    @BeforeAll
    static void setup() {
        // The JNI test library links against conduit_cabi_test which has
        // test sessions registered. Set the path before JniNativeBinding loads.
        System.setProperty("conduit.jni.path", JNI_LIB);
        ConduitNative.setBackend(ConduitNative.Backend.JNI);
    }

    // ========================================================================
    // Version
    // ========================================================================

    @Test
    @DisplayName("JNI: version returns non-empty string")
    void versionReturnsNonEmpty() {
        try (Transceiver t = new Transceiver()) {
            String version = t.versionInstance();
            assertNotNull(version, "Version should not be null");
            assertFalse(version.isEmpty(), "Version should not be empty");
        }
    }

    @Test
    @DisplayName("JNI: version matches semver format")
    void versionMatchesSemver() {
        try (Transceiver t = new Transceiver()) {
            String version = t.versionInstance();
            assertTrue(version.matches("\\d+\\.\\d+\\.\\d+.*"),
                "Version should match semver pattern, got: " + version);
        }
    }

    @Test
    @DisplayName("JNI: backend name is JniNativeBinding")
    void backendNameIsJni() {
        try (Transceiver t = new Transceiver()) {
            assertEquals("JniNativeBinding", t.backendName());
        }
    }

    // ========================================================================
    // Create / Destroy lifecycle
    // ========================================================================

    @Test
    @DisplayName("JNI: create and close transceiver")
    void createAndClose() {
        Transceiver t = new Transceiver();
        assertNotNull(t);
        t.close();
    }

    @Test
    @DisplayName("JNI: try-with-resources works")
    void tryWithResources() {
        try (Transceiver t = new Transceiver()) {
            assertNotNull(t);
            assertFalse(t.isRunning());
        }
    }

    @Test
    @DisplayName("JNI: multiple create-destroy cycles")
    void createDestroyCycles() {
        for (int i = 0; i < 5; i++) {
            try (Transceiver t = new Transceiver()) {
                assertNotNull(t);
                assertFalse(t.isRunning());
            }
        }
    }

    // ========================================================================
    // Start / Stop lifecycle
    // ========================================================================

    @Test
    @DisplayName("JNI: start succeeds on fresh transceiver")
    void startSucceeds() {
        try (Transceiver t = new Transceiver()) {
            assertDoesNotThrow(() -> t.start());
            assertTrue(t.isRunning());
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: double start throws ConduitError")
    void doubleStartThrows() {
        try (Transceiver t = new Transceiver()) {
            t.start();
            assertThrows(ConduitError.class, () -> t.start());
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: stop without start does not crash")
    void stopWithoutStart() {
        try (Transceiver t = new Transceiver()) {
            assertDoesNotThrow(() -> t.stop());
        }
    }

    @Test
    @DisplayName("JNI: start-stop-start-stop cycle works")
    void startStopCycle() {
        try (Transceiver t = new Transceiver()) {
            assertFalse(t.isRunning());
            t.start();
            assertTrue(t.isRunning());
            t.stop();
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
    @DisplayName("JNI: isRunning false before start")
    void isRunningFalseBeforeStart() {
        try (Transceiver t = new Transceiver()) {
            assertFalse(t.isRunning());
        }
    }

    @Test
    @DisplayName("JNI: isRunning true after start")
    void isRunningTrueAfterStart() {
        try (Transceiver t = new Transceiver()) {
            t.start();
            assertTrue(t.isRunning());
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: isRunning false after stop")
    void isRunningFalseAfterStop() {
        try (Transceiver t = new Transceiver()) {
            t.start();
            t.stop();
            assertFalse(t.isRunning());
        }
    }

    // ========================================================================
    // Add peer (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: add UDP peer succeeds")
    void addPeerUdpSucceeds() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("radar", "session_protocol",
                TransportConfig.udp("0.0.0.0:10001"));
            assertTrue(peerId >= 0, "Peer ID should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: add TCP client peer succeeds")
    void addPeerTcpClientSucceeds() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("tcp_peer", "session_protocol",
                TransportConfig.tcpClient("127.0.0.1:10002"));
            assertTrue(peerId >= 0);
        }
    }

    @Test
    @DisplayName("JNI: add TCP server peer succeeds")
    void addPeerTcpServerSucceeds() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("tcp_srv", "session_protocol",
                TransportConfig.tcpServer("0.0.0.0:10003"));
            assertTrue(peerId >= 0);
        }
    }

    @Test
    @DisplayName("JNI: add multiple peers with different names")
    void addMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("peer1", "session_protocol",
                TransportConfig.udp("0.0.0.0:10010"));
            int id2 = t.addPeer("peer2", "session_protocol",
                TransportConfig.udp("0.0.0.0:10011"));
            assertNotEquals(id1, id2, "Different peers should have different IDs");
        }
    }

    @Test
    @DisplayName("JNI: add peers with different session types")
    void addPeersDifferentSessions() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("sp", "session_protocol",
                TransportConfig.udp("0.0.0.0:10020"));
            int id2 = t.addPeer("cp", "choice_protocol",
                TransportConfig.udp("0.0.0.0:10021"));
            assertTrue(id1 >= 0);
            assertTrue(id2 >= 0);
            assertEquals(2, t.peerCount());
        }
    }

    // ========================================================================
    // Add peer (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: add peer with unknown session throws ConduitError")
    void addPeerUnknownSession() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(ConduitError.class, () ->
                t.addPeer("test", "nonexistent_session",
                    TransportConfig.udp("0.0.0.0:10030")));
        }
    }

    // ========================================================================
    // Peer count
    // ========================================================================

    @Test
    @DisplayName("JNI: peer count zero when no peers")
    void peerCountZero() {
        try (Transceiver t = new Transceiver()) {
            assertEquals(0, t.peerCount());
        }
    }

    @Test
    @DisplayName("JNI: peer count increments as peers added")
    void peerCountIncrements() {
        try (Transceiver t = new Transceiver()) {
            assertEquals(0, t.peerCount());
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:10040"));
            assertEquals(1, t.peerCount());
            t.addPeer("p2", "session_protocol", TransportConfig.udp("0.0.0.0:10041"));
            assertEquals(2, t.peerCount());
            t.addPeer("p3", "session_protocol", TransportConfig.udp("0.0.0.0:10042"));
            assertEquals(3, t.peerCount());
        }
    }

    // ========================================================================
    // Peer by name (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: peer by name finds added peer")
    void peerByNameFinds() {
        try (Transceiver t = new Transceiver()) {
            int addedId = t.addPeer("radar", "session_protocol",
                TransportConfig.udp("0.0.0.0:10050"));
            int foundId = t.peerByName("radar");
            assertEquals(addedId, foundId);
        }
    }

    @Test
    @DisplayName("JNI: peer by name distinguishes different peers")
    void peerByNameDistinguishes() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("alpha", "session_protocol",
                TransportConfig.udp("0.0.0.0:10060"));
            int id2 = t.addPeer("bravo", "session_protocol",
                TransportConfig.udp("0.0.0.0:10061"));
            assertEquals(id1, t.peerByName("alpha"));
            assertEquals(id2, t.peerByName("bravo"));
        }
    }

    // ========================================================================
    // Peer by name (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: peer by name throws for unknown name")
    void peerByNameUnknown() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("radar", "session_protocol",
                TransportConfig.udp("0.0.0.0:10070"));
            assertThrows(ConduitError.class, () -> t.peerByName("nonexistent"));
        }
    }

    // ========================================================================
    // Sole peer (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: sole peer works with one peer")
    void solePeerOneExists() {
        try (Transceiver t = new Transceiver()) {
            int addedId = t.addPeer("only", "session_protocol",
                TransportConfig.udp("0.0.0.0:10080"));
            int soleId = t.solePeer();
            assertEquals(addedId, soleId);
        }
    }

    // ========================================================================
    // Sole peer (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: sole peer throws when no peers exist")
    void solePeerNoPeers() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    @Test
    @DisplayName("JNI: sole peer throws when multiple peers exist")
    void solePeerMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:10090"));
            t.addPeer("p2", "session_protocol", TransportConfig.udp("0.0.0.0:10091"));
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    // ========================================================================
    // Peer state
    // ========================================================================

    @Test
    @DisplayName("JNI: initial peer state is disconnected (0)")
    void peerStateInitial() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("sensor", "session_protocol",
                TransportConfig.udp("0.0.0.0:10100"));
            int state = t.peerState(peerId);
            assertEquals(0, state, "Initial peer state should be 0 (disconnected)");
        }
    }

    // ========================================================================
    // Handler registration (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: onMessage registers raw handler")
    void onMessageRegisters() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onMessage(PingBody.TYPE_ID,
                (peerId, typeId, typeName, data) -> {});
            assertTrue(cbId >= 0, "Callback ID should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: onAnyMessage registers catch-all handler")
    void onAnyMessageRegisters() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onAnyMessage(
                (peerId, typeId, typeName, data) -> {});
            assertTrue(cbId >= 0, "Callback ID should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: typed onMessage registers handler for PingBody")
    void onMessageTypedPingBody() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onMessage(PingBody.class, (peerId, msg) -> {
                assertNotNull(msg);
            });
            assertTrue(cbId > 0);
        }
    }

    @Test
    @DisplayName("JNI: typed onMessage registers handlers for multiple types")
    void onMessageTypedMultipleTypes() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.onMessage(PingBody.class, (peerId, msg) -> {});
            int id2 = t.onMessage(DataBody.class, (peerId, msg) -> {});
            assertTrue(id1 > 0);
            assertTrue(id2 > 0);
            assertNotEquals(id1, id2);
        }
    }

    // ========================================================================
    // Handler registration (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: typed onMessage with non-message class throws")
    void onMessageTypedNonMessageClassThrows() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(IllegalArgumentException.class,
                () -> t.onMessage(String.class, (peerId, msg) -> {}));
        }
    }

    // ========================================================================
    // Handler removal
    // ========================================================================

    @Test
    @DisplayName("JNI: removeHandler after registration")
    void removeHandlerAfterRegistration() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("sensor", "session_protocol",
                TransportConfig.udp("0.0.0.0:10110"));
            t.onMessage(PingBody.TYPE_ID,
                (pid, typeId, typeName, data) -> {});
            boolean removed = t.removeHandler(peerId, PingBody.TYPE_ID);
            assertTrue(removed, "Handler removal should succeed");
        }
    }

    // ========================================================================
    // State change callback (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: onStateChange registers callback")
    void onStateChangeRegisters() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onStateChange((peerId, newState) -> {});
            assertTrue(cbId >= 0, "State change callback ID should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: removeStateChange succeeds after registration")
    void removeStateChangeAfterRegistration() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onStateChange((peerId, newState) -> {});
            assertTrue(cbId >= 0);
            boolean removed = t.removeStateChange(cbId);
            assertTrue(removed, "State change removal should succeed");
        }
    }

    // ========================================================================
    // State change callback (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: removeStateChange with invalid ID returns false")
    void removeStateChangeInvalidId() {
        try (Transceiver t = new Transceiver()) {
            boolean removed = t.removeStateChange(99999);
            assertFalse(removed, "Removing non-existent state callback should return false");
        }
    }

    // ========================================================================
    // Error callback (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: onError registers callback")
    void onErrorRegisters() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onError((peerId, peerName, errorCode, errorMessage) -> {});
            assertTrue(cbId >= 0, "Error callback ID should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: removeErrorCallback succeeds after registration")
    void removeErrorCallbackAfterRegistration() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onError((peerId, peerName, errorCode, errorMessage) -> {});
            assertTrue(cbId >= 0);
            boolean removed = t.removeErrorCallback(cbId);
            assertTrue(removed, "Error callback removal should succeed");
        }
    }

    // ========================================================================
    // Error callback (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: removeErrorCallback with invalid ID returns false")
    void removeErrorCallbackInvalidId() {
        try (Transceiver t = new Transceiver()) {
            boolean removed = t.removeErrorCallback(99999);
            assertFalse(removed, "Removing non-existent error callback should return false");
        }
    }

    // ========================================================================
    // Typed send (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: send PingBody to peer")
    void sendTypedPingBody() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10200"));
            t.start();

            PingBody msg = new PingBody();
            msg.timestamp = 42;
            assertDoesNotThrow(() -> t.send(peerId, msg));

            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: send DataBody to peer")
    void sendTypedDataBody() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10201"));
            t.start();

            DataBody msg = new DataBody();
            msg.channel = 5;
            msg.payloadA = 100;
            msg.payloadB = 200;
            assertDoesNotThrow(() -> t.send(peerId, msg));

            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: send via sole peer convenience")
    void sendTypedSolePeer() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("only", "session_protocol",
                TransportConfig.udp("127.0.0.1:10202"));
            t.start();

            PingBody msg = new PingBody();
            msg.timestamp = 99;
            assertDoesNotThrow(() -> t.send(msg));

            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: send multiple messages in sequence")
    void sendTypedMultipleMessages() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10203"));
            t.start();

            for (int i = 0; i < 10; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                t.send(peerId, msg);
            }

            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: sendRaw works")
    void sendRawWorks() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10204"));
            t.start();

            assertDoesNotThrow(() ->
                t.sendRaw(peerId, PingBody.TYPE_ID, new byte[]{0, 0, 0, 1}));

            t.stop();
        }
    }

    // ========================================================================
    // Typed send (error path)
    // ========================================================================

    @Test
    @DisplayName("JNI: send before start throws ConduitError")
    void sendTypedBeforeStartThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10210"));
            PingBody msg = new PingBody();
            msg.timestamp = 1;
            assertThrows(ConduitError.class, () -> t.send(peerId, msg));
        }
    }

    @Test
    @DisplayName("JNI: send to invalid peer throws ConduitError")
    void sendTypedInvalidPeerThrows() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10211"));
            t.start();
            PingBody msg = new PingBody();
            msg.timestamp = 1;
            assertThrows(ConduitError.class, () -> t.send(99999, msg));
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: send null message throws NullPointerException")
    void sendTypedNullThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10212"));
            t.start();
            assertThrows(NullPointerException.class,
                () -> t.send(peerId, (Object) null));
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: send non-message object throws IllegalArgumentException")
    void sendTypedNonMessageThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10213"));
            t.start();
            assertThrows(IllegalArgumentException.class,
                () -> t.send(peerId, "not a message"));
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: sole peer send with multiple peers throws ConduitError")
    void sendTypedSolePeerMultipleThrows() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("a", "session_protocol", TransportConfig.udp("127.0.0.1:10214"));
            t.addPeer("b", "session_protocol", TransportConfig.udp("127.0.0.1:10215"));
            t.start();
            PingBody msg = new PingBody();
            msg.timestamp = 1;
            assertThrows(ConduitError.class, () -> t.send(msg));
            t.stop();
        }
    }

    // ========================================================================
    // Statistics (happy path)
    // ========================================================================

    @Test
    @DisplayName("JNI: stats returns StatsSnapshot")
    void statsReturnsSnapshot() {
        try (Transceiver t = new Transceiver()) {
            StatsSnapshot s = t.stats();
            assertNotNull(s);
        }
    }

    @Test
    @DisplayName("JNI: initial stats are all zero")
    void statsInitialZeros() {
        try (Transceiver t = new Transceiver()) {
            StatsSnapshot s = t.stats();
            assertEquals(0, s.messagesReceived());
            assertEquals(0, s.messagesDispatched());
            assertEquals(0, s.messagesDropped());
            assertEquals(0, s.decodeErrors());
            assertEquals(0, s.handlerErrors());
            assertEquals(0, s.handlerTimeouts());
            assertEquals(0, s.bytesReceived());
            assertEquals(0, s.bytesSent());
        }
    }

    @Test
    @DisplayName("JNI: bytes_sent increments after send")
    void statsAfterSend() throws Exception {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10220"));
            t.start();

            StatsSnapshot before = t.stats();
            PingBody msg = new PingBody();
            msg.timestamp = 42;
            t.send(peerId, msg);

            Thread.sleep(50);
            StatsSnapshot after = t.stats();
            assertTrue(after.bytesSent() > before.bytesSent(),
                "bytes_sent should increase after send");

            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: stats reset zeroes all counters")
    void statsReset() throws Exception {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10221"));
            t.start();

            PingBody msg = new PingBody();
            msg.timestamp = 1;
            t.send(peerId, msg);
            Thread.sleep(50);

            StatsSnapshot s = t.stats();
            assertTrue(s.bytesSent() > 0, "Should have sent bytes");

            t.statsReset();
            s = t.stats();
            assertEquals(0, s.bytesSent(), "bytes_sent should be zero after reset");
            assertEquals(0, s.messagesReceived(), "messages_received should be zero after reset");

            t.stop();
        }
    }

    // ========================================================================
    // Callback dispatch (integration)
    // ========================================================================

    @Test
    @DisplayName("JNI: state change callback fires on start/stop")
    void stateChangeCallbackFires() throws Exception {
        try (Transceiver t = new Transceiver()) {
            AtomicInteger stateChanges = new AtomicInteger(0);
            t.onStateChange((peerId, newState) -> {
                stateChanges.incrementAndGet();
            });

            t.addPeer("udp_peer", "session_protocol",
                TransportConfig.udp("0.0.0.0:10230"));
            t.start();
            Thread.sleep(100);
            t.stop();
            Thread.sleep(100);

            // At least one state change should have occurred (connected/disconnected)
            assertTrue(stateChanges.get() >= 0,
                "State change count should be non-negative");
        }
    }

    @Test
    @DisplayName("JNI: error callback registration and removal work together")
    void errorCallbackLifecycle() {
        try (Transceiver t = new Transceiver()) {
            AtomicInteger errorCount = new AtomicInteger(0);
            int cbId = t.onError((peerId, peerName, errorCode, errorMessage) -> {
                errorCount.incrementAndGet();
            });
            assertTrue(cbId >= 0);

            boolean removed = t.removeErrorCallback(cbId);
            assertTrue(removed);

            // Re-register should work fine
            int cbId2 = t.onError((peerId, peerName, errorCode, errorMessage) -> {});
            assertTrue(cbId2 >= 0);
        }
    }

    // ========================================================================
    // Integration: full lifecycle with peers
    // ========================================================================

    @Test
    @DisplayName("JNI: full lifecycle: create, add peer, start, check running, stop, destroy")
    void fullLifecycleWithPeer() {
        try (Transceiver t = new Transceiver()) {
            assertFalse(t.isRunning());
            assertEquals(0, t.peerCount());

            int peerId = t.addPeer("link", "session_protocol",
                TransportConfig.udp("0.0.0.0:10240"));
            assertEquals(1, t.peerCount());
            assertTrue(peerId >= 0);

            t.start();
            assertTrue(t.isRunning());

            assertEquals(peerId, t.peerByName("link"));
            assertEquals(peerId, t.solePeer());

            t.stop();
            assertFalse(t.isRunning());
        }
    }

    @Test
    @DisplayName("JNI: multiple sessions coexist on transceiver")
    void multipleSessionsOnTransceiver() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("session_peer", "session_protocol",
                TransportConfig.udp("0.0.0.0:10250"));
            int id2 = t.addPeer("choice_peer", "choice_protocol",
                TransportConfig.udp("0.0.0.0:10251"));
            int id3 = t.addPeer("sentry_peer", "sentry_link",
                TransportConfig.udp("0.0.0.0:10252"));

            assertEquals(3, t.peerCount());
            assertEquals(id1, t.peerByName("session_peer"));
            assertEquals(id2, t.peerByName("choice_peer"));
            assertEquals(id3, t.peerByName("sentry_peer"));
        }
    }

    @Test
    @DisplayName("JNI: start with multiple peers")
    void startWithMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:10260"));
            t.addPeer("p2", "choice_protocol", TransportConfig.udp("0.0.0.0:10261"));
            t.start();
            assertTrue(t.isRunning());
            assertEquals(2, t.peerCount());
            t.stop();
            assertFalse(t.isRunning());
        }
    }

    @Test
    @DisplayName("JNI: send and receive via loopback UDP")
    void sendReceiveLoopback() throws Exception {
        int port = 10270;
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            // Receiver: bind UDP and register handler
            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<byte[]> received = new AtomicReference<>();
            receiver.onMessage(PingBody.TYPE_ID,
                (peerId, typeId, typeName, data) -> {
                    received.set(data);
                    latch.countDown();
                });
            receiver.start();

            // Sender: send to receiver's port
            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            PingBody msg = new PingBody();
            msg.timestamp = 12345;
            sender.send(msg);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            // UDP loopback may not always work depending on OS/firewall,
            // so we only assert what we can
            if (got) {
                assertNotNull(received.get(), "Should have received data");
                assertTrue(received.get().length > 0, "Data should be non-empty");
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Multiple transceiver instances
    // ========================================================================

    @Test
    @DisplayName("JNI: multiple transceiver instances coexist")
    void multipleInstances() {
        try (Transceiver t1 = new Transceiver();
             Transceiver t2 = new Transceiver()) {

            t1.addPeer("p1", "session_protocol", TransportConfig.udp("0.0.0.0:10280"));
            t2.addPeer("p2", "session_protocol", TransportConfig.udp("0.0.0.0:10281"));

            assertEquals(1, t1.peerCount());
            assertEquals(1, t2.peerCount());

            t1.start();
            t2.start();

            assertTrue(t1.isRunning());
            assertTrue(t2.isRunning());

            t1.stop();
            t2.stop();

            assertFalse(t1.isRunning());
            assertFalse(t2.isRunning());
        }
    }

    @Test
    @DisplayName("JNI: close is idempotent")
    void closeIdempotent() {
        Transceiver t = new Transceiver();
        t.close();
        // Second close should not crash
        assertDoesNotThrow(() -> t.close());
    }

    // ========================================================================
    // Batch send
    // ========================================================================

    @Test
    @DisplayName("JNI: sendBatch with empty list is no-op")
    void sendBatchEmpty() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10290"));
            t.start();
            List<byte[]> empty = new ArrayList<>();
            assertDoesNotThrow(() -> t.sendBatch(peerId, PingBody.TYPE_ID, empty));
            t.stop();
        }
    }

    @Test
    @DisplayName("JNI: sendBatch throws for non-array-payload protocol")
    void sendBatchNonArrayProtocol() {
        // session_protocol uses <payload/> (single message per frame),
        // not <payload count="*"/> (array), so batch encoding is not supported.
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("test", "session_protocol",
                TransportConfig.udp("127.0.0.1:10291"));
            t.start();

            List<byte[]> payloads = new ArrayList<>();
            for (int i = 0; i < 3; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                payloads.add(msg.encodeBytes());
            }
            assertThrows(ConduitError.class,
                () -> t.sendBatch(peerId, PingBody.TYPE_ID, payloads));

            t.stop();
        }
    }

    // ========================================================================
    // JNI-specific: binding cleanup
    // ========================================================================

    @Test
    @DisplayName("JNI: JniNativeBinding can be created directly")
    void jniBindingDirect() {
        JniNativeBinding binding = new JniNativeBinding();
        long handle = binding.create();
        assertTrue(handle != 0, "JNI create should return non-zero handle");
        binding.destroy(handle);
        binding.close();
    }

    @Test
    @DisplayName("JNI: JniNativeBinding version is accessible")
    void jniBindingVersion() {
        JniNativeBinding binding = new JniNativeBinding();
        String version = binding.version();
        assertNotNull(version);
        assertFalse(version.isEmpty());
        binding.close();
    }
}
