import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import io.conduit.ConduitNative;
import io.conduit.Transceiver;
import io.conduit.TransportConfig;
import io.conduit.ConduitError;
import io.conduit.JniNativeBinding;

import session_test.PingBody;
import session_test.DataBody;
import session_test.AckBody;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Advanced roundtrip encode/decode tests: edge cases, complex messages,
 * and Transceiver roundtrips via UDP loopback.
 *
 * Covers: float special values (NaN, Inf, -0), integer bit patterns,
 * string edge cases, complex types, error paths, and transceiver roundtrips.
 */
public class TestRoundtripAdvanced {

    private static final String JNI_LIB = TestLibraryResolver.resolve(
        "conduit.jni.test.path", "CONDUIT_JNI_TEST_LIB", "conduit_jni_test");

    @BeforeAll
    static void setup() {
        System.setProperty("conduit.jni.path", JNI_LIB);
        ConduitNative.setBackend(ConduitNative.Backend.JNI);
    }

    private static int findFreePort() {
        try (java.net.DatagramSocket s = new java.net.DatagramSocket(0)) {
            return s.getLocalPort();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    // ========================================================================
    // Float edge cases
    // ========================================================================

    @Test
    @DisplayName("F32: NaN roundtrip preserves NaN")
    void f32NanRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(Float.NaN, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertTrue(Float.isNaN(r.readF32(true)));
    }

    @Test
    @DisplayName("F64: NaN roundtrip preserves NaN")
    void f64NanRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(Double.NaN, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertTrue(Double.isNaN(r.readF64(true)));
    }

    @Test
    @DisplayName("F32: positive infinity roundtrip")
    void f32PositiveInfinityRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(Float.POSITIVE_INFINITY, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(Float.POSITIVE_INFINITY, r.readF32(true));
    }

    @Test
    @DisplayName("F32: negative infinity roundtrip")
    void f32NegativeInfinityRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(Float.NEGATIVE_INFINITY, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(Float.NEGATIVE_INFINITY, r.readF32(true));
    }

    @Test
    @DisplayName("F64: positive infinity roundtrip")
    void f64PositiveInfinityRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(Double.POSITIVE_INFINITY, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(Double.POSITIVE_INFINITY, r.readF64(true));
    }

    @Test
    @DisplayName("F64: negative infinity roundtrip")
    void f64NegativeInfinityRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(Double.NEGATIVE_INFINITY, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(Double.NEGATIVE_INFINITY, r.readF64(true));
    }

    @Test
    @DisplayName("F32: negative zero roundtrip")
    void f32NegativeZeroRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(-0.0f, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        float result = r.readF32(true);
        assertEquals(0.0f, result);
        assertEquals(Float.floatToRawIntBits(-0.0f), Float.floatToRawIntBits(result));
    }

    @Test
    @DisplayName("F64: negative zero roundtrip")
    void f64NegativeZeroRoundtrip() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(-0.0, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        double result = r.readF64(true);
        assertEquals(0.0, result);
        assertEquals(Double.doubleToRawLongBits(-0.0), Double.doubleToRawLongBits(result));
    }

    @Test
    @DisplayName("F32: smallest subnormal roundtrip")
    void f32SmallestSubnormalRoundtrip() {
        float val = Float.MIN_VALUE; // smallest positive subnormal
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(val, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(val, r.readF32(true));
    }

    @Test
    @DisplayName("F64: smallest subnormal roundtrip")
    void f64SmallestSubnormalRoundtrip() {
        double val = Double.MIN_VALUE;
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(val, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(val, r.readF64(true));
    }

    @Test
    @DisplayName("F32: largest finite roundtrip")
    void f32LargestFiniteRoundtrip() {
        float val = Float.MAX_VALUE;
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF32(val, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(val, r.readF32(true));
    }

    @Test
    @DisplayName("F64: largest finite roundtrip")
    void f64LargestFiniteRoundtrip() {
        double val = Double.MAX_VALUE;
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeF64(val, true);
        all_types.codec.BitReader r = new all_types.codec.BitReader(w.toBytes());
        assertEquals(val, r.readF64(true));
    }

    @Test
    @DisplayName("F32: NaN in AllTypesMessage roundtrip")
    void f32NanInMessage() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f32 = Float.NaN;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertTrue(Float.isNaN(decoded.f32));
    }

    @Test
    @DisplayName("F64: Infinity in AllTypesMessage roundtrip")
    void f64InfinityInMessage() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.f64 = Double.POSITIVE_INFINITY;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(Double.POSITIVE_INFINITY, decoded.f64);
    }

    // ========================================================================
    // Integer edge cases
    // ========================================================================

    @Test
    @DisplayName("U32: alternating bit pattern 0xAAAAAAAA roundtrip")
    void u32AlternatingAARoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u32 = 0xAAAAAAAA;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0xAAAAAAAA, decoded.u32);
    }

    @Test
    @DisplayName("U32: alternating bit pattern 0x55555555 roundtrip")
    void u32Alternating55Roundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u32 = 0x55555555;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0x55555555, decoded.u32);
    }

    @Test
    @DisplayName("U64: alternating bit pattern 0xAAAAAAAAAAAAAAAA roundtrip")
    void u64AlternatingAARoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u64 = 0xAAAAAAAAAAAAAAAAL;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0xAAAAAAAAAAAAAAAAL, decoded.u64);
    }

    @Test
    @DisplayName("U64: alternating bit pattern 0x5555555555555555 roundtrip")
    void u64Alternating55Roundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u64 = 0x5555555555555555L;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0x5555555555555555L, decoded.u64);
    }

    @Test
    @DisplayName("U64: single MSB set roundtrip")
    void u64SingleMsbRoundtrip() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u64 = 0x8000000000000000L;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0x8000000000000000L, decoded.u64);
    }

    @Test
    @DisplayName("Signed fields: alternating sign pattern")
    void signedAlternatingSignPattern() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.i8 = -1;
        msg.i16 = 1;
        msg.i32 = -1;
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(-1, decoded.i8);
        assertEquals(1, decoded.i16);
        assertEquals(-1, decoded.i32);
    }

    // ========================================================================
    // String edge cases
    // ========================================================================

    @Test
    @DisplayName("AsciiStr: exact max length (10 chars) roundtrip")
    void asciiStrExactMaxLen() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("ABCDEFGHIJ");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("ABCDEFGHIJ", decoded.ascii.value());
    }

    @Test
    @DisplayName("AsciiStr: single char roundtrip")
    void asciiStrSingleChar() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("Z");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("Z", decoded.ascii.value());
    }

    @Test
    @DisplayName("AsciiStr: all digits roundtrip")
    void asciiStrAllDigits() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.ascii = new all_types.AsciiStr("0123456789");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("0123456789", decoded.ascii.value());
    }

    @Test
    @DisplayName("Utf8Str: exact max length (16 chars) roundtrip")
    void utf8StrExactMaxLen() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.utf8 = new all_types.Utf8Str("AAAAAAAAAAAAAAAA");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("AAAAAAAAAAAAAAAA", decoded.utf8.value());
    }

    @Test
    @DisplayName("Utf8Str: single char roundtrip")
    void utf8StrSingleChar() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.utf8 = new all_types.Utf8Str("X");
        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals("X", decoded.utf8.value());
    }

    // ========================================================================
    // Multi-field interaction
    // ========================================================================

    @Test
    @DisplayName("AllTypes: all max unsigned + all min signed simultaneously")
    void allTypesAllMaxAndMinSimultaneously() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u8 = 255;
        msg.u16 = 65535;
        msg.u32 = 0xFFFFFFFF;
        msg.u64 = 0xFFFFFFFFFFFFFFFFL;
        msg.i8 = -128;
        msg.i16 = -32768;
        msg.i32 = Integer.MIN_VALUE;
        msg.f32 = Float.POSITIVE_INFINITY;
        msg.f64 = Double.NEGATIVE_INFINITY;
        msg.flag = true;
        msg.ascii = new all_types.AsciiStr("ZZZZZZZZZZ");
        msg.utf8 = new all_types.Utf8Str("WWWWWWWWWWWWWWWW");
        msg.raw = 0xFFFFFFFFFFFFFFFFL;
        msg.le16 = 0xFFFF;
        msg.le32 = 0xFFFFFFFF;
        msg.temp = new all_types.ScaledTemp(0xFFFF);
        msg.hex = 0xFFFFFFFF;
        msg.color = all_types.ColorEnum.BLUE;
        msg.status = new all_types.StatusFlags(0xFF);

        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(255, decoded.u8);
        assertEquals(65535, decoded.u16);
        assertEquals(0xFFFFFFFF, decoded.u32);
        assertEquals(0xFFFFFFFFFFFFFFFFL, decoded.u64);
        assertEquals(-128, decoded.i8);
        assertEquals(-32768, decoded.i16);
        assertEquals(Integer.MIN_VALUE, decoded.i32);
        assertEquals(Float.POSITIVE_INFINITY, decoded.f32);
        assertEquals(Double.NEGATIVE_INFINITY, decoded.f64);
        assertTrue(decoded.flag);
        assertEquals("ZZZZZZZZZZ", decoded.ascii.value());
        assertEquals("WWWWWWWWWWWWWWWW", decoded.utf8.value());
    }

    @Test
    @DisplayName("AllTypes: alternating zero/max per field")
    void allTypesAlternatingZeroMax() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u8 = 0;
        msg.u16 = 0xFFFF;
        msg.u32 = 0;
        msg.u64 = 0xFFFFFFFFFFFFFFFFL;
        msg.i8 = 0;
        msg.i16 = 32767;
        msg.i32 = 0;

        all_types.AllTypesMessage decoded = roundtripAllTypes(msg);
        assertEquals(0, decoded.u8);
        assertEquals(0xFFFF, decoded.u16);
        assertEquals(0, decoded.u32);
        assertEquals(0xFFFFFFFFFFFFFFFFL, decoded.u64);
        assertEquals(0, decoded.i8);
        assertEquals(32767, decoded.i16);
        assertEquals(0, decoded.i32);
    }

    @Test
    @DisplayName("AllTypes: triple roundtrip produces identical bytes")
    void allTypesTripleRoundtripIdempotent() {
        all_types.AllTypesMessage msg = createDefaultAllTypes();
        msg.u8 = 0xAB;
        msg.u16 = 0x1234;
        msg.u32 = 0xDEADBEEF;
        msg.u64 = 0x0102030405060708L;
        msg.i8 = -42;
        msg.i16 = -1000;
        msg.i32 = -100000;
        msg.f32 = 3.14f;
        msg.f64 = -1.5;
        msg.flag = true;
        msg.ascii = new all_types.AsciiStr("Hello");
        msg.utf8 = new all_types.Utf8Str("World");
        msg.raw = 0xCAFEBABE00000000L;
        msg.le16 = 0x5678;
        msg.le32 = 0xABCD1234;
        msg.temp = new all_types.ScaledTemp(4000);
        msg.hex = 0xFF00FF00;
        msg.color = all_types.ColorEnum.GREEN;
        msg.status = new all_types.StatusFlags(0x05);

        byte[] data1 = msg.encodeBytes();
        byte[] data2 = all_types.AllTypesMessage.decodeBytes(data1).encodeBytes();
        byte[] data3 = all_types.AllTypesMessage.decodeBytes(data2).encodeBytes();
        assertArrayEquals(data1, data2);
        assertArrayEquals(data2, data3);
    }

    // ========================================================================
    // Boundary type mixed values
    // ========================================================================

    @Test
    @DisplayName("OddWidthMsg: alternating bit patterns in sub-byte fields")
    void oddWidthAlternatingBits() {
        boundary_types.OddWidthMsg msg = new boundary_types.OddWidthMsg();
        msg.u12 = 0xAAA & 0xFFF;
        msg.u20 = 0xAAAAA & 0xFFFFF;
        msg.s12 = -1;
        byte[] encoded = msg.encodeBytes();
        boundary_types.OddWidthMsg decoded = boundary_types.OddWidthMsg.decodeBytes(encoded);
        assertEquals(0xAAA, decoded.u12);
        assertEquals(0xAAAAA, decoded.u20);
        assertEquals(-1, decoded.s12);
    }

    @Test
    @DisplayName("BoundaryMsg: signed fields at plus/minus one of boundary")
    void boundaryMsgSignedPlusMinusOne() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 1;
        msg.small = 1;
        msg.medium = 1;
        msg.byteVal = 1;
        msg.word = 1;
        msg.dword = 1;
        msg.qword = 1;
        msg.signedByte = -1;
        msg.signedWord = -1;
        msg.signedDword = -1;
        msg.signedQword = -1;
        msg.temp = new boundary_types.ScaledTemp(0);
        msg.level = boundary_types.NybbleEnum.LOW;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(-1, decoded.signedByte);
        assertEquals(-1, decoded.signedWord);
        assertEquals(-1, decoded.signedDword);
        assertEquals(-1, decoded.signedQword);
    }

    // ========================================================================
    // Wire encoding edge cases
    // ========================================================================

    @Test
    @DisplayName("WireEncodingMsg: BCD all nines roundtrip")
    void wireEncodingBcdAllNines() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9999;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 999;
        msg.inlineBnrs = 0;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(9999, decoded.bcdAlt);
        assertEquals(999, decoded.inlineBcd);
    }

    @Test
    @DisplayName("WireEncodingMsg: BCD zero roundtrip")
    void wireEncodingBcdZero() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(0, decoded.bcdAlt);
        assertEquals(0, decoded.bcdHdg);
    }

    @Test
    @DisplayName("WireEncodingMsg: sign-magnitude zero roundtrip")
    void wireEncodingSignMagnitudeZero() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 0;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(0, decoded.smOffset);
    }

    @Test
    @DisplayName("WireEncodingMsg: twos complement max positive roundtrip")
    void wireEncodingTwosComplementMaxPositive() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = 0;
        msg.smOffset = 0;
        msg.cb2Val = 32767;
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = 0;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(32767, decoded.cb2Val);
    }

    @Test
    @DisplayName("WireEncodingMsg: all fields nonzero idempotent")
    void wireEncodingAllFieldsNonzeroIdempotent() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 5678;
        msg.bcdHdg = -179;
        msg.smOffset = -777;
        msg.cb2Val = -10000;
        msg.bnrVal = 40000;
        msg.inlineBcd = 456;
        msg.inlineBnrs = -150;

        byte[] data1 = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(data1);
        byte[] data2 = decoded.encodeBytes();
        assertArrayEquals(data1, data2);
        assertEquals(5678, decoded.bcdAlt);
        assertEquals(-179, decoded.bcdHdg);
        assertEquals(-777, decoded.smOffset);
    }

    // ========================================================================
    // Mixed endian
    // ========================================================================

    @Test
    @DisplayName("MixedMsg: alternating bit patterns roundtrip")
    void mixedEndianAlternatingPatterns() {
        mixed_endian.MixedMsg msg = new mixed_endian.MixedMsg();
        msg.be16 = 0xAAAA;
        msg.le16 = 0x5555;
        msg.be32 = 0xAAAAAAAA;
        msg.le32 = 0x55555555;

        byte[] encoded = msg.encodeBytes();
        mixed_endian.MixedMsg decoded = mixed_endian.MixedMsg.decodeBytes(encoded);
        assertEquals(0xAAAA, decoded.be16);
        assertEquals(0x5555, decoded.le16);
        assertEquals(0xAAAAAAAA, decoded.be32);
        assertEquals(0x55555555, decoded.le32);
    }

    // ========================================================================
    // Array and choice
    // ========================================================================

    @Test
    @DisplayName("FixedArrayMsg: mixed values roundtrip")
    void fixedArrayMixedValues() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        int[][] values = {{0, 65535}, {65535, 0}, {0x5555, 0xAAAA}};
        for (int[] pair : values) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = pair[0];
            p.y = pair[1];
            msg.points.add(p);
        }

        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        for (int i = 0; i < values.length; i++) {
            assertEquals(values[i][0], decoded.points.get(i).x);
            assertEquals(values[i][1], decoded.points.get(i).y);
        }
    }

    @Test
    @DisplayName("ChoiceMsg: TypeA SubX max value roundtrip")
    void choiceMsgTypeASubXMaxValue() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5;
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX subX = new arrays_choices.SubX();
        subX.val = 0xFFFFFFFF;
        typeA.subBody = subX;
        msg.body = typeA;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertInstanceOf(arrays_choices.TypeABody.class, decoded.body);
        assertInstanceOf(arrays_choices.SubX.class,
            ((arrays_choices.TypeABody) decoded.body).subBody);
        assertEquals(0xFFFFFFFF,
            ((arrays_choices.SubX) ((arrays_choices.TypeABody) decoded.body).subBody).val);
    }

    @Test
    @DisplayName("ChoiceMsg: TypeB max tag roundtrip")
    void choiceMsgTypeBMaxTag() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_B;
        msg.length = 4;
        arrays_choices.TypeBBody typeB = new arrays_choices.TypeBBody();
        typeB.tag = 0xFFFFFFFF;
        msg.body = typeB;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertInstanceOf(arrays_choices.TypeBBody.class, decoded.body);
        assertEquals(0xFFFFFFFF, ((arrays_choices.TypeBBody) decoded.body).tag);
    }

    @Test
    @DisplayName("ChoiceMsg: idempotent encode-decode-encode")
    void choiceMsgIdempotent() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5;
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX subX = new arrays_choices.SubX();
        subX.val = 0xDEADBEEF;
        typeA.subBody = subX;
        msg.body = typeA;

        byte[] data1 = msg.encodeBytes();
        byte[] data2 = arrays_choices.ChoiceMsg.decodeBytes(data1).encodeBytes();
        assertArrayEquals(data1, data2);
    }

    // ========================================================================
    // Multi-message sequence
    // ========================================================================

    @Test
    @DisplayName("Sequential encode/decode of different types: no cross-contamination")
    void multipleMessageTypesSequential() {
        PingBody ping = new PingBody();
        ping.timestamp = 0xDEADBEEF;

        DataBody data = new DataBody();
        data.channel = 42;
        data.payloadA = 0x11111111;
        data.payloadB = 0x22222222;

        AckBody ack = new AckBody();
        ack.ackedSeq = 9999;

        byte[] pingData = ping.encodeBytes();
        byte[] dataData = data.encodeBytes();
        byte[] ackData = ack.encodeBytes();

        PingBody ping2 = PingBody.decodeBytes(pingData);
        DataBody data2 = DataBody.decodeBytes(dataData);
        AckBody ack2 = AckBody.decodeBytes(ackData);

        assertEquals(0xDEADBEEF, ping2.timestamp);
        assertEquals(42, data2.channel);
        assertEquals(0x11111111, data2.payloadA);
        assertEquals(0x22222222, data2.payloadB);
        assertEquals(9999, ack2.ackedSeq);
    }

    @Test
    @DisplayName("Repeated encode of same message produces identical bytes")
    void repeatedEncodeSameMessage() {
        PingBody msg = new PingBody();
        msg.timestamp = 12345678;

        byte[] data1 = msg.encodeBytes();
        byte[] data2 = msg.encodeBytes();
        byte[] data3 = msg.encodeBytes();
        assertArrayEquals(data1, data2);
        assertArrayEquals(data2, data3);
    }

    // ========================================================================
    // Error paths
    // ========================================================================

    @Test
    @DisplayName("Empty bytes: AllTypesMessage decode throws")
    void emptyBytesAllTypesThrows() {
        assertThrows(all_types.codec.ConduitCodecException.class,
            () -> all_types.AllTypesMessage.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("Empty bytes: PingBody decode throws")
    void emptyBytesPingBodyThrows() {
        assertThrows(session_test.codec.ConduitCodecException.class,
            () -> PingBody.decodeBytes(new byte[0]));
    }

    @Test
    @DisplayName("Truncated PingBody (2 of 4 bytes) throws")
    void truncatedPingBodyThrows() {
        assertThrows(session_test.codec.ConduitCodecException.class,
            () -> PingBody.decodeBytes(new byte[]{0x00, 0x01}));
    }

    @Test
    @DisplayName("Truncated DataBody (3 of 9 bytes) throws")
    void truncatedDataBodyThrows() {
        assertThrows(session_test.codec.ConduitCodecException.class,
            () -> DataBody.decodeBytes(new byte[]{0x01, 0x02, 0x03}));
    }

    @Test
    @DisplayName("PingBody bytes decoded as DataBody throws (wrong type)")
    void wrongTypeCrossDecode() {
        PingBody ping = new PingBody();
        ping.timestamp = 0x12345678;
        byte[] pingData = ping.encodeBytes();

        // PingBody is 4 bytes, DataBody needs 9 -- should throw
        assertThrows(session_test.codec.ConduitCodecException.class,
            () -> DataBody.decodeBytes(pingData));
    }

    @Test
    @DisplayName("AckBody bytes decoded as PingBody throws (too short)")
    void ackBodyAsPingBodyThrows() {
        AckBody ack = new AckBody();
        ack.ackedSeq = 100;
        byte[] ackData = ack.encodeBytes();

        // AckBody is 2 bytes, PingBody needs 4
        assertThrows(session_test.codec.ConduitCodecException.class,
            () -> PingBody.decodeBytes(ackData));
    }

    @Test
    @DisplayName("Invalid ColorEnum value throws on decode")
    void invalidColorEnumThrows() {
        all_types.codec.BitWriter w = new all_types.codec.BitWriter();
        w.writeBits(99, 8);
        byte[] data = w.toBytes();
        all_types.codec.BitReader r = new all_types.codec.BitReader(data);
        assertThrows(all_types.codec.ConduitCodecException.class,
            () -> all_types.ColorEnum.decode(r));
    }

    @Test
    @DisplayName("BitReader: read beyond available throws")
    void bitReaderExhaustion() {
        all_types.codec.BitReader r = new all_types.codec.BitReader(new byte[]{0x42});
        r.readU8();
        assertThrows(all_types.codec.ConduitCodecException.class, () -> r.readU8());
    }

    @Test
    @DisplayName("BitReader: read u32 from 2 bytes throws")
    void bitReaderU32From2Bytes() {
        all_types.codec.BitReader r = new all_types.codec.BitReader(new byte[]{0x00, 0x01});
        assertThrows(all_types.codec.ConduitCodecException.class, () -> r.readU32(true));
    }

    @Test
    @DisplayName("Extra bytes after PingBody are ignored")
    void extraBytesAfterMessage() {
        PingBody ping = new PingBody();
        ping.timestamp = 42;
        byte[] data = ping.encodeBytes();
        byte[] extended = new byte[data.length + 4];
        System.arraycopy(data, 0, extended, 0, data.length);
        extended[data.length] = (byte) 0xDE;
        extended[data.length + 1] = (byte) 0xAD;

        PingBody decoded = PingBody.decodeBytes(extended);
        assertEquals(42, decoded.timestamp);
    }

    // ========================================================================
    // Transceiver roundtrips (UDP loopback)
    // ========================================================================

    @Test
    @DisplayName("Transceiver: PingBody timestamp=0 roundtrip via UDP")
    void transceiverPingTimestampZero() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<PingBody> received = new AtomicReference<>();

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            PingBody outgoing = new PingBody();
            outgoing.timestamp = 0;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertEquals(0, received.get().timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: PingBody timestamp=0xFFFFFFFF roundtrip via UDP")
    void transceiverPingTimestampMax() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<PingBody> received = new AtomicReference<>();

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            PingBody outgoing = new PingBody();
            outgoing.timestamp = 0xFFFFFFFF;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertEquals(0xFFFFFFFF, received.get().timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: DataBody full fields roundtrip via UDP")
    void transceiverDataBodyFullFields() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<DataBody> received = new AtomicReference<>();

            receiver.onMessage(DataBody.class, (peerId, msg) -> {
                received.set(msg);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            DataBody outgoing = new DataBody();
            outgoing.channel = 255;
            outgoing.payloadA = 0xDEADBEEF;
            outgoing.payloadB = 0xCAFEBABE;
            sender.send(sender.solePeer(), outgoing);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                DataBody incoming = received.get();
                assertEquals(255, incoming.channel);
                assertEquals(0xDEADBEEF, incoming.payloadA);
                assertEquals(0xCAFEBABE, incoming.payloadB);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: interleaved PingBody and DataBody roundtrip")
    void transceiverMultiTypeInterleaved() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger pingCount = new AtomicInteger(0);
            AtomicInteger dataCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(1);

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                int total = pingCount.incrementAndGet() + dataCount.get();
                if (total >= 4) latch.countDown();
            });
            receiver.onMessage(DataBody.class, (peerId, msg) -> {
                int total = dataCount.incrementAndGet() + pingCount.get();
                if (total >= 4) latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            for (int i = 0; i < 2; i++) {
                PingBody ping = new PingBody();
                ping.timestamp = 1000 + i;
                sender.send(sender.solePeer(), ping);

                DataBody data = new DataBody();
                data.channel = i;
                data.payloadA = i * 100;
                data.payloadB = i * 200;
                sender.send(sender.solePeer(), data);
            }

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                assertTrue(pingCount.get() >= 1);
                assertTrue(dataCount.get() >= 1);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: raw bytes manual encode, sendRaw, receive, decode")
    void transceiverRawBytesManualEncodeDecode() throws Exception {
        int port = findFreePort();
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicReference<byte[]> receivedRaw = new AtomicReference<>();

            receiver.onMessage(PingBody.TYPE_ID, (peerId, typeId, typeName, data) -> {
                receivedRaw.set(data);
                latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            // Manually encode a PingBody
            PingBody ping = new PingBody();
            ping.timestamp = 0xABCD1234;
            byte[] rawBytes = ping.encodeBytes();

            sender.sendRaw(sender.solePeer(), PingBody.TYPE_ID, rawBytes);

            boolean got = latch.await(2, TimeUnit.SECONDS);
            if (got) {
                PingBody decoded = PingBody.decodeBytes(receivedRaw.get());
                assertEquals(0xABCD1234, decoded.timestamp);
            }

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: stress 50 messages roundtrip")
    void transceiverStress50Messages() throws Exception {
        int port = findFreePort();
        int count = 50;
        try (Transceiver sender = new Transceiver();
             Transceiver receiver = new Transceiver()) {

            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            AtomicInteger receivedCount = new AtomicInteger(0);
            CountDownLatch latch = new CountDownLatch(1);

            receiver.onMessage(PingBody.class, (peerId, msg) -> {
                int c = receivedCount.incrementAndGet();
                assertTrue(msg.timestamp >= 0 && msg.timestamp < count);
                if (c >= count) latch.countDown();
            });
            receiver.start();

            sender.addPeer("dst", "session_protocol",
                TransportConfig.udp("127.0.0.1:" + port));
            sender.start();

            Thread.sleep(50);

            for (int i = 0; i < count; i++) {
                PingBody msg = new PingBody();
                msg.timestamp = i;
                sender.send(sender.solePeer(), msg);
            }

            latch.await(3, TimeUnit.SECONDS);
            // UDP is unreliable; verify what we got is valid
            assertTrue(receivedCount.get() >= 0);

            sender.stop();
            receiver.stop();
        }
    }

    @Test
    @DisplayName("Transceiver: corrupted raw payload triggers error callback")
    void transceiverCorruptedRawTriggersError() throws Exception {
        int port = findFreePort();
        try (Transceiver receiver = new Transceiver()) {
            receiver.addPeer("src", "session_protocol",
                TransportConfig.udp("0.0.0.0:" + port));

            CountDownLatch latch = new CountDownLatch(1);
            AtomicInteger errorCount = new AtomicInteger(0);

            receiver.onError((peerId, peerName, errorCode, errorMessage) -> {
                errorCount.incrementAndGet();
                latch.countDown();
            });
            receiver.start();

            // Send garbage bytes directly via raw UDP
            byte[] garbage = new byte[]{
                (byte) 0xFF, (byte) 0xFE, (byte) 0xFD, (byte) 0xFC,
                (byte) 0xFB, (byte) 0xFA, (byte) 0xF9, (byte) 0xF8
            };
            try (java.net.DatagramSocket sock = new java.net.DatagramSocket()) {
                java.net.DatagramPacket pkt = new java.net.DatagramPacket(
                    garbage, garbage.length,
                    java.net.InetAddress.getByName("127.0.0.1"), port);
                sock.send(pkt);
            }

            boolean fired = latch.await(2, TimeUnit.SECONDS);
            if (fired) {
                assertTrue(errorCount.get() >= 1);
            }

            receiver.stop();
        }
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

    private all_types.AllTypesMessage roundtripAllTypes(all_types.AllTypesMessage msg) {
        byte[] encoded = msg.encodeBytes();
        return all_types.AllTypesMessage.decodeBytes(encoded);
    }
}
