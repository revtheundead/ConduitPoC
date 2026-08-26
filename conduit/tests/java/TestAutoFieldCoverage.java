import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for auto-length, auto-count, and auto-sequence field features.
 */
public class TestAutoFieldCoverage {

    // ========================================================================
    // auto_struct_length: TlvMsg with auto-length field
    // ========================================================================

    @Test
    @DisplayName("TlvMsg: basic roundtrip with data bytes")
    void tlvMsgBasicRoundtrip() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x42;
        msg.data = new byte[]{(byte) 0xAA, (byte) 0xBB, (byte) 0xCC};
        msg.suffix = 0xFF;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(0x42, d.tag);
        assertArrayEquals(new byte[]{(byte) 0xAA, (byte) 0xBB, (byte) 0xCC}, d.data);
        assertEquals(0xFF, d.suffix);
    }

    @Test
    @DisplayName("TlvMsg: auto-length is computed correctly from data size")
    void tlvMsgAutoLengthComputed() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x01;
        msg.data = new byte[]{0x10, 0x20, 0x30, 0x40, 0x50};
        msg.suffix = 0x00;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(5, d.len);
    }

    @Test
    @DisplayName("TlvMsg: empty data produces length 0")
    void tlvMsgEmptyData() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x01;
        msg.data = new byte[0];
        msg.suffix = 0x00;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(0, d.len);
        assertEquals(0, d.data.length);
    }

    @Test
    @DisplayName("TlvMsg: single-byte data produces length 1")
    void tlvMsgSingleByteData() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x02;
        msg.data = new byte[]{(byte) 0xDE};
        msg.suffix = 0x03;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(1, d.len);
        assertArrayEquals(new byte[]{(byte) 0xDE}, d.data);
    }

    @Test
    @DisplayName("TlvMsg: wire size = tag(1) + len(1) + data(N) + suffix(1)")
    void tlvMsgWireSize() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x01;
        msg.data = new byte[]{0x0A, 0x0B, 0x0C};
        msg.suffix = 0x99;
        byte[] encoded = msg.encodeBytes();
        // 1 (tag) + 1 (len) + 3 (data) + 1 (suffix) = 6
        assertEquals(6, encoded.length);
    }

    @Test
    @DisplayName("TlvMsg: wire size with empty data = 3 bytes")
    void tlvMsgWireSizeEmpty() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x00;
        msg.data = new byte[0];
        msg.suffix = 0x00;
        byte[] encoded = msg.encodeBytes();
        // 1 (tag) + 1 (len) + 0 (data) + 1 (suffix) = 3
        assertEquals(3, encoded.length);
    }

    @Test
    @DisplayName("TlvMsg: length byte in wire format matches data size")
    void tlvMsgLengthByteInWire() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0xAA;
        msg.data = new byte[]{(byte) 0xDE, (byte) 0xAD, (byte) 0xBE, (byte) 0xEF};
        msg.suffix = (byte) 0xBB;
        byte[] encoded = msg.encodeBytes();
        // encoded[1] is the auto-length byte
        assertEquals(4, encoded[1]);
    }

    @Test
    @DisplayName("TlvMsg: double-encode stability")
    void tlvMsgDoubleEncode() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x55;
        msg.data = new byte[]{0x11, 0x22, 0x33};
        msg.suffix = 0x77;
        byte[] data1 = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("TlvMsg: double-encode stability with empty data")
    void tlvMsgDoubleEncodeEmpty() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x00;
        msg.data = new byte[0];
        msg.suffix = 0x00;
        byte[] data1 = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("TlvMsg: boundary tag value 0xFF")
    void tlvMsgBoundaryTag() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0xFF;
        msg.data = new byte[]{0x01};
        msg.suffix = 0xFF;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(0xFF, d.tag);
        assertEquals(0xFF, d.suffix);
    }

    @Test
    @DisplayName("TlvMsg: tag=0 roundtrip")
    void tlvMsgZeroTag() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x00;
        msg.data = new byte[]{0x01, 0x02};
        msg.suffix = 0x00;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertEquals(0x00, d.tag);
        assertEquals(0x00, d.suffix);
    }

    @Test
    @DisplayName("TlvMsg: all-0xFF data bytes roundtrip")
    void tlvMsgAllFFData() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x10;
        msg.data = new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF};
        msg.suffix = 0x20;
        byte[] encoded = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(encoded);
        assertArrayEquals(new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF}, d.data);
    }

    // ========================================================================
    // Message-log formatting: auto-managed ("patched") fields must render the
    // value written to the wire, not the zero-initialized in-memory member.
    // ========================================================================

    @Test
    @DisplayName("toString: body auto-length shows target field length, not 0")
    void toStringBodyAutoLength() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x42;
        msg.data = new byte[]{1, 2, 3, 4, 5};
        msg.suffix = 0xFF;
        assertEquals(0, msg.len); // not populated in memory
        assertTrue(msg.toString().contains("len=5"), msg.toString());
    }

    @Test
    @DisplayName("toString: body auto-count shows array size, not 0")
    void toStringBodyAutoCount() {
        auto_count.Container c = new auto_count.Container();
        c.tag = 0xAA;
        auto_count.Record r1 = new auto_count.Record(); r1.value = 1111;
        auto_count.Record r2 = new auto_count.Record(); r2.value = 2222;
        c.items.add(r1);
        c.items.add(r2);
        assertEquals(0, c.count);
        assertTrue(c.toString().contains("count=2"), c.toString());
    }

    @Test
    @DisplayName("toString: self auto-length is derived by re-encoding")
    void toStringSelfAutoLength() {
        outer_scope.Packet p = new outer_scope.Packet();
        p.tag = 1;
        outer_scope.DataA a = new outer_scope.DataA(); a.x = 10; a.y = 20;
        p.payload = a;
        assertEquals(0, p.len);
        assertTrue(p.toString().contains("len=4"), p.toString());
    }

    @Test
    @DisplayName("session: encodeWrap records count and formatOutbound surfaces it")
    void sessionFrameCountWrap() {
        frame_count.CountFrameSession session = new frame_count.CountFrameSession();
        frame_count.DataItem d = new frame_count.DataItem();
        d.value = 0x1234;
        java.util.Map<String, Object> r = session.encodeWrap(frame_count.DataItem.TYPE_ID, d);
        assertNotNull(r);
        @SuppressWarnings("unchecked")
        java.util.List<String[]> af = (java.util.List<String[]>) r.get("auto_fields");
        assertEquals("1", lookup(af, "count"));

        String formatted = session.formatOutbound(frame_count.DataItem.TYPE_ID, d, af);
        assertTrue(formatted.contains("count=1"), formatted);
        assertFalse(formatted.contains("count=0"), formatted);
        assertTrue(formatted.contains("msgType=1"), formatted);
    }

    @Test
    @DisplayName("session: encodeBatch records payload count")
    void sessionFrameCountBatch() {
        frame_count.CountFrameSession session = new frame_count.CountFrameSession();
        java.util.List<frame_count.DataItem> items = new java.util.ArrayList<>();
        for (int i = 0; i < 3; i++) {
            frame_count.DataItem d = new frame_count.DataItem();
            d.value = i;
            items.add(d);
        }
        java.util.Map<String, Object> r = session.encodeBatch(frame_count.DataItem.TYPE_ID, items);
        assertNotNull(r);
        @SuppressWarnings("unchecked")
        java.util.List<String[]> af = (java.util.List<String[]>) r.get("auto_fields");
        assertEquals("3", lookup(af, "count"));

        String formatted = session.formatOutbound(frame_count.DataItem.TYPE_ID, items.get(0), af);
        assertTrue(formatted.contains("count=3"), formatted);
    }

    private static String lookup(java.util.List<String[]> autoFields, String key) {
        for (String[] kv : autoFields) {
            if (kv[0].equals(key)) return kv[1];
        }
        return null;
    }
}
