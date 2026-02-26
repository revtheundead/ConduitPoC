import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for verifying specific byte patterns from encoding,
 * endianness, frame wire format, and bool encoding.
 */
public class TestWireFormat {

    // ========================================================================
    // PingBody wire format
    // ========================================================================

    @Test
    @DisplayName("PingBody: wire format for timestamp=0x12345678 is big-endian")
    void pingBodyWireFormat() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 0x12345678;
        byte[] encoded = msg.encodeBytes();
        assertEquals(4, encoded.length);
        assertEquals((byte)0x12, encoded[0]);
        assertEquals((byte)0x34, encoded[1]);
        assertEquals((byte)0x56, encoded[2]);
        assertEquals((byte)0x78, encoded[3]);
    }

    @Test
    @DisplayName("PingBody: wire format for timestamp=0 is all zeros")
    void pingBodyWireFormatZero() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 0;
        byte[] encoded = msg.encodeBytes();
        assertArrayEquals(new byte[] { 0, 0, 0, 0 }, encoded);
    }

    @Test
    @DisplayName("PingBody: wire format for timestamp=1")
    void pingBodyWireFormatOne() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 1;
        byte[] encoded = msg.encodeBytes();
        assertArrayEquals(new byte[] { 0, 0, 0, 1 }, encoded);
    }

    // ========================================================================
    // DataBody wire format
    // ========================================================================

    @Test
    @DisplayName("DataBody: wire format channel=0xFF payloadA=1 payloadB=2")
    void dataBodyWireFormat() {
        session_test.DataBody msg = new session_test.DataBody();
        msg.channel = 0xFF;
        msg.payloadA = 1;
        msg.payloadB = 2;
        byte[] encoded = msg.encodeBytes();
        assertEquals(9, encoded.length);
        // channel u8
        assertEquals((byte)0xFF, encoded[0]);
        // payloadA big-endian u32 = 1
        assertEquals((byte)0x00, encoded[1]);
        assertEquals((byte)0x00, encoded[2]);
        assertEquals((byte)0x00, encoded[3]);
        assertEquals((byte)0x01, encoded[4]);
        // payloadB big-endian u32 = 2
        assertEquals((byte)0x00, encoded[5]);
        assertEquals((byte)0x00, encoded[6]);
        assertEquals((byte)0x00, encoded[7]);
        assertEquals((byte)0x02, encoded[8]);
    }

    // ========================================================================
    // AckBody wire format
    // ========================================================================

    @Test
    @DisplayName("AckBody: wire format for ackedSeq=0x1234 is big-endian")
    void ackBodyWireFormat() {
        session_test.AckBody msg = new session_test.AckBody();
        msg.ackedSeq = 0x1234;
        byte[] encoded = msg.encodeBytes();
        assertEquals(2, encoded.length);
        assertEquals((byte)0x12, encoded[0]);
        assertEquals((byte)0x34, encoded[1]);
    }

    // ========================================================================
    // Endianness verification
    // ========================================================================

    @Test
    @DisplayName("Big-endian U16: 0xABCD encoded as AB CD")
    void bigEndianU16() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU16(0xABCD, true);  // big-endian
        byte[] data = w.toBytes();
        assertEquals(2, data.length);
        assertEquals((byte)0xAB, data[0]);
        assertEquals((byte)0xCD, data[1]);
    }

    @Test
    @DisplayName("Little-endian U16: 0xABCD encoded as CD AB")
    void littleEndianU16() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU16(0xABCD, false);  // little-endian
        byte[] data = w.toBytes();
        assertEquals(2, data.length);
        assertEquals((byte)0xCD, data[0]);
        assertEquals((byte)0xAB, data[1]);
    }

    @Test
    @DisplayName("Big-endian U32: 0x12345678 encoded as 12 34 56 78")
    void bigEndianU32() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU32(0x12345678, true);
        byte[] data = w.toBytes();
        assertEquals(4, data.length);
        assertEquals((byte)0x12, data[0]);
        assertEquals((byte)0x34, data[1]);
        assertEquals((byte)0x56, data[2]);
        assertEquals((byte)0x78, data[3]);
    }

    @Test
    @DisplayName("Little-endian U32: 0x12345678 encoded as 78 56 34 12")
    void littleEndianU32() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU32(0x12345678, false);
        byte[] data = w.toBytes();
        assertEquals(4, data.length);
        assertEquals((byte)0x78, data[0]);
        assertEquals((byte)0x56, data[1]);
        assertEquals((byte)0x34, data[2]);
        assertEquals((byte)0x12, data[3]);
    }

    @Test
    @DisplayName("Big-endian U16 readback: AB CD -> 0xABCD")
    void bigEndianU16Read() {
        byte[] data = new byte[] { (byte)0xAB, (byte)0xCD };
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0xABCD, r.readU16(true));
    }

    @Test
    @DisplayName("Little-endian U16 readback: CD AB -> 0xABCD")
    void littleEndianU16Read() {
        byte[] data = new byte[] { (byte)0xCD, (byte)0xAB };
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0xABCD, r.readU16(false));
    }

    @Test
    @DisplayName("Big-endian U32 readback: 12 34 56 78 -> 0x12345678")
    void bigEndianU32Read() {
        byte[] data = new byte[] { 0x12, 0x34, 0x56, 0x78 };
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0x12345678, r.readU32(true));
    }

    @Test
    @DisplayName("Little-endian U32 readback: 78 56 34 12 -> 0x12345678")
    void littleEndianU32Read() {
        byte[] data = new byte[] { 0x78, 0x56, 0x34, 0x12 };
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0x12345678, r.readU32(false));
    }

    @Test
    @DisplayName("Little-endian field in AllTypesMessage: le16 wire format")
    void allTypesLe16WireFormat() {
        // Build a minimal AllTypesMessage and check the le16 wire position
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.le16 = 0xABCD;
        byte[] encoded = msg.encodeBytes();
        // le16 is encoded at offset: u8(1) + u16(2) + u32(4) + u64(8) + i8(1) + i16(2) + i32(4)
        //   + f32(4) + f64(8) + bool(1) + ascii(10) + utf8(16) + raw(8) = 69 bytes offset
        int offset = 1 + 2 + 4 + 8 + 1 + 2 + 4 + 4 + 8 + 1 + 10 + 16 + 8;
        // little-endian: 0xABCD -> CD AB
        assertEquals((byte)0xCD, encoded[offset]);
        assertEquals((byte)0xAB, encoded[offset + 1]);
    }

    @Test
    @DisplayName("Big-endian U64 write/read roundtrip")
    void bigEndianU64Roundtrip() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU64(0x123456789ABCDEF0L, true);
        byte[] data = w.toBytes();
        assertEquals(8, data.length);
        assertEquals((byte)0x12, data[0]);
        assertEquals((byte)0x34, data[1]);
        assertEquals((byte)0x56, data[2]);
        assertEquals((byte)0x78, data[3]);
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0x123456789ABCDEF0L, r.readU64(true));
    }

    @Test
    @DisplayName("Little-endian U64 write/read roundtrip")
    void littleEndianU64Roundtrip() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeU64(0x123456789ABCDEF0L, false);
        byte[] data = w.toBytes();
        assertEquals(8, data.length);
        // Little-endian: LSB first
        assertEquals((byte)0xF0, data[0]);
        assertEquals((byte)0xDE, data[1]);
        assertEquals((byte)0xBC, data[2]);
        assertEquals((byte)0x9A, data[3]);
        assertEquals((byte)0x78, data[4]);
        assertEquals((byte)0x56, data[5]);
        assertEquals((byte)0x34, data[6]);
        assertEquals((byte)0x12, data[7]);
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(0x123456789ABCDEF0L, r.readU64(false));
    }

    // ========================================================================
    // Packet frame wire format
    // ========================================================================

    @Test
    @DisplayName("Packet frame: SYNC bytes are DE AD at start")
    void packetFrameSyncBytes() {
        session_test.PingBody ping = new session_test.PingBody();
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        assertEquals((byte)0xDE, encoded[0]);
        assertEquals((byte)0xAD, encoded[1]);
    }

    @Test
    @DisplayName("Packet frame: seq field at offset 2-3")
    void packetFrameSeqField() {
        session_test.PingBody ping = new session_test.PingBody();
        session_test.Packet frame = session_test.Packet.wrap(ping);
        frame.seq = 0x0042;
        byte[] encoded = frame.encodeBytes();
        // seq at offset 2-3 (big-endian u16)
        assertEquals((byte)0x00, encoded[2]);
        assertEquals((byte)0x42, encoded[3]);
    }

    @Test
    @DisplayName("Packet frame: msgId field at offset 4")
    void packetFrameMsgIdField() {
        session_test.PingBody ping = new session_test.PingBody();
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        // msgId at offset 4 (u8), PingBody ID_VALUE=1
        assertEquals((byte)0x01, encoded[4]);
    }

    @Test
    @DisplayName("Packet frame: length field at offset 5-6")
    void packetFrameLengthField() {
        session_test.PingBody ping = new session_test.PingBody();
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        // length at offset 5-6 (big-endian u16), total frame = 11
        assertEquals((byte)0x00, encoded[5]);
        assertEquals((byte)0x0B, encoded[6]);
    }

    @Test
    @DisplayName("Packet frame: payload starts at offset 7")
    void packetFramePayloadOffset() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0xAABBCCDD;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        byte[] encoded = frame.encodeBytes();
        // payload starts at offset 7
        assertEquals((byte)0xAA, encoded[7]);
        assertEquals((byte)0xBB, encoded[8]);
        assertEquals((byte)0xCC, encoded[9]);
        assertEquals((byte)0xDD, encoded[10]);
    }

    @Test
    @DisplayName("Packet frame: complete wire format for PingBody")
    void packetFrameCompletePing() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0x01020304;
        session_test.Packet frame = session_test.Packet.wrap(ping);
        frame.seq = 7;
        byte[] encoded = frame.encodeBytes();
        byte[] expected = new byte[] {
            (byte)0xDE, (byte)0xAD,  // sync
            0x00, 0x07,               // seq = 7
            0x01,                     // msgId = 1 (PingBody)
            0x00, 0x0B,               // length = 11
            0x01, 0x02, 0x03, 0x04    // timestamp
        };
        assertArrayEquals(expected, encoded);
    }

    @Test
    @DisplayName("Packet frame: complete wire format for DataBody")
    void packetFrameCompleteData() {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 0xFF;
        data.payloadA = 0x0A0B0C0D;
        data.payloadB = 0x01020304;
        session_test.Packet frame = session_test.Packet.wrap(data);
        frame.seq = 1;
        byte[] encoded = frame.encodeBytes();
        // Total: 7 header + 9 payload = 16 bytes
        assertEquals(16, encoded.length);
        byte[] expected = new byte[] {
            (byte)0xDE, (byte)0xAD,   // sync
            0x00, 0x01,                // seq = 1
            0x02,                      // msgId = 2 (DataBody)
            0x00, 0x10,                // length = 16
            (byte)0xFF,                // channel
            0x0A, 0x0B, 0x0C, 0x0D,   // payloadA
            0x01, 0x02, 0x03, 0x04     // payloadB
        };
        assertArrayEquals(expected, encoded);
    }

    @Test
    @DisplayName("Packet frame: complete wire format for AckBody")
    void packetFrameCompleteAck() {
        session_test.AckBody ack = new session_test.AckBody();
        ack.ackedSeq = 0x1234;
        session_test.Packet frame = session_test.Packet.wrap(ack);
        frame.seq = 0;
        byte[] encoded = frame.encodeBytes();
        // Total: 7 header + 2 payload = 9 bytes
        assertEquals(9, encoded.length);
        byte[] expected = new byte[] {
            (byte)0xDE, (byte)0xAD,   // sync
            0x00, 0x00,                // seq = 0
            0x03,                      // msgId = 3 (AckBody)
            0x00, 0x09,                // length = 9
            0x12, 0x34                 // ackedSeq
        };
        assertArrayEquals(expected, encoded);
    }

    // ========================================================================
    // Frame wire format (choice_protocol)
    // ========================================================================

    @Test
    @DisplayName("Frame: SYNC bytes are BE EF at start")
    void frameSyncBytes() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        assertEquals((byte)0xBE, encoded[0]);
        assertEquals((byte)0xEF, encoded[1]);
    }

    @Test
    @DisplayName("Frame: messageType at offset 2")
    void frameMessageTypeOffset() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        assertEquals((byte)0x01, encoded[2]);  // AlphaBody ID_VALUE = 1
    }

    @Test
    @DisplayName("Frame: length at offset 3-4")
    void frameLengthOffset() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        // Header: sync(2) + messageType(1) + length(2) = 5, payload: x(2) + y(2) = 4, total = 9
        assertEquals((byte)0x00, encoded[3]);
        assertEquals((byte)0x09, encoded[4]);
    }

    @Test
    @DisplayName("Frame: complete wire format for AlphaBody")
    void frameCompleteAlpha() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        alpha.x = 0x0100;
        alpha.y = 0x0200;
        choice_test.Frame frame = choice_test.Frame.wrap(alpha);
        byte[] encoded = frame.encodeBytes();
        byte[] expected = new byte[] {
            (byte)0xBE, (byte)0xEF,   // sync
            0x01,                      // messageType = 1 (AlphaBody)
            0x00, 0x09,                // length = 9
            0x01, 0x00,                // x = 0x0100
            0x02, 0x00                 // y = 0x0200
        };
        assertArrayEquals(expected, encoded);
    }

    @Test
    @DisplayName("Frame: complete wire format for BetaBody")
    void frameCompleteBeta() {
        choice_test.BetaBody beta = new choice_test.BetaBody();
        beta.payloadSize = 0x0A;
        beta.tag = 0x01020304;
        choice_test.Frame frame = choice_test.Frame.wrap(beta);
        byte[] encoded = frame.encodeBytes();
        byte[] expected = new byte[] {
            (byte)0xBE, (byte)0xEF,  // sync
            0x02,                     // messageType = 2 (BetaBody)
            0x00, 0x0A,              // length = 10
            0x0A,                     // payloadSize
            0x01, 0x02, 0x03, 0x04   // tag
        };
        assertArrayEquals(expected, encoded);
    }

    // ========================================================================
    // Bool encoding verification
    // ========================================================================

    @Test
    @DisplayName("Bool true is encoded as byte 0x01")
    void boolTrueEncoding() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.flag = true;
        byte[] encoded = msg.encodeBytes();
        // flag is at offset: u8(1) + u16(2) + u32(4) + u64(8) + i8(1) + i16(2) + i32(4)
        //   + f32(4) + f64(8) = 34
        int flagOffset = 1 + 2 + 4 + 8 + 1 + 2 + 4 + 4 + 8;
        assertEquals((byte)0x01, encoded[flagOffset]);
    }

    @Test
    @DisplayName("Bool false is encoded as byte 0x00")
    void boolFalseEncoding() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.flag = false;
        byte[] encoded = msg.encodeBytes();
        int flagOffset = 1 + 2 + 4 + 8 + 1 + 2 + 4 + 4 + 8;
        assertEquals((byte)0x00, encoded[flagOffset]);
    }

    // ========================================================================
    // ColorEnum encoding verification
    // ========================================================================

    @Test
    @DisplayName("ColorEnum RED encodes as byte 0x01")
    void colorEnumRedEncoding() {
        all_types.BitWriter w = new all_types.BitWriter();
        all_types.ColorEnum.RED.encode(w);
        byte[] data = w.toBytes();
        assertEquals(1, data.length);
        assertEquals((byte)0x01, data[0]);
    }

    @Test
    @DisplayName("ColorEnum GREEN encodes as byte 0x02")
    void colorEnumGreenEncoding() {
        all_types.BitWriter w = new all_types.BitWriter();
        all_types.ColorEnum.GREEN.encode(w);
        byte[] data = w.toBytes();
        assertEquals((byte)0x02, data[0]);
    }

    @Test
    @DisplayName("ColorEnum BLUE encodes as byte 0x03")
    void colorEnumBlueEncoding() {
        all_types.BitWriter w = new all_types.BitWriter();
        all_types.ColorEnum.BLUE.encode(w);
        byte[] data = w.toBytes();
        assertEquals((byte)0x03, data[0]);
    }

    // ========================================================================
    // StatusFlags encoding verification
    // ========================================================================

    @Test
    @DisplayName("StatusFlags: active only -> raw=1 -> byte 0x01")
    void statusFlagsActiveOnlyEncoding() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setActive(true);
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        assertEquals((byte)0x01, w.toBytes()[0]);
    }

    @Test
    @DisplayName("StatusFlags: error only -> raw=2 -> byte 0x02")
    void statusFlagsErrorOnlyEncoding() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setError(true);
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        assertEquals((byte)0x02, w.toBytes()[0]);
    }

    @Test
    @DisplayName("StatusFlags: ready only -> raw=4 -> byte 0x04")
    void statusFlagsReadyOnlyEncoding() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setReady(true);
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        assertEquals((byte)0x04, w.toBytes()[0]);
    }

    @Test
    @DisplayName("StatusFlags: all set -> raw=7 -> byte 0x07")
    void statusFlagsAllSetEncoding() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setActive(true);
        flags.setError(true);
        flags.setReady(true);
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        assertEquals((byte)0x07, w.toBytes()[0]);
    }

    // ========================================================================
    // ScaledTemp encoding verification
    // ========================================================================

    @Test
    @DisplayName("ScaledTemp: raw=0 encodes as 2 zero bytes")
    void scaledTempZeroEncoding() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp(0);
        all_types.BitWriter w = new all_types.BitWriter();
        temp.encode(w);
        byte[] data = w.toBytes();
        assertEquals(2, data.length);
        assertEquals((byte)0x00, data[0]);
        assertEquals((byte)0x00, data[1]);
    }

    @Test
    @DisplayName("ScaledTemp: raw=0x1234 encodes correctly")
    void scaledTempNonZeroEncoding() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp(0x1234);
        all_types.BitWriter w = new all_types.BitWriter();
        temp.encode(w);
        byte[] data = w.toBytes();
        assertEquals(2, data.length);
        // 16-bit value 0x1234 written as bits (big-endian bit order)
        assertEquals((byte)0x12, data[0]);
        assertEquals((byte)0x34, data[1]);
    }

    // ========================================================================
    // BitWriter / BitReader bit-level operations
    // ========================================================================

    @Test
    @DisplayName("WriteBits/ReadBits: 3-bit value roundtrip")
    void bitLevelThreeBit() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeBits(5, 3);  // 0b101
        w.writeBits(3, 3);  // 0b011
        w.writeBits(0, 2);  // 0b00
        byte[] data = w.toBytes();
        assertEquals(1, data.length);
        // 101 011 00 = 0xAC
        assertEquals((byte)0xAC, data[0]);
    }

    @Test
    @DisplayName("WriteBits/ReadBits: multi-byte bit packing")
    void bitLevelMultiByte() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeBits(0xFF, 8);
        w.writeBits(0x0, 4);
        w.writeBits(0xF, 4);
        byte[] data = w.toBytes();
        assertEquals(2, data.length);
        assertEquals((byte)0xFF, data[0]);
        assertEquals((byte)0x0F, data[1]);
    }

    @Test
    @DisplayName("SignedBits: write -1 in 8 bits, read back as -1")
    void signedBitsNegOne() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeSignedBits(-1, 8);
        byte[] data = w.toBytes();
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(-1, r.readSignedBits(8));
    }

    @Test
    @DisplayName("SignedBits: write -128 in 8 bits, read back as -128")
    void signedBitsMin8() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeSignedBits(-128, 8);
        byte[] data = w.toBytes();
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(-128, r.readSignedBits(8));
    }

    @Test
    @DisplayName("SignedBits: write 127 in 8 bits, read back as 127")
    void signedBitsMax8() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeSignedBits(127, 8);
        byte[] data = w.toBytes();
        session_test.BitReader r = new session_test.BitReader(data);
        assertEquals(127, r.readSignedBits(8));
    }

    // ========================================================================
    // F32/F64 wire format verification
    // ========================================================================

    @Test
    @DisplayName("F32: 1.0f encodes as 3F800000 in big-endian")
    void f32OneWireFormat() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeF32(1.0f, true);
        byte[] data = w.toBytes();
        assertEquals(4, data.length);
        assertEquals((byte)0x3F, data[0]);
        assertEquals((byte)0x80, data[1]);
        assertEquals((byte)0x00, data[2]);
        assertEquals((byte)0x00, data[3]);
    }

    @Test
    @DisplayName("F64: 1.0 encodes as 3FF0000000000000 in big-endian")
    void f64OneWireFormat() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeF64(1.0, true);
        byte[] data = w.toBytes();
        assertEquals(8, data.length);
        assertEquals((byte)0x3F, data[0]);
        assertEquals((byte)0xF0, data[1]);
        assertEquals((byte)0x00, data[2]);
        assertEquals((byte)0x00, data[3]);
        assertEquals((byte)0x00, data[4]);
        assertEquals((byte)0x00, data[5]);
        assertEquals((byte)0x00, data[6]);
        assertEquals((byte)0x00, data[7]);
    }

    // ========================================================================
    // String wire format verification
    // ========================================================================

    @Test
    @DisplayName("String: write 'AB' padded to 4 with NUL")
    void stringNulPadded() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeString("AB", 4, 0);
        byte[] data = w.toBytes();
        assertEquals(4, data.length);
        assertEquals((byte)'A', data[0]);
        assertEquals((byte)'B', data[1]);
        assertEquals((byte)0x00, data[2]);
        assertEquals((byte)0x00, data[3]);
    }

    @Test
    @DisplayName("String: write 'Hi' padded to 4 with space (0x20)")
    void stringSpacePadded() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeString("Hi", 4, 32);
        byte[] data = w.toBytes();
        assertEquals(4, data.length);
        assertEquals((byte)'H', data[0]);
        assertEquals((byte)'i', data[1]);
        assertEquals((byte)0x20, data[2]);
        assertEquals((byte)0x20, data[3]);
    }

    // ========================================================================
    // Point wire format verification
    // ========================================================================

    @Test
    @DisplayName("Point: x=0x0100, y=0x0200 encodes as 01 00 02 00")
    void pointWireFormat() {
        arrays_choices.Point p = new arrays_choices.Point();
        p.x = 0x0100;
        p.y = 0x0200;
        byte[] encoded = p.encodeBytes();
        byte[] expected = new byte[] { 0x01, 0x00, 0x02, 0x00 };
        assertArrayEquals(expected, encoded);
    }

    // ========================================================================
    // BitWriter sizeBytes verification
    // ========================================================================

    @Test
    @DisplayName("BitWriter: sizeBytes reports correct size")
    void bitWriterSizeBytes() {
        session_test.BitWriter w = new session_test.BitWriter();
        assertEquals(0, w.sizeBytes());
        w.writeU8(0);
        assertEquals(1, w.sizeBytes());
        w.writeU16(0, true);
        assertEquals(3, w.sizeBytes());
        w.writeU32(0, true);
        assertEquals(7, w.sizeBytes());
    }

    @Test
    @DisplayName("BitWriter: sizeBytes with bit-level writes")
    void bitWriterSizeBytesPartial() {
        session_test.BitWriter w = new session_test.BitWriter();
        w.writeBits(1, 1);
        assertEquals(1, w.sizeBytes());  // 1 bit rounds up to 1 byte
        w.writeBits(0, 7);
        assertEquals(1, w.sizeBytes());  // exactly 8 bits = 1 byte
        w.writeBits(0, 1);
        assertEquals(2, w.sizeBytes());  // 9 bits rounds up to 2 bytes
    }

    // ========================================================================
    // Helper methods
    // ========================================================================

    private all_types.AllTypesMessage createDefaultAllTypes() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.ascii = new all_types.AsciiStr("");
        msg.utf8 = new all_types.Utf8Str("");
        msg.temp = new all_types.ScaledTemp(0);
        msg.color = all_types.ColorEnum.RED;
        msg.status = new all_types.StatusFlags();
        return msg;
    }
}
