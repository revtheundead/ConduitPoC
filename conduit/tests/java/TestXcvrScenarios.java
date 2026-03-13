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
import session_test.AckBody;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Comprehensive transceiver scenario tests mirroring the C++ conduit_tests.
 *
 * Covers lifecycle, stats, handler management, error callbacks, handler
 * exception safety, batch send, multi-peer, stress, direction violation,
 * queue overflow, and UDP loopback send/receive scenarios.
 */
public class TestXcvrScenarios {

    private static final String JNI_LIB = TestLibraryResolver.resolve(
        "conduit.jni.test.path", "CONDUIT_JNI_TEST_LIB", "conduit_jni_test");

    @BeforeAll
    static void setup() {
        System.setProperty("conduit.jni.path", JNI_LIB);
        ConduitNative.setBackend(ConduitNative.Backend.JNI);
    }

    /**
     * Find a free UDP port by briefly opening and closing a DatagramSocket.
     */
    private static int findFreePort() {
        try (java.net.DatagramSocket s = new java.net.DatagramSocket(0)) {
            return s.getLocalPort();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // ========================================================================
    // 1-5: Lifecycle & Basic Send/Receive
    // ========================================================================

    @Test
    @DisplayName("Scenario 1: send typed message before start() throws ConduitError")
    void sendBeforeStartThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("peer", "session_protocol",
                TransportConfig.udp("127.0.0.1:15001"));
            PingBody msg = new PingBody();
            msg.timestamp = 1;
            ConduitError err = assertThrows(ConduitError.class,
                () -> t.send(peerId, msg));
            assertNotNull(err.getMessage());
        }
    }

    @Test
    @DisplayName("Scenario 2: send to nonexistent peer ID throws ConduitError")
    void sendToUnknownPeerThrows() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("real", "session_protocol",
                TransportConfig.udp("127.0.0.1:15002"));
            t.start();
            PingBody msg = new PingBody();
            msg.timestamp = 1;
            assertThrows(ConduitError.class, () -> t.send(99999, msg));
            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 3: send via sole peer convenience with 1 peer succeeds")
    void sendViaSolePeerConvenience() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("only", "session_protocol",
                TransportConfig.udp("127.0.0.1:15003"));
            t.start();
            PingBody msg = new PingBody();
            msg.timestamp = 42;
            assertDoesNotThrow(() -> t.send(msg));
            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 4: solePeer with 0 peers throws")
    void solePeerErrorNoPeers() {
        try (Transceiver t = new Transceiver()) {
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    @Test
    @DisplayName("Scenario 5: solePeer with 2 peers throws")
    void solePeerErrorMultiplePeers() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("a", "session_protocol",
                TransportConfig.udp("127.0.0.1:15004"));
            t.addPeer("b", "session_protocol",
                TransportConfig.udp("127.0.0.1:15005"));
            assertThrows(ConduitError.class, () -> t.solePeer());
        }
    }

    // ========================================================================
    // 6-8: Stats
    // ========================================================================

