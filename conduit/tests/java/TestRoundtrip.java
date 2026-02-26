import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Comprehensive roundtrip encode/decode tests for all primitive types,
 * message bodies, enums, flags, and scaled values.
 */
public class TestRoundtrip {

    // ========================================================================
    // PingBody roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("PingBody: default values roundtrip")
    void pingBodyDefault() {
        session_test.PingBody msg = new session_test.PingBody();
        byte[] encoded = msg.encodeBytes();
        session_test.PingBody decoded = session_test.PingBody.decodeBytes(encoded);
        assertEquals(0, decoded.timestamp);
    }

    @Test
    @DisplayName("PingBody: set timestamp and roundtrip")
    void pingBodyWithTimestamp() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 0x12345678;
        byte[] encoded = msg.encodeBytes();
        session_test.PingBody decoded = session_test.PingBody.decodeBytes(encoded);
        assertEquals(0x12345678, decoded.timestamp);
    }

    @Test
    @DisplayName("PingBody: max u32 timestamp roundtrip")
    void pingBodyMaxTimestamp() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 0xFFFFFFFF;
        byte[] encoded = msg.encodeBytes();
        session_test.PingBody decoded = session_test.PingBody.decodeBytes(encoded);
        assertEquals(0xFFFFFFFF, decoded.timestamp);
    }

    @Test
    @DisplayName("PingBody: timestamp=1 roundtrip")
    void pingBodyTimestampOne() {
        session_test.PingBody msg = new session_test.PingBody();
        msg.timestamp = 1;
        byte[] encoded = msg.encodeBytes();
        session_test.PingBody decoded = session_test.PingBody.decodeBytes(encoded);
        assertEquals(1, decoded.timestamp);
    }

    @Test
    @DisplayName("PingBody: encodeBytes produces 4 bytes")
    void pingBodyEncodedSize() {
        session_test.PingBody msg = new session_test.PingBody();
        byte[] encoded = msg.encodeBytes();
        assertEquals(4, encoded.length);
    }

    @Test
    @DisplayName("PingBody: TYPE_ID constant is correct")
    void pingBodyTypeId() {
        assertEquals(0x0ad7bb3ecc473399L, session_test.PingBody.TYPE_ID);
    }

    @Test
    @DisplayName("PingBody: TYPE_NAME constant is correct")
    void pingBodyTypeName() {
        assertEquals("PingBody", session_test.PingBody.TYPE_NAME);
    }

    @Test
    @DisplayName("PingBody: ID_VALUE constant is correct")
    void pingBodyIdValue() {
        assertEquals(1, session_test.PingBody.ID_VALUE);
    }

    // ========================================================================
    // DataBody roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("DataBody: default values roundtrip")
    void dataBodyDefault() {
        session_test.DataBody msg = new session_test.DataBody();
        byte[] encoded = msg.encodeBytes();
        session_test.DataBody decoded = session_test.DataBody.decodeBytes(encoded);
        assertEquals(0, decoded.channel);
        assertEquals(0, decoded.payloadA);
        assertEquals(0, decoded.payloadB);
    }

    @Test
    @DisplayName("DataBody: set all fields and roundtrip")
    void dataBodyAllFields() {
        session_test.DataBody msg = new session_test.DataBody();
        msg.channel = 42;
        msg.payloadA = 0xDEADBEEF;
        msg.payloadB = 0xCAFEBABE;
        byte[] encoded = msg.encodeBytes();
        session_test.DataBody decoded = session_test.DataBody.decodeBytes(encoded);
        assertEquals(42, decoded.channel);
        assertEquals(0xDEADBEEF, decoded.payloadA);
        assertEquals(0xCAFEBABE, decoded.payloadB);
    }

    @Test
    @DisplayName("DataBody: max channel value (u8)")
    void dataBodyMaxChannel() {
        session_test.DataBody msg = new session_test.DataBody();
        msg.channel = 255;
        byte[] encoded = msg.encodeBytes();
        session_test.DataBody decoded = session_test.DataBody.decodeBytes(encoded);
        assertEquals(255, decoded.channel);
    }

    @Test
    @DisplayName("DataBody: encodeBytes produces 9 bytes")
    void dataBodyEncodedSize() {
        session_test.DataBody msg = new session_test.DataBody();
        byte[] encoded = msg.encodeBytes();
        assertEquals(9, encoded.length); // 1 (u8) + 4 (u32) + 4 (u32)
    }

    @Test
    @DisplayName("DataBody: TYPE_ID constant is correct")
    void dataBodyTypeId() {
        assertEquals(0x29d16b9e73f85835L, session_test.DataBody.TYPE_ID);
    }

    @Test
    @DisplayName("DataBody: ID_VALUE is 2")
    void dataBodyIdValue() {
        assertEquals(2, session_test.DataBody.ID_VALUE);
    }

    // ========================================================================
    // AckBody roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("AckBody: default values roundtrip")
    void ackBodyDefault() {
        session_test.AckBody msg = new session_test.AckBody();
        byte[] encoded = msg.encodeBytes();
        session_test.AckBody decoded = session_test.AckBody.decodeBytes(encoded);
        assertEquals(0, decoded.ackedSeq);
    }

    @Test
    @DisplayName("AckBody: set ackedSeq and roundtrip")
    void ackBodyWithAckedSeq() {
        session_test.AckBody msg = new session_test.AckBody();
        msg.ackedSeq = 12345;
        byte[] encoded = msg.encodeBytes();
        session_test.AckBody decoded = session_test.AckBody.decodeBytes(encoded);
        assertEquals(12345, decoded.ackedSeq);
    }

    @Test
    @DisplayName("AckBody: max u16 ackedSeq roundtrip")
    void ackBodyMaxAckedSeq() {
        session_test.AckBody msg = new session_test.AckBody();
        msg.ackedSeq = 65535;
        byte[] encoded = msg.encodeBytes();
        session_test.AckBody decoded = session_test.AckBody.decodeBytes(encoded);
        assertEquals(65535, decoded.ackedSeq);
    }

    @Test
    @DisplayName("AckBody: encodeBytes produces 2 bytes")
    void ackBodyEncodedSize() {
        session_test.AckBody msg = new session_test.AckBody();
        byte[] encoded = msg.encodeBytes();
        assertEquals(2, encoded.length);
    }

    @Test
    @DisplayName("AckBody: TYPE_ID constant is correct")
    void ackBodyTypeId() {
        assertEquals(0xcc431e5e357bc2e6L, session_test.AckBody.TYPE_ID);
    }

    // ========================================================================
    // AllTypesMessage roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("AllTypesMessage: full roundtrip with all field types")
    void allTypesFullRoundtrip() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u8 = 200;
        msg.u16 = 50000;
        msg.u32 = 0x7FFFFFFF;
        msg.u64 = 0x123456789ABCDEF0L;
        msg.i8 = -100;
        msg.i16 = -30000;
        msg.i32 = -2000000000;
        msg.f32 = 3.14f;
        msg.f64 = 2.718281828;
        msg.flag = true;
        msg.ascii = new all_types.AsciiStr("Hello");
        msg.utf8 = new all_types.Utf8Str("World");
        msg.raw = 0xFEDCBA9876543210L;
        msg.le16 = 0x1234;
        msg.le32 = 0x12345678;
        msg.temp = new all_types.ScaledTemp(5000);
        msg.hex = 0xABCD1234;
        msg.color = all_types.ColorEnum.GREEN;
        msg.status = new all_types.StatusFlags();
        msg.status.setActive(true);
        msg.status.setReady(true);

        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);

        assertEquals(200, decoded.u8);
        assertEquals(50000, decoded.u16);
        assertEquals(0x7FFFFFFF, decoded.u32);
        assertEquals(0x123456789ABCDEF0L, decoded.u64);
        assertEquals(-100, decoded.i8);
        assertEquals(-30000, decoded.i16);
        assertEquals(-2000000000, decoded.i32);
        assertEquals(3.14f, decoded.f32, 0.001f);
        assertEquals(2.718281828, decoded.f64, 0.0000001);
        assertTrue(decoded.flag);
        assertEquals("Hello", decoded.ascii.value());
        assertEquals("World", decoded.utf8.value());
        assertEquals(0xFEDCBA9876543210L, decoded.raw);
        assertEquals(0x1234, decoded.le16);
        assertEquals(0x12345678, decoded.le32);
        assertEquals(5000, decoded.temp.raw());
        assertEquals(0xABCD1234, decoded.hex);
        assertEquals(all_types.ColorEnum.GREEN, decoded.color);
        assertTrue(decoded.status.active());
        assertFalse(decoded.status.error());
        assertTrue(decoded.status.ready());
    }

    // ========================================================================
    // U8 boundary value tests
    // ========================================================================

    @Test
    @DisplayName("U8: zero value roundtrip")
    void u8ZeroRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u8 = 0;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0, decoded.u8);
    }

    @Test
    @DisplayName("U8: max value (255) roundtrip")
    void u8MaxRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u8 = 255;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(255, decoded.u8);
    }

    @Test
    @DisplayName("U16: max value (65535) roundtrip")
    void u16MaxRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u16 = 65535;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(65535, decoded.u16);
    }

    @Test
    @DisplayName("U32: max value roundtrip")
    void u32MaxRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u32 = 0xFFFFFFFF;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0xFFFFFFFF, decoded.u32);
    }

    @Test
    @DisplayName("U64: max value roundtrip")
    void u64MaxRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u64 = 0xFFFFFFFFFFFFFFFFL;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0xFFFFFFFFFFFFFFFFL, decoded.u64);
    }

    // ========================================================================
    // Signed int tests
    // ========================================================================

    @Test
    @DisplayName("I8: negative value roundtrip")
    void i8NegativeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i8 = -128;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(-128, decoded.i8);
    }

    @Test
    @DisplayName("I8: positive value roundtrip")
    void i8PositiveRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i8 = 127;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(127, decoded.i8);
    }

    @Test
    @DisplayName("I8: zero value roundtrip")
    void i8ZeroRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i8 = 0;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0, decoded.i8);
    }

    @Test
    @DisplayName("I16: negative value roundtrip")
    void i16NegativeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i16 = -32768;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(-32768, decoded.i16);
    }

    @Test
    @DisplayName("I16: max positive value roundtrip")
    void i16MaxPositiveRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i16 = 32767;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(32767, decoded.i16);
    }

    @Test
    @DisplayName("I32: min negative value roundtrip")
    void i32MinNegativeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i32 = Integer.MIN_VALUE;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(Integer.MIN_VALUE, decoded.i32);
    }

    @Test
    @DisplayName("I32: max positive value roundtrip")
    void i32MaxPositiveRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i32 = Integer.MAX_VALUE;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(Integer.MAX_VALUE, decoded.i32);
    }

    // ========================================================================
    // Float/double roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("F32: pi roundtrip")
    void f32PiRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f32 = 3.14159f;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(3.14159f, decoded.f32, 0.00001f);
    }

    @Test
    @DisplayName("F32: negative value roundtrip")
    void f32NegativeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f32 = -999.5f;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(-999.5f, decoded.f32, 0.001f);
    }

    @Test
    @DisplayName("F32: zero roundtrip")
    void f32ZeroRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f32 = 0.0f;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0.0f, decoded.f32);
    }

    @Test
    @DisplayName("F64: pi roundtrip")
    void f64PiRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f64 = Math.PI;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(Math.PI, decoded.f64, 0.000000001);
    }

    @Test
    @DisplayName("F64: negative value roundtrip")
    void f64NegativeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f64 = -1.23456789012345;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(-1.23456789012345, decoded.f64, 0.000000000001);
    }

    @Test
    @DisplayName("F64: very large value roundtrip")
    void f64LargeRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f64 = 1.0e300;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(1.0e300, decoded.f64, 1.0e290);
    }

    // ========================================================================
    // Bool roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("Bool: true roundtrip")
    void boolTrueRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.flag = true;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertTrue(decoded.flag);
    }

    @Test
    @DisplayName("Bool: false roundtrip")
    void boolFalseRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.flag = false;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertFalse(decoded.flag);
    }

    // ========================================================================
    // String roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("AsciiStr: ASCII string roundtrip")
    void asciiStrRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("TestStr");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("TestStr", decoded.ascii.value());
    }

    @Test
    @DisplayName("AsciiStr: empty string roundtrip")
    void asciiStrEmptyRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("", decoded.ascii.value());
    }

    @Test
    @DisplayName("AsciiStr: max length string roundtrip")
    void asciiStrMaxLenRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("0123456789");  // exactly 10 chars
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("0123456789", decoded.ascii.value());
    }

    @Test
    @DisplayName("Utf8Str: string roundtrip")
    void utf8StrRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.utf8 = new all_types.Utf8Str("Hello World");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("Hello World", decoded.utf8.value());
    }

    @Test
    @DisplayName("Utf8Str: empty string roundtrip")
    void utf8StrEmptyRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.utf8 = new all_types.Utf8Str("");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("", decoded.utf8.value());
    }

    // ========================================================================
    // ColorEnum roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("ColorEnum: RED roundtrip")
    void colorEnumRedRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.color = all_types.ColorEnum.RED;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(all_types.ColorEnum.RED, decoded.color);
    }

    @Test
    @DisplayName("ColorEnum: GREEN roundtrip")
    void colorEnumGreenRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.color = all_types.ColorEnum.GREEN;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(all_types.ColorEnum.GREEN, decoded.color);
    }

    @Test
    @DisplayName("ColorEnum: BLUE roundtrip")
    void colorEnumBlueRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.color = all_types.ColorEnum.BLUE;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(all_types.ColorEnum.BLUE, decoded.color);
    }

    @Test
    @DisplayName("ColorEnum: enum values are correct")
    void colorEnumValues() {
        assertEquals(1, all_types.ColorEnum.RED.value);
        assertEquals(2, all_types.ColorEnum.GREEN.value);
        assertEquals(3, all_types.ColorEnum.BLUE.value);
    }

    @Test
    @DisplayName("ColorEnum: standalone encode/decode roundtrip")
    void colorEnumStandaloneRoundtrip() {
        all_types.BitWriter w = new all_types.BitWriter();
        all_types.ColorEnum.BLUE.encode(w);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        all_types.ColorEnum decoded = all_types.ColorEnum.decode(r);
        assertEquals(all_types.ColorEnum.BLUE, decoded);
    }

    // ========================================================================
    // StatusFlags roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("StatusFlags: all flags set roundtrip")
    void statusFlagsAllSet() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setActive(true);
        flags.setError(true);
        flags.setReady(true);
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        all_types.StatusFlags decoded = all_types.StatusFlags.decode(r);
        assertTrue(decoded.active());
        assertTrue(decoded.error());
        assertTrue(decoded.ready());
    }

    @Test
    @DisplayName("StatusFlags: no flags set roundtrip")
    void statusFlagsNoneSet() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        all_types.BitWriter w = new all_types.BitWriter();
        flags.encode(w);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        all_types.StatusFlags decoded = all_types.StatusFlags.decode(r);
        assertFalse(decoded.active());
        assertFalse(decoded.error());
        assertFalse(decoded.ready());
    }

    @Test
    @DisplayName("StatusFlags: individual flags")
    void statusFlagsIndividual() {
        all_types.StatusFlags flags = new all_types.StatusFlags();
        flags.setActive(true);
        assertTrue(flags.active());
        assertFalse(flags.error());
        assertFalse(flags.ready());

        flags.setError(true);
        assertTrue(flags.active());
        assertTrue(flags.error());

        flags.setActive(false);
        assertFalse(flags.active());
        assertTrue(flags.error());
    }

    @Test
    @DisplayName("StatusFlags: raw value roundtrip")
    void statusFlagsRaw() {
        all_types.StatusFlags flags = new all_types.StatusFlags(0x07);
        assertEquals(0x07, flags.raw());
        assertTrue(flags.active());
        assertTrue(flags.error());
        assertTrue(flags.ready());
    }

    // ========================================================================
    // ScaledTemp roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("ScaledTemp: value calculation (raw=5000 -> 10.0)")
    void scaledTempValueCalc() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp(5000);
        // value = 5000 * 0.01 + (-40) = 50 - 40 = 10.0
        assertEquals(10.0, temp.value(), 0.001);
    }

    @Test
    @DisplayName("ScaledTemp: zero raw -> -40.0")
    void scaledTempZeroRaw() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp(0);
        assertEquals(-40.0, temp.value(), 0.001);
    }

    @Test
    @DisplayName("ScaledTemp: roundtrip encode/decode")
    void scaledTempRoundtrip() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp(8000);
        all_types.BitWriter w = new all_types.BitWriter();
        temp.encode(w);
        byte[] data = w.toBytes();
        all_types.BitReader r = new all_types.BitReader(data);
        all_types.ScaledTemp decoded = all_types.ScaledTemp.decode(r);
        assertEquals(8000, decoded.raw());
        assertEquals(temp.value(), decoded.value(), 0.001);
    }

    @Test
    @DisplayName("ScaledTemp: raw setter works")
    void scaledTempSetRaw() {
        all_types.ScaledTemp temp = new all_types.ScaledTemp();
        temp.setRaw(4000);
        assertEquals(4000, temp.raw());
        // value = 4000 * 0.01 + (-40) = 40 - 40 = 0.0
        assertEquals(0.0, temp.value(), 0.001);
    }

    // ========================================================================
    // Little-endian field tests
    // ========================================================================

    @Test
    @DisplayName("LE16: little-endian u16 roundtrip")
    void le16Roundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.le16 = 0xABCD;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0xABCD, decoded.le16);
    }

    @Test
    @DisplayName("LE32: little-endian u32 roundtrip")
    void le32Roundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.le32 = 0x12345678;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0x12345678, decoded.le32);
    }

    // ========================================================================
    // WireEncodingMsg roundtrip tests
    // ========================================================================

    @Test
    @DisplayName("WireEncodingMsg: BCD altitude roundtrip")
    void wireEncodingBcdAltRoundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 1234;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(1234, decoded.bcdAlt);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD signed heading roundtrip")
    void wireEncodingBcdHdgRoundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = -123;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-123, decoded.bcdHdg);
    }

    @Test
    @DisplayName("WireEncodingMsg: sign-magnitude offset roundtrip")
    void wireEncodingSmOffsetRoundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = -500;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-500, decoded.smOffset);
    }

    @Test
    @DisplayName("WireEncodingMsg: complement-2 value roundtrip")
    void wireEncodingCb2Roundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = -1000;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-1000, decoded.cb2Val);
    }

    @Test
    @DisplayName("WireEncodingMsg: full message roundtrip")
    void wireEncodingFullRoundtrip() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9999;
        msg.bcdHdg = 360;
        msg.smOffset = 100;
        msg.cb2Val = -5000;
        msg.bnrVal = 30000;
        msg.inlineBcd = 999;
        msg.inlineBnrs = -200;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(9999, decoded.bcdAlt);
        assertEquals(360, decoded.bcdHdg);
        assertEquals(100, decoded.smOffset);
        assertEquals(-5000, decoded.cb2Val);
        assertEquals(30000, decoded.bnrVal);
        assertEquals(999, decoded.inlineBcd);
        assertEquals(-200, decoded.inlineBnrs);
    }

    // ========================================================================
    // BcdScaled roundtrip
    // ========================================================================

    @Test
    @DisplayName("BcdScaled: value calculation")
    void bcdScaledValue() {
        wire_encodings.BcdScaled s = new wire_encodings.BcdScaled(100);
        // value = 100 * 0.1 = 10.0
        assertEquals(10.0, s.value(), 0.001);
    }

    @Test
    @DisplayName("BcdScaled: roundtrip encode/decode")
    void bcdScaledRoundtrip() {
        wire_encodings.BcdScaled s = new wire_encodings.BcdScaled(500);
        wire_encodings.BitWriter w = new wire_encodings.BitWriter();
        s.encode(w);
        byte[] data = w.toBytes();
        wire_encodings.BitReader r = new wire_encodings.BitReader(data);
        wire_encodings.BcdScaled decoded = wire_encodings.BcdScaled.decode(r);
        assertEquals(500, decoded.raw());
    }

    // ========================================================================
    // AlphaBody / BetaBody from choice_protocol
    // ========================================================================

    @Test
    @DisplayName("AlphaBody: roundtrip")
    void alphaBodyRoundtrip() {
        choice_test.AlphaBody msg = new choice_test.AlphaBody();
        msg.x = 100;
        msg.y = 200;
        byte[] encoded = msg.encodeBytes();
        choice_test.AlphaBody decoded = choice_test.AlphaBody.decodeBytes(encoded);
        assertEquals(100, decoded.x);
        assertEquals(200, decoded.y);
    }

    @Test
    @DisplayName("BetaBody: roundtrip")
    void betaBodyRoundtrip() {
        choice_test.BetaBody msg = new choice_test.BetaBody();
        msg.payloadSize = 128;
        msg.tag = 0xCAFE;
        byte[] encoded = msg.encodeBytes();
        choice_test.BetaBody decoded = choice_test.BetaBody.decodeBytes(encoded);
        assertEquals(128, decoded.payloadSize);
        assertEquals(0xCAFE, decoded.tag);
    }

    // ========================================================================
    // Protocol metadata tests
    // ========================================================================

    @Test
    @DisplayName("Protocol: session_test name and version")
    void sessionProtocolMetadata() {
        assertEquals("session_test", session_test.Protocol.NAME);
        assertEquals("2.0", session_test.Protocol.VERSION);
    }

    @Test
    @DisplayName("Protocol: findById returns correct TypeInfo")
    void protocolFindById() {
        session_test.Protocol.TypeInfo info = session_test.Protocol.findById(0x0ad7bb3ecc473399L);
        assertNotNull(info);
        assertEquals("PingBody", info.typeName());
    }

    @Test
    @DisplayName("Protocol: findByName returns correct TypeInfo")
    void protocolFindByName() {
        session_test.Protocol.TypeInfo info = session_test.Protocol.findByName("DataBody");
        assertNotNull(info);
        assertEquals(0x29d16b9e73f85835L, info.typeId());
    }

    @Test
    @DisplayName("Protocol: findById returns null for unknown")
    void protocolFindByIdUnknown() {
        assertNull(session_test.Protocol.findById(0x9999999999999999L));
    }

    @Test
    @DisplayName("Protocol: findByName returns null for unknown")
    void protocolFindByNameUnknown() {
        assertNull(session_test.Protocol.findByName("NonExistent"));
    }

    // ========================================================================
    // Helper methods
    // ========================================================================

    /** Create an AllTypesMessage with all required sub-objects initialized. */
    private all_types.AllTypesMessage createDefaultAllTypes() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.ascii = new all_types.AsciiStr("");
        msg.utf8 = new all_types.Utf8Str("");
        msg.temp = new all_types.ScaledTemp(0);
        msg.color = all_types.ColorEnum.RED;
        msg.status = new all_types.StatusFlags();
        return msg;
    }

    /** Encode and decode an AllTypesMessage to perform a roundtrip. */
    private all_types.AllTypesMessage roundtripAllTypes(all_types.AllTypesMessage msg) {
        byte[] encoded = msg.encodeBytes();
        return all_types.AllTypesMessage.decodeBytes(encoded);
    }
}
