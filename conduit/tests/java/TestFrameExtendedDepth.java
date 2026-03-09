import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Deep frame tests matching C++ test_frame_roundtrip.cpp depth.
 * Covers: frame_config (config field), frame_footer (checksum footer),
 * frame_direction (direction-qualified messages), frame_array (array payload).
 */
public class TestFrameExtendedDepth {

    // ========================================================================
    // frame_footer: FooterFrame with checksum footer field
    // (C++ "frame_footer: Data roundtrip with footer")
    // ========================================================================

    @Test
    @DisplayName("frame_footer: Data roundtrip with checksum")
    void frameFooterDataRoundtrip() {
        frame_footer.Data dataMsg = new frame_footer.Data();
        dataMsg.value = 0xABCD;

        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(dataMsg);
        frame.checksum = 0x42;

        byte[] encoded = frame.encodeBytes();

        // Wire: [msg_type:1][length:2][value:2][checksum:1] = 6 bytes
        assertEquals(6, encoded.length);
        assertEquals(1, encoded[0] & 0xFF);          // msg_type
        assertEquals(0x42, encoded[5] & 0xFF);        // checksum (footer)

        frame_footer.FooterFrame decoded = frame_footer.FooterFrame.decodeBytes(encoded);
        assertEquals(1, decoded.msgType);
        assertEquals(6, decoded.length);
        assertEquals(0x42, decoded.checksum);
        assertInstanceOf(frame_footer.Data.class, decoded.payload);
        assertEquals(0xABCD, ((frame_footer.Data) decoded.payload).value);
    }

    @Test
    @DisplayName("frame_footer: Ack roundtrip with checksum")
    void frameFooterAckRoundtrip() {
        frame_footer.Ack ack = new frame_footer.Ack();
        ack.seq = 99;

        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(ack);
        frame.checksum = (byte) 0xFF;

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2][seq:1][checksum:1] = 5 bytes
        assertEquals(5, encoded.length);

