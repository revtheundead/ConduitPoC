import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Comprehensive tests for newly generated Java schemas.
 * Covers: expr_features, inline_enum, constants_everywhere, constraints_ext,
 * format_binary, auto_count, auto_struct_length, bitmap_advanced, fx_block,
 * fx_advanced, inline_field_types, present_when_complex, frame_basic,
 * bytes_numeric, outer_scope.
 */
public class TestNewSchemas {

    // ========================================================================
    // expr_features
    // ========================================================================

    @Test
    @DisplayName("ArithmeticLengthMsg: TypeA roundtrip")
    void exprArithTypeA() {
        expr_features.ArithmeticLengthMsg msg = new expr_features.ArithmeticLengthMsg();
        msg.totalLength = 6;
        msg.headerSize = 4;
        msg.tag = (int) expr_features.Constants.TYPE_A;
        expr_features.ItemA body = new expr_features.ItemA();
        body.val = 0x1234;
        msg.body = body;
        byte[] data = msg.encodeBytes();
        expr_features.ArithmeticLengthMsg d = expr_features.ArithmeticLengthMsg.decodeBytes(data);
        assertEquals((int) expr_features.Constants.TYPE_A, d.tag);
        assertInstanceOf(expr_features.ItemA.class, d.body);
        assertEquals(0x1234, ((expr_features.ItemA) d.body).val);
    }

    @Test
    @DisplayName("ComparisonMsg: all fields present")
    void exprComparisonAllPresent() {
        expr_features.ComparisonMsg msg = new expr_features.ComparisonMsg();
        msg.flags = 1;
        msg.level = 5;
        msg.optA = 100;
        msg.optB = 200;
        msg.optC = 300;
        msg.optD = 400;
        byte[] data = msg.encodeBytes();
        expr_features.ComparisonMsg d = expr_features.ComparisonMsg.decodeBytes(data);
        assertEquals(100, d.optA);
        assertEquals(200, d.optB);
        assertEquals(300, d.optC);
        assertEquals(400, d.optD);
    }

    @Test
    @DisplayName("BitwiseMsg: with extension")
    void exprBitwiseWithExt() {
        expr_features.BitwiseMsg msg = new expr_features.BitwiseMsg();
        msg.mask = 0x01;
        msg.base = 0x5678;
        msg.extended = 0xABCD;
        byte[] data = msg.encodeBytes();
        expr_features.BitwiseMsg d = expr_features.BitwiseMsg.decodeBytes(data);
        assertEquals((Integer) 0xABCD, d.extended);
    }

    @Test
    @DisplayName("BitwiseMsg: without extension")
    void exprBitwiseNoExt() {
        expr_features.BitwiseMsg msg = new expr_features.BitwiseMsg();
        msg.mask = 0xFE;
        msg.base = 0x1234;
        byte[] data = msg.encodeBytes();
        expr_features.BitwiseMsg d = expr_features.BitwiseMsg.decodeBytes(data);
        assertNull(d.extended);
    }

    @Test
    @DisplayName("LogicalMsg: both flags set")
    void exprLogicalBothFlags() {
        expr_features.LogicalMsg msg = new expr_features.LogicalMsg();
        msg.flagA = 1;
        msg.flagB = 1;
        msg.value = 0x1234;
        msg.conditional = 0xABCD;
        byte[] data = msg.encodeBytes();
        expr_features.LogicalMsg d = expr_features.LogicalMsg.decodeBytes(data);
        assertEquals((Integer) 0xABCD, d.conditional);
    }

    // ========================================================================
    // inline_enum
    // ========================================================================

    @Test
    @DisplayName("InlineEnumMsg: roundtrip")
    void inlineEnumRoundtrip() {
        inline_enum.InlineEnumMsg msg = new inline_enum.InlineEnumMsg();
        msg.mode = inline_enum.InlineEnumMsgMode.ACTIVE;
        msg.priority = inline_enum.InlineEnumMsgPriority.HIGH;
        msg.data = 0x1234;
        byte[] data = msg.encodeBytes();
        inline_enum.InlineEnumMsg d = inline_enum.InlineEnumMsg.decodeBytes(data);
        assertEquals(inline_enum.InlineEnumMsgMode.ACTIVE, d.mode);
        assertEquals(inline_enum.InlineEnumMsgPriority.HIGH, d.priority);
        assertEquals(0x1234, d.data);
    }

