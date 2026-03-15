import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Miscellaneous codegen coverage tests.
 * Covers: annotation_scope (annotated messages), default_initial (default/initial values),
 * hex_default (hex default values), constraints (constrained fields),
 * boundary_types (edge-case type sizes).
 */
public class TestMiscCoverage {

    // ========================================================================
    // annotation_scope: MsgWithAnnotations and MsgNoAnnotations
    // ========================================================================

    @Test
    @DisplayName("annotation_scope: MsgWithAnnotations roundtrip")
    void annotationScopeMsgWithAnnotations() {
        annotation_scope.MsgWithAnnotations msg = new annotation_scope.MsgWithAnnotations();
        msg.value = 0xABCD;

        byte[] encoded = msg.encodeBytes();
        annotation_scope.MsgWithAnnotations decoded =
            annotation_scope.MsgWithAnnotations.decodeBytes(encoded);
        assertEquals(0xABCD, decoded.value);
    }

    @Test
    @DisplayName("annotation_scope: MsgNoAnnotations roundtrip")
    void annotationScopeMsgNoAnnotations() {
        annotation_scope.MsgNoAnnotations msg = new annotation_scope.MsgNoAnnotations();
        msg.data = 0x1234;

        byte[] encoded = msg.encodeBytes();
        annotation_scope.MsgNoAnnotations decoded =
            annotation_scope.MsgNoAnnotations.decodeBytes(encoded);
        assertEquals(0x1234, decoded.data);
    }

    @Test
    @DisplayName("annotation_scope: MsgWithAnnotations via Frame roundtrip")
    void annotationScopeFrameRoundtrip() {
        annotation_scope.MsgWithAnnotations msg = new annotation_scope.MsgWithAnnotations();
        msg.value = 5000;

        annotation_scope.Frame frame = annotation_scope.Frame.wrap(msg);
        byte[] encoded = frame.encodeBytes();

        annotation_scope.Frame decoded = annotation_scope.Frame.decodeBytes(encoded);
        assertEquals(1, decoded.msgType);
        assertInstanceOf(annotation_scope.MsgWithAnnotations.class, decoded.payload);
        assertEquals(5000, ((annotation_scope.MsgWithAnnotations) decoded.payload).value);
    }

    @Test
    @DisplayName("annotation_scope: MsgNoAnnotations via Frame roundtrip")
    void annotationScopeNoAnnotationsFrame() {
        annotation_scope.MsgNoAnnotations msg = new annotation_scope.MsgNoAnnotations();
        msg.data = 0xFFFF;

        annotation_scope.Frame frame = annotation_scope.Frame.wrap(msg);
        byte[] encoded = frame.encodeBytes();

        annotation_scope.Frame decoded = annotation_scope.Frame.decodeBytes(encoded);
        assertEquals(2, decoded.msgType);
        assertInstanceOf(annotation_scope.MsgNoAnnotations.class, decoded.payload);
        assertEquals(0xFFFF, ((annotation_scope.MsgNoAnnotations) decoded.payload).data);
    }

