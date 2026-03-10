import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.List;

/**
 * Tests for array messages, choice/discriminator messages,
 * nested choices, and struct roundtrips.
 */
public class TestArraysChoices {

    // ========================================================================
    // Point struct tests
    // ========================================================================

    @Test
    @DisplayName("Point: default values roundtrip")
    void pointDefault() {
        arrays_choices.Point p = new arrays_choices.Point();
        byte[] encoded = p.encodeBytes();
        arrays_choices.Point decoded = arrays_choices.Point.decodeBytes(encoded);
        assertEquals(0, decoded.x);
        assertEquals(0, decoded.y);
    }

    @Test
    @DisplayName("Point: set values and roundtrip")
    void pointWithValues() {
        arrays_choices.Point p = new arrays_choices.Point();
        p.x = 100;
        p.y = 200;
        byte[] encoded = p.encodeBytes();
        arrays_choices.Point decoded = arrays_choices.Point.decodeBytes(encoded);
        assertEquals(100, decoded.x);
        assertEquals(200, decoded.y);
    }

    @Test
    @DisplayName("Point: max u16 values roundtrip")
    void pointMaxValues() {
        arrays_choices.Point p = new arrays_choices.Point();
        p.x = 65535;
        p.y = 65535;
        byte[] encoded = p.encodeBytes();
        arrays_choices.Point decoded = arrays_choices.Point.decodeBytes(encoded);
        assertEquals(65535, decoded.x);
        assertEquals(65535, decoded.y);
    }

    @Test
    @DisplayName("Point: encodeBytes produces 4 bytes")
    void pointEncodedSize() {
        arrays_choices.Point p = new arrays_choices.Point();
        assertEquals(4, p.encodeBytes().length);
    }

    // ========================================================================
    // FixedArrayMsg tests
    // ========================================================================