    // ========================================================================
    // constants_everywhere
    // ========================================================================

    @Test
    @DisplayName("Versioned: roundtrip with correct version")
    void constEverywhereVersioned() {
        constants_everywhere.Versioned msg = new constants_everywhere.Versioned();
        msg.data = 0x5678;
        byte[] data = msg.encodeBytes();
        constants_everywhere.Versioned d = constants_everywhere.Versioned.decodeBytes(data);
        assertEquals(constants_everywhere.Constants.VERSION, d.version);
        assertEquals(0x5678, d.data);
    }

    @Test
    @DisplayName("Versioned: wrong version raises exception")
    void constEverywhereVersionViolation() {
        constants_everywhere.codec.BitWriter w = new constants_everywhere.codec.BitWriter();
        w.writeU8(99);
        w.writeU16(0, true);
        assertThrows(constants_everywhere.codec.ConduitCodecException.class,
            () -> constants_everywhere.Versioned.decodeBytes(w.toBytes()));
    }

    @Test
    @DisplayName("WithHeader: magic roundtrip")
    void constEverywhereWithHeader() {
        constants_everywhere.WithHeaderHdr hdr = new constants_everywhere.WithHeaderHdr();
        hdr.flags = 0x42;
        constants_everywhere.WithHeader msg = new constants_everywhere.WithHeader();
        msg.hdr = hdr;
        msg.payloadData = 0x12345678;
        byte[] data = msg.encodeBytes();
        constants_everywhere.WithHeader d = constants_everywhere.WithHeader.decodeBytes(data);
        assertEquals(constants_everywhere.Constants.HEADER_MAGIC, d.hdr.magic);
        assertEquals(0x42, d.hdr.flags);
    }

    @Test
    @DisplayName("Frame wrap/encode/decode")
    void constEverywhereFrame() {
        constants_everywhere.Versioned v = new constants_everywhere.Versioned();
        v.data = 42;
        constants_everywhere.Frame frame = constants_everywhere.Frame.wrap(v);
        byte[] data = frame.encodeBytes();
        constants_everywhere.Frame d = constants_everywhere.Frame.decodeBytes(data);
        assertEquals(constants_everywhere.Constants.SYNC, d.sync);
        assertInstanceOf(constants_everywhere.Versioned.class, d.payload);
    }

    // ========================================================================
    // constraints_ext
    // ========================================================================

    @Test
    @DisplayName("ExtConstraintMsg: valid roundtrip")
    void constraintsExtValid() {
        constraints_ext.ExtConstraintMsg msg = new constraints_ext.ExtConstraintMsg();
        msg.temperature = 25;
        msg.count = 5;
        msg.index = 50;
        msg.data = 0x1234;
        byte[] data = msg.encodeBytes();
        constraints_ext.ExtConstraintMsg d = constraints_ext.ExtConstraintMsg.decodeBytes(data);
        assertEquals(0xABCD, d.syncWord);
        assertEquals(42, d.version);
        assertEquals(25, d.temperature);
    }

    @Test
    @DisplayName("ExtConstraintMsg: temperature boundary max=85")
    void constraintsExtTempMax() {
        constraints_ext.ExtConstraintMsg msg = new constraints_ext.ExtConstraintMsg();
        msg.temperature = 85;
        msg.count = 1;
        msg.index = 0;
        msg.data = 0;
        byte[] data = msg.encodeBytes();
        constraints_ext.ExtConstraintMsg d = constraints_ext.ExtConstraintMsg.decodeBytes(data);
        assertEquals(85, d.temperature);
    }

    @Test
    @DisplayName("ExtConstraintMsg: temperature boundary min=-40")
    void constraintsExtTempMin() {
        constraints_ext.ExtConstraintMsg msg = new constraints_ext.ExtConstraintMsg();
        msg.temperature = -40;
        msg.count = 1;
        msg.index = 0;
        msg.data = 0;
        byte[] data = msg.encodeBytes();
        constraints_ext.ExtConstraintMsg d = constraints_ext.ExtConstraintMsg.decodeBytes(data);
        assertEquals(-40, d.temperature);
    }

