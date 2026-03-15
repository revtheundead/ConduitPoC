import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import fx_block.FxBlockMsg;

import fx_ia5_string.FxIa5Msg;
import fx_string.FxStringMsg;

public class TestFxCoverage {

    // ========== FxBlockMsg Tests ==========

    @Test
    @DisplayName("FxBlockMsg: basic roundtrip with all fields set")
    void testFxBlockMsgBasicRoundtrip() {
        FxBlockMsg msg = new FxBlockMsg();
        msg.header = 0x01;
        msg.fxField1 = (short) 100;
        msg.fxField2 = (short) 200;

        byte[] data = msg.encodeBytes();
        FxBlockMsg decoded = FxBlockMsg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals(msg.fxField1, decoded.fxField1);
        assertEquals(msg.fxField2, decoded.fxField2);
    }

    @Test
    @DisplayName("FxBlockMsg: optional FX fields can be null")
    void testFxBlockMsgOptionalFieldsNull() {
        FxBlockMsg msg = new FxBlockMsg();
        msg.header = 0x02;
        msg.fxField1 = null;
        msg.fxField2 = null;

        byte[] data = msg.encodeBytes();
        FxBlockMsg decoded = FxBlockMsg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertNull(decoded.fxField1);
        assertNull(decoded.fxField2);
    }

    @Test
    @DisplayName("FxBlockMsg: partial FX fields - first set, second null")
    void testFxBlockMsgPartialFields() {
        FxBlockMsg msg = new FxBlockMsg();
        msg.header = 0x03;
        msg.fxField1 = (short) 42;
        msg.fxField2 = null;

        byte[] data = msg.encodeBytes();
        FxBlockMsg decoded = FxBlockMsg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals((short) 42, decoded.fxField1);
        assertNull(decoded.fxField2);
    }

    @Test
    @DisplayName("FxBlockMsg: boundary value 0xFF in FX field")
    void testFxBlockMsgBoundary0xFF() {
        FxBlockMsg msg = new FxBlockMsg();
        msg.header = 0x04;
        msg.fxField1 = (short) 0xFF;
        msg.fxField2 = null;

        byte[] data = msg.encodeBytes();
        FxBlockMsg decoded = FxBlockMsg.decodeBytes(data);

        assertEquals((short) 0xFF, decoded.fxField1);
    }

    @Test
    @DisplayName("FxBlockMsg: double-encode stability")
    void testFxBlockMsgDoubleEncode() {
        FxBlockMsg msg = new FxBlockMsg();
        msg.header = 0x05;
        msg.fxField1 = (short) 50;
        msg.fxField2 = (short) 60;

        byte[] data1 = msg.encodeBytes();
        FxBlockMsg decoded1 = FxBlockMsg.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    // ========== FxIa5Msg Tests ==========

    @Test
    @DisplayName("FxIa5Msg: roundtrip with IA5 string and uint16")
    void testFxIa5MsgRoundtrip() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x20;
        msg.ia5Value = "ABCDEFGH";
        msg.numValue = 1234;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals("ABCDEFGH", decoded.ia5Value.trim());
        assertEquals(1234, decoded.numValue);
    }

    @Test
    @DisplayName("FxIa5Msg: IA5 string padding and trimming")
    void testFxIa5MsgStringPadding() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x21;
        msg.ia5Value = "AB";
        msg.numValue = 0;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals("AB", decoded.ia5Value.trim());
    }

    @Test
    @DisplayName("FxIa5Msg: uint16 boundary 0xFFFF")
    void testFxIa5MsgBoundary0xFFFF() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x22;
        msg.ia5Value = "TESTTEST";
        msg.numValue = 0xFFFF;

        byte[] data = msg.encodeBytes();
        FxIa5Msg decoded = FxIa5Msg.decodeBytes(data);

        assertEquals(0xFFFF, decoded.numValue);
    }

    @Test
    @DisplayName("FxIa5Msg: double-encode stability")
    void testFxIa5MsgDoubleEncode() {
        FxIa5Msg msg = new FxIa5Msg();
        msg.header = 0x23;
        msg.ia5Value = "ROUNDTRP";
        msg.numValue = 500;

        byte[] data1 = msg.encodeBytes();
        FxIa5Msg decoded1 = FxIa5Msg.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    // ========== FxStringMsg Tests ==========

    @Test
    @DisplayName("FxStringMsg: roundtrip with ASCII string, bytes, and uint16")
    void testFxStringMsgRoundtrip() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x30;
        msg.strValue = "ABCDEFGHIJ";
        msg.bytesValue = new byte[]{0x01, 0x02, 0x03, 0x04};
        msg.numValue = 9999;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertEquals(msg.header, decoded.header);
        assertEquals("ABCDEFGHIJ", decoded.strValue.trim());
        assertArrayEquals(new byte[]{0x01, 0x02, 0x03, 0x04}, decoded.bytesValue);
        assertEquals(9999, decoded.numValue);
    }

    @Test
    @DisplayName("FxStringMsg: short string is padded correctly")
    void testFxStringMsgShortStringPadding() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x31;
        msg.strValue = "HI";
        msg.bytesValue = new byte[]{0x00, 0x00, 0x00, 0x00};
        msg.numValue = 0;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertEquals("HI", decoded.strValue.trim());
    }

    @Test
    @DisplayName("FxStringMsg: boundary values for bytes and uint16")
    void testFxStringMsgBoundaryValues() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x32;
        msg.strValue = "BOUNDARY01";
        msg.bytesValue = new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF};
        msg.numValue = 0xFFFF;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertArrayEquals(new byte[]{(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF}, decoded.bytesValue);
        assertEquals(0xFFFF, decoded.numValue);
    }

    @Test
    @DisplayName("FxStringMsg: zero uint16 value")
    void testFxStringMsgZeroNum() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x33;
        msg.strValue = "ZEROTEST00";
        msg.bytesValue = new byte[]{0x0A, 0x0B, 0x0C, 0x0D};
        msg.numValue = 0;

        byte[] data = msg.encodeBytes();
        FxStringMsg decoded = FxStringMsg.decodeBytes(data);

        assertEquals(0, decoded.numValue);
    }

    @Test
    @DisplayName("FxStringMsg: double-encode stability")
    void testFxStringMsgDoubleEncode() {
        FxStringMsg msg = new FxStringMsg();
        msg.header = 0x34;
        msg.strValue = "STABLE0001";
        msg.bytesValue = new byte[]{0x11, 0x22, 0x33, 0x44};
        msg.numValue = 777;

        byte[] data1 = msg.encodeBytes();
        FxStringMsg decoded1 = FxStringMsg.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }
}
