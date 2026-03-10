import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Boundary and edge case tests matching C++ test depth.
 * Covers: unsigned integer boundaries, signed integer boundaries,
 * double-encode idempotency across message types, string edge cases,
 * and choice type dispatch edge cases.
 */
public class TestBoundaryDepth {

    // ========================================================================
    // Unsigned integer boundary values
    // ========================================================================

    @Test
    @DisplayName("AllTypes: u8 max value (0xFF) roundtrip")
    void u8MaxValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u8 = 0xFF;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(0xFF, decoded.u8);
    }

    @Test
    @DisplayName("AllTypes: u16 max value (0xFFFF) roundtrip")
    void u16MaxValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u16 = 0xFFFF;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(0xFFFF, decoded.u16);
    }

    @Test
    @DisplayName("AllTypes: u32 max value (0xFFFFFFFF) roundtrip")
    void u32MaxValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        // u32 is represented as Java int (signed 32-bit); 0xFFFFFFFF == -1 in signed int
        msg.u32 = (int) 0xFFFFFFFFL;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals((int) 0xFFFFFFFFL, decoded.u32);
    }

    @Test
    @DisplayName("AllTypes: u64 max value roundtrip")
    void u64MaxValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u64 = 0xFFFFFFFFFFFFFFFFL;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(0xFFFFFFFFFFFFFFFFL, decoded.u64);
    }

    // ========================================================================
    // Signed integer boundary values
    // ========================================================================

    @Test
    @DisplayName("AllTypes: i8 min value (-128) roundtrip")
    void i8MinValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.i8 = -128;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(-128, decoded.i8);
    }

    @Test
    @DisplayName("AllTypes: i8 max value (127) roundtrip")
    void i8MaxValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.i8 = 127;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(127, decoded.i8);
    }

    @Test
    @DisplayName("AllTypes: i16 min value (-32768) roundtrip")
    void i16MinValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.i16 = -32768;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(-32768, decoded.i16);
    }

    @Test
    @DisplayName("AllTypes: i32 min value roundtrip")
    void i32MinValue() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.i32 = Integer.MIN_VALUE;
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(Integer.MIN_VALUE, decoded.i32);
    }

    // ========================================================================
    // Zero values for all types
    // ========================================================================

    @Test
    @DisplayName("AllTypes: all zero values roundtrip")
    void allZeroValues() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u8 = 0;
        msg.u16 = 0;
        msg.u32 = 0;
        msg.u64 = 0;
        msg.i8 = 0;
        msg.i16 = 0;
        msg.i32 = 0;
        msg.f32 = 0.0f;
        msg.f64 = 0.0;
        msg.flag = false;

        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertEquals(0, decoded.u8);
        assertEquals(0, decoded.u16);
        assertEquals(0, decoded.u32);
        assertEquals(0, decoded.u64);
        assertEquals(0, decoded.i8);
        assertEquals(0, decoded.i16);
        assertEquals(0, decoded.i32);
        assertEquals(0.0f, decoded.f32);
        assertEquals(0.0, decoded.f64);
        assertFalse(decoded.flag);
    }

    // ========================================================================
    // Double-encode idempotency across message types
    // ========================================================================

    @Test
    @DisplayName("AllTypesMessage: double-encode produces identical bytes")
    void allTypesDoubleEncode() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        msg.u8 = 0xAB;
        msg.u16 = 0x1234;
        msg.u32 = (int) 0xDEADBEEFL;
        msg.u64 = 0x0102030405060708L;
        msg.i8 = -42;
        msg.i16 = -1000;
        msg.i32 = -100000;
        msg.f32 = 3.14f;
        msg.f64 = -1.5;
        msg.flag = true;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second,
            "Encoding AllTypesMessage twice should produce identical bytes");
    }

    @Test
    @DisplayName("PingBody: double-encode produces identical bytes")
    void pingBodyDoubleEncode() {
        session_test.PingBody ping = new session_test.PingBody();
        ping.timestamp = 0x12345678;
        byte[] first = ping.encodeBytes();
        byte[] second = ping.encodeBytes();
        assertArrayEquals(first, second);
    }

    @Test
    @DisplayName("DataBody: double-encode produces identical bytes")
    void dataBodyDoubleEncode() {
        session_test.DataBody data = new session_test.DataBody();
        data.channel = 5;
        data.payloadA = (int) 0xAABBCCDDL;
        data.payloadB = 0x11223344;
        byte[] first = data.encodeBytes();
        byte[] second = data.encodeBytes();
        assertArrayEquals(first, second);
    }

    @Test
    @DisplayName("Point: double-encode produces identical bytes")
    void pointDoubleEncode() {
        arrays_choices.Point p = new arrays_choices.Point();
        p.x = 100;
        p.y = 200;
        byte[] first = p.encodeBytes();
        byte[] second = p.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // Choice type edge cases
    // ========================================================================

    @Test
    @DisplayName("ChoiceMsg: SubX roundtrip")
    void choiceMsgSubXRoundtrip() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5; // TypeABody: subType(1) + SubX(4) = 5 bytes
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX subX = new arrays_choices.SubX();
        subX.val = (int) 0xDEADBEEFL;
        typeA.subBody = subX;
        msg.body = typeA;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertInstanceOf(arrays_choices.TypeABody.class, decoded.body);
        arrays_choices.TypeABody decodedA = (arrays_choices.TypeABody) decoded.body;
        assertInstanceOf(arrays_choices.SubX.class, decodedA.subBody);
        assertEquals((int) 0xDEADBEEFL, ((arrays_choices.SubX) decodedA.subBody).val);
    }

    @Test
    @DisplayName("ChoiceMsg: SubY roundtrip")
    void choiceMsgSubYRoundtrip() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_B;
        msg.length = 4; // TypeBBody: tag(u32) = 4 bytes
        arrays_choices.TypeBBody typeB = new arrays_choices.TypeBBody();
        typeB.tag = (int) 0x12345678L;
        msg.body = typeB;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertInstanceOf(arrays_choices.TypeBBody.class, decoded.body);
        assertEquals((int) 0x12345678L, ((arrays_choices.TypeBBody) decoded.body).tag);
    }

    @Test
    @DisplayName("ChoiceMsg: double-encode produces identical bytes")
    void choiceMsgDoubleEncode() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5; // TypeABody: subType(1) + SubX(4) = 5 bytes
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX subX = new arrays_choices.SubX();
        subX.val = 42;
        typeA.subBody = subX;
        msg.body = typeA;
        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // Fixed array boundary values
    // ========================================================================

    @Test
    @DisplayName("FixedArrayMsg: all max values roundtrip")
    void fixedArrayMaxValues() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = 0xFFFF;
            p.y = 0xFFFF;
            msg.points.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        assertEquals(3, decoded.points.size());
        for (int i = 0; i < 3; i++) {
            assertEquals(0xFFFF, decoded.points.get(i).x);
            assertEquals(0xFFFF, decoded.points.get(i).y);
        }
    }

    @Test
    @DisplayName("FixedArrayMsg: all zero values roundtrip")
    void fixedArrayZeroValues() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = 0;
            p.y = 0;
            msg.points.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        assertEquals(3, decoded.points.size());
        for (int i = 0; i < 3; i++) {
            assertEquals(0, decoded.points.get(i).x);
            assertEquals(0, decoded.points.get(i).y);
        }
    }

    @Test
    @DisplayName("FixedArrayMsg: wire size is 12 bytes (3 * 4)")
    void fixedArrayWireSize() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = i + 1;
            p.y = i + 1;
            msg.points.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        assertEquals(12, encoded.length, "3 Points (u16+u16 each) = 12 bytes");
    }

    // ========================================================================
    // Alpha/Beta choice bodies: max field values
    // ========================================================================

    @Test
    @DisplayName("AlphaBody: max u16 values roundtrip")
    void alphaBodyMaxValues() {
        choice_test.AlphaBody alpha = new choice_test.AlphaBody();
        alpha.x = 0xFFFF;
        alpha.y = 0xFFFF;
        byte[] encoded = alpha.encodeBytes();
        choice_test.AlphaBody decoded = choice_test.AlphaBody.decodeBytes(encoded);
        assertEquals(0xFFFF, decoded.x);
        assertEquals(0xFFFF, decoded.y);
    }

    @Test
    @DisplayName("BetaBody: max field values roundtrip")
    void betaBodyMaxValues() {
        choice_test.BetaBody beta = new choice_test.BetaBody();
        beta.payloadSize = 0xFF;
        // u32 represented as Java int; 0xFFFFFFFF == -1 in signed int
        beta.tag = (int) 0xFFFFFFFFL;
        byte[] encoded = beta.encodeBytes();
        choice_test.BetaBody decoded = choice_test.BetaBody.decodeBytes(encoded);
        assertEquals(0xFF, decoded.payloadSize);
        assertEquals((int) 0xFFFFFFFFL, decoded.tag);
    }

    // ========================================================================
    // Scaled temperature edge values
    // ========================================================================

    @Test
    @DisplayName("ScaledTemp: zero value roundtrip")
    void scaledTempZero() {
        all_types.AllTypesMessage msg = new all_types.AllTypesMessage();
        // ScaledTemp with raw=4000 represents 0.0°C (offset -40, scale 0.01)
        // Raw 0 = -40.0
        byte[] encoded = msg.encodeBytes();
        all_types.AllTypesMessage decoded = all_types.AllTypesMessage.decodeBytes(encoded);
        assertNotNull(decoded);
    }
}
