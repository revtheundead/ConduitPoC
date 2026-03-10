import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import java.util.List;
import java.util.Map;

/**
 * Extended session tests covering direction-qualified messages, sentry-link
 * auto-increment wrap-around, frame error cases, float special values, and
 * wire encoding overflow — matching C++ test_generated_session.cpp and
 * test_frame_roundtrip.cpp coverage.
 */
public class TestSessionExtended {

    // ========================================================================
    // Direction-qualified session tests (direction_qualified protocol)
    // Matches C++ test_generated_session.cpp direction tests
    // ========================================================================

    private direction_qualified.FrameSession dirSession;

    @BeforeEach
    void setUp() {
        dirSession = new direction_qualified.FrameSession();
    }

    @Test
    @DisplayName("direction: session creation succeeds")
    void directionSessionCreation() {
        assertNotNull(dirSession);
    }

    @Test
    @DisplayName("direction: leafTypeIds returns 3 types")
    void directionLeafTypeIds() {
        long[] ids = dirSession.leafTypeIds();
        assertEquals(3, ids.length);
    }

    @Test
    @DisplayName("direction: typeName for UplinkPayload")
    void directionTypeNameUplink() {
        assertEquals("UplinkPayload", dirSession.typeName(direction_qualified.UplinkPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: typeName for DownlinkPayload")
    void directionTypeNameDownlink() {
        assertEquals("DownlinkPayload", dirSession.typeName(direction_qualified.DownlinkPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: typeName for CommonPayload")
    void directionTypeNameCommon() {
        assertEquals("CommonPayload", dirSession.typeName(direction_qualified.CommonPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: protocolName returns direction_qualified")
    void directionProtocolName() {
        assertEquals("direction_qualified", dirSession.protocolName());
    }

    @Test
    @DisplayName("direction: isReceiveOnly for DownlinkPayload returns true")
    void directionDownlinkIsReceiveOnly() {
        assertTrue(dirSession.isReceiveOnly(direction_qualified.DownlinkPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: isReceiveOnly for UplinkPayload returns false")
    void directionUplinkIsNotReceiveOnly() {
        assertFalse(dirSession.isReceiveOnly(direction_qualified.UplinkPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: isReceiveOnly for CommonPayload returns false")
    void directionCommonIsNotReceiveOnly() {
        assertFalse(dirSession.isReceiveOnly(direction_qualified.CommonPayload.TYPE_ID));
    }

    @Test
    @DisplayName("direction: syncPattern is empty")
    void directionSyncPattern() {
        assertEquals(0, dirSession.syncPattern().length);
    }

    @Test
    @DisplayName("direction: minFrameHeaderSize is 1")
    void directionMinFrameHeaderSize() {
        assertEquals(1, dirSession.minFrameHeaderSize());
    }

    @Test
    @DisplayName("direction: decode shared discriminator produces receive variant (DownlinkPayload)")
    void directionDecodeProducesReceiveVariant() {
        // Build wire: tag=1 (shared ID for uplink/downlink) + uint32 rx-data
        direction_qualified.codec.BitWriter w = new direction_qualified.codec.BitWriter();
        w.writeU8(direction_qualified.DownlinkPayload.ID_VALUE);  // tag = 1
        w.writeU32(0xAABBCCDD, true);
        byte[] bytes = w.toBytes();

        direction_qualified.Frame decoded = direction_qualified.Frame.decodeBytes(bytes);
        // Decoder should prefer receive variant (DownlinkPayload) for shared ID
        assertInstanceOf(direction_qualified.DownlinkPayload.class, decoded.payload);
    }

    @Test
    @DisplayName("direction: decode shared discriminator never produces send variant (UplinkPayload)")
    void directionDecodeNeverProducesSendVariant() {
        // tag=1 is shared by UplinkPayload (send) and DownlinkPayload (receive)
        direction_qualified.codec.BitWriter w = new direction_qualified.codec.BitWriter();
        w.writeU8(1);  // shared ID
        w.writeU32(0x12345678, true);
        byte[] bytes = w.toBytes();

        direction_qualified.Frame decoded = direction_qualified.Frame.decodeBytes(bytes);
        assertFalse(decoded.payload instanceof direction_qualified.UplinkPayload,
            "Decoder should not produce send-only variant for shared discriminator");
    }

    @Test
    @DisplayName("direction: encode_wrap UplinkPayload (send-only) succeeds")
    void directionEncodeWrapUplinkSucceeds() {
        direction_qualified.UplinkPayload uplink = new direction_qualified.UplinkPayload();
        uplink.txData = 0x1234;

        Map<String, Object> result = dirSession.encodeWrap(
            direction_qualified.UplinkPayload.TYPE_ID, uplink);
        assertNotNull(result);
        assertNotNull(result.get("bytes"));
    }

    @Test
    @DisplayName("direction: encode_wrap DownlinkPayload (receive-only) succeeds")
    void directionEncodeWrapDownlinkSucceeds() {
        direction_qualified.DownlinkPayload downlink = new direction_qualified.DownlinkPayload();
        downlink.rxData = 0xDEADBEEF;

        Map<String, Object> result = dirSession.encodeWrap(
            direction_qualified.DownlinkPayload.TYPE_ID, downlink);
        assertNotNull(result, "encode_wrap should succeed even for receive-only (direction is documentary)");
    }

    @Test
    @DisplayName("direction: encode_wrap CommonPayload roundtrip")
    void directionEncodeWrapCommonRoundtrip() {
        direction_qualified.CommonPayload common = new direction_qualified.CommonPayload();
        common.commonData = 42;

        Map<String, Object> wrapped = dirSession.encodeWrap(
            direction_qualified.CommonPayload.TYPE_ID, common);
        assertNotNull(wrapped);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = dirSession.decodeFrame(frameBytes);
        assertEquals(1, messages.size());
        assertEquals("CommonPayload", messages.get(0).get("type_name"));
        direction_qualified.CommonPayload decoded =
            (direction_qualified.CommonPayload) messages.get(0).get("payload");
        assertEquals(42, decoded.commonData);
    }

    @Test
    @DisplayName("direction: wrap UplinkPayload sets correct discriminator (tag)")
    void directionWrapUplinkSetsCorrectTag() {
        direction_qualified.UplinkPayload uplink = new direction_qualified.UplinkPayload();
        uplink.txData = 100;
        direction_qualified.Frame frame = direction_qualified.Frame.wrap(uplink);
        assertEquals(direction_qualified.UplinkPayload.ID_VALUE, frame.tag);
    }

    @Test
    @DisplayName("direction: wrap DownlinkPayload sets correct discriminator (tag)")
    void directionWrapDownlinkSetsCorrectTag() {
        direction_qualified.DownlinkPayload downlink = new direction_qualified.DownlinkPayload();
        downlink.rxData = 200;
        direction_qualified.Frame frame = direction_qualified.Frame.wrap(downlink);
        assertEquals(direction_qualified.DownlinkPayload.ID_VALUE, frame.tag);
    }

    @Test
    @DisplayName("direction: wrap CommonPayload sets correct discriminator (tag)")
    void directionWrapCommonSetsCorrectTag() {
        direction_qualified.CommonPayload common = new direction_qualified.CommonPayload();
        common.commonData = 7;
        direction_qualified.Frame frame = direction_qualified.Frame.wrap(common);
        assertEquals(direction_qualified.CommonPayload.ID_VALUE, frame.tag);
    }

    @Test
    @DisplayName("direction: decode_frame extracts receive variant from shared discriminator")
    void directionDecodeFrameExtractsReceiveVariant() {
        // Wire bytes for DownlinkPayload (receive): tag=1, u32 data
        direction_qualified.codec.BitWriter w = new direction_qualified.codec.BitWriter();
        w.writeU8(direction_qualified.DownlinkPayload.ID_VALUE);
        w.writeU32(0xCAFEBABE, true);
        byte[] bytes = w.toBytes();

        List<Map<String, Object>> messages = dirSession.decodeFrame(bytes);
        assertNotNull(messages);
        assertEquals(1, messages.size());
        assertEquals("DownlinkPayload", messages.get(0).get("type_name"));
        assertInstanceOf(direction_qualified.DownlinkPayload.class, messages.get(0).get("payload"));
    }

    @Test
    @DisplayName("direction: encode_wrap unknown type returns null")
    void directionEncodeWrapUnknownType() {
        assertNull(dirSession.encodeWrap(0x1111111111111111L, new Object()));
    }

    // ========================================================================
    // Sentry-link session tests (8-bit auto-increment wrap-around)
    // Matches C++ "auto-increment 8-bit wrap-around" test
    // ========================================================================

    @Test
    @DisplayName("sentry_link: session creation succeeds")
    void sentryLinkCreation() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        assertNotNull(session);
    }

    @Test
    @DisplayName("sentry_link: leafTypeIds returns 4 types")
    void sentryLinkLeafTypeIds() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        long[] ids = session.leafTypeIds();
        assertEquals(4, ids.length);
    }

    @Test
    @DisplayName("sentry_link: protocolName returns sentry_link")
    void sentryLinkProtocolName() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        assertEquals("sentry_link", session.protocolName());
    }

    @Test
    @DisplayName("sentry_link: syncPattern returns AA 55")
    void sentryLinkSyncPattern() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        byte[] expected = new byte[] { (byte) 0xAA, (byte) 0x55 };
        assertArrayEquals(expected, session.syncPattern());
    }

    @Test
    @DisplayName("sentry_link: minFrameHeaderSize returns 6")
    void sentryLinkMinFrameHeaderSize() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        assertEquals(6, session.minFrameHeaderSize());
    }

    @Test
    @DisplayName("sentry_link: isReceiveOnly for HeartbeatBody returns true")
    void sentryLinkHeartbeatReceiveOnly() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        assertTrue(session.isReceiveOnly(sentry_link.HeartbeatBody.TYPE_ID));
    }

    @Test
    @DisplayName("sentry_link: isReceiveOnly for ConfigBody returns false (send-only)")
    void sentryLinkConfigNotReceiveOnly() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        assertFalse(session.isReceiveOnly(sentry_link.ConfigBody.TYPE_ID));
    }

    @Test
    @DisplayName("sentry_link: encodeWrap HeartbeatBody roundtrip")
    void sentryLinkEncodeDecodeHeartbeat() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        sentry_link.HeartbeatBody hb = new sentry_link.HeartbeatBody();
        hb.timestamp = 1000;
        hb.uptimeHours = 48;
        hb.status = sentry_link.DeviceStatus.ONLINE;
        hb.cpuLoad = 75;

        Map<String, Object> wrapped = session.encodeWrap(
            sentry_link.HeartbeatBody.TYPE_ID, hb);
        assertNotNull(wrapped);
        byte[] frameBytes = (byte[]) wrapped.get("bytes");

        List<Map<String, Object>> messages = session.decodeFrame(frameBytes);
        assertNotNull(messages);
        assertEquals(1, messages.size());
        assertEquals("HeartbeatBody", messages.get(0).get("type_name"));
        sentry_link.HeartbeatBody decoded =
            (sentry_link.HeartbeatBody) messages.get(0).get("payload");
        assertEquals(1000, decoded.timestamp);
        assertEquals(48, decoded.uptimeHours);
        assertEquals(sentry_link.DeviceStatus.ONLINE, decoded.status);
        assertEquals(75, decoded.cpuLoad);
    }

    @Test
    @DisplayName("sentry_link: auto-increment 8-bit wrap-around")
    void sentryLinkAutoIncrement8BitWrapAround() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        sentry_link.HeartbeatBody hb = new sentry_link.HeartbeatBody();
        hb.status = sentry_link.DeviceStatus.ONLINE;

        // Send 260 messages — sequence should wrap at 256 (8-bit counter)
        for (int i = 0; i < 260; i++) {
            Map<String, Object> result = session.encodeWrap(
                sentry_link.HeartbeatBody.TYPE_ID, hb);
            assertNotNull(result, "encodeWrap should succeed at iteration " + i);
        }

        // After 260 sends, counter should be 260 (wrapping is applied per-frame, not to counter)
        assertEquals(260, session.sequenceCounter());

        // Verify the sequence byte in the encoded frame wraps modulo 256
        session.reset();
        for (int i = 0; i < 256; i++) {
            session.encodeWrap(sentry_link.HeartbeatBody.TYPE_ID, hb);
        }
        // Counter is now 256, masked to 8 bits = 0
        Map<String, Object> wrap256 = session.encodeWrap(
            sentry_link.HeartbeatBody.TYPE_ID, hb);
        byte[] frame256 = (byte[]) wrap256.get("bytes");
        // Sequence byte is at offset 5 (sync:2 + msg_type:1 + length:2)
        assertEquals(0, frame256[5] & 0xFF,
            "Sequence byte should wrap to 0 at 256th message");
    }

    @Test
    @DisplayName("sentry_link: auto-increment is per-session not per-type")
    void sentryLinkAutoIncrementPerSession() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        sentry_link.HeartbeatBody hb = new sentry_link.HeartbeatBody();
        hb.status = sentry_link.DeviceStatus.ONLINE;
        sentry_link.AlertBody alert = new sentry_link.AlertBody();

        session.encodeWrap(sentry_link.HeartbeatBody.TYPE_ID, hb);
        assertEquals(1, session.sequenceCounter());

        session.encodeWrap(sentry_link.AlertBody.TYPE_ID, alert);
        assertEquals(2, session.sequenceCounter(),
            "Sequence counter should be shared across message types");

        session.encodeWrap(sentry_link.HeartbeatBody.TYPE_ID, hb);
        assertEquals(3, session.sequenceCounter());
    }

    @Test
    @DisplayName("sentry_link: reset clears sequence counter")
    void sentryLinkResetClearsSequence() {
        sentry_link.FrameSession session = new sentry_link.FrameSession();
        sentry_link.HeartbeatBody hb = new sentry_link.HeartbeatBody();
        hb.status = sentry_link.DeviceStatus.ONLINE;

        session.encodeWrap(sentry_link.HeartbeatBody.TYPE_ID, hb);
        session.encodeWrap(sentry_link.HeartbeatBody.TYPE_ID, hb);
        assertEquals(2, session.sequenceCounter());

        session.reset();
        assertEquals(0, session.sequenceCounter());
    }

    // ========================================================================
    // Frame error cases
    // Matches C++ "decode_frame with truncated data" and "unknown ID" tests
    // ========================================================================

    @Test
    @DisplayName("Packet: decode truncated data throws exception")
    void packetDecodeTruncatedThrows() {
        // Less than minimum frame header (7 bytes)
        byte[] truncated = new byte[] { (byte) 0xDE, (byte) 0xAD, 0x00 };
        assertThrows(Exception.class, () -> {
            session_test.Packet.decodeBytes(truncated);
        });
    }

    @Test
    @DisplayName("Packet: decode_frame with truncated data returns empty or null")
    void packetSessionDecodeTruncated() {
        session_test.PacketSession session = new session_test.PacketSession();
        byte[] truncated = new byte[] { (byte) 0xDE, (byte) 0xAD, 0x00 };
        // Should not crash — either returns empty list or throws
        try {
            List<Map<String, Object>> result = session.decodeFrame(truncated);
            // If it doesn't throw, the result should be valid (possibly empty)
            assertNotNull(result);
        } catch (Exception e) {
            // Throwing is acceptable for truncated input
        }
    }

    @Test
    @DisplayName("ID wire byte matches message ID_VALUE in encoded frame")
    void idWireByteMatchesIdValue() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        frame.seq = 0;
        byte[] encoded = frame.encodeBytes();

        // msg-id byte is at offset 4 (sync:2 + seq:2 + msgId:1)
        int wireId = encoded[4] & 0xFF;
        assertEquals(session_test.PingBody.ID_VALUE, wireId,
            "Wire msg-id byte should match PingBody.ID_VALUE");
    }

    @Test
    @DisplayName("DataBody ID wire byte matches ID_VALUE")
    void dataBodyIdWireByteMatches() {
        session_test.DataBody data = new session_test.DataBody();
        session_test.Packet frame = session_test.Packet.wrap(data);
        byte[] encoded = frame.encodeBytes();
        int wireId = encoded[4] & 0xFF;
        assertEquals(session_test.DataBody.ID_VALUE, wireId);
    }

    @Test
    @DisplayName("AckBody ID wire byte matches ID_VALUE")
    void ackBodyIdWireByteMatches() {
        session_test.AckBody ack = new session_test.AckBody();
        session_test.Packet frame = session_test.Packet.wrap(ack);
        byte[] encoded = frame.encodeBytes();
        int wireId = encoded[4] & 0xFF;
        assertEquals(session_test.AckBody.ID_VALUE, wireId);
    }

    // ========================================================================
    // Float special value roundtrip tests
    // Matches C++ test_generated_roundtrip.cpp float NaN/Infinity tests
    // ========================================================================

    @Test
    @DisplayName("float32 NaN roundtrip")
    void float32NanRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f32 = Float.NaN;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertTrue(Float.isNaN(decoded.f32), "Decoded f32 should be NaN");
    }

    @Test
    @DisplayName("float32 +Infinity roundtrip")
    void float32PosInfinityRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f32 = Float.POSITIVE_INFINITY;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Float.POSITIVE_INFINITY, decoded.f32);
    }

    @Test
    @DisplayName("float32 -Infinity roundtrip")
    void float32NegInfinityRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f32 = Float.NEGATIVE_INFINITY;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Float.NEGATIVE_INFINITY, decoded.f32);
    }

    @Test
    @DisplayName("float32 negative zero roundtrip")
    void float32NegZeroRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f32 = -0.0f;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Float.floatToRawIntBits(-0.0f), Float.floatToRawIntBits(decoded.f32),
            "Decoded f32 should preserve negative zero");
    }

    @Test
    @DisplayName("float64 NaN roundtrip")
    void float64NanRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f64 = Double.NaN;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertTrue(Double.isNaN(decoded.f64), "Decoded f64 should be NaN");
    }

    @Test
    @DisplayName("float64 +Infinity roundtrip")
    void float64PosInfinityRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f64 = Double.POSITIVE_INFINITY;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Double.POSITIVE_INFINITY, decoded.f64);
    }

    @Test
    @DisplayName("float64 -Infinity roundtrip")
    void float64NegInfinityRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f64 = Double.NEGATIVE_INFINITY;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Double.NEGATIVE_INFINITY, decoded.f64);
    }

    @Test
    @DisplayName("float64 negative zero roundtrip")
    void float64NegZeroRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.f64 = -0.0;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Double.doubleToRawLongBits(-0.0), Double.doubleToRawLongBits(decoded.f64),
            "Decoded f64 should preserve negative zero");
    }

    // ========================================================================
    // Wire encoding overflow tests
    // Matches C++ test_wire_encoding_roundtrip.cpp overflow tests
    // ========================================================================

    @Test
    @DisplayName("WireEncodingMsg: basic roundtrip")
    void wireEncodingMsgRoundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 1234;
        msg.bcdHdg = 90;
        msg.smOffset = -100;
        msg.cb2Val = -500;
        msg.bnrVal = 1000;
        msg.inlineBcd = 567;
        msg.inlineBnrs = 200;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(1234, decoded.bcdAlt);
        assertEquals(90, decoded.bcdHdg);
        assertEquals(-100, decoded.smOffset);
        assertEquals(-500, decoded.cb2Val);
        assertEquals(1000, decoded.bnrVal);
        assertEquals(567, decoded.inlineBcd);
        assertEquals(200, decoded.inlineBnrs);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD zero values roundtrip")
    void wireEncodingBcdZero() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(0, decoded.bcdAlt);
        assertEquals(0, decoded.bcdHdg);
        assertEquals(0, decoded.smOffset);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD max values")
    void wireEncodingBcdMax() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9999;  // Max for 16-bit BCD (4 digits)
        msg.inlineBcd = 999;  // Max for 12-bit BCD (3 digits)

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(9999, decoded.bcdAlt);
        assertEquals(999, decoded.inlineBcd);
    }

    @Test
    @DisplayName("WireEncodingMsg: sign-magnitude positive values")
    void wireEncodingSignMagnitudePositive() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.smOffset = 100;
        msg.inlineBnrs = 32767;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(100, decoded.smOffset);
        assertEquals(32767, decoded.inlineBnrs);
    }

    @Test
    @DisplayName("WireEncodingMsg: negative BCD heading roundtrip")
    void wireEncodingNegativeBcdHeading() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdHdg = -180;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-180, decoded.bcdHdg);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD altitude wire format")
    void wireEncodingBcdAltWireFormat() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9876;
        byte[] encoded = msg.encodeBytes();
        // First two bytes should be BCD 9876 = 0x98 0x76
        assertEquals((byte) 0x98, encoded[0]);
        assertEquals((byte) 0x76, encoded[1]);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD overflow on encode 16-bit")
    void wireEncodingBcdOverflow16() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 10000;  // Exceeds 4-digit BCD (0-9999)
        assertThrows(Exception.class, () -> msg.encodeBytes(),
            "BCD encode should fail for value > max digits");
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD overflow on encode 12-bit")
    void wireEncodingBcdOverflow12() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.inlineBcd = 1000;  // Exceeds 3-digit BCD (0-999)
        assertThrows(Exception.class, () -> msg.encodeBytes(),
            "BCD encode should fail for value > max digits");
    }

    @Test
    @DisplayName("WireEncodingMsg: decode from truncated buffer fails")
    void wireEncodingDecodeTruncated() {
        byte[] truncated = new byte[] { 0x12, 0x34 };  // Only 2 bytes, need 14
        assertThrows(Exception.class, () ->
            wire_encodings.WireEncodingMsg.decodeBytes(truncated));
    }

    @Test
    @DisplayName("WireEncodingMsg: decode from empty buffer fails")
    void wireEncodingDecodeEmpty() {
        assertThrows(Exception.class, () ->
            wire_encodings.WireEncodingMsg.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("WireEncodingMsg: tight packing wire size is 14 bytes")
    void wireEncodingTightPackingSize() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 1234;
        msg.bcdHdg = 90;
        msg.smOffset = 100;
        msg.cb2Val = -1;
        msg.bnrVal = 1;
        msg.inlineBcd = 123;
        msg.inlineBnrs = 50;

        byte[] encoded = msg.encodeBytes();
        // Total bits: 16+13+16+16+16+12+16 = 105 bits = ceil(105/8) = 14 bytes
        assertEquals(14, encoded.length, "Wire format should be tightly packed to 14 bytes");
    }
}
