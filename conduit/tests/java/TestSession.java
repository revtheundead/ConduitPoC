import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * Tests for session management: PacketSession, FrameSession,
 * frame encode/decode roundtrips, and session-level wrap/unwrap.
 */
public class TestSession {

    private session_test.PacketSession packetSession;
    private choice_test.FrameSession frameSession;

    @BeforeEach
    void setUp() {
        packetSession = new session_test.PacketSession();
        frameSession = new choice_test.FrameSession();
    }

    // ========================================================================
    // PacketSession basic tests
    // ========================================================================

    @Test
    @DisplayName("PacketSession: creation succeeds")
    void packetSessionCreation() {
        assertNotNull(packetSession);
    }

    @Test
    @DisplayName("PacketSession: leafTypeIds returns 3 types")
    void packetSessionLeafTypeIds() {
        long[] ids = packetSession.leafTypeIds();
        assertEquals(3, ids.length);
    }

    @Test
    @DisplayName("PacketSession: leafTypeIds contains PingBody type ID")
    void packetSessionContainsPingTypeId() {
        long[] ids = packetSession.leafTypeIds();
        assertTrue(containsId(ids, 0x0ad7bb3ecc473399L), "Should contain PingBody type ID");
    }

    @Test
    @DisplayName("PacketSession: leafTypeIds contains DataBody type ID")
    void packetSessionContainsDataTypeId() {
        long[] ids = packetSession.leafTypeIds();
        assertTrue(containsId(ids, 0x29d16b9e73f85835L), "Should contain DataBody type ID");
    }

    @Test
    @DisplayName("PacketSession: leafTypeIds contains AckBody type ID")
    void packetSessionContainsAckTypeId() {
        long[] ids = packetSession.leafTypeIds();
        assertTrue(containsId(ids, 0xcc431e5e357bc2e6L), "Should contain AckBody type ID");
    }

    @Test
    @DisplayName("PacketSession: typeName for PingBody")
    void packetSessionTypeNamePing() {
        assertEquals("PingBody", packetSession.typeName(0x0ad7bb3ecc473399L));
    }

    @Test
    @DisplayName("PacketSession: typeName for DataBody")
    void packetSessionTypeNameData() {
        assertEquals("DataBody", packetSession.typeName(0x29d16b9e73f85835L));
    }

    @Test
    @DisplayName("PacketSession: typeName for AckBody")
    void packetSessionTypeNameAck() {
        assertEquals("AckBody", packetSession.typeName(0xcc431e5e357bc2e6L));
    }

    @Test
    @DisplayName("PacketSession: typeName for unknown returns 'unknown'")
    void packetSessionTypeNameUnknown() {
        assertEquals("unknown", packetSession.typeName(0x1111111111111111L));
    }

    @Test
    @DisplayName("PacketSession: protocolName returns 'session_test'")
    void packetSessionProtocolName() {
        assertEquals("session_test", packetSession.protocolName());
    }

    @Test
    @DisplayName("PacketSession: isReceiveOnly for AckBody returns true")
    void packetSessionAckIsReceiveOnly() {
        assertTrue(packetSession.isReceiveOnly(0xcc431e5e357bc2e6L));
    }

    @Test
    @DisplayName("PacketSession: isReceiveOnly for PingBody returns false")
    void packetSessionPingIsNotReceiveOnly() {
        assertFalse(packetSession.isReceiveOnly(0x0ad7bb3ecc473399L));
    }

    @Test
    @DisplayName("PacketSession: isReceiveOnly for DataBody returns false")
    void packetSessionDataIsNotReceiveOnly() {
        assertFalse(packetSession.isReceiveOnly(0x29d16b9e73f85835L));
    }

    @Test
    @DisplayName("PacketSession: syncPattern returns DE AD")
    void packetSessionSyncPattern() {
        byte[] expected = new byte[] { (byte)0xDE, (byte)0xAD };
        assertArrayEquals(expected, packetSession.syncPattern());
    }

    @Test
    @DisplayName("PacketSession: minFrameHeaderSize returns 7")
    void packetSessionMinFrameHeaderSize() {
        assertEquals(7, packetSession.minFrameHeaderSize());
    }

    // ========================================================================
    // Packet frame encode/decode roundtrip
    // ========================================================================

    @Test
    @DisplayName("Packet: wrap PingBody and encode/decode roundtrip")
    void packetWrapPingRoundtrip() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 1000;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        frame.seq = 42;
        byte[] encoded = frame.encodeBytes();

