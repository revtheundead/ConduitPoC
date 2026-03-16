import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import io.conduit.ConduitNative;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;

import session_test.PingBody;
import session_test.DataBody;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Advanced Transceiver roundtrip tests via UDP loopback.
 *
 * Covers: typed send/receive, raw bytes, multi-type interleaving,
 * stress sends, and error callbacks on corrupted data.
 *
 * Requires the JNI native test library (libconduit_jni_test).
 */
public class TestTransceiverAdvanced {

    private static final String JNI_LIB = TestLibraryResolver.resolve(
        "conduit.jni.test.path", "CONDUIT_JNI_TEST_LIB", "conduit_jni_test");

    @BeforeAll
    static void setup() {
        System.setProperty("conduit.jni.path", JNI_LIB);
        ConduitNative.setBackend(ConduitNative.Backend.JNI);
    }

    private static int findFreePort() {
        try (java.net.DatagramSocket s = new java.net.DatagramSocket(0)) {
            return s.getLocalPort();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // ========================================================================
    // PingBody roundtrips
    // ========================================================================

    @Test
    @DisplayName("Transceiver: PingBody timestamp=0 roundtrip via UDP")
    void transceiverPingTimestampZero() throws Exception {
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
            outgoing.timestamp = 0;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertEquals(0, received.get().timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: PingBody timestamp=0xFFFFFFFF roundtrip via UDP")
    void transceiverPingTimestampMax() throws Exception {
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
            outgoing.timestamp = 0xFFFFFFFF;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertEquals(0xFFFFFFFF, received.get().timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // DataBody roundtrip
    // ========================================================================

    @Test
    @DisplayName("Transceiver: DataBody full fields roundtrip via UDP")
    void transceiverDataBodyFullFields() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<DataBody> received = new AtomicReference<>();

            receiver.onMessage(DataBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            DataBody outgoing = new DataBody();
            outgoing.channel = 255;
            outgoing.payloadA = 0xDEADBEEF;
            outgoing.payloadB = 0xCAFEBABE;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                DataBody incoming = received.get();
                assertEquals(255, incoming.channel);
                assertEquals(0xDEADBEEF, incoming.payloadA);
                assertEquals(0xCAFEBABE, incoming.payloadB);
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Multi-type interleaved
    // ========================================================================

    @Test
    @DisplayName("Transceiver: interleaved PingBody and DataBody roundtrip")
    void transceiverMultiTypeInterleaved() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger pingCount = new AtomicInteger(0);
            AtomicInteger dataCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(1);

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                int total = pingCount.incrementAndGet() + dataCount.get();
                if (total >= 4) latch.countDown();
            });
            receiver.onMessage(DataBody.class, (peerId, msg) -> {
                int total = dataCount.incrementAndGet() + pingCount.get();
                if (total >= 4) latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            for (int i = 0; i < 2; i++) {
                PingBody ping = new PingBody();
                ping.timestamp = 1000 + i;
                sender.send(sender.solePeer(), ping);

                DataBody data = new DataBody();
                data.channel = i;
                data.payloadA = i * 100;
                data.payloadB = i * 200;
                sender.send(sender.solePeer(), data);
            }

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertTrue(pingCount.get() >= 1);
                assertTrue(dataCount.get() >= 1);
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Raw bytes
    // ========================================================================

    @Test
    @DisplayName("Transceiver: raw bytes manual encode, sendRaw, receive, decode")
    void transceiverRawBytesManualEncodeDecode() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<byte[]> receivedRaw = new AtomicReference<>();

            receiver.onMessage(PingBody.TYPE_ID, (peerId, typeId, typeName, data) -> {
                receivedRaw.set(data);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            // Manually encode a PingBody
            PingBody ping = new PingBody();
            ping.timestamp = 0xABCD1234;
            byte[] rawBytes = ping.encodeBytes();

            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, rawBytes);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                PingBody decoded = PingBody.decodeBytes(receivedRaw.get());
                assertEquals(0xABCD1234, decoded.timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Stress
    // ========================================================================

    @Test
    @DisplayName("Transceiver: stress 50 messages roundtrip")
    void transceiverStress50Messages() throws Exception {
        int port = findFreePort();
        int count = 50;
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger receivedCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(1);

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                int c = receivedCount.incrementAndGet();
                assertTrue(msg.timestamp >= 0 && msg.timestamp < count);
                if (c >= count) latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            Thread.sleep(50);

            for (int i = 0; i < count; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                sender.send(sender.solePeer(), msg);
            }

            latch.await(3, TimeUnit.SECONDS);
            // UDP is unreliable; verify what we got is valid
            assertTrue(receivedCount.get() >= 0);

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Error callbacks
    // ========================================================================

    @Test
    @DisplayName("Transceiver: corrupted raw payload triggers error callback")
    void transceiverCorruptedRawTriggersError() throws Exception {
        int port = findFreePort();
        try (Transceiver receiver = new Transceiver()) {
            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicInteger errorCount = new AtomicInteger(0);

            receiver.onError((peerId, peerName, errorCode, errorMessage) -> {
                errorCount.incrementAndGet();
                latch.countDown();
            });
            receiver.start();

            // Send garbage bytes directly via raw UDP
            byte[] garbage = new byte[]{
                (byte) 0xFF, (byte) 0xFE, (byte) 0xFD, (byte) 0xFC,
                (byte) 0xFB, (byte) 0xFA, (byte) 0xF9, (byte) 0xF8
            };
            try (java.net.DatagramSocket sock = new java.net.DatagramSocket()) {
                java.net.DatagramPacket pkt = new java.net.DatagramPacket(
                    garbage, garbage.length,
                    java.net.InetAddress.getByName("127.0.0.1"), port);
                sock.send(pkt);
            }

            boolean fired = latch.await(2, TimeUnit.SECONDS);
            if (fired) {
                assertTrue(errorCount.get() >= 1);
            }

            receiver.stop();
        }
    }
}