    @Test
    @DisplayName("ExtConstraintMsg: temperature exceeds max")
    void constraintsExtTempExceedsMax() {
        constraints_ext.codec.BitWriter w = new constraints_ext.codec.BitWriter();
        w.writeU16(0xABCD, true);
        w.writeU8(42);
        w.writeSignedBits(86, 16);
        w.writeU16(5, true);
        w.writeU8(50);
        w.writeU16(0, true);
        assertThrows(constraints_ext.codec.ConduitCodecException.class,
            () -> constraints_ext.ExtConstraintMsg.decodeBytes(w.toBytes()));
    }

    // ========================================================================
    // format_binary
    // ========================================================================

    @Test
    @DisplayName("BinaryMsg: roundtrip with nibbles")
    void formatBinaryRoundtrip() {
        format_binary.BinaryMsg msg = new format_binary.BinaryMsg();
        msg.flags = 0xAB;
        msg.mask = 0x0F;
        msg.tag = 0x05;
        msg.value = 0x1234;
        byte[] data = msg.encodeBytes();
        format_binary.BinaryMsg d = format_binary.BinaryMsg.decodeBytes(data);
        assertEquals(0xAB, d.flags);
        assertEquals(0x0F, d.mask);
        assertEquals(0x05, d.tag);
        assertEquals(0x1234, d.value);
    }

    @Test
    @DisplayName("BinaryMsg: wire size is 4 bytes")
    void formatBinaryWireSize() {
        format_binary.BinaryMsg msg = new format_binary.BinaryMsg();
        assertEquals(4, msg.encodeBytes().length);
    }

    // ========================================================================
    // auto_count
    // ========================================================================

    @Test
    @DisplayName("Container: auto-count roundtrip")
    void autoCountContainer() {
        auto_count.Container c = new auto_count.Container();
        c.tag = 42;
        for (int v : new int[]{100, 200, 300}) {
            auto_count.Record r = new auto_count.Record();
            r.value = v;
            c.items.add(r);
        }
        byte[] data = c.encodeBytes();
        auto_count.Container d = auto_count.Container.decodeBytes(data);
        assertEquals(42, d.tag);
        assertEquals(3, d.count);
        assertEquals(3, d.items.size());
        assertEquals(100, d.items.get(0).value);
    }

    @Test
    @DisplayName("Container: empty items")
    void autoCountContainerEmpty() {
        auto_count.Container c = new auto_count.Container();
        c.tag = 1;
        byte[] data = c.encodeBytes();
        auto_count.Container d = auto_count.Container.decodeBytes(data);
        assertEquals(0, d.count);
        assertEquals(0, d.items.size());
    }

    // ========================================================================
    // auto_struct_length
    // ========================================================================