    @Test
    @DisplayName("annotation_scope: double-encode stability")
    void annotationScopeDoubleEncode() {
        annotation_scope.MsgWithAnnotations msg = new annotation_scope.MsgWithAnnotations();
        msg.value = 42;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // default_initial: messages with default/initial values
    // ========================================================================

    @Test
    @DisplayName("default_initial: DefaultMsg has default field values")
    void defaultInitialDefaultValues() {
        default_initial.DefaultMsg msg = new default_initial.DefaultMsg();
        // version defaults to 1, priority defaults to 0
        assertEquals(1, msg.version);
        assertEquals(0, msg.priority);
    }

    @Test
    @DisplayName("default_initial: DefaultMsg roundtrip preserves defaults")
    void defaultInitialDefaultRoundtrip() {
        default_initial.DefaultMsg msg = new default_initial.DefaultMsg();
        msg.data = 0xDEADBEEF;

        byte[] encoded = msg.encodeBytes();
        default_initial.DefaultMsg decoded = default_initial.DefaultMsg.decodeBytes(encoded);
        assertEquals(1, decoded.version);
        assertEquals(0, decoded.priority);
        assertEquals((int) 0xDEADBEEFL, decoded.data);
    }

    @Test
    @DisplayName("default_initial: DefaultMsg with overridden defaults")
    void defaultInitialOverriddenDefaults() {
        default_initial.DefaultMsg msg = new default_initial.DefaultMsg();
        msg.version = 5;
        msg.priority = 10;
        msg.data = 1000;

        byte[] encoded = msg.encodeBytes();
        default_initial.DefaultMsg decoded = default_initial.DefaultMsg.decodeBytes(encoded);
        assertEquals(5, decoded.version);
        assertEquals(10, decoded.priority);
        assertEquals(1000, decoded.data);
    }

    @Test
    @DisplayName("default_initial: InitialMsg has default counter and status")
    void defaultInitialInitialMsg() {
        default_initial.InitialMsg msg = new default_initial.InitialMsg();
        assertEquals(100, msg.counter);
        assertEquals(0, msg.status);
    }

    @Test
    @DisplayName("default_initial: InitialMsg roundtrip")
    void defaultInitialInitialMsgRoundtrip() {
        default_initial.InitialMsg msg = new default_initial.InitialMsg();
        msg.payload = 0x12345678;

        byte[] encoded = msg.encodeBytes();
        default_initial.InitialMsg decoded = default_initial.InitialMsg.decodeBytes(encoded);
        assertEquals(100, decoded.counter);
        assertEquals(0, decoded.status);
        assertEquals(0x12345678, decoded.payload);
    }

    @Test
    @DisplayName("default_initial: double-encode stability")
    void defaultInitialDoubleEncode() {
        default_initial.DefaultMsg msg = new default_initial.DefaultMsg();
        msg.data = 999;

        byte[] first = msg.encodeBytes();
        default_initial.DefaultMsg decoded = default_initial.DefaultMsg.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // hex_default: messages with hex default values
    // ========================================================================

    @Test
    @DisplayName("hex_default: HexMsg has hex default field values")
    void hexDefaultValues() {
        hex_default.HexMsg msg = new hex_default.HexMsg();
        assertEquals(0xFF, msg.header);
        assertEquals(0xBEEF, msg.magic);
    }

    @Test
    @DisplayName("hex_default: HexMsg roundtrip preserves hex defaults")
    void hexDefaultRoundtrip() {
        hex_default.HexMsg msg = new hex_default.HexMsg();
        msg.data = 0x42;

        byte[] encoded = msg.encodeBytes();
        hex_default.HexMsg decoded = hex_default.HexMsg.decodeBytes(encoded);
        assertEquals(0xFF, decoded.header);
        assertEquals(0xBEEF, decoded.magic);
        assertEquals(0x42, decoded.data);
    }

    @Test
    @DisplayName("hex_default: HexMsg with overridden hex defaults")
    void hexDefaultOverridden() {
        hex_default.HexMsg msg = new hex_default.HexMsg();
        msg.header = 0x00;
        msg.magic = 0x0000;
        msg.data = 0xAA;

        byte[] encoded = msg.encodeBytes();
        hex_default.HexMsg decoded = hex_default.HexMsg.decodeBytes(encoded);
        assertEquals(0x00, decoded.header);
        assertEquals(0x0000, decoded.magic);
        assertEquals(0xAA, decoded.data);
    }

    @Test
    @DisplayName("hex_default: double-encode stability")
    void hexDefaultDoubleEncode() {
        hex_default.HexMsg msg = new hex_default.HexMsg();
        msg.data = 0x55;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // constraints: messages with constrained fields
    // ========================================================================

    @Test
    @DisplayName("constraints: ConstraintMsg valid roundtrip")
    void constraintsValidRoundtrip() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 50;
        msg.deferredVal = 100;
        msg.payload = 0x12345678;

        byte[] encoded = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(encoded);
        assertEquals(0xBEEF, decoded.magic);
        assertEquals(50, decoded.percent);
        assertEquals(100, decoded.deferredVal);
        assertEquals(0x12345678, decoded.payload);
    }

    @Test
    @DisplayName("constraints: ConstraintMsg percent at min boundary (0)")
    void constraintsPercentMin() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 0;
        msg.deferredVal = 10;
        msg.payload = 0;

        byte[] encoded = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(encoded);
        assertEquals(0, decoded.percent);
    }

    @Test
    @DisplayName("constraints: ConstraintMsg percent at max boundary (100)")
    void constraintsPercentMax() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 100;
        msg.deferredVal = 500;
        msg.payload = 0;

        byte[] encoded = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(encoded);
        assertEquals(100, decoded.percent);
    }

