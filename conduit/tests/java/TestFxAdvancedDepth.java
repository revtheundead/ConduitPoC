import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Deep FxAdvanced and FxChoice tests matching C++ test_fx_edge_cases.cpp depth.
 * Covers: nested FX blocks (outer extent only, both extents, inner triggers outer),
 * FxAdvanced wire format sizes, FxChoice (choice inside FX) roundtrip.
 */
public class TestFxAdvancedDepth {

    // ========================================================================
    // FxAdvanced: no FX items (C++ "FxAdvanced no FX items roundtrip")
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: no FX items roundtrip")
    void fxAdvancedNoItems() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0xBB;

        byte[] encoded = msg.encodeBytes();
        fx_advanced.FxAdvancedMsg decoded = fx_advanced.FxAdvancedMsg.decodeBytes(encoded);
        assertEquals(0xBB, decoded.header);
        assertNull(decoded.scaledTemp, "scaledTemp should not be present");
        assertNull(decoded.status, "status should not be present");
        assertNull(decoded.label, "label should not be present");
    }

    // ========================================================================
    // FxAdvanced: outer extent only
    // (C++ "FxAdvanced outer extent only (no nested FX)")
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: outer extent only")
    void fxAdvancedOuterExtentOnly() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0x11;
        msg.scaledTemp = 100.0;
        msg.status = fx_advanced.FxStatus.ACTIVE;
        msg.label = "Test";

        byte[] encoded = msg.encodeBytes();
        fx_advanced.FxAdvancedMsg decoded = fx_advanced.FxAdvancedMsg.decodeBytes(encoded);
        assertEquals(0x11, decoded.header);

        // Outer extent fields
        assertNotNull(decoded.scaledTemp);
        assertEquals(100.0, decoded.scaledTemp, 0.02);
        assertEquals(fx_advanced.FxStatus.ACTIVE, decoded.status);
        assertEquals("Test", decoded.label);

        // Inner FX items should NOT be present
        assertNull(decoded.sub);
        assertNull(decoded.values);
    }

    // ========================================================================
    // FxAdvanced: both extents populated
    // (C++ "FxAdvanced both extents populated")
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: both extents populated")
    void fxAdvancedBothExtents() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0x22;
        msg.scaledTemp = -100.0;
        msg.status = fx_advanced.FxStatus.COMPLETE;
        msg.label = "Full";

        fx_advanced.FxSubStruct sub = new fx_advanced.FxSubStruct();
        sub.a = 0xAA;
        sub.b = 0xBB;
        msg.sub = sub;

        byte[] encoded = msg.encodeBytes();
        fx_advanced.FxAdvancedMsg decoded = fx_advanced.FxAdvancedMsg.decodeBytes(encoded);
        assertEquals(0x22, decoded.header);

        // Outer extent
        assertNotNull(decoded.scaledTemp);
        assertEquals(-100.0, decoded.scaledTemp, 0.02);
        assertEquals(fx_advanced.FxStatus.COMPLETE, decoded.status);
        assertEquals("Full", decoded.label);

        // Inner extent
        assertNotNull(decoded.sub);
        assertEquals(0xAA, decoded.sub.a);
        assertEquals(0xBB, decoded.sub.b);
    }

    // ========================================================================
    // FxAdvanced: inner extent triggers outer
    // (C++ "FxAdvanced nested extent only triggers full outer")
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: inner extent triggers outer")
    void fxAdvancedInnerTriggersOuter() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0x33;

        fx_advanced.FxSubStruct sub = new fx_advanced.FxSubStruct();
        sub.a = 0x11;
        sub.b = 0x22;
        msg.sub = sub;

        byte[] encoded = msg.encodeBytes();
        fx_advanced.FxAdvancedMsg decoded = fx_advanced.FxAdvancedMsg.decodeBytes(encoded);
        assertEquals(0x33, decoded.header);

        // Outer extent items should be present (all-or-nothing per extent)
        assertNotNull(decoded.scaledTemp);
        assertNotNull(decoded.status);
        assertNotNull(decoded.label);

        // Inner extent
        assertNotNull(decoded.sub);
        assertEquals(0x11, decoded.sub.a);
        assertEquals(0x22, decoded.sub.b);
        // values is in same inner extent
        assertNotNull(decoded.values);
    }

    // ========================================================================
    // FxAdvanced: wire format sizes
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: no items = 2 bytes")
    void fxAdvancedWireFormatNoItems() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0x00;
        byte[] encoded = msg.encodeBytes();
        // header(8) + FX=0(1) = 9 bits -> 2 bytes
        assertEquals(2, encoded.length);
    }

    @Test
    @DisplayName("FxAdvanced: outer extent larger than empty")
    void fxAdvancedWireFormatOuterLarger() {
        fx_advanced.FxAdvancedMsg msgEmpty = new fx_advanced.FxAdvancedMsg();
        msgEmpty.header = 0x00;
        byte[] emptyBytes = msgEmpty.encodeBytes();

        fx_advanced.FxAdvancedMsg msgOuter = new fx_advanced.FxAdvancedMsg();
        msgOuter.header = 0x00;
        msgOuter.scaledTemp = 0.0;
        byte[] outerBytes = msgOuter.encodeBytes();

        assertTrue(outerBytes.length > emptyBytes.length);
    }

    @Test
    @DisplayName("FxAdvanced: both extents larger than outer only")
    void fxAdvancedWireFormatBothLarger() {
        fx_advanced.FxAdvancedMsg msgOuter = new fx_advanced.FxAdvancedMsg();
        msgOuter.header = 0x00;
        msgOuter.scaledTemp = 0.0;
        byte[] outerBytes = msgOuter.encodeBytes();

        fx_advanced.FxAdvancedMsg msgBoth = new fx_advanced.FxAdvancedMsg();
        msgBoth.header = 0x00;
        msgBoth.scaledTemp = 0.0;
        fx_advanced.FxSubStruct sub = new fx_advanced.FxSubStruct();
        sub.a = 0;
        sub.b = 0;
        msgBoth.sub = sub;
        byte[] bothBytes = msgBoth.encodeBytes();

        assertTrue(bothBytes.length > outerBytes.length);
    }

    // ========================================================================
    // FxAdvanced: decode empty buffer fails
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: decode empty buffer fails")
    void fxAdvancedDecodeEmptyFails() {
        assertThrows(Exception.class, () -> fx_advanced.FxAdvancedMsg.decodeBytes(new byte[0]));
    }

    // ========================================================================
    // FxChoice: CaseA roundtrip (C++ "FxChoice: CaseA roundtrip")
    // ========================================================================

    @Test
    @DisplayName("FxChoice: CaseA roundtrip")
    void fxChoiceCaseARoundtrip() {
        fx_choice.FxChoiceMsg msg = new fx_choice.FxChoiceMsg();
        msg.header = 0x42;
        msg.item1 = 1000;
        msg.tag = fx_choice.TagType.CASE_A;
        fx_choice.CaseABody body = new fx_choice.CaseABody();
        body.x = 0xABCD;
        msg.payload = body;
        msg.item3 = 99;

        byte[] encoded = msg.encodeBytes();
        fx_choice.FxChoiceMsg decoded = fx_choice.FxChoiceMsg.decodeBytes(encoded);
        assertEquals(0x42, decoded.header);
        assertEquals((Integer) 1000, decoded.item1);
        assertEquals(fx_choice.TagType.CASE_A, decoded.tag);
        assertInstanceOf(fx_choice.CaseABody.class, decoded.payload);
        assertEquals(0xABCD, ((fx_choice.CaseABody) decoded.payload).x);
        assertEquals((Integer) 99, decoded.item3);
    }

    // ========================================================================
    // FxChoice: CaseB roundtrip (C++ "FxChoice: CaseB roundtrip")
    // ========================================================================

    @Test
    @DisplayName("FxChoice: CaseB roundtrip")
    void fxChoiceCaseBRoundtrip() {
        fx_choice.FxChoiceMsg msg = new fx_choice.FxChoiceMsg();
        msg.header = 0x55;
        msg.item1 = 2000;
        msg.tag = fx_choice.TagType.CASE_B;
        fx_choice.CaseBBody body = new fx_choice.CaseBBody();
        body.y = 0xDEADBEEFL;
        msg.payload = body;
        msg.item3 = 7;

        byte[] encoded = msg.encodeBytes();
        fx_choice.FxChoiceMsg decoded = fx_choice.FxChoiceMsg.decodeBytes(encoded);
        assertEquals(0x55, decoded.header);
        assertEquals(fx_choice.TagType.CASE_B, decoded.tag);
        assertInstanceOf(fx_choice.CaseBBody.class, decoded.payload);
        assertEquals(0xDEADBEEFL, ((fx_choice.CaseBBody) decoded.payload).y);
        assertEquals((Integer) 7, decoded.item3);
    }

    // ========================================================================
    // FxChoice: no FX items (C++ "FxChoice: no FX items roundtrip")
    // ========================================================================

    @Test
    @DisplayName("FxChoice: no FX items roundtrip")
    void fxChoiceNoItemsRoundtrip() {
        fx_choice.FxChoiceMsg msg = new fx_choice.FxChoiceMsg();
        msg.header = 0x00;

        byte[] encoded = msg.encodeBytes();
        fx_choice.FxChoiceMsg decoded = fx_choice.FxChoiceMsg.decodeBytes(encoded);
        assertEquals(0x00, decoded.header);
        assertNull(decoded.item1);
        assertNull(decoded.tag);
        assertNull(decoded.payload);
        assertNull(decoded.item3);
    }

    // ========================================================================
    // FxAdvanced: double-encode idempotency
    // ========================================================================

    @Test
    @DisplayName("FxAdvanced: double-encode produces identical bytes")
    void fxAdvancedDoubleEncode() {
        fx_advanced.FxAdvancedMsg msg = new fx_advanced.FxAdvancedMsg();
        msg.header = 0x10;
        msg.scaledTemp = -10.0;
        msg.status = fx_advanced.FxStatus.ACTIVE;
        msg.label = "hello";
        msg.sub = new fx_advanced.FxSubStruct();
        msg.sub.a = 11;
        msg.sub.b = 22;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second);
    }
}
