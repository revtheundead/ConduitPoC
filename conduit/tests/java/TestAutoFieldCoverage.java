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
}
