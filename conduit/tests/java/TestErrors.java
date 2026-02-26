import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for error handling: truncated data, invalid enum values,
 * empty arrays, underflow conditions, and exception types.
 */
public class TestErrors {

    // ========================================================================
    // Truncated data tests
    // ========================================================================

    @Test
    @DisplayName("PingBody: truncated data throws ConduitCodecException")
    void pingBodyTruncated() {
        // PingBody needs 4 bytes (u32), give only 2
        byte[] truncated = new byte[] { 0x00, 0x01 };
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.PingBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("DataBody: truncated data throws ConduitCodecException")
    void dataBodyTruncated() {
        // DataBody needs 9 bytes (u8 + u32 + u32), give only 3
        byte[] truncated = new byte[] { 0x01, 0x02, 0x03 };
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.DataBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("AckBody: truncated data throws ConduitCodecException")
    void ackBodyTruncated() {
        // AckBody needs 2 bytes (u16), give only 1
        byte[] truncated = new byte[] { 0x01 };
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.AckBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("Point: truncated data throws ConduitCodecException")
    void pointTruncated() {
        // Point needs 4 bytes (u16 + u16), give only 2
        byte[] truncated = new byte[] { 0x00, 0x01 };
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.Point.decodeBytes(truncated));
    }

    @Test
    @DisplayName("AlphaBody: truncated data throws ConduitCodecException")
    void alphaBodyTruncated() {
        // AlphaBody needs 4 bytes (u16 + u16), give only 1
        byte[] truncated = new byte[] { 0x01 };
        assertThrows(choice_test.ConduitCodecException.class,
            () -> choice_test.AlphaBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("BetaBody: truncated data throws ConduitCodecException")
    void betaBodyTruncated() {
        // BetaBody needs 5 bytes (u8 + u32), give only 3
        byte[] truncated = new byte[] { 0x01, 0x02, 0x03 };
        assertThrows(choice_test.ConduitCodecException.class,
            () -> choice_test.BetaBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("TypeBBody: truncated data throws ConduitCodecException")
    void typeBBodyTruncated() {
        // TypeBBody needs 4 bytes (u32), give only 2
        byte[] truncated = new byte[] { 0x00, 0x01 };
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.TypeBBody.decodeBytes(truncated));
    }

    @Test
    @DisplayName("SubX: truncated data throws ConduitCodecException")
    void subXTruncated() {
        // SubX needs 4 bytes (u32), give only 1
        byte[] truncated = new byte[] { 0x01 };
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.SubX.decodeBytes(truncated));
    }

    @Test
    @DisplayName("SubY: truncated data throws ConduitCodecException")
    void subYTruncated() {
        // SubY needs 4 bytes (u16 + u16), give only 3
        byte[] truncated = new byte[] { 0x00, 0x01, 0x02 };
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.SubY.decodeBytes(truncated));
    }

    @Test
    @DisplayName("FixedArrayMsg: truncated data (need 12 bytes, give 8)")
    void fixedArrayMsgTruncated() {
        // FixedArrayMsg needs 3*4=12 bytes, give 8
        byte[] truncated = new byte[8];
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.FixedArrayMsg.decodeBytes(truncated));
    }

    @Test
    @DisplayName("WireEncodingMsg: truncated data throws ConduitCodecException")
    void wireEncodingMsgTruncated() {
        byte[] truncated = new byte[] { 0x01, 0x02, 0x03 };
        assertThrows(wire_encodings.ConduitCodecException.class,
            () -> wire_encodings.WireEncodingMsg.decodeBytes(truncated));
    }

    // ========================================================================
    // Invalid enum value tests
    // ========================================================================

    @Test
    @DisplayName("ColorEnum: invalid value 0 throws ConduitCodecException")
    void colorEnumInvalidZero() {
        all_types.BitWriter w = new all_types.BitWriter();
        w.writeBits(0, 8);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        assertThrows(all_types.ConduitCodecException.class,
            () -> all_types.ColorEnum.decode(r));
    }

    @Test
    @DisplayName("ColorEnum: invalid value 4 throws ConduitCodecException")
    void colorEnumInvalidFour() {
        all_types.BitWriter w = new all_types.BitWriter();
        w.writeBits(4, 8);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        assertThrows(all_types.ConduitCodecException.class,
            () -> all_types.ColorEnum.decode(r));
    }

    @Test
    @DisplayName("ColorEnum: invalid value 255 throws ConduitCodecException")
    void colorEnumInvalid255() {
        all_types.BitWriter w = new all_types.BitWriter();
        w.writeBits(255, 8);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        assertThrows(all_types.ConduitCodecException.class,
            () -> all_types.ColorEnum.decode(r));
    }

    @Test
    @DisplayName("ColorEnum: valid values do not throw")
    void colorEnumValidValues() {
        for (int v : new int[] { 1, 2, 3 }) {
            all_types.BitWriter w = new all_types.BitWriter();
            w.writeBits(v, 8);
            byte[] data = w.toBytes();
            all_types.BitReader r = new all_types.BitReader(data);
            assertDoesNotThrow(() -> all_types.ColorEnum.decode(r));
        }
    }

    // ========================================================================
    // Decode from empty array tests
    // ========================================================================

    @Test
    @DisplayName("PingBody: decode empty array throws ConduitCodecException")
    void pingBodyEmptyArray() {
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.PingBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("DataBody: decode empty array throws ConduitCodecException")
    void dataBodyEmptyArray() {
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.DataBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("AckBody: decode empty array throws ConduitCodecException")
    void ackBodyEmptyArray() {
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.AckBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("AlphaBody: decode empty array throws ConduitCodecException")
    void alphaBodyEmptyArray() {
        assertThrows(choice_test.ConduitCodecException.class,
            () -> choice_test.AlphaBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("Point: decode empty array throws ConduitCodecException")
    void pointEmptyArray() {
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.Point.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("TypeBBody: decode empty array throws ConduitCodecException")
    void typeBBodyEmptyArray() {
        assertThrows(arrays_choices.ConduitCodecException.class,
            () -> arrays_choices.TypeBBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("WireEncodingMsg: decode empty array throws ConduitCodecException")
    void wireEncodingMsgEmptyArray() {
        assertThrows(wire_encodings.ConduitCodecException.class,
            () -> wire_encodings.WireEncodingMsg.decodeBytes(new byte[0]));
    }

    // ========================================================================
    // BitReader underflow tests
    // ========================================================================

    @Test
    @DisplayName("BitReader: readU8 on empty data throws")
    void bitReaderU8Empty() {
        session_test.BitReader r = new session_test.BitReader(new byte[0]);
        assertThrows(session_test.ConduitCodecException.class, () -> r.readU8());
    }

    @Test
    @DisplayName("BitReader: readU16 on 1-byte data throws")
    void bitReaderU16Short() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01 });
        assertThrows(session_test.ConduitCodecException.class, () -> r.readU16(true));
    }

    @Test
    @DisplayName("BitReader: readU32 on 2-byte data throws")
    void bitReaderU32Short() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01, 0x02 });
        assertThrows(session_test.ConduitCodecException.class, () -> r.readU32(true));
    }

    @Test
    @DisplayName("BitReader: readU64 on 4-byte data throws")
    void bitReaderU64Short() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01, 0x02, 0x03, 0x04 });
        assertThrows(session_test.ConduitCodecException.class, () -> r.readU64(true));
    }

    @Test
    @DisplayName("BitReader: readBits beyond available throws")
    void bitReaderReadBitsBeyond() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01 });
        assertThrows(session_test.ConduitCodecException.class, () -> r.readBits(16));
    }

    @Test
    @DisplayName("BitReader: skipBits beyond available throws")
    void bitReaderSkipBeyond() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01 });
        assertThrows(session_test.ConduitCodecException.class, () -> r.skipBits(16));
    }

    @Test
    @DisplayName("BitReader: sequential reads exhaust data correctly")
    void bitReaderSequentialExhaustion() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01, 0x02 });
        r.readU8(); // ok
        r.readU8(); // ok
        assertThrows(session_test.ConduitCodecException.class, () -> r.readU8()); // underflow
    }

    @Test
    @DisplayName("BitReader: remainingBytes reports correctly")
    void bitReaderRemainingBytes() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { 0x01, 0x02, 0x03 });
        assertEquals(3, r.remainingBytes());
        r.readU8();
        assertEquals(2, r.remainingBytes());
        r.readU8();
        assertEquals(1, r.remainingBytes());
        r.readU8();
        assertEquals(0, r.remainingBytes());
    }

    @Test
    @DisplayName("BitReader: remainingBits reports correctly")
    void bitReaderRemainingBits() {
        session_test.BitReader r = new session_test.BitReader(new byte[] { (byte)0xFF });
        assertEquals(8, r.remainingBits());
        r.readBits(3);
        assertEquals(5, r.remainingBits());
    }

    // ========================================================================
    // ConduitCodecException is a RuntimeException
    // ========================================================================

    @Test
    @DisplayName("ConduitCodecException: is a RuntimeException")
    void codecExceptionIsRuntime() {
        session_test.ConduitCodecException ex = new session_test.ConduitCodecException("test");
        assertInstanceOf(RuntimeException.class, ex);
        assertEquals("test", ex.getMessage());
    }

    @Test
    @DisplayName("ConduitCodecException: message preserved")
    void codecExceptionMessage() {
        try {
            session_test.PingBody.decodeBytes(new byte[0]);
            fail("Should have thrown ConduitCodecException");
        } catch (session_test.ConduitCodecException e) {
            assertTrue(e.getMessage().contains("underflow"));
        }
    }

    // ========================================================================
    // Packet frame decode errors
    // ========================================================================

    @Test
    @DisplayName("Packet: decode truncated frame throws")
    void packetDecodeTruncated() {
        // Need at least 7 bytes for header
        byte[] truncated = new byte[] { (byte)0xDE, (byte)0xAD, 0x00, 0x00, 0x01 };
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.Packet.decodeBytes(truncated));
    }

    @Test
    @DisplayName("Packet: decode empty data throws")
    void packetDecodeEmpty() {
        assertThrows(session_test.ConduitCodecException.class,
            () -> session_test.Packet.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("Frame: decode truncated frame throws")
    void frameDecodeTruncated() {
        // Need at least 5 bytes for header
        byte[] truncated = new byte[] { (byte)0xBE, (byte)0xEF, 0x01 };
        assertThrows(choice_test.ConduitCodecException.class,
            () -> choice_test.Frame.decodeBytes(truncated));
    }

    @Test
    @DisplayName("Frame: decode empty data throws")
    void frameDecodeEmpty() {
        assertThrows(choice_test.ConduitCodecException.class,
            () -> choice_test.Frame.decodeBytes(new byte[0]));
    }
}
