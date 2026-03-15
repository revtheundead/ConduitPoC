import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for EBCDIC string encoding roundtrip in generated Java codecs.
 */
public class TestEbcdicCoverage {

    // ========================================================================
    // ebcdic_strings: EbcdicMsg with EBCDIC-encoded label field
    // ========================================================================

    @Test
    @DisplayName("EbcdicMsg: roundtrip with empty string (space-padded)")
    void ebcdicEmptyString() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0;
        msg.label = "";
        msg.trailer = 0;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals(0, d.id);
        assertTrue(d.label.trim().isEmpty());
        assertEquals(0, d.trailer);
    }

    @Test
    @DisplayName("EbcdicMsg: single character roundtrip")
    void ebcdicSingleChar() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 1;
        msg.label = "X";
        msg.trailer = 0x0001;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals("X", d.label.trim());
        assertEquals(0x0001, d.trailer);
    }

    @Test
    @DisplayName("EbcdicMsg: full-length 8-char string roundtrip")
    void ebcdicFullLength() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 2;
        msg.label = "ABCDEFGH";
        msg.trailer = 0xFFFF;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals("ABCDEFGH", d.label.trim());
        assertEquals(0xFFFF, d.trailer);
    }

    @Test
    @DisplayName("EbcdicMsg: short string is right-padded with spaces")
    void ebcdicShortString() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 3;
        msg.label = "HELLO";
        msg.trailer = 0x1234;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals("HELLO", d.label.trim());
        assertEquals(0x1234, d.trailer);
    }

    @Test
    @DisplayName("EbcdicMsg: numeric string content")
    void ebcdicNumericString() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 4;
        msg.label = "12345678";
        msg.trailer = 0;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals("12345678", d.label.trim());
    }

    @Test
    @DisplayName("EbcdicMsg: mixed alphanumeric content")
    void ebcdicMixedContent() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 5;
        msg.label = "AB12CD34";
        msg.trailer = 0x5678;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals("AB12CD34", d.label.trim());
    }

    @Test
    @DisplayName("EbcdicMsg: wire format is 11 bytes (1 + 8 + 2)")
    void ebcdicWireSize() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0;
        msg.label = "";
        msg.trailer = 0;
        byte[] encoded = msg.encodeBytes();
        assertEquals(11, encoded.length);
    }

    @Test
    @DisplayName("EbcdicMsg: wire size unchanged regardless of string length")
    void ebcdicWireSizeConsistent() {
        ebcdic_strings.EbcdicMsg msg1 = new ebcdic_strings.EbcdicMsg();
        msg1.id = 0;
        msg1.label = "A";
        msg1.trailer = 0;

        ebcdic_strings.EbcdicMsg msg2 = new ebcdic_strings.EbcdicMsg();
        msg2.id = 0;
        msg2.label = "ABCDEFGH";
        msg2.trailer = 0;

        assertEquals(msg1.encodeBytes().length, msg2.encodeBytes().length);
    }

    @Test
    @DisplayName("EbcdicMsg: double-encode stability")
    void ebcdicDoubleEncode() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0xFF;
        msg.label = "TEST";
        msg.trailer = 0xABCD;
        byte[] data1 = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("EbcdicMsg: double-encode stability with full-length string")
    void ebcdicDoubleEncodeFullLength() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0x42;
        msg.label = "ROUNDTRP";
        msg.trailer = 0x9999;
        byte[] data1 = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("EbcdicMsg: max id boundary 0xFF")
    void ebcdicMaxId() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0xFF;
        msg.label = "DATA";
        msg.trailer = 0;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals(0xFF, d.id);
    }

    @Test
    @DisplayName("EbcdicMsg: zero id and zero trailer")
    void ebcdicZeroFields() {
        ebcdic_strings.EbcdicMsg msg = new ebcdic_strings.EbcdicMsg();
        msg.id = 0;
        msg.label = "ZERO";
        msg.trailer = 0;
        byte[] encoded = msg.encodeBytes();
        ebcdic_strings.EbcdicMsg d = ebcdic_strings.EbcdicMsg.decodeBytes(encoded);
        assertEquals(0, d.id);
        assertEquals("ZERO", d.label.trim());
        assertEquals(0, d.trailer);
    }
}