    @Test
    @DisplayName("FixedArrayMsg: roundtrip with 3 Points")
    void fixedArrayMsgRoundtrip() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = (i + 1) * 10;
            p.y = (i + 1) * 20;
            msg.points.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        assertEquals(3, decoded.points.size());
        assertEquals(10, decoded.points.get(0).x);
        assertEquals(20, decoded.points.get(0).y);
        assertEquals(20, decoded.points.get(1).x);
        assertEquals(40, decoded.points.get(1).y);
        assertEquals(30, decoded.points.get(2).x);
        assertEquals(60, decoded.points.get(2).y);
    }

    @Test
    @DisplayName("FixedArrayMsg: encoded size is 12 bytes (3 x 4)")
    void fixedArrayMsgEncodedSize() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            msg.points.add(new arrays_choices.Point());
        }
        byte[] encoded = msg.encodeBytes();
        assertEquals(12, encoded.length);  // 3 Points * 4 bytes each
    }

    @Test
    @DisplayName("FixedArrayMsg: all zero points roundtrip")
    void fixedArrayMsgZeroPoints() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            msg.points.add(new arrays_choices.Point());
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        assertEquals(3, decoded.points.size());
        for (arrays_choices.Point p : decoded.points) {
            assertEquals(0, p.x);
            assertEquals(0, p.y);
        }
    }

    @Test
    @DisplayName("FixedArrayMsg: max value points roundtrip")
    void fixedArrayMsgMaxPoints() {
        arrays_choices.FixedArrayMsg msg = new arrays_choices.FixedArrayMsg();
        for (int i = 0; i < 3; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = 65535;
            p.y = 65535;
            msg.points.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FixedArrayMsg decoded = arrays_choices.FixedArrayMsg.decodeBytes(encoded);
        for (arrays_choices.Point p : decoded.points) {
            assertEquals(65535, p.x);
            assertEquals(65535, p.y);
        }
    }

    // ========================================================================
    // CountFromArrayMsg tests
    // ========================================================================

    @Test
    @DisplayName("CountFromArrayMsg: roundtrip with 2 items")
    void countFromArrayMsg2Items() {
        arrays_choices.CountFromArrayMsg msg = new arrays_choices.CountFromArrayMsg();
        msg.numItems = 2;
        for (int i = 0; i < 2; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = (i + 1) * 100;
            p.y = (i + 1) * 200;
            msg.items.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.CountFromArrayMsg decoded = arrays_choices.CountFromArrayMsg.decodeBytes(encoded);
        assertEquals(2, decoded.numItems);
        assertEquals(2, decoded.items.size());
        assertEquals(100, decoded.items.get(0).x);
        assertEquals(200, decoded.items.get(0).y);
        assertEquals(200, decoded.items.get(1).x);
        assertEquals(400, decoded.items.get(1).y);
    }

    @Test
    @DisplayName("CountFromArrayMsg: roundtrip with 0 items")
    void countFromArrayMsg0Items() {
        arrays_choices.CountFromArrayMsg msg = new arrays_choices.CountFromArrayMsg();
        msg.numItems = 0;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.CountFromArrayMsg decoded = arrays_choices.CountFromArrayMsg.decodeBytes(encoded);
        assertEquals(0, decoded.numItems);
        assertEquals(0, decoded.items.size());
    }

    @Test
    @DisplayName("CountFromArrayMsg: roundtrip with 5 items")
    void countFromArrayMsg5Items() {
        arrays_choices.CountFromArrayMsg msg = new arrays_choices.CountFromArrayMsg();
        msg.numItems = 5;
        for (int i = 0; i < 5; i++) {
            arrays_choices.Point p = new arrays_choices.Point();
            p.x = i;
            p.y = i * 10;
            msg.items.add(p);
        }
        byte[] encoded = msg.encodeBytes();
        arrays_choices.CountFromArrayMsg decoded = arrays_choices.CountFromArrayMsg.decodeBytes(encoded);
        assertEquals(5, decoded.numItems);
        assertEquals(5, decoded.items.size());
        for (int i = 0; i < 5; i++) {
            assertEquals(i, decoded.items.get(i).x);
            assertEquals(i * 10, decoded.items.get(i).y);
        }
    }

    @Test
    @DisplayName("CountFromArrayMsg: encoded size is 1 + numItems*4")
    void countFromArrayMsgEncodedSize() {
        arrays_choices.CountFromArrayMsg msg = new arrays_choices.CountFromArrayMsg();
        msg.numItems = 3;
        for (int i = 0; i < 3; i++) {
            msg.items.add(new arrays_choices.Point());
        }
        byte[] encoded = msg.encodeBytes();
        assertEquals(1 + 3 * 4, encoded.length); // 1 byte count + 3*4 bytes points
    }

    // ========================================================================
    // ChoiceMsg tests
    // ========================================================================

    @Test
    @DisplayName("ChoiceMsg: TypeABody choice roundtrip")
    void choiceMsgTypeA() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5; // TypeABody: subType(1) + SubX.val(4)
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX subX = new arrays_choices.SubX();
        subX.val = 12345;
        typeA.subBody = subX;
        msg.body = typeA;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertEquals((int) arrays_choices.Constants.TYPE_A, decoded.msgType);
        assertInstanceOf(arrays_choices.TypeABody.class, decoded.body);
        arrays_choices.TypeABody decodedTypeA = (arrays_choices.TypeABody) decoded.body;
        assertEquals((int) arrays_choices.Constants.SUB_X, decodedTypeA.subType);
        assertInstanceOf(arrays_choices.SubX.class, decodedTypeA.subBody);
        assertEquals(12345, ((arrays_choices.SubX) decodedTypeA.subBody).val);
    }

    @Test
    @DisplayName("ChoiceMsg: TypeBBody choice roundtrip")
    void choiceMsgTypeB() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_B;
        msg.length = 4; // TypeBBody: tag(4)
        arrays_choices.TypeBBody typeB = new arrays_choices.TypeBBody();
        typeB.tag = 0xABCDEF;
        msg.body = typeB;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);
        assertEquals((int) arrays_choices.Constants.TYPE_B, decoded.msgType);
        assertInstanceOf(arrays_choices.TypeBBody.class, decoded.body);
        assertEquals(0xABCDEF, ((arrays_choices.TypeBBody) decoded.body).tag);
    }

    @Test
    @DisplayName("ChoiceMsg: Fallback choice for unknown msgType")
    void choiceMsgFallback() {
        // Build the raw bytes manually for an unknown msgType
        arrays_choices.codec.BitWriter w = new arrays_choices.codec.BitWriter();
        w.writeU8(99);          // unknown msgType
        w.writeU16(4, true);    // length = 4 bytes for FallbackBody
        w.writeU32(0xDEADBEEF, true); // raw data for FallbackBody
        byte[] data = w.toBytes();

        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(data);
        assertEquals(99, decoded.msgType);
        assertInstanceOf(arrays_choices.FallbackBody.class, decoded.body);
        assertEquals(0xDEADBEEF, ((arrays_choices.FallbackBody) decoded.body).raw);
    }

    // ========================================================================
    // TypeABody nested choice tests
    // ========================================================================

    @Test
    @DisplayName("TypeABody: SubX nested choice roundtrip")
    void typeABodySubX() {
        arrays_choices.TypeABody msg = new arrays_choices.TypeABody();
        msg.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX sub = new arrays_choices.SubX();
        sub.val = 999;
        msg.subBody = sub;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.TypeABody decoded = arrays_choices.TypeABody.decodeBytes(encoded);
        assertEquals((int) arrays_choices.Constants.SUB_X, decoded.subType);
        assertInstanceOf(arrays_choices.SubX.class, decoded.subBody);
        assertEquals(999, ((arrays_choices.SubX) decoded.subBody).val);
    }

    @Test
    @DisplayName("TypeABody: SubY nested choice roundtrip")
    void typeABodySubY() {
        arrays_choices.TypeABody msg = new arrays_choices.TypeABody();
        msg.subType = (int) arrays_choices.Constants.SUB_Y;
        arrays_choices.SubY sub = new arrays_choices.SubY();
        sub.a = 100;
        sub.b = 200;
        msg.subBody = sub;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.TypeABody decoded = arrays_choices.TypeABody.decodeBytes(encoded);
        assertEquals((int) arrays_choices.Constants.SUB_Y, decoded.subType);
        assertInstanceOf(arrays_choices.SubY.class, decoded.subBody);
        arrays_choices.SubY decodedSub = (arrays_choices.SubY) decoded.subBody;
        assertEquals(100, decodedSub.a);
        assertEquals(200, decodedSub.b);
    }

    @Test
    @DisplayName("TypeABody: unknown subType throws exception")
    void typeABodyUnknownSubType() {
        arrays_choices.codec.BitWriter w = new arrays_choices.codec.BitWriter();
        w.writeU8(99);  // unknown subType
        byte[] data = w.toBytes();
        assertThrows(arrays_choices.codec.ConduitCodecException.class, () -> {
            arrays_choices.TypeABody.decodeBytes(data);
        });
    }

    // ========================================================================
    // TypeBBody tests
    // ========================================================================

    @Test
    @DisplayName("TypeBBody: roundtrip")
    void typeBBodyRoundtrip() {
        arrays_choices.TypeBBody msg = new arrays_choices.TypeBBody();
        msg.tag = 0x12345678;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.TypeBBody decoded = arrays_choices.TypeBBody.decodeBytes(encoded);
        assertEquals(0x12345678, decoded.tag);
    }

    @Test
    @DisplayName("TypeBBody: zero tag roundtrip")
    void typeBBodyZero() {
        arrays_choices.TypeBBody msg = new arrays_choices.TypeBBody();
        msg.tag = 0;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.TypeBBody decoded = arrays_choices.TypeBBody.decodeBytes(encoded);
        assertEquals(0, decoded.tag);
    }

    // ========================================================================
    // SubX / SubY standalone tests
    // ========================================================================

    @Test
    @DisplayName("SubX: roundtrip")
    void subXRoundtrip() {
        arrays_choices.SubX msg = new arrays_choices.SubX();
        msg.val = 0xFFFF;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.SubX decoded = arrays_choices.SubX.decodeBytes(encoded);
        assertEquals(0xFFFF, decoded.val);
    }

    @Test
    @DisplayName("SubY: roundtrip")
    void subYRoundtrip() {
        arrays_choices.SubY msg = new arrays_choices.SubY();
        msg.a = 5000;
        msg.b = 6000;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.SubY decoded = arrays_choices.SubY.decodeBytes(encoded);
        assertEquals(5000, decoded.a);
        assertEquals(6000, decoded.b);
    }

    // ========================================================================
    // FallbackBody tests
    // ========================================================================

    @Test
    @DisplayName("FallbackBody: roundtrip")
    void fallbackBodyRoundtrip() {
        arrays_choices.FallbackBody msg = new arrays_choices.FallbackBody();
        msg.raw = 0xCAFEBABE;
        byte[] encoded = msg.encodeBytes();
        arrays_choices.FallbackBody decoded = arrays_choices.FallbackBody.decodeBytes(encoded);
        assertEquals(0xCAFEBABE, decoded.raw);
    }

    // ========================================================================
    // Constants tests
    // ========================================================================

    @Test
    @DisplayName("Constants: TYPE_A is 1")
    void constantsTypeA() {
        assertEquals(1, arrays_choices.Constants.TYPE_A);
    }

    @Test
    @DisplayName("Constants: TYPE_B is 2")
    void constantsTypeB() {
        assertEquals(2, arrays_choices.Constants.TYPE_B);
    }

    @Test
    @DisplayName("Constants: SUB_X is 10")
    void constantsSubX() {
        assertEquals(10, arrays_choices.Constants.SUB_X);
    }

    @Test
    @DisplayName("Constants: SUB_Y is 20")
    void constantsSubY() {
        assertEquals(20, arrays_choices.Constants.SUB_Y);
    }

    // ========================================================================
    // Deep nested roundtrip: ChoiceMsg -> TypeABody -> SubX/SubY
    // ========================================================================

    @Test
    @DisplayName("Deep nested: ChoiceMsg -> TypeABody -> SubY roundtrip")
    void deepNestedChoiceSubY() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5; // TypeABody: subType(1) + SubY(2+2)
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_Y;
        arrays_choices.SubY sub = new arrays_choices.SubY();
        sub.a = 1111;
        sub.b = 2222;
        typeA.subBody = sub;
        msg.body = typeA;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);

        assertInstanceOf(arrays_choices.TypeABody.class, decoded.body);
        arrays_choices.TypeABody decodedA = (arrays_choices.TypeABody) decoded.body;
        assertInstanceOf(arrays_choices.SubY.class, decodedA.subBody);
        arrays_choices.SubY decodedSub = (arrays_choices.SubY) decodedA.subBody;
        assertEquals(1111, decodedSub.a);
        assertEquals(2222, decodedSub.b);
    }

    @Test
    @DisplayName("Deep nested: ChoiceMsg -> TypeABody -> SubX roundtrip with max values")
    void deepNestedChoiceSubXMax() {
        arrays_choices.ChoiceMsg msg = new arrays_choices.ChoiceMsg();
        msg.msgType = (int) arrays_choices.Constants.TYPE_A;
        msg.length = 5; // TypeABody: subType(1) + SubX.val(4)
        arrays_choices.TypeABody typeA = new arrays_choices.TypeABody();
        typeA.subType = (int) arrays_choices.Constants.SUB_X;
        arrays_choices.SubX sub = new arrays_choices.SubX();
        sub.val = 0x7FFFFFFF;
        typeA.subBody = sub;
        msg.body = typeA;

        byte[] encoded = msg.encodeBytes();
        arrays_choices.ChoiceMsg decoded = arrays_choices.ChoiceMsg.decodeBytes(encoded);

        arrays_choices.TypeABody decodedA = (arrays_choices.TypeABody) decoded.body;
        arrays_choices.SubX decodedSub = (arrays_choices.SubX) decodedA.subBody;
        assertEquals(0x7FFFFFFF, decodedSub.val);
    }
}