    @Test
    @DisplayName("TlvMsg: auto-length backpatch")
    void autoStructLengthTlv() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x42;
        msg.data = new byte[]{0x01, 0x02, 0x03, 0x04, 0x05};
        msg.suffix = 0xFF;
        byte[] data = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(data);
        assertEquals(0x42, d.tag);
        assertEquals(5, d.len);
        assertArrayEquals(new byte[]{0x01, 0x02, 0x03, 0x04, 0x05}, d.data);
        assertEquals(0xFF, d.suffix);
    }

    @Test
    @DisplayName("TlvMsg: empty data")
    void autoStructLengthEmpty() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 1;
        msg.data = new byte[0];
        msg.suffix = 0;
        byte[] data = msg.encodeBytes();
        auto_struct_length.TlvMsg d = auto_struct_length.TlvMsg.decodeBytes(data);
        assertEquals(0, d.len);
    }

    @Test
    @DisplayName("TlvMsg: length field only counts data, not suffix")
    void autoStructLengthCorrect() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0xAA;
        msg.data = new byte[]{(byte) 0xDE, (byte) 0xAD, (byte) 0xBE, (byte) 0xEF};
        msg.suffix = (byte) 0xBB;
        byte[] data = msg.encodeBytes();
        assertEquals(7, data.length);
        assertEquals(4, data[1]); // length byte
    }

    // ========================================================================
    // bitmap_advanced
    // ========================================================================

    @Test
    @DisplayName("AdvancedBitmap: all fields present")
    void bitmapAdvancedAllFields() {
        bitmap_advanced.AdvancedBitmap bm = new bitmap_advanced.AdvancedBitmap();
        bm.counter = 0x1234;
        bm.bcdField = 12;
        bm.data = new byte[]{(byte) 0xAA, (byte) 0xBB, (byte) 0xCC, (byte) 0xDD};
        bm.label = "test";
        bm.status = bitmap_advanced.StatusCode.OK;
        byte[] data = bm.encodeBytes();
        bitmap_advanced.AdvancedBitmap d = bitmap_advanced.AdvancedBitmap.decodeBytes(data);
        assertEquals((Integer) 0x1234, d.counter);
        assertEquals((Integer) 12, d.bcdField);
        assertEquals(bitmap_advanced.StatusCode.OK, d.status);
    }

    @Test
    @DisplayName("AdvancedBitmap: no fields present")
    void bitmapAdvancedNoFields() {
        bitmap_advanced.AdvancedBitmap bm = new bitmap_advanced.AdvancedBitmap();
        byte[] data = bm.encodeBytes();
        bitmap_advanced.AdvancedBitmap d = bitmap_advanced.AdvancedBitmap.decodeBytes(data);
        assertNull(d.counter);
        assertNull(d.bcdField);
        assertNull(d.data);
        assertNull(d.label);
        assertNull(d.status);
        assertEquals(1, data.length);
    }

    // ========================================================================
    // fx_block
    // ========================================================================

    @Test
    @DisplayName("FxMessage: with extension")
    void fxBlockWithExtension() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0xAA;
        msg.item1 = 0x1234;
        msg.item2 = 0x12345678;
        msg.item3 = 0x42;
        byte[] data = msg.encodeBytes();
        fx_block.FxMessage d = fx_block.FxMessage.decodeBytes(data);
        assertEquals(0xAA, d.header);
        assertEquals((Integer) 0x1234, d.item1);
        assertEquals((Integer) 0x12345678, d.item2);
        assertEquals((Integer) 0x42, d.item3);
    }

    @Test
    @DisplayName("FxMessage: without extension")
    void fxBlockNoExtension() {
        fx_block.FxMessage msg = new fx_block.FxMessage();
        msg.header = 0xBB;
        byte[] data = msg.encodeBytes();
        fx_block.FxMessage d = fx_block.FxMessage.decodeBytes(data);
        assertEquals(0xBB, d.header);
        assertNull(d.item1);
    }

    // ========================================================================
    // frame_basic
    // ========================================================================

    @Test
    @DisplayName("SimpleFrame: heartbeat wrap/encode/decode")
    void frameBasicHeartbeat() {
        frame_basic.Heartbeat hb = new frame_basic.Heartbeat();
        hb.timestamp = 0x1234;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(hb);
        byte[] data = frame.encodeBytes();
        frame_basic.SimpleFrame d = frame_basic.SimpleFrame.decodeBytes(data);
        assertEquals(1, d.msgType);
        assertInstanceOf(frame_basic.Heartbeat.class, d.payload);
        assertEquals(0x1234, ((frame_basic.Heartbeat) d.payload).timestamp);
    }

    @Test
    @DisplayName("SimpleFrame: status wrap/encode/decode")
    void frameBasicStatus() {
        frame_basic.Status st = new frame_basic.Status();
        st.code = 5;
        st.detail = 0xABCD;
        frame_basic.SimpleFrame frame = frame_basic.SimpleFrame.wrap(st);
        byte[] data = frame.encodeBytes();
        frame_basic.SimpleFrame d = frame_basic.SimpleFrame.decodeBytes(data);
        assertEquals(2, d.msgType);
        assertInstanceOf(frame_basic.Status.class, d.payload);
        assertEquals(5, ((frame_basic.Status) d.payload).code);
    }

    // ========================================================================
    // bytes_numeric
    // ========================================================================

    @Test
    @DisplayName("SmallBytesMsg: multi-width integer roundtrip")
    void bytesNumericRoundtrip() {
        bytes_numeric.SmallBytesMsg msg = new bytes_numeric.SmallBytesMsg();
        msg.val16 = 0x1234;
        msg.val24 = 0xABCDEF;
        msg.val32 = 0x12345678;
        msg.val56 = 0x12345678ABCDEFL;
        msg.val64 = 0xFEDCBA9876543210L;
        byte[] data = msg.encodeBytes();
        bytes_numeric.SmallBytesMsg d = bytes_numeric.SmallBytesMsg.decodeBytes(data);
        assertEquals(0x1234, d.val16);
        assertEquals(0xABCDEF, d.val24);
        assertEquals(0x12345678, d.val32);
        assertEquals(0x12345678ABCDEFL, d.val56);
        assertEquals(0xFEDCBA9876543210L, d.val64);
    }

    @Test
    @DisplayName("SmallBytesMsg: wire size is 24 bytes")
    void bytesNumericWireSize() {
        bytes_numeric.SmallBytesMsg msg = new bytes_numeric.SmallBytesMsg();
        assertEquals(24, msg.encodeBytes().length);
    }

    // ========================================================================
    // outer_scope
    // ========================================================================

    @Test
    @DisplayName("Packet: DataA roundtrip via outer scope")
    void outerScopeDataA() {
        outer_scope.Packet pkt = new outer_scope.Packet();
        pkt.tag = 1;
        outer_scope.DataA payload = new outer_scope.DataA();
        payload.x = 100;
        payload.y = 200;
        pkt.payload = payload;
        byte[] data = pkt.encodeBytes();
        outer_scope.Packet d = outer_scope.Packet.decodeBytes(data);
        assertEquals(1, d.tag);
        assertInstanceOf(outer_scope.DataA.class, d.payload);
        assertEquals(100, ((outer_scope.DataA) d.payload).x);
    }

    @Test
    @DisplayName("Packet: DataB roundtrip")
    void outerScopeDataB() {
        outer_scope.Packet pkt = new outer_scope.Packet();
        pkt.tag = 2;
        outer_scope.DataB payload = new outer_scope.DataB();
        payload.value = 0xBEEF;
        pkt.payload = payload;
        byte[] data = pkt.encodeBytes();
        outer_scope.Packet d = outer_scope.Packet.decodeBytes(data);
        assertEquals(2, d.tag);
        assertInstanceOf(outer_scope.DataB.class, d.payload);
        assertEquals(0xBEEF, ((outer_scope.DataB) d.payload).value);
    }

    // ========================================================================
    // Double encode (idempotency) tests
    // ========================================================================

    @Test
    @DisplayName("TlvMsg: double encode idempotent")
    void doubleEncodeTlv() {
        auto_struct_length.TlvMsg msg = new auto_struct_length.TlvMsg();
        msg.tag = 0x10;
        msg.data = new byte[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        msg.suffix = 0x20;
        byte[] d1 = msg.encodeBytes();
        auto_struct_length.TlvMsg m2 = auto_struct_length.TlvMsg.decodeBytes(d1);
        byte[] d2 = m2.encodeBytes();
        assertArrayEquals(d1, d2);
    }

    @Test
    @DisplayName("BitwiseMsg: double encode idempotent")
    void doubleEncodeBitwise() {
        expr_features.BitwiseMsg msg = new expr_features.BitwiseMsg();
        msg.mask = 0x01;
        msg.base = 42;
        msg.extended = 99;
        byte[] d1 = msg.encodeBytes();
        expr_features.BitwiseMsg m2 = expr_features.BitwiseMsg.decodeBytes(d1);
        byte[] d2 = m2.encodeBytes();
        assertArrayEquals(d1, d2);
    }

    @Test
    @DisplayName("ExtConstraintMsg: double encode idempotent")
    void doubleEncodeConstraints() {
        constraints_ext.ExtConstraintMsg msg = new constraints_ext.ExtConstraintMsg();
        msg.temperature = 0;
        msg.count = 100;
        msg.index = 0;
        msg.data = 0xFFFF;
        byte[] d1 = msg.encodeBytes();
        constraints_ext.ExtConstraintMsg m2 = constraints_ext.ExtConstraintMsg.decodeBytes(d1);
        byte[] d2 = m2.encodeBytes();
        assertArrayEquals(d1, d2);
    }
}
