import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import io.conduit.ConduitNative;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;

import session_test.PingBody;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * UDP multicast transport tests via the JNI native binding.
 *
 * Covers: multicast start/stop, error cases, loopback roundtrip.
 * Requires the JNI native test library (libconduit_jni_test).
 */
public class TestUdpMulticast {

    private static final String JNI_LIB = TestLibraryResolver.resolve(
        "conduit.jni.test.path", "CONDUIT_JNI_TEST_LIB", "conduit_jni_test");

    private static final String MCAST_GROUP = "239.255.0.1";

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

    /** Probe whether the OS supports multicast (IP_ADD_MEMBERSHIP). */
    @SuppressWarnings("deprecation")
    private static boolean multicastAvailable() {
        try (java.net.MulticastSocket ms = new java.net.MulticastSocket(0)) {
            java.net.InetAddress group = java.net.InetAddress.getByName(MCAST_GROUP);
            ms.joinGroup(group);
            ms.leaveGroup(group);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // ========================================================================
    // Happy-path tests
    // ========================================================================

    @Test
    @DisplayName("Multicast: start and stop cleanly")
    void multicastStartStop() throws Exception {
        assumeTrue(multicastAvailable(), "Multicast not available on this host");
        int port = findFreePort();
        try (Transceiver t = new Transceiver()) {
            t.addPeer("mcast", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindAddress("0.0.0.0")
                    .bindPort(port)
                    .multicastGroup(MCAST_GROUP)
                    .multicastLoop(true));
            t.start();
            Thread.sleep(50);
            t.stop();
        }
    }

    @Test
    @DisplayName("Multicast: custom TTL accepted")
    void multicastCustomTtl() throws Exception {
        assumeTrue(multicastAvailable(), "Multicast not available on this host");
        int port = findFreePort();
        try (Transceiver t = new Transceiver()) {
            t.addPeer("mcast", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindAddress("0.0.0.0")
                    .bindPort(port)
                    .multicastGroup(MCAST_GROUP)
                    .multicastTtl(4)
                    .multicastLoop(true));
            t.start();
            t.stop();
        }
    }

    @Test
    @DisplayName("Multicast: PingBody loopback roundtrip")
    void multicastLoopbackRoundtrip() throws Exception {
        assumeTrue(multicastAvailable(), "Multicast not available on this host");
        int port = findFreePort();

        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("mcast_rx", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindAddress("0.0.0.0")
                    .bindPort(port)
                    .multicastGroup(MCAST_GROUP)
                    .multicastLoop(true));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<PingBody> received = new AtomicReference<>();

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("mcast_tx", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindAddress("0.0.0.0")
                    .bindPort(port)
                    .multicastGroup(MCAST_GROUP)
                    .multicastLoop(true));
            sender.start();

            PingBody outgoing = new PingBody();
            outgoing.timestamp = 99887766;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertEquals(99887766, received.get().timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    // ========================================================================
    // Error-path tests
    // ========================================================================

    @Test
    @DisplayName("Multicast: non-multicast address rejected")
    void multicastNonMulticastAddressRejected() {
        int port = findFreePort();
        try (Transceiver t = new Transceiver()) {
            t.addPeer("bad", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindPort(port)
                    .multicastGroup("192.168.1.1"));
            assertThrows(Exception.class, t::start);
        }
    }

    @Test
    @DisplayName("Multicast: malformed group rejected")
    void multicastMalformedGroupRejected() {
        int port = findFreePort();
        try (Transceiver t = new Transceiver()) {
            t.addPeer("bad", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindPort(port)
                    .multicastGroup("not-an-ip"));
            assertThrows(Exception.class, t::start);
        }
    }

    @Test
    @DisplayName("Multicast: bind_port=0 rejected")
    void multicastBindPortZeroRejected() {
        try (Transceiver t = new Transceiver()) {
            t.addPeer("bad", "session_protocol",
                new TransportConfig.UdpConfig()
                    .bindPort(0)
                    .multicastGroup(MCAST_GROUP));
            assertThrows(Exception.class, t::start);
        }
    }
}