        session_test.Packet decoded = session_test.Packet.decodeBytes(encoded);
        assertEquals((int) session_test.Constants.SYNC, decoded.sync);
        assertEquals(42, decoded.seq);
        assertEquals(1, decoded.msgId);
        assertInstanceOf(session_test.PingBody.class, decoded.payload);
        session_test.PingBody decodedPing = (session_test.PingBody) decoded.payload;
        assertEquals(1000, decodedPing.timestamp);
    }

    @Test
    @DisplayName("Packet: wrap DataBody and encode/decode roundtrip")
    void packetWrapDataRoundtrip() {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 10;
        data.payloadA = 0xAAAA;
        data.payloadB = 0xBBBB;
        session_test.Packet frame = session_test.Packet.wrap(data);
        frame.seq = 5;
        byte[] encoded = frame.encodeBytes();

        session_test.Packet decoded = session_test.Packet.decodeBytes(encoded);
        assertEquals(2, decoded.msgId);
        assertInstanceOf(session_test.DataBody.class, decoded.payload);
        session_test.DataBody decodedData = (session_test.DataBody) decoded.payload;
        assertEquals(10, decodedData.channel);
        assertEquals(0xAAAA, decodedData.payloadA);
        assertEquals(0xBBBB, decodedData.payloadB);
    }

    @Test
    @DisplayName("Packet: wrap AckBody and encode/decode roundtrip")
    void packetWrapAckRoundtrip() {
        session_test.AckBody ack = new session_test.AckBody();
        ack.ackedSeq = 999;
        session_test.Packet frame = session_test.Packet.wrap(ack);
        frame.seq = 100;
        byte[] encoded = frame.encodeBytes();

        session_test.Packet decoded = session_test.Packet.decodeBytes(encoded);
        assertEquals(3, decoded.msgId);
        assertInstanceOf(session_test.AckBody.class, decoded.payload);
        session_test.AckBody decodedAck = (session_test.AckBody) decoded.payload;
        assertEquals(999, decodedAck.ackedSeq);
    }

    @Test
    @DisplayName("Packet: frame length is correctly calculated")
    void packetFrameLength() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 42;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        // Header is 7 bytes (sync:2 + seq:2 + msgId:1 + length:2), payload is 4 bytes (u32)
        assertEquals(11, encoded.length);

        session_test.Packet decoded = session_test.Packet.decodeBytes(encoded);
        assertEquals(11, decoded.length);
    }

    @Test
    @DisplayName("Packet: frame sync value is DEAD")
    void packetFrameSync() {
        session_test.PingBody ping = new session_test.PingBody();
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        session_test.Packet decoded = session_test.Packet.decodeBytes(encoded);
        assertEquals(0xDEAD, decoded.sync);
    }

    // ========================================================================
    // PacketSession encodeWrap / decodeFrame roundtrip
    // ========================================================================

    @Test
    @DisplayName("PacketSession: encodeWrap PingBody returns valid result")
    void sessionEncodeWrapPing() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 12345;
        Map<String, Object> result = packetSession.encodeWrap(
            session_test.PingBody.TYPE_ID, ping);
        assertNotNull(result);
        assertNotNull(result.get("bytes"));
        assertEquals(session_test.PingBody.TYPE_ID, result.get("type_id"));
    }

    @Test
    @DisplayName("PacketSession: encodeWrap/decodeFrame PingBody roundtrip")
    void sessionEncodeDecodeRoundtripPing() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 54321;
        Map<String, Object> wrapped = packetSession.encodeWrap(
            session_test.PingBody.TYPE_ID, ping);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = packetSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        Map<String, Object> msg = messages.get(0);
        assertEquals(session_test.PingBody.TYPE_ID, msg.get("type_id"));
        assertEquals("PingBody", msg.get("type_name"));
        session_test.PingBody decoded = (session_test.PingBody) msg.get("payload");
        assertEquals(54321, decoded.timestamp);
    }

    @Test
    @DisplayName("PacketSession: encodeWrap/decodeFrame DataBody roundtrip")
    void sessionEncodeDecodeRoundtripData() {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 7;
        data.payloadA = 111;
        data.payloadB = 222;
        Map<String, Object> wrapped = packetSession.encodeWrap(
            session_test.DataBody.TYPE_ID, data);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = packetSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        session_test.DataBody decoded = (session_test.DataBody) messages.get(0).get("payload");
        assertEquals(7, decoded.channel);
        assertEquals(111, decoded.payloadA);
        assertEquals(222, decoded.payloadB);
    }

    @Test
    @DisplayName("PacketSession: encodeWrap/decodeFrame AckBody roundtrip")
    void sessionEncodeDecodeRoundtripAck() {
        session_test.AckBody ack = new session_test.AckBody();
        ack.ackedSeq = 777;
        Map<String, Object> wrapped = packetSession.encodeWrap(
            session_test.AckBody.TYPE_ID, ack);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = packetSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        session_test.AckBody decoded = (session_test.AckBody) messages.get(0).get("payload");
        assertEquals(777, decoded.ackedSeq);
    }

    @Test
    @DisplayName("PacketSession: sequence counter increments")
    void sessionSequenceCounterIncrements() {
        assertEquals(0, packetSession.sequenceCounter());
        session_test.PingBody ping = new session_test.PingBody();
        packetSession.encodeWrap(session_test.PingBody.TYPE_ID, ping);
        assertEquals(1, packetSession.sequenceCounter());
        packetSession.encodeWrap(session_test.PingBody.TYPE_ID, ping);
        assertEquals(2, packetSession.sequenceCounter());
    }

    @Test
    @DisplayName("PacketSession: reset clears sequence counter")
    void sessionResetSequenceCounter() {
        session_test.PingBody ping = new session_test.PingBody();
        packetSession.encodeWrap(session_test.PingBody.TYPE_ID, ping);
        packetSession.encodeWrap(session_test.PingBody.TYPE_ID, ping);
        assertEquals(2, packetSession.sequenceCounter());
        packetSession.reset();
        assertEquals(0, packetSession.sequenceCounter());
    }

    @Test
    @DisplayName("PacketSession: encodeWrap unknown type returns null")
    void sessionEncodeWrapUnknownType() {
        assertNull(packetSession.encodeWrap(0x1111111111111111L, new Object()));
    }

    @Test
    @DisplayName("PacketSession: extractFrameLength works")
    void sessionExtractFrameLength() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 100;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        int length = packetSession.extractFrameLength(encoded);
        assertEquals(11, length);  // 7 header + 4 payload
    }

    @Test
    @DisplayName("PacketSession: extractFrameLength with short header returns 0")
    void sessionExtractFrameLengthShort() {
        assertEquals(0, packetSession.extractFrameLength(new byte[3]));
    }

    @Test
    @DisplayName("PacketSession: formatMessage returns non-empty string")
    void sessionFormatMessage() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 42;
        String formatted = packetSession.formatMessage(session_test.PingBody.TYPE_ID, ping);
        assertNotNull(formatted);
        assertTrue(formatted.contains("42"));
    }

    @Test
    @DisplayName("PacketSession: formatMessage null payload returns empty")
    void sessionFormatMessageNull() {
        assertEquals("", packetSession.formatMessage(0L, null));
    }

    // ========================================================================
    // FrameSession tests (choice_protocol)
    // ========================================================================

    @Test
    @DisplayName("FrameSession: creation succeeds")
    void frameSessionCreation() {
        assertNotNull(frameSession);
    }

    @Test
    @DisplayName("FrameSession: leafTypeIds returns 2 types")
    void frameSessionLeafTypeIds() {
        long[] ids = frameSession.leafTypeIds();
        assertEquals(2, ids.length);
    }

    @Test
    @DisplayName("FrameSession: typeName for AlphaBody")
    void frameSessionTypeNameAlpha() {
        assertEquals("AlphaBody", frameSession.typeName(0x24395aaf5388c853L));
    }

    @Test
    @DisplayName("FrameSession: typeName for BetaBody")
    void frameSessionTypeNameBeta() {
        assertEquals("BetaBody", frameSession.typeName(0x8b968b89bfd16bffL));
    }

    @Test
    @DisplayName("FrameSession: protocolName returns 'choice_test'")
    void frameSessionProtocolName() {
        assertEquals("choice_test", frameSession.protocolName());
    }

    @Test
    @DisplayName("FrameSession: isReceiveOnly for BetaBody")
    void frameSessionBetaIsReceiveOnly() {
        assertTrue(frameSession.isReceiveOnly(0x8b968b89bfd16bffL));
    }

    @Test
    @DisplayName("FrameSession: isReceiveOnly for AlphaBody is false")
    void frameSessionAlphaIsNotReceiveOnly() {
        assertFalse(frameSession.isReceiveOnly(0x24395aaf5388c853L));
    }

    @Test
    @DisplayName("FrameSession: syncPattern returns BE EF")
    void frameSessionSyncPattern() {
        byte[] expected = new byte[] { (byte)0xBE, (byte)0xEF };
        assertArrayEquals(expected, frameSession.syncPattern());
    }

    @Test
    @DisplayName("FrameSession: minFrameHeaderSize returns 5")
    void frameSessionMinFrameHeaderSize() {
        assertEquals(5, frameSession.minFrameHeaderSize());
    }

    // ========================================================================
    // Frame encode/decode roundtrip (choice_protocol)
    // ========================================================================

    @Test
    @DisplayName("Frame: wrap AlphaBody and encode/decode roundtrip")
    void frameWrapAlphaRoundtrip() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        alpha.x = 300;
        alpha.y = 400;
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();

        choice_test.Frame decoded = choice_test.Frame.decodeBytes(encoded);
        assertEquals((int) choice_test.Constants.SYNC, decoded.sync);
        assertEquals(1, decoded.messageType);
        assertInstanceOf(choice_test.AlphaBody.class, decoded.payload);
        choice_test.AlphaBody decodedAlpha = (choice_test.AlphaBody) decoded.payload;
        assertEquals(300, decodedAlpha.x);
        assertEquals(400, decodedAlpha.y);
    }

    @Test
    @DisplayName("Frame: wrap BetaBody and encode/decode roundtrip")
    void frameWrapBetaRoundtrip() {
        choice_test.BetaBody beta = new choice_test.BetaBody();
        beta.payloadSize = 50;
        beta.tag = 0xFACE;
        choice_test.Frame frame = choice_test.Frame.wrap(beta);
        byte[] encoded = frame.encodeBytes();

        choice_test.Frame decoded = choice_test.Frame.decodeBytes(encoded);
        assertEquals(2, decoded.messageType);
        assertInstanceOf(choice_test.BetaBody.class, decoded.payload);
        choice_test.BetaBody decodedBeta = (choice_test.BetaBody) decoded.payload;
        assertEquals(50, decodedBeta.payloadSize);
        assertEquals(0xFACE, decodedBeta.tag);
    }

    @Test
    @DisplayName("Frame: length is correctly calculated for AlphaBody")
    void frameAlphaLength() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        // Header: sync(2) + messageType(1) + length(2) = 5, payload: x(2) + y(2) = 4
        assertEquals(9, encoded.length);
        choice_test.Frame decoded = choice_test.Frame.decodeBytes(encoded);
        assertEquals(9, decoded.length);
    }

    @Test
    @DisplayName("Frame: sync value is BEEF")
    void frameSync() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        choice_test.Frame decoded = choice_test.Frame.decodeBytes(encoded);
        assertEquals(0xBEEF, decoded.sync);
    }

    // ========================================================================
    // FrameSession encodeWrap / decodeFrame roundtrip
    // ========================================================================

    @Test
    @DisplayName("FrameSession: encodeWrap/decodeFrame AlphaBody roundtrip")
    void frameSessionEncodeDecodeAlpha() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        alpha.x = 1000;
        alpha.y = 2000;
        Map<String, Object> wrapped = frameSession.encodeWrap(
            choice_test.AlphaBody.TYPE_ID, alpha);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = frameSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        choice_test.AlphaBody decoded = (choice_test.AlphaBody) messages.get(0).get("payload");
        assertEquals(1000, decoded.x);
        assertEquals(2000, decoded.y);
    }

    @Test
    @DisplayName("FrameSession: encodeWrap/decodeFrame BetaBody roundtrip")
    void frameSessionEncodeDecodeBeta() {
        choice_test.BetaBody beta = new choice_test.BetaBody();
        beta.payloadSize = 200;
        beta.tag = 0xBEEF;
        Map<String, Object> wrapped = frameSession.encodeWrap(
            choice_test.BetaBody.TYPE_ID, beta);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = frameSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        choice_test.BetaBody decoded = (choice_test.BetaBody) messages.get(0).get("payload");
        assertEquals(200, decoded.payloadSize);
        assertEquals(0xBEEF, decoded.tag);
    }

    // ========================================================================
    // Constants tests
    // ========================================================================

    @Test
    @DisplayName("Constants: session_test SYNC is 0xDEAD")
    void sessionConstantsSync() {
        assertEquals(0xDEAD, session_test.Constants.SYNC);
    }

    @Test
    @DisplayName("Constants: choice_test SYNC is 0xBEEF")
    void choiceConstantsSync() {
        assertEquals(0xBEEF, choice_test.Constants.SYNC);
    }

    // ========================================================================
    // Helper methods
    // ========================================================================

    private boolean containsId(long[] ids, long target) {
        for (long id : ids) {
            if (id == target) return true;
        }
        return false;
    }
}
