import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Deep FX block and frame_basic tests matching C++ test_fx_edge_cases.cpp
 * and test_frame_roundtrip.cpp depth.
 * Covers: FX truncation, FX wire format sizes, encode idempotency,
 * frame_basic wire format and message constants.
 */
public class TestFxBlockDepth {

    // ========================================================================
    // FX truncation: partial item data (C++ test_fx_edge_cases.cpp)
    // ========================================================================

    @Test
    @DisplayName("FX decode: FX=1 with partial item1 data fails")
    void fxDecodePartialItem1() {
        // header(8) + FX=1(1) + only 8 bits of item1 (needs 16)
        fx_block.codec.BitWriter w = new fx_block.codec.BitWriter();
        w.writeU8(0x00);      // header
        w.writeBits(1, 1);    // FX bit = 1
        w.writeU8(0xAA);      // only 1 byte of item1 (needs 2)
        byte[] bytes = w.toBytes();

        assertThrows(Exception.class, () -> fx_block.FxMessage.decodeBytes(bytes),
            "FX decode with partial item1 data should fail");
    }

    @Test
    @DisplayName("FX decode: single byte buffer (just header, no FX bit) fails")
    void fxDecodeSingleByte() {
        byte[] data = new byte[] { 0x42 };
        assertThrows(Exception.class, () -> fx_block.FxMessage.decodeBytes(data),
            "FX decode with insufficient data for FX bit should fail");
    }

    @Test
    @DisplayName("FX decode: empty buffer fails")
    void fxDecodeEmpty() {
        assertThrows(Exception.class, () -> fx_block.FxMessage.decodeBytes(new byte[0]),
            "FX decode of empty buffer should fail");
    }

    // ========================================================================
    // FX no items roundtrip
    // ========================================================================

    @Test
    @DisplayName("FX no items: roundtrip preserves header only")
    void fxNoItemsRoundtrip() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0xBB;

        byte[] encoded = msg.encodeBytes();
        fx_block.FxMessage decoded = fx_block.FxMessage.decodeBytes(encoded);
        assertEquals(0xBB, decoded.header);
        assertNull(decoded.item1, "item1 should not be present");
    }

    // ========================================================================
    // FX with item1 roundtrip
    // ========================================================================

    @Test
    @DisplayName("FX with item1: roundtrip preserves value")
    void fxWithItem1Roundtrip() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0x11;
        msg.item1 = 0x1234;

        byte[] encoded = msg.encodeBytes();
        fx_block.FxMessage decoded = fx_block.FxMessage.decodeBytes(encoded);
        assertEquals(0x11, decoded.header);
        assertEquals((Integer) 0x1234, decoded.item1);
    }

    // ========================================================================
    // FX double-encode idempotency
    // ========================================================================

    @Test
    @DisplayName("FX: double-encode produces identical bytes")
    void fxDoubleEncodeIdempotency() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0x42;
        msg.item1 = 0x5678;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second,
            "Encoding the same FX message twice should produce identical bytes");
    }

    // ========================================================================
    // FX wire format sizes
    // ========================================================================

    @Test
    @DisplayName("FX wire format: no items = 2 bytes (header + FX=0)")
    void fxWireFormatNoItems() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0x00;
        byte[] encoded = msg.encodeBytes();
        // header(8) + FX=0(1) = 9 bits -> 2 bytes
        assertEquals(2, encoded.length, "FX message with no items should be 2 bytes");
    }

    @Test
    @DisplayName("FX wire format: with items is larger than without")
    void fxWireFormatWithItemsLarger() {
        fx_block.FxMessage noItems = new fx_block.FxMessage();
        noItems.header = 0x00;
        byte[] noItemsBytes = noItems.encodeBytes();

        fx_block.FxMessage withItems = new fx_block.FxMessage();
        withItems.header = 0x00;
        withItems.item1 = 0;
        byte[] withItemsBytes = withItems.encodeBytes();

        assertTrue(withItemsBytes.length > noItemsBytes.length,
            "FX message with items should be larger than without");
    }

    // ========================================================================
    // Frame basic: roundtrip depth tests (from C++ test_frame_roundtrip.cpp)
    // ========================================================================

    @Test
    @DisplayName("frame_basic: Heartbeat roundtrip via frame")
    void frameBasicHeartbeatRoundtrip() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 12345;

        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);
        byte[] encoded = frame.encodeBytes();

        // Verify wire layout: [msg_type:1][length:2][timestamp:2] = 5 bytes total
        assertEquals(5, encoded.length);
        assertEquals(1, encoded[0] & 0xFF);        // msg_type = 1
        assertEquals(0, encoded[1] & 0xFF);        // length high byte
        assertEquals(5, encoded[2] & 0xFF);        // length low byte
        assertEquals(0x30, encoded[3] & 0xFF);     // timestamp high (0x3039)
        assertEquals(0x39, encoded[4] & 0xFF);     // timestamp low

        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(encoded);
        assertEquals(1, decoded.msgType);
        assertEquals(5, decoded.length);
        assertInstanceOf(frame_basic.Heartbeat.class, decoded.payload);
        assertEquals(12345, ((frame_basic.Heartbeat) decoded.payload).timestamp);
    }

    @Test
    @DisplayName("frame_basic: Status roundtrip via frame")
    void frameBasicStatusRoundtrip() {
        frame_basic.Status st = new frame_basic.Status();
        st.code = 42;
        st.detail = 9999;

        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(st);
        byte[] encoded = frame.encodeBytes();

        // [msg_type:1][length:2][code:1][detail:2] = 6 bytes
        assertEquals(6, encoded.length);
        assertEquals(2, encoded[0] & 0xFF);  // msg_type = 2 (Status)

        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(encoded);
        assertEquals(2, decoded.msgType);
        assertEquals(6, decoded.length);
        assertInstanceOf(frame_basic.Status.class, decoded.payload);
        assertEquals(42, ((frame_basic.Status) decoded.payload).code);
        assertEquals(9999, ((frame_basic.Status) decoded.payload).detail);
    }

    @Test
    @DisplayName("frame_basic: decode invalid message id throws")
    void frameBasicDecodeInvalidId() {
        // Construct bytes with unknown msg_type=99
        byte[] data = new byte[] { 99, 0, 5, 0x30, 0x39 };
        assertThrows(Exception.class, () -> frame_basic.SimpleFrame.decodeBytes(data),
            "Decoding an unknown message type should throw");
    }

    @Test
    @DisplayName("frame_basic: message TYPE_ID constants are distinct")
    void frameBasicTypeIdConstants() {
        assertNotEquals(0, frame_basic.Heartbeat.TYPE_ID);
        assertNotEquals(0, frame_basic.Status.TYPE_ID);
        assertNotEquals(frame_basic.Heartbeat.TYPE_ID, frame_basic.Status.TYPE_ID,
            "Heartbeat and Status TYPE_IDs should be distinct");
    }

    @Test
    @DisplayName("frame_basic: message ID_VALUE constants")
    void frameBasicIdValueConstants() {
        assertEquals(1, frame_basic.Heartbeat.ID_VALUE);
        assertEquals(2, frame_basic.Status.ID_VALUE);
    }

    @Test
    @DisplayName("frame_basic: TYPE_NAME constants")
    void frameBasicTypeNameConstants() {
        assertEquals("Heartbeat", frame_basic.Heartbeat.TYPE_NAME);
        assertEquals("Status", frame_basic.Status.TYPE_NAME);
    }

    @Test
    @DisplayName("frame_basic: double-encode idempotency")
    void frameBasicDoubleEncode() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 42;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }
}