        frame_footer.FooterFrame decoded = frame_footer.FooterFrame.decodeBytes(encoded);
        assertEquals(0xFF, decoded.checksum & 0xFF);
        assertInstanceOf(frame_footer.Ack.class, decoded.payload);
        assertEquals(99, ((frame_footer.Ack) decoded.payload).seq);
    }

    @Test
    @DisplayName("frame_footer: double-encode idempotency")
    void frameFooterDoubleEncode() {
        frame_footer.Data dataMsg = new frame_footer.Data();
        dataMsg.value = 12345;
        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(dataMsg);
        frame.checksum = 0x42;

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // frame_direction: DirFrame with direction-qualified messages
    // (C++ "frame_direction: CommandResponse roundtrip")
    // ========================================================================

    @Test
    @DisplayName("frame_direction: CommandResponse roundtrip (receive)")
    void frameDirCommandResponseRoundtrip() {
        frame_direction.CommandResponse msg = new frame_direction.CommandResponse();
        msg.status = 1;
        msg.detail = 500;

        frame_direction.DirFrame frame = frame_direction.DirFrame.wrap(msg);
        assertEquals(1, frame.msgType);  // ID_VALUE = 1

        byte[] encoded = frame.encodeBytes();
        frame_direction.DirFrame decoded = frame_direction.DirFrame.decodeBytes(encoded);

        assertInstanceOf(frame_direction.CommandResponse.class, decoded.payload);
        assertEquals(1, ((frame_direction.CommandResponse) decoded.payload).status);
        assertEquals(500, ((frame_direction.CommandResponse) decoded.payload).detail);
    }

    @Test
    @DisplayName("frame_direction: Heartbeat roundtrip (bidirectional)")
    void frameDirHeartbeatRoundtrip() {
        frame_direction.Heartbeat msg = new frame_direction.Heartbeat();
        msg.seq = 9999;

        frame_direction.DirFrame frame = frame_direction.DirFrame.wrap(msg);
        assertEquals(2, frame.msgType);

        byte[] encoded = frame.encodeBytes();
        frame_direction.DirFrame decoded = frame_direction.DirFrame.decodeBytes(encoded);

        assertInstanceOf(frame_direction.Heartbeat.class, decoded.payload);
        assertEquals(9999, ((frame_direction.Heartbeat) decoded.payload).seq);
    }

    @Test
    @DisplayName("frame_direction: message constants are distinct")
    void frameDirMessageConstants() {
        assertEquals(1, frame_direction.CommandRequest.ID_VALUE);
        assertEquals(1, frame_direction.CommandResponse.ID_VALUE);
        assertEquals(2, frame_direction.Heartbeat.ID_VALUE);
        assertNotEquals(frame_direction.CommandRequest.TYPE_ID,
            frame_direction.CommandResponse.TYPE_ID);
    }

    @Test
    @DisplayName("frame_direction: double-encode idempotency")
    void frameDirDoubleEncode() {
        frame_direction.Heartbeat msg = new frame_direction.Heartbeat();
        msg.seq = 42;
        frame_direction.DirFrame frame = frame_direction.DirFrame.wrap(msg);

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // frame_array: ArrayFrame with count="*" array payload
    // (C++ "frame_array: single record roundtrip")
    // ========================================================================

    @Test
    @DisplayName("frame_array: single record roundtrip")
    void frameArraySingleRecord() {
        frame_array.ArrayFrame frame = new frame_array.ArrayFrame();
        frame_array.Record rec = new frame_array.Record();
        rec.key = 1;
        rec.value = 1000;
        frame.payload.add(rec);
        frame.msgType = 1;

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2][key:1][value:2] = 6 bytes
        assertEquals(6, encoded.length);

        frame_array.ArrayFrame decoded = frame_array.ArrayFrame.decodeBytes(encoded);
        assertEquals(1, decoded.payload.size());
        assertEquals(1, decoded.payload.get(0).key);
        assertEquals(1000, decoded.payload.get(0).value);
    }

    @Test
    @DisplayName("frame_array: multiple records roundtrip")
    void frameArrayMultipleRecords() {
        frame_array.ArrayFrame frame = new frame_array.ArrayFrame();
        frame.msgType = 1;
        for (int i = 0; i < 5; i++) {
            frame_array.Record rec = new frame_array.Record();
            rec.key = (byte) i;
            rec.value = i * 100;
            frame.payload.add(rec);
        }

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2] + 5 * [key:1][value:2] = 3 + 15 = 18 bytes
        assertEquals(18, encoded.length);

        frame_array.ArrayFrame decoded = frame_array.ArrayFrame.decodeBytes(encoded);
        assertEquals(5, decoded.payload.size());
        for (int i = 0; i < 5; i++) {
            assertEquals(i, decoded.payload.get(i).key);
            assertEquals(i * 100, decoded.payload.get(i).value);
        }
    }

    @Test
    @DisplayName("frame_array: empty payload roundtrip")
    void frameArrayEmptyPayload() {
        frame_array.ArrayFrame frame = new frame_array.ArrayFrame();
        frame.msgType = 1;

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2] = 3 bytes (header only)
        assertEquals(3, encoded.length);

        frame_array.ArrayFrame decoded = frame_array.ArrayFrame.decodeBytes(encoded);
        assertEquals(0, decoded.payload.size());
    }

    @Test
    @DisplayName("frame_array: double-encode idempotency")
    void frameArrayDoubleEncode() {
        frame_array.ArrayFrame frame = new frame_array.ArrayFrame();
        frame.msgType = 1;
        frame_array.Record rec = new frame_array.Record();
        rec.key = 7;
        rec.value = 777;
        frame.payload.add(rec);

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // frame_config: ConfigFrame with auto="config(system-id)"
    // (C++ "frame_config: config field set during encode")
    // ========================================================================

    @Test
    @DisplayName("frame_config: direct frame encode/decode")
    void frameConfigDirectRoundtrip() {
        frame_config.Ping ping = new frame_config.Ping();
        ping.seq = 300;

        frame_config.ConfigFrame frame = frame_config.ConfigFrame.wrap(ping);
        frame.systemId = 10;

        byte[] encoded = frame.encodeBytes();
        // Wire: [system-id:1][msg-type:1][length:2][seq:2] = 6 bytes
        assertEquals(6, encoded.length);
        assertEquals(10, encoded[0] & 0xFF);  // system-id from config
        assertEquals(1, encoded[1] & 0xFF);   // msg-type = Ping::ID_VALUE

        frame_config.ConfigFrame decoded = frame_config.ConfigFrame.decodeBytes(encoded);
        assertEquals(10, decoded.systemId);
        assertEquals(1, decoded.msgType);
        assertEquals(6, decoded.length);
        assertInstanceOf(frame_config.Ping.class, decoded.payload);
        assertEquals(300, ((frame_config.Ping) decoded.payload).seq);
    }

    @Test
    @DisplayName("frame_config: Pong roundtrip with config")
    void frameConfigPongRoundtrip() {
        frame_config.Pong pong = new frame_config.Pong();
        pong.seq = 200;

        frame_config.ConfigFrame frame = frame_config.ConfigFrame.wrap(pong);
        frame.systemId = 42;

        byte[] encoded = frame.encodeBytes();
        assertEquals(42, encoded[0] & 0xFF);  // system-id
        assertEquals(2, encoded[1] & 0xFF);   // msg-type = Pong::ID_VALUE

        frame_config.ConfigFrame decoded = frame_config.ConfigFrame.decodeBytes(encoded);
        assertEquals(42, decoded.systemId);
        assertInstanceOf(frame_config.Pong.class, decoded.payload);
        assertEquals(200, ((frame_config.Pong) decoded.payload).seq);
    }

    @Test
    @DisplayName("frame_config: double-encode idempotency")
    void frameConfigDoubleEncode() {
        frame_config.Ping ping = new frame_config.Ping();
        ping.seq = 100;
        frame_config.ConfigFrame frame = frame_config.ConfigFrame.wrap(ping);
        frame.systemId = 7;

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }
}
