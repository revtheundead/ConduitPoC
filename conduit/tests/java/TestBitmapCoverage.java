import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests for bitmap/FSPEC field presence and encode/decode in generated Java codecs.
 */
public class TestBitmapCoverage {

    // ========================================================================
    // bitmap_wide_fixed: WideBitmapMsg with optional alpha, beta, gamma
    // ========================================================================

    @Test
    @DisplayName("WideBitmapMsg: all fields populated roundtrip")
    void wideBitmapAllFields() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x01;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0x42;
        msg.bitmapData.beta = 0x1234;
        msg.bitmapData.gamma = 0xDEADBEEFL;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals(0x01, d.header);
        assertEquals((Integer) 0x42, d.bitmapData.alpha);
        assertEquals((Integer) 0x1234, d.bitmapData.beta);
        assertEquals((Long) 0xDEADBEEFL, d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: all fields null (empty bitmap)")
    void wideBitmapAllNull() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x02;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals(0x02, d.header);
        assertNull(d.bitmapData.alpha);
        assertNull(d.bitmapData.beta);
        assertNull(d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: only alpha present")
    void wideBitmapOnlyAlpha() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x03;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0xFF;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0xFF, d.bitmapData.alpha);
        assertNull(d.bitmapData.beta);
        assertNull(d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: only beta present")
    void wideBitmapOnlyBeta() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x04;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.beta = 0xFFFF;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertNull(d.bitmapData.alpha);
        assertEquals((Integer) 0xFFFF, d.bitmapData.beta);
        assertNull(d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: only gamma present")
    void wideBitmapOnlyGamma() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x05;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.gamma = 0xFFFFFFFFL;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertNull(d.bitmapData.alpha);
        assertNull(d.bitmapData.beta);
        assertEquals((Long) 0xFFFFFFFFL, d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: alpha and gamma present, beta null")
    void wideBitmapAlphaGamma() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x06;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0x0A;
        msg.bitmapData.gamma = 0x12345678L;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0x0A, d.bitmapData.alpha);
        assertNull(d.bitmapData.beta);
        assertEquals((Long) 0x12345678L, d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: boundary values 0 for all fields")
    void wideBitmapAllZero() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x00;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0;
        msg.bitmapData.beta = 0;
        msg.bitmapData.gamma = 0L;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0, d.bitmapData.alpha);
        assertEquals((Integer) 0, d.bitmapData.beta);
        assertEquals((Long) 0L, d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: max boundary values for all fields")
    void wideBitmapMaxBoundary() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0xFF;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0xFF;
        msg.bitmapData.beta = 0xFFFF;
        msg.bitmapData.gamma = 0xFFFFFFFFL;
        byte[] encoded = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0xFF, d.bitmapData.alpha);
        assertEquals((Integer) 0xFFFF, d.bitmapData.beta);
        assertEquals((Long) 0xFFFFFFFFL, d.bitmapData.gamma);
    }

    @Test
    @DisplayName("WideBitmapMsg: double-encode stability (all fields)")
    void wideBitmapDoubleEncodeAll() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0xAB;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.alpha = 0x55;
        msg.bitmapData.beta = 0xAAAA;
        msg.bitmapData.gamma = 0x55555555L;
        byte[] data1 = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("WideBitmapMsg: double-encode stability (empty bitmap)")
    void wideBitmapDoubleEncodeEmpty() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x00;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        byte[] data1 = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("WideBitmapMsg: double-encode stability (partial fields)")
    void wideBitmapDoubleEncodePartial() {
        bitmap_wide_fixed.WideBitmapMsg msg = new bitmap_wide_fixed.WideBitmapMsg();
        msg.header = 0x10;
        msg.bitmapData = new bitmap_wide_fixed.WideBitmap();
        msg.bitmapData.beta = 0x7FFF;
        byte[] data1 = msg.encodeBytes();
        bitmap_wide_fixed.WideBitmapMsg d = bitmap_wide_fixed.WideBitmapMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    // ========================================================================
    // nested_bitmap: NestedBitmapMsg with nested bitmap structs
    // ========================================================================

    @Test
    @DisplayName("NestedBitmapMsg: all outer fields populated")
    void nestedBitmapAllOuter() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0x01;
        msg.items = new nested_bitmap.OuterItems();
        msg.items.id = 0x42;
        msg.items.status = nested_bitmap.DeviceStatus.OK;
        byte[] encoded = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
        assertEquals(0x01, d.header);
        assertEquals((Integer) 0x42, d.items.id);
        assertEquals(nested_bitmap.DeviceStatus.OK, d.items.status);
    }

    @Test
    @DisplayName("NestedBitmapMsg: all fields null (empty outer bitmap)")
    void nestedBitmapAllNull() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0x02;
        msg.items = new nested_bitmap.OuterItems();
        byte[] encoded = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
        assertEquals(0x02, d.header);
        assertNull(d.items.id);
        assertNull(d.items.status);
    }

    @Test
    @DisplayName("NestedBitmapMsg: only id present, others null")
    void nestedBitmapOnlyId() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0x03;
        msg.items = new nested_bitmap.OuterItems();
        msg.items.id = 0xFF;
        byte[] encoded = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0xFF, d.items.id);
        assertNull(d.items.status);
    }

    @Test
    @DisplayName("NestedBitmapMsg: status enum values roundtrip")
    void nestedBitmapStatusValues() {
        for (nested_bitmap.DeviceStatus status : nested_bitmap.DeviceStatus.values()) {
            nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
            msg.header = 0x10;
            msg.items = new nested_bitmap.OuterItems();
            msg.items.status = status;
            byte[] encoded = msg.encodeBytes();
            nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
            assertEquals(status, d.items.status);
        }
    }

    @Test
    @DisplayName("NestedBitmapMsg: boundary value 0 for id")
    void nestedBitmapIdZero() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0x00;
        msg.items = new nested_bitmap.OuterItems();
        msg.items.id = 0;
        byte[] encoded = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
        assertEquals((Integer) 0, d.items.id);
    }

    @Test
    @DisplayName("NestedBitmapMsg: boundary value 0xFF for header")
    void nestedBitmapHeaderMax() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0xFF;
        msg.items = new nested_bitmap.OuterItems();
        byte[] encoded = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(encoded);
        assertEquals(0xFF, d.header);
    }

    @Test
    @DisplayName("NestedBitmapMsg: double-encode stability (all outer fields)")
    void nestedBitmapDoubleEncodeAll() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0xAB;
        msg.items = new nested_bitmap.OuterItems();
        msg.items.id = 0x55;
        msg.items.status = nested_bitmap.DeviceStatus.WARNING;
        byte[] data1 = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }

    @Test
    @DisplayName("NestedBitmapMsg: double-encode stability (empty bitmap)")
    void nestedBitmapDoubleEncodeEmpty() {
        nested_bitmap.NestedBitmapMsg msg = new nested_bitmap.NestedBitmapMsg();
        msg.header = 0x00;
        msg.items = new nested_bitmap.OuterItems();
        byte[] data1 = msg.encodeBytes();
        nested_bitmap.NestedBitmapMsg d = nested_bitmap.NestedBitmapMsg.decodeBytes(data1);
        byte[] data2 = d.encodeBytes();
        assertArrayEquals(data1, data2);
    }
}