    @Test
    @DisplayName("constraints: ConstraintMsg invalid magic on decode throws")
    void constraintsInvalidMagic() {
        constraints.codec.BitWriter w = new constraints.codec.BitWriter();
        w.writeU16(0xDEAD, true);  // wrong magic
        w.writeU8(50);             // percent
        w.writeU16(100, true);     // deferred-val
        w.writeU32(0, true);       // payload

        assertThrows(constraints.codec.ConduitCodecException.class,
            () -> constraints.ConstraintMsg.decodeBytes(w.toBytes()));
    }

    @Test
    @DisplayName("constraints: ConstraintMsg deferred-val at min boundary (10)")
    void constraintsDeferredMin() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 50;
        msg.deferredVal = 10;
        msg.payload = 0;

        byte[] encoded = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(encoded);
        assertEquals(10, decoded.deferredVal);
    }

    @Test
    @DisplayName("constraints: ConstraintMsg deferred-val at max boundary (500)")
    void constraintsDeferredMax() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 50;
        msg.deferredVal = 500;
        msg.payload = 0;

        byte[] encoded = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(encoded);
        assertEquals(500, decoded.deferredVal);
    }

    @Test
    @DisplayName("constraints: ConstraintMsg double-encode stability")
    void constraintsDoubleEncode() {
        constraints.ConstraintMsg msg = new constraints.ConstraintMsg();
        msg.magic = 0xBEEF;
        msg.percent = 75;
        msg.deferredVal = 250;
        msg.payload = 999;

        byte[] first = msg.encodeBytes();
        constraints.ConstraintMsg decoded = constraints.ConstraintMsg.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }

    // ========================================================================
    // boundary_types: edge-case type sizes
    // ========================================================================

    @Test
    @DisplayName("boundary_types: BoundaryMsg sub-byte fields (uint1, uint3, uint7)")
    void boundaryTypesSubByteFields() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 1;
        msg.small = 7;    // uint3 max = 7
        msg.medium = 127; // uint7 max = 127
        msg.byteVal = 0xFF;
        msg.word = 0;
        msg.dword = 0;
        msg.qword = 0;
        msg.signedByte = 0;
        msg.signedWord = 0;
        msg.signedDword = 0;
        msg.signedQword = 0;
        msg.temp = 0;
        msg.level = 0;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(1, decoded.flag);
        assertEquals(7, decoded.small);
        assertEquals(127, decoded.medium);
        assertEquals(0xFF, decoded.byteVal);
    }

    @Test
    @DisplayName("boundary_types: BoundaryMsg u16/u32/u64 max values")
    void boundaryTypesUnsignedMax() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 0;
        msg.small = 0;
        msg.medium = 0;
        msg.byteVal = 0;
        msg.word = 0xFFFF;
        msg.dword = (int) 0xFFFFFFFFL;
        msg.qword = 0xFFFFFFFFFFFFFFFFL;
        msg.signedByte = 0;
        msg.signedWord = 0;
        msg.signedDword = 0;
        msg.signedQword = 0;
        msg.temp = 0;
        msg.level = 0;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(0xFFFF, decoded.word);
        assertEquals((int) 0xFFFFFFFFL, decoded.dword);
        assertEquals(0xFFFFFFFFFFFFFFFFL, decoded.qword);
    }

    @Test
    @DisplayName("boundary_types: BoundaryMsg negative signed values (min bounds)")
    void boundaryTypesSignedNegative() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 0;
        msg.small = 0;
        msg.medium = 0;
        msg.byteVal = 0;
        msg.word = 0;
        msg.dword = 0;
        msg.qword = 0;
        msg.signedByte = -128;
        msg.signedWord = -32768;
        msg.signedDword = Integer.MIN_VALUE;
        msg.signedQword = Long.MIN_VALUE;
        msg.temp = 0;
        msg.level = 0;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(-128, decoded.signedByte);
        assertEquals(-32768, decoded.signedWord);
        assertEquals(Integer.MIN_VALUE, decoded.signedDword);
        assertEquals(Long.MIN_VALUE, decoded.signedQword);
    }

    @Test
    @DisplayName("boundary_types: BoundaryMsg positive signed max values")
    void boundaryTypesSignedPositiveMax() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 0;
        msg.small = 0;
        msg.medium = 0;
        msg.byteVal = 0;
        msg.word = 0;
        msg.dword = 0;
        msg.qword = 0;
        msg.signedByte = 127;
        msg.signedWord = 32767;
        msg.signedDword = Integer.MAX_VALUE;
        msg.signedQword = Long.MAX_VALUE;
        msg.temp = 0;
        msg.level = 0;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(127, decoded.signedByte);
        assertEquals(32767, decoded.signedWord);
        assertEquals(Integer.MAX_VALUE, decoded.signedDword);
        assertEquals(Long.MAX_VALUE, decoded.signedQword);
    }

    @Test
    @DisplayName("boundary_types: BoundaryMsg all zero values roundtrip")
    void boundaryTypesAllZeros() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 0;
        msg.small = 0;
        msg.medium = 0;
        msg.byteVal = 0;
        msg.word = 0;
        msg.dword = 0;
        msg.qword = 0;
        msg.signedByte = 0;
        msg.signedWord = 0;
        msg.signedDword = 0;
        msg.signedQword = 0;
        msg.temp = 0;
        msg.level = 0;

        byte[] encoded = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(encoded);
        assertEquals(0, decoded.flag);
        assertEquals(0, decoded.small);
        assertEquals(0, decoded.medium);
        assertEquals(0, decoded.byteVal);
        assertEquals(0, decoded.word);
        assertEquals(0, decoded.dword);
        assertEquals(0, decoded.qword);
        assertEquals(0, decoded.signedByte);
        assertEquals(0, decoded.signedWord);
        assertEquals(0, decoded.signedDword);
        assertEquals(0, decoded.signedQword);
    }

    @Test
    @DisplayName("boundary_types: OddWidthMsg uint12/uint20/int12 max roundtrip")
    void boundaryTypesOddWidthMax() {
        boundary_types.OddWidthMsg msg = new boundary_types.OddWidthMsg();
        msg.u12 = 0xFFF; // uint12 max
        msg.u20 = 0xFFFFF; // uint20 max
        msg.s12 = -2048; // int12 min

        byte[] encoded = msg.encodeBytes();
        boundary_types.OddWidthMsg decoded = boundary_types.OddWidthMsg.decodeBytes(encoded);
        assertEquals(0xFFF, decoded.u12);
        assertEquals(0xFFFFF, decoded.u20);
        assertEquals(-2048, decoded.s12);
    }

    @Test
    @DisplayName("boundary_types: OddWidthMsg positive int12 max roundtrip")
    void boundaryTypesOddWidthPositive() {
        boundary_types.OddWidthMsg msg = new boundary_types.OddWidthMsg();
        msg.u12 = 100;
        msg.u20 = 50000;
        msg.s12 = 2047; // int12 max

        byte[] encoded = msg.encodeBytes();
        boundary_types.OddWidthMsg decoded = boundary_types.OddWidthMsg.decodeBytes(encoded);
        assertEquals(100, decoded.u12);
        assertEquals(50000, decoded.u20);
        assertEquals(2047, decoded.s12);
    }

    @Test
    @DisplayName("boundary_types: BoundaryMsg double-encode stability")
    void boundaryTypesDoubleEncode() {
        boundary_types.BoundaryMsg msg = new boundary_types.BoundaryMsg();
        msg.flag = 1;
        msg.small = 5;
        msg.medium = 100;
        msg.byteVal = 0xAB;
        msg.word = 0x1234;
        msg.dword = 0x56789ABC;
        msg.qword = 0x0102030405060708L;
        msg.signedByte = -42;
        msg.signedWord = -1000;
        msg.signedDword = -100000;
        msg.signedQword = -999999999L;
        msg.temp = 500;
        msg.level = 3;

        byte[] first = msg.encodeBytes();
        boundary_types.BoundaryMsg decoded = boundary_types.BoundaryMsg.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }

    @Test
    @DisplayName("boundary_types: OddWidthMsg double-encode stability")
    void boundaryTypesOddWidthDoubleEncode() {
        boundary_types.OddWidthMsg msg = new boundary_types.OddWidthMsg();
        msg.u12 = 0xABC;
        msg.u20 = 0x12345;
        msg.s12 = -100;

        byte[] first = msg.encodeBytes();
        boundary_types.OddWidthMsg decoded = boundary_types.OddWidthMsg.decodeBytes(first);
        byte[] second = decoded.encodeBytes();
        assertArrayEquals(first, second);
    }
}
