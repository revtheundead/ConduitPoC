import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Frame encode/decode coverage tests.
 * Covers: frame_basic (SimpleFrame with header/payload),
 * frame_footer (FooterFrame with checksum footer),
 * frame_array (ArrayFrame with array payload),
 * frame_count (CountFrame with auto count(payload)).
 */
public class TestFrameCoverage {

    // ========================================================================
    // frame_basic: SimpleFrame with auto-id and auto-length
    // ========================================================================

    @Test
    @DisplayName("frame_basic: Heartbeat wrap/encode/decode roundtrip")
    void frameBasicHeartbeatRoundtrip() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 0x1234;

        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);
        byte[] encoded = frame.encodeBytes();

        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(encoded);
        assertEquals(1, decoded.msgType);
        assertInstanceOf(frame_basic.Heartbeat.class, decoded.payload);
        assertEquals(0x1234, ((frame_basic.Heartbeat) decoded.payload).timestamp);
    }

    @Test
    @DisplayName("frame_basic: Status wrap/encode/decode roundtrip")
    void frameBasicStatusRoundtrip() {
        frame_basic.Status st = new frame_basic.Status();
        st.code = 42;
        st.detail = 9999;

        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(st);
        byte[] encoded = frame.encodeBytes();

        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(encoded);
        assertEquals(2, decoded.msgType);
        assertInstanceOf(frame_basic.Status.class, decoded.payload);
        assertEquals(42, ((frame_basic.Status) decoded.payload).code);
        assertEquals(9999, ((frame_basic.Status) decoded.payload).detail);
    }

    @Test
    @DisplayName("frame_basic: auto-id is set correctly for each message type")
    void frameBasicAutoId() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 1;
        frame_basic.SimpleFrame f1 = frame_basic.SimpleFrame.wrap(hb);
        byte[] e1 = f1.encodeBytes();
        assertEquals(1, e1[0] & 0xFF);

        frame_basic.Status st = new frame_basic.Status();
        st.code = 1;
        st.detail = 1;
        frame_basic.SimpleFrame f2 = frame_basic.SimpleFrame.wrap(st);
        byte[] e2 = f2.encodeBytes();
        assertEquals(2, e2[0] & 0xFF);
    }

    @Test
    @DisplayName("frame_basic: auto-length is computed correctly")
    void frameBasicAutoLength() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 500;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);
        byte[] encoded = frame.encodeBytes();

        // Wire: [msg_type:1][length:2][timestamp:2] = 5 bytes
        assertEquals(5, encoded.length);

        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(encoded);
        assertEquals(5, decoded.length);
    }

    @Test
    @DisplayName("frame_basic: Status wire size includes all payload fields")
    void frameBasicStatusWireSize() {
        frame_basic.Status st = new frame_basic.Status();
        st.code = 1;
        st.detail = 2;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(st);
        byte[] encoded = frame.encodeBytes();

        // Wire: [msg_type:1][length:2][code:1][detail:2] = 6 bytes
        assertEquals(6, encoded.length);
    }

    @Test
    @DisplayName("frame_basic: ID_VALUE constants match schema")
    void frameBasicIdValueConstants() {
        assertEquals(1, frame_basic.Heartbeat.ID_VALUE);
        assertEquals(2, frame_basic.Status.ID_VALUE);
    }

    @Test
    @DisplayName("frame_basic: TYPE_ID constants are distinct")
    void frameBasicTypeIdDistinct() {
        assertNotEquals(frame_basic.Heartbeat.TYPE_ID, frame_basic.Status.TYPE_ID);
    }

    @Test
    @DisplayName("frame_basic: decode with invalid msg-type throws")
    void frameBasicInvalidMsgType() {
        byte[] data = new byte[]{(byte) 0xFF, 0x00, 0x03};
        assertThrows(Exception.class, () -> frame_basic.SimpleFrame.decodeBytes(data));
    }

    @Test
    @DisplayName("frame_basic: double-encode stability")
    void frameBasicDoubleEncode() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 0xBEEF;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }

    @Test
    @DisplayName("frame_basic: encode-decode-reencode produces identical bytes")
    void frameBasicReencode() {
        frame_basic.Status st = new frame_basic.Status();
        st.code = 0xFF;
        st.detail = 0xFFFF;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(st);

        byte[] first = frame.encodeBytes();
        frame_basic.SimpleFrame decoded = frame_basic.SimpleFrame.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // frame_footer: FooterFrame with checksum after payload
    // ========================================================================

    @Test
    @DisplayName("frame_footer: Data roundtrip with checksum footer")
    void frameFooterDataRoundtrip() {
        frame_footer.Data dataMsg = new frame_footer.Data();
        dataMsg.value = 0xABCD;

        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(dataMsg);
        frame.checksum = 0x42;

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2][value:2][checksum:1] = 6 bytes
        assertEquals(6, encoded.length);

        frame_footer.FooterFrame decoded = frame_footer.FooterFrame.decodeBytes(encoded);
        assertEquals(0x42, decoded.checksum);
        assertInstanceOf(frame_footer.Data.class, decoded.payload);
        assertEquals(0xABCD, ((frame_footer.Data) decoded.payload).value);
    }

    @Test
    @DisplayName("frame_footer: Ack roundtrip with footer")
    void frameFooterAckRoundtrip() {
        frame_footer.Ack ack = new frame_footer.Ack();
        ack.seq = 99;

        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(ack);
        frame.checksum = (byte) 0xFF;

        byte[] encoded = frame.encodeBytes();
        frame_footer.FooterFrame decoded = frame_footer.FooterFrame.decodeBytes(encoded);
        assertEquals(0xFF, decoded.checksum & 0xFF);
        assertInstanceOf(frame_footer.Ack.class, decoded.payload);
        assertEquals(99, ((frame_footer.Ack) decoded.payload).seq);
    }

    @Test
    @DisplayName("frame_footer: checksum zero value roundtrip")
    void frameFooterChecksumZero() {
        frame_footer.Data dataMsg = new frame_footer.Data();
        dataMsg.value = 100;

        frame_footer.FooterFrame frame = frame_footer.FooterFrame.wrap(dataMsg);
        frame.checksum = 0;

        byte[] encoded = frame.encodeBytes();
        frame_footer.FooterFrame decoded = frame_footer.FooterFrame.decodeBytes(encoded);
        assertEquals(0, decoded.checksum);
    }

    @Test
    @DisplayName("frame_footer: double-encode stability")
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
    // frame_array: ArrayFrame with count="*" array payload
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
        assertEquals(6, encoded.length);

        frame_array.ArrayFrame decoded = frame_array.ArrayFrame.decodeBytes(encoded);
        assertEquals(1, decoded.payload.size());
        assertEquals(1, ((frame_array.Record) decoded.payload.get(0)).key);
        assertEquals(1000, ((frame_array.Record) decoded.payload.get(0)).value);
    }

    @Test
    @DisplayName("frame_array: multiple records roundtrip")
    void frameArrayMultipleRecords() {
        frame_array.ArrayFrame frame = new frame_array.ArrayFrame();
        frame.msgType = 1;
        for (int i = 0; i < 4; i++) {
            frame_array.Record rec = new frame_array.Record();
            rec.key = (byte) (i + 10);
            rec.value = (i + 1) * 111;
            frame.payload.add(rec);
        }

        byte[] encoded = frame.encodeBytes();
        // Wire: [msg_type:1][length:2] + 4 * [key:1][value:2] = 3 + 12 = 15 bytes
        assertEquals(15, encoded.length);

        frame_array.ArrayFrame decoded = frame_array.ArrayFrame.decodeBytes(encoded);
        assertEquals(4, decoded.payload.size());
        for (int i = 0; i < 4; i++) {
            assertEquals(i + 10, ((frame_array.Record) decoded.payload.get(i)).key);
            assertEquals((i + 1) * 111, ((frame_array.Record) decoded.payload.get(i)).value);
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
    @DisplayName("frame_array: double-encode stability")
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
    // frame_count: CountFrame with auto count(payload), DataItem
    // ========================================================================

    @Test
    @DisplayName("frame_count: single DataItem roundtrip with auto-count")
    void frameCountSingleItem() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;
        frame_count.DataItem item = new frame_count.DataItem();
        item.value = 0x1234;
        frame.payload.add(item);

        byte[] encoded = frame.encodeBytes();

        frame_count.CountFrame decoded = frame_count.CountFrame.decodeBytes(encoded);
        assertEquals(1, decoded.count);
        assertEquals(1, decoded.payload.size());
        assertEquals(0x1234, ((frame_count.DataItem) decoded.payload.get(0)).value);
    }

    @Test
    @DisplayName("frame_count: multiple DataItems with auto-count")
    void frameCountMultipleItems() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;
        for (int i = 0; i < 5; i++) {
            frame_count.DataItem item = new frame_count.DataItem();
            item.value = (i + 1) * 100;
            frame.payload.add(item);
        }

        byte[] encoded = frame.encodeBytes();
        frame_count.CountFrame decoded = frame_count.CountFrame.decodeBytes(encoded);
        assertEquals(5, decoded.count);
        assertEquals(5, decoded.payload.size());
        for (int i = 0; i < 5; i++) {
            assertEquals((i + 1) * 100, ((frame_count.DataItem) decoded.payload.get(i)).value);
        }
    }

    @Test
    @DisplayName("frame_count: empty payload produces count=0")
    void frameCountEmptyPayload() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;

        byte[] encoded = frame.encodeBytes();
        frame_count.CountFrame decoded = frame_count.CountFrame.decodeBytes(encoded);
        assertEquals(0, decoded.count);
        assertEquals(0, decoded.payload.size());
    }

    @Test
    @DisplayName("frame_count: auto-count field reflects payload size after encode")
    void frameCountAutoCountField() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;
        for (int i = 0; i < 3; i++) {
            frame_count.DataItem item = new frame_count.DataItem();
            item.value = i;
            frame.payload.add(item);
        }

        byte[] encoded = frame.encodeBytes();
        frame_count.CountFrame decoded = frame_count.CountFrame.decodeBytes(encoded);
        assertEquals(3, decoded.count);
    }

    @Test
    @DisplayName("frame_count: double-encode stability")
    void frameCountDoubleEncode() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;
        frame_count.DataItem item = new frame_count.DataItem();
        item.value = 42;
        frame.payload.add(item);

        byte[] first = frame.encodeBytes();
        byte[] second = frame.encodeBytes();
        assertArrayEquals(first, second);
    }

    @Test
    @DisplayName("frame_count: encode-decode-reencode produces identical bytes")
    void frameCountReencode() {
        frame_count.CountFrame frame = new frame_count.CountFrame();
        frame.msgType = 1;
        for (int i = 0; i < 3; i++) {
            frame_count.DataItem item = new frame_count.DataItem();
            item.value = (i + 1) * 500;
            frame.payload.add(item);
        }

        byte[] first = frame.encodeBytes();
        frame_count.CountFrame decoded = frame_count.CountFrame.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }
}