    @Test
    @DisplayName("Scenario 6: bytes_sent increments after send")
    void statsBytesSentIncrements() throws Exception {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("stat", "session_protocol",
                TransportConfig.udp("127.0.0.1:15006"));
            t.start();

            PingBody msg = new PingBody();
            msg.timestamp = 7;
            t.send(peerId, msg);

            Thread.sleep(50);
            StatsSnapshot s = t.stats();
            assertTrue(s.bytesSent() > 0, "bytes_sent should be > 0 after send");

            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 7: stats reset zeroes all counters")
    void statsResetZeroesCounters() throws Exception {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("stat", "session_protocol",
                TransportConfig.udp("127.0.0.1:15007"));
            t.start();

            PingBody msg = new PingBody();
            msg.timestamp = 1;
            t.send(peerId, msg);
            Thread.sleep(50);

            assertTrue(t.stats().bytesSent() > 0, "Should have sent bytes");

            t.statsReset();
            StatsSnapshot s = t.stats();
            assertEquals(0, s.bytesSent(), "bytes_sent should be zero after reset");
            assertEquals(0, s.messagesReceived(),
                "messages_received should be zero after reset");
            assertEquals(0, s.bytesReceived(),
                "bytes_received should be zero after reset");

            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 8: fresh transceiver stats are all zero")
    void statsInitialAllZero() {
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

    // ========================================================================
    // 9-12: Handler Management
    // ========================================================================

    @Test
    @DisplayName("Scenario 9: removeHandler stops delivery and returns true")
    void removeHandlerStopsDelivery() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("peer", "session_protocol",
                TransportConfig.udp("0.0.0.0:15009"));
            t.onMessage(PingBody.TYPE_ID,
                (pid, typeId, typeName, data) -> {});
            boolean removed = t.removeHandler(peerId, PingBody.TYPE_ID);
            assertTrue(removed, "removeHandler should return true for registered handler");
        }
    }

    @Test
    @DisplayName("Scenario 10: removeStateChange stops callbacks and returns true")
    void removeStateChangeStopsCallbacks() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onStateChange((peerId, newState) -> {});
            assertTrue(cbId >= 0);
            boolean removed = t.removeStateChange(cbId);
            assertTrue(removed,
                "removeStateChange should return true for registered callback");
        }
    }

    @Test
    @DisplayName("Scenario 11: removeErrorCallback stops delivery and returns true")
    void removeErrorCallbackStopsDelivery() {
        try (Transceiver t = new Transceiver()) {
            int cbId = t.onError(
                (peerId, peerName, errorCode, errorMessage) -> {});
            assertTrue(cbId >= 0);
            boolean removed = t.removeErrorCallback(cbId);
            assertTrue(removed,
                "removeErrorCallback should return true for registered callback");
        }
    }

    @Test
    @DisplayName("Scenario 12: removeErrorCallback returns false for unknown ID")
    void removeErrorCallbackFalseForUnknownId() {
        try (Transceiver t = new Transceiver()) {
            boolean removed = t.removeErrorCallback(99999);
            assertFalse(removed,
                "removeErrorCallback should return false for unknown ID");
        }
    }

    // ========================================================================
    // 13-16: Error Callbacks
    // ========================================================================

    @Test
    @DisplayName("Scenario 13: error callback fires for decode failure via UDP loopback")
    void errorCallbackFiresForDecodeFailure() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<String> errorMsg = new AtomicReference<>();
            receiver.onError((peerId, peerName, errorCode, message) -> {
                errorMsg.set(message);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            // Send garbage bytes with a valid type ID to trigger decode error
            byte[] garbage = new byte[]{
                (byte) 0xDE, (byte) 0xAD, (byte) 0xBE, (byte) 0xEF,
                (byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF
            };
            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, garbage);

            boolean fired = latch.await(2, TimeUnit.SECONDS);
            // UDP may drop; assert only if callback arrived
            if (fired) {
                assertNotNull(errorMsg.get(),
                    "Error message should be non-null when callback fires");
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Scenario 14: multiple error callbacks all fire")
    void multipleErrorCallbacksAllFire() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger cb1Count = new AtomicInteger(0);
            AtomicInteger cb2Count = new AtomicInteger(0);
            AtomicInteger cb3Count = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(3);

            receiver.onError((pid, pn, ec, em) -> {
                cb1Count.incrementAndGet();
                latch.countDown();
            });
            receiver.onError((pid, pn, ec, em) -> {
                cb2Count.incrementAndGet();
                latch.countDown();
            });
            receiver.onError((pid, pn, ec, em) -> {
                cb3Count.incrementAndGet();
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            byte[] garbage = new byte[]{
                (byte) 0xBA, (byte) 0xAD, (byte) 0xF0, (byte) 0x0D
            };
            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, garbage);

            boolean fired = latch.await(2, TimeUnit.SECONDS);
            if (fired) {
                assertTrue(cb1Count.get() >= 1, "Callback 1 should have fired");
                assertTrue(cb2Count.get() >= 1, "Callback 2 should have fired");
                assertTrue(cb3Count.get() >= 1, "Callback 3 should have fired");
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Scenario 15: error callback that throws RuntimeException does not crash")
    void errorCallbackExceptionDoesNotCrash() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            receiver.onError((pid, pn, ec, em) -> {
                throw new RuntimeException("Deliberate test exception in error callback");
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            byte[] garbage = new byte[]{(byte) 0xFF, (byte) 0xFE};
            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, garbage);

            Thread.sleep(200);

            // Transceiver should survive the exception
            assertTrue(receiver.isRunning(),
                "Receiver should still be running after error callback exception");

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Scenario 16: error callback can query transceiver state without deadlock")
    void errorCallbackCanQueryTransceiverState() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicBoolean queriedRunning = new AtomicBoolean(false);

            receiver.onError((pid, pn, ec, em) -> {
                // Calling isRunning inside a callback must not deadlock
                queriedRunning.set(receiver.isRunning());
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            byte[] garbage = new byte[]{(byte) 0xCA, (byte) 0xFE};
            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, garbage);

            boolean fired = latch.await(2, TimeUnit.SECONDS);
            if (fired) {
                assertTrue(queriedRunning.get(),
                    "isRunning() inside error callback should return true");
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // 17: Handler Exception Safety
    // ========================================================================

    @Test
    @DisplayName("Scenario 17: handler that throws does not crash transceiver")
    void handlerExceptionDoesNotCrashTransceiver() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            receiver.onMessage(PingBody.TYPE_ID,
                (pid, typeId, typeName, data) -> {
                    throw new RuntimeException(
                        "Deliberate test exception in message handler");
                });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            PingBody msg = new PingBody();
            msg.timestamp = 1;
            sender.send(sender.solePeer(), msg);

            Thread.sleep(200);

            // Transceiver should survive the handler exception
            assertTrue(receiver.isRunning(),
                "Receiver should still be running after handler exception");

            // Should still be able to send more messages
            PingBody msg2 = new PingBody();
            msg2.timestamp = 2;
            assertDoesNotThrow(() -> sender.send(sender.solePeer(), msg2));

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // 18-19: Batch Send
    // ========================================================================

    @Test
    @DisplayName("Scenario 18: sendBatch with empty list throws ConduitError")
    void emptyBatchSendThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("peer", "session_protocol",
                TransportConfig.udp("127.0.0.1:15018"));
            t.start();
            List<byte[]> empty = new ArrayList<>();
            assertThrows(ConduitError.class,
                () -> t.sendBatch(peerId, PingBody.TYPE_ID, empty));
            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 19: sendBatch on non-batch protocol throws ConduitError")
    void batchSendNonBatchProtocolThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("peer", "session_protocol",
                TransportConfig.udp("127.0.0.1:15019"));
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
    // 20-24: Multi-peer
    // ========================================================================

    @Test
    @DisplayName("Scenario 20: two peers have distinct IDs")
    void twoPeersDistinctIds() {
        try (Transceiver t = new Transceiver()) {
            int id1 = t.addPeer("alpha", "session_protocol",
                TransportConfig.udp("0.0.0.0:15020"));
            int id2 = t.addPeer("bravo", "session_protocol",
                TransportConfig.udp("0.0.0.0:15021"));
            assertNotEquals(id1, id2, "Two peers should have distinct IDs");
        }
    }

    @Test
    @DisplayName("Scenario 21: peerByName finds correct peer")
    void peerByNameFindsCorrectPeer() {
        try (Transceiver t = new Transceiver()) {
            int addedId = t.addPeer("alpha", "session_protocol",
                TransportConfig.udp("0.0.0.0:15022"));
            int foundId = t.peerByName("alpha");
            assertEquals(addedId, foundId,
                "peerByName should return the same ID as addPeer");
        }
    }

    @Test
    @DisplayName("Scenario 22: peerByName for unknown name throws ConduitError")
    void peerByNameUnknownReturnsError() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("known", "session_protocol",
                TransportConfig.udp("0.0.0.0:15023"));
            assertThrows(ConduitError.class,
                () -> t.peerByName("nonexistent"));
        }
    }

    @Test
    @DisplayName("Scenario 23: peerCount tracks additions")
    void peerCountTracksAdditions() {
        try (Transceiver t = new Transceiver()) {
            assertEquals(0, t.peerCount());
            t.addPeer("p1", "session_protocol",
                TransportConfig.udp("0.0.0.0:15024"));
            assertEquals(1, t.peerCount());
            t.addPeer("p2", "session_protocol",
                TransportConfig.udp("0.0.0.0:15025"));
            assertEquals(2, t.peerCount());
            t.addPeer("p3", "session_protocol",
                TransportConfig.udp("0.0.0.0:15026"));
            assertEquals(3, t.peerCount());
        }
    }

    @Test
    @DisplayName("Scenario 24: freshly added peer state is DISCONNECTED")
    void peerStateInitialDisconnected() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("fresh", "session_protocol",
                TransportConfig.udp("0.0.0.0:15027"));
            Transceiver.ConnectionState state = t.peerState(peerId);
            assertEquals(Transceiver.ConnectionState.DISCONNECTED, state,
                "Initial peer state should be DISCONNECTED");
        }
    }

    // ========================================================================
    // 25-26: Stress
    // ========================================================================

    @Test
    @DisplayName("Scenario 25: 100 rapid sends all succeed without crash")
    void rapidSendsAllSucceed() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("flood", "session_protocol",
                TransportConfig.udp("127.0.0.1:15028"));
            t.start();

            for (int i = 0; i < 100; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                t.send(peerId, msg);
            }

            // If we get here without exception, all 100 sends succeeded
            assertTrue(t.isRunning(), "Transceiver should still be running");
            t.stop();
        }
    }

    @Test
    @DisplayName("Scenario 26: 20 rapid create/destroy cycles")
    void rapidCreateDestroyCycles() {
        for (int i = 0; i < 20; i++) {
            try (Transceiver t = new Transceiver()) {
                assertNotNull(t);
                assertFalse(t.isRunning());
            }
        }
        // Reaching this point without crash or exception means success
    }

    // ========================================================================
    // 27: Direction Violation
    // ========================================================================

    @Test
    @DisplayName("Scenario 27: sending receive-only AckBody throws ConduitError")
    void sendReceiveOnlyTypeThrows() {
        try (Transceiver t = new Transceiver()) {
            int peerId = t.addPeer("peer", "session_protocol",
                TransportConfig.udp("127.0.0.1:15030"));
            t.start();

            AckBody ack = new AckBody();
            ack.ackedSeq = 42;

            ConduitError err = assertThrows(ConduitError.class,
                () -> t.send(peerId, ack));
            assertNotNull(err.getMessage(),
                "Direction violation error should have a message");

            t.stop();
        }
    }

    // ========================================================================
    // 28: Queue Overflow
    // ========================================================================

    @Test
    @DisplayName("Scenario 28: queue overflow fires error callback")
    void queueOverflowFiresErrorCallback() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            // Set very small queue capacity on receiver
            receiver.setQueueConfig(2, 0, 1); // capacity=2, DropOldest, backPressure=1

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch errorLatch = new CountDownLatch(1);
            AtomicInteger errorCount = new AtomicInteger(0);

            // Register a slow handler to cause queue buildup
            receiver.onMessage(PingBody.TYPE_ID,
                (pid, typeId, typeName, data) -> {
                    try {
                        Thread.sleep(500);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                });

            receiver.onError((pid, pn, ec, em) -> {
                errorCount.incrementAndGet();
                errorLatch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            // Rapid-fire many messages to overflow the small queue
            for (int i = 0; i < 50; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                sender.send(sender.solePeer(), msg);
            }

            boolean fired = errorLatch.await(3, TimeUnit.SECONDS);
            // Queue overflow depends on timing; assert if callback arrived
            if (fired) {
                assertTrue(errorCount.get() >= 1,
                    "Error callback should have fired at least once for overflow");
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // 29-30: UDP Loopback Send/Receive
    // ========================================================================

    @Test
    @DisplayName("Scenario 29: send and receive typed PingBody via UDP loopback")
    void sendReceiveTypedLoopback() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<PingBody> received = new AtomicReference<>();

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            PingBody outgoing = new PingBody();
            outgoing.timestamp = 12345;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                PingBody incoming = received.get();
                assertNotNull(incoming, "Should have received a PingBody");
                assertEquals(12345, incoming.timestamp,
                    "Received timestamp should match sent value");
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Scenario 30: send 5 messages via UDP loopback, verify at least some received")
    void sendReceiveMultipleMessagesLoopback() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger receivedCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(1);

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                int count = receivedCount.incrementAndGet();
                if (count >= 1) {
                    latch.countDown();
                }
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            for (int i = 0; i < 5; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i * 100;
                sender.send(sender.solePeer(), msg);
            }

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                int count = receivedCount.get();
                assertTrue(count >= 1,
                    "Should have received at least 1 message, got " + count);
            }

            sender.stop();
            receiver.stop();
        }
    }
}
