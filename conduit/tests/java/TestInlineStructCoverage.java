import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

import inline_struct.BodyX;
import inline_struct.BodyY;

public class TestInlineStructCoverage {

    // ========== BodyX Tests ==========

    @Test
    @DisplayName("BodyX: basic roundtrip with xData")
    void testBodyXBasicRoundtrip() {
        BodyX msg = new BodyX();
        msg.xData = 12345678L;

        byte[] data = msg.encodeBytes();
        BodyX decoded = BodyX.decodeBytes(data);

        assertEquals(12345678L, decoded.xData);
    }

    @Test
    @DisplayName("BodyX: zero value roundtrip")
    void testBodyXZeroValue() {
        BodyX msg = new BodyX();
        msg.xData = 0L;

        byte[] data = msg.encodeBytes();
        BodyX decoded = BodyX.decodeBytes(data);

        assertEquals(0L, decoded.xData);
    }

    @Test
    @DisplayName("BodyX: boundary value 0xFFFFFFFF")
    void testBodyXMaxUint32() {
        BodyX msg = new BodyX();
        msg.xData = 0xFFFFFFFFL;

        byte[] data = msg.encodeBytes();
        BodyX decoded = BodyX.decodeBytes(data);

        assertEquals(0xFFFFFFFFL, decoded.xData);
    }

    @Test
    @DisplayName("BodyX: wire size is 4 bytes for uint32")
    void testBodyXWireSize() {
        BodyX msg = new BodyX();
        msg.xData = 1L;

        byte[] data = msg.encodeBytes();

        assertEquals(4, data.length);
    }

    @Test
    @DisplayName("BodyX: ID_VALUE constant equals 1")
    void testBodyXIdValue() {
        assertEquals(1, BodyX.ID_VALUE);
    }

    @Test
    @DisplayName("BodyX: TYPE_ID constant is defined")
    void testBodyXTypeId() {
        assertNotNull(BodyX.TYPE_ID);
    }

    @Test
    @DisplayName("BodyX: double-encode stability")
    void testBodyXDoubleEncode() {
        BodyX msg = new BodyX();
        msg.xData = 0xDEADBEEFL;

        byte[] data1 = msg.encodeBytes();
        BodyX decoded1 = BodyX.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("BodyX: mid-range value roundtrip")
    void testBodyXMidRange() {
        BodyX msg = new BodyX();
        msg.xData = 0x7FFFFFFFL;

        byte[] data = msg.encodeBytes();
        BodyX decoded = BodyX.decodeBytes(data);

        assertEquals(0x7FFFFFFFL, decoded.xData);
    }

    // ========== BodyY Tests ==========

    @Test
    @DisplayName("BodyY: basic roundtrip with yData")
    void testBodyYBasicRoundtrip() {
        BodyY msg = new BodyY();
        msg.yData = 54321;

        byte[] data = msg.encodeBytes();
        BodyY decoded = BodyY.decodeBytes(data);

        assertEquals(54321, decoded.yData);
    }

    @Test
    @DisplayName("BodyY: zero value roundtrip")
    void testBodyYZeroValue() {
        BodyY msg = new BodyY();
        msg.yData = 0;

        byte[] data = msg.encodeBytes();
        BodyY decoded = BodyY.decodeBytes(data);

        assertEquals(0, decoded.yData);
    }

    @Test
    @DisplayName("BodyY: boundary value 0xFFFF")
    void testBodyYMaxUint16() {
        BodyY msg = new BodyY();
        msg.yData = 0xFFFF;

        byte[] data = msg.encodeBytes();
        BodyY decoded = BodyY.decodeBytes(data);

        assertEquals(0xFFFF, decoded.yData);
    }

    @Test
    @DisplayName("BodyY: wire size is 2 bytes for uint16")
    void testBodyYWireSize() {
        BodyY msg = new BodyY();
        msg.yData = 1;

        byte[] data = msg.encodeBytes();

        assertEquals(2, data.length);
    }

    @Test
    @DisplayName("BodyY: ID_VALUE constant equals 2")
    void testBodyYIdValue() {
        assertEquals(2, BodyY.ID_VALUE);
    }

    @Test
    @DisplayName("BodyY: TYPE_ID constant is defined")
    void testBodyYTypeId() {
        assertNotNull(BodyY.TYPE_ID);
    }

    @Test
    @DisplayName("BodyY: double-encode stability")
    void testBodyYDoubleEncode() {
        BodyY msg = new BodyY();
        msg.yData = 0xBEEF;

        byte[] data1 = msg.encodeBytes();
        BodyY decoded1 = BodyY.decodeBytes(data1);
        byte[] data2 = decoded1.encodeBytes();

        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("BodyY: mid-range value roundtrip")
    void testBodyYMidRange() {
        BodyY msg = new BodyY();
        msg.yData = 0x7FFF;

        byte[] data = msg.encodeBytes();
        BodyY decoded = BodyY.decodeBytes(data);

        assertEquals(0x7FFF, decoded.yData);
    }
}
