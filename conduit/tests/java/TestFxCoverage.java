import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import fx_block.FxMessage;
import fx_ia5_string.FxIa5Msg;
import fx_string.FxStringMsg;

public class TestFxCoverage {

    // ========== FxMessage Tests ==========

    @Test
    @DisplayName("FxMessage: basic roundtrip with all fields set")
    void testFxMessageBasicRoundtrip() {
        FxMessage msg = new FxMessage();
        msg.header = 0x01;
        msg.item1 = 100;
        msg.item2 = 200;

        byte[] data = msg.encodeBytes();
        FxMessage decoded = FxMessage.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals(msg.item1, decoded.item1);
        assertEquals(msg.item2, decoded.item2);
    }

    @Test
    @DisplayName("FxMessage: optional FX fields can be null")
    void testFxMessageOptionalFieldsNull() {
        FxMessage msg = new FxMessage();
        msg.header = 0x02;
        msg.item1 = null;
        msg.item2 = null;

        byte[] data = msg.encodeBytes();
        FxMessage decoded = FxMessage.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertNull(decoded.item1);
        assertNull(decoded.item2);
    }

    @Test
    @DisplayName("FxMessage: partial FX fields - first set, second null")
    void testFxMessagePartialFields() {
        FxMessage msg = new FxMessage();
        msg.header = 0x03;
        msg.item1 = 42;
        msg.item2 = null;

        byte[] data = msg.encodeBytes();
        FxMessage decoded = FxMessage.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals((Integer) 42, decoded.item1);
        assertEquals((Integer) 0, decoded.item2);
        assertEquals((Integer) 0, decoded.item3);
    }

    @Test
    @DisplayName("FxMessage: boundary value 0xFF in FX field")
    void testFxMessageBoundary0xFF() {
        FxMessage msg = new FxMessage();
        msg.header = 0x04;
        msg.item1 = 0xFF;
        msg.item2 = null;

        byte[] data = msg.encodeBytes();
        FxMessage decoded = FxMessage.decodeBytes(data);

        assertEquals((Integer) 0xFF, decoded.item1);
    }

    @Test
    @DisplayName("FxMessage: double-encode stability")
    void testFxMessageDoubleEncode() {
        FxMessage msg = new FxMessage();
        msg.header = 0x05;
        msg.item1 = 50;
        msg.item2 = 60;

        byte[] data1 = msg.encodeBytes();
        FxMessage decoded1 = FxMessage.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    // ========== FxIa5Msg Tests ==========

    @Test
    @DisplayName("FxIa5Msg: roundtrip with IA5 string and code")
    void testFxIa5MsgRoundtrip() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x20;
        msg.label = "ABCDEFGH";
        msg.code = 1234;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertTrue(decoded.label.trim().startsWith("ABCDEFGH"));
        assertEquals((Integer) 1234, decoded.code);
    }

    @Test
    @DisplayName("FxIa5Msg: label padding and trimming")
    void testFxIa5MsgStringPadding() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x21;
        msg.label = "AB";
        msg.code = 0;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals("AB", decoded.label.trim());
    }

    @Test
    @DisplayName("FxIa5Msg: code boundary 0xFFFF")
    void testFxIa5MsgBoundary0xFFFF() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x22;
        msg.label = "TESTTEST";
        msg.code = 0xFFFF;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals((Integer) 0xFFFF, decoded.code);
    }

    @Test
    @DisplayName("FxIa5Msg: double-encode stability")
    void testFxIa5MsgDoubleEncode() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x23;
        msg.label = "ROUNDTRP";
        msg.code = 500;

        byte[] data1 = msg.encodeBytes();
        FxIa5Msg decoded1 = FxIa5Msg.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    // ========== FxStringMsg Tests ==========

    @Test
    @DisplayName("FxStringMsg: roundtrip with label, payload, and extra")
    void testFxStringMsgRoundtrip() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x30;
        msg.label = "ABCDEFGHIJ";
        msg.payload = new byte[]{0x01, 0x02, 0x03, 0x04};
        msg.extra = 9999;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertTrue(decoded.label.trim().startsWith("ABCDEFGHIJ"));
        assertArrayEquals(new byte[]{0x01, 0x02, 0x03, 0x04}, decoded.payload);
        assertEquals((Integer) 9999, decoded.extra);
    }

    @Test
    @DisplayName("FxStringMsg: short label is padded correctly")
    void testFxStringMsgShortStringPadding() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x31;
        msg.label = "HI";
        msg.payload = new byte[]{0x00, 0x00, 0x00, 0x00};
        msg.extra = 0;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertEquals("HI", decoded.label.trim());
    }

    @Test
    @DisplayName("FxStringMsg: boundary values for payload and extra")
    void testFxStringMsgBoundaryValues() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x32;
        msg.label = "BOUNDARY01";
        msg.payload = new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF};
        msg.extra = 0xFFFF;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertArrayEquals(new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF}, decoded.payload);
        assertEquals((Integer) 0xFFFF, decoded.extra);
    }

    @Test
    @DisplayName("FxStringMsg: double-encode stability")
    void testFxStringMsgDoubleEncode() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x34;
        msg.label = "STABLE0001";
        msg.payload = new byte[]{0x11, 0x22, 0x33, 0x44};
        msg.extra = 777;

        byte[] data1 = msg.encodeBytes();
        FxStringMsg decoded1 = FxStringMsg.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }
}
