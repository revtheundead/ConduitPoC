import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Deep wire encoding tests matching C++ test_wire_encoding_roundtrip.cpp depth.
 * Covers: BCD_S overflow, all-negative stress, extreme value stress,
 * non-zero tight packing, multiple negative/positive BCD heading values.
 */
public class TestWireEncodingDepth {

    // ========================================================================
    // BCD_S overflow on encode (C++ "BCD_S overflow on encode" test)
    // ========================================================================

    @Test
    @DisplayName("BCD_S overflow: value 1000 exceeds 13-bit BCD_S max (999)")
    void bcdSOverflow() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdHdg = 1000;  // Max for 13-bit BCD_S (sign + 3 digits) is ±999
        assertThrows(Exception.class, () -> msg.encodeBytes(),
            "BCD_S encode should fail for value > max magnitude");
    }

    @Test
    @DisplayName("BCD_S overflow: value -1000 exceeds 13-bit BCD_S max")
    void bcdSOverflowNegative() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdHdg = -1000;  // Exceeds ±999
        assertThrows(Exception.class, () -> msg.encodeBytes(),
            "BCD_S encode should fail for negative value > max magnitude");
    }

    // ========================================================================
    // All-negative stress test (C++ "all-negative stress roundtrip")
    // ========================================================================

    @Test
    @DisplayName("All-negative stress: all signed fields at max negative")
    void allNegativeStress() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 0;
        msg.bcdHdg = -999;         // max negative 13-bit BCD_S
        msg.smOffset = -32767;     // max negative 16-bit BNR_S
        msg.cb2Val = -32768;       // min int16 CB2 (two's complement)
        msg.bnrVal = 0;
        msg.inlineBcd = 0;
        msg.inlineBnrs = -32767;   // max negative 16-bit BNR_S

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(0, decoded.bcdAlt);
        assertEquals(-999, decoded.bcdHdg);
        assertEquals(-32767, decoded.smOffset);
        assertEquals(-32768, decoded.cb2Val);
        assertEquals(0, decoded.bnrVal);
        assertEquals(0, decoded.inlineBcd);
        assertEquals(-32767, decoded.inlineBnrs);
    }

    // ========================================================================
    // Extreme value stress test (C++ "extreme value stress roundtrip")
    // ========================================================================

    @Test
    @DisplayName("Extreme value stress: all fields at max simultaneously")
    void extremeValueStress() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9999;         // max 16-bit BCD
        msg.bcdHdg = 999;          // max positive 13-bit BCD_S
        msg.smOffset = 32767;      // max positive 16-bit BNR_S
        msg.cb2Val = 32767;        // max positive int16 CB2
        msg.bnrVal = 65535;        // max uint16 BNR
        msg.inlineBcd = 999;       // max 12-bit BCD
        msg.inlineBnrs = 32767;    // max positive 16-bit BNR_S

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(9999, decoded.bcdAlt);
        assertEquals(999, decoded.bcdHdg);
        assertEquals(32767, decoded.smOffset);
        assertEquals(32767, decoded.cb2Val);
        assertEquals(65535, decoded.bnrVal);
        assertEquals(999, decoded.inlineBcd);
        assertEquals(32767, decoded.inlineBnrs);
    }

    // ========================================================================
    // Non-zero tight packing stress (C++ "non-zero tight packing roundtrip")
    // Tests bit-boundary correctness after 13-bit BCD_S shifts alignment
    // ========================================================================

    @Test
    @DisplayName("Non-zero tight packing: stress bit-boundary correctness")
    void nonZeroTightPacking() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 9876;         // 16-bit BCD
        msg.bcdHdg = -999;         // 13-bit BCD_S — shifts alignment by 5 bits
        msg.smOffset = -32767;     // 16-bit BNR_S, now at bit 29 (not byte-aligned)
        msg.cb2Val = -1;           // 16-bit CB2, now at bit 45
        msg.bnrVal = 65535;        // 16-bit BNR, now at bit 61
        msg.inlineBcd = 888;       // 12-bit BCD, now at bit 77
        msg.inlineBnrs = -1000;    // 16-bit BNR_S, now at bit 89

        byte[] encoded = msg.encodeBytes();
        assertEquals(14, encoded.length, "Tight-packed wire size should be 14 bytes");

        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(9876, decoded.bcdAlt);
        assertEquals(-999, decoded.bcdHdg);
        assertEquals(-32767, decoded.smOffset);
        assertEquals(-1, decoded.cb2Val);
        assertEquals(65535, decoded.bnrVal);
        assertEquals(888, decoded.inlineBcd);
        assertEquals(-1000, decoded.inlineBnrs);
    }

    // ========================================================================
    // Multiple negative BCD heading values
    // (C++ "negative BCD heading roundtrip" tests several values)
    // ========================================================================

    @Test
    @DisplayName("BCD heading negative: -1")
    void bcdHeadingNeg1() {
        assertBcdHdgRoundtrip(-1);
    }

    @Test
    @DisplayName("BCD heading negative: -99")
    void bcdHeadingNeg99() {
        assertBcdHdgRoundtrip(-99);
    }

    @Test
    @DisplayName("BCD heading negative: -456")
    void bcdHeadingNeg456() {
        assertBcdHdgRoundtrip(-456);
    }

    @Test
    @DisplayName("BCD heading negative: -999")
    void bcdHeadingNeg999() {
        assertBcdHdgRoundtrip(-999);
    }

    // ========================================================================
    // Multiple positive BCD heading values
    // (C++ "positive BCD heading roundtrip" tests several values)
    // ========================================================================

    @Test
    @DisplayName("BCD heading positive: 0")
    void bcdHeadingPos0() {
        assertBcdHdgRoundtrip(0);
    }

    @Test
    @DisplayName("BCD heading positive: 1")
    void bcdHeadingPos1() {
        assertBcdHdgRoundtrip(1);
    }

    @Test
    @DisplayName("BCD heading positive: 42")
    void bcdHeadingPos42() {
        assertBcdHdgRoundtrip(42);
    }

    @Test
    @DisplayName("BCD heading positive: 123")
    void bcdHeadingPos123() {
        assertBcdHdgRoundtrip(123);
    }

    @Test
    @DisplayName("BCD heading positive: 999")
    void bcdHeadingPos999() {
        assertBcdHdgRoundtrip(999);
    }

    // ========================================================================
    // Double-encode idempotency for wire encodings
    // ========================================================================

    @Test
    @DisplayName("Wire encoding: double-encode produces identical bytes")
    void doubleEncodeIdempotency() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdAlt = 4567;
        msg.bcdHdg = -321;
        msg.smOffset = 1234;
        msg.cb2Val = -5678;
        msg.bnrVal = 9999;
        msg.inlineBcd = 789;
        msg.inlineBnrs = -500;

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second, "Encoding the same message twice should produce identical bytes");
    }

    // ========================================================================
    // Sign-magnitude boundary: maximum negative value
    // ========================================================================

    @Test
    @DisplayName("Sign-magnitude: max negative 16-bit (-32767)")
    void signMagnitudeMaxNegative() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.smOffset = -32767;
        msg.inlineBnrs = -32767;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-32767, decoded.smOffset);
        assertEquals(-32767, decoded.inlineBnrs);
    }

    @Test
    @DisplayName("Two's complement: min int16 (-32768) roundtrip")
    void twosComplementMinInt16() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.cb2Val = -32768;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-32768, decoded.cb2Val);
    }

    @Test
    @DisplayName("Two's complement: -1 roundtrip")
    void twosComplementNeg1() {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.cb2Val = -1;

        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(-1, decoded.cb2Val);
    }

    // ========================================================================
    // Helper
    // ========================================================================

    private void assertBcdHdgRoundtrip(int value) {
        wire_encodings.WireEncodingMsg msg = new wire_encodings.WireEncodingMsg();
        msg.bcdHdg = value;
        byte[] encoded = msg.encodeBytes();
        wire_encodings.WireEncodingMsg decoded = wire_encodings.WireEncodingMsg.decodeBytes(encoded);
        assertEquals(value, decoded.bcdHdg,
            "BCD heading value " + value + " should roundtrip correctly");
    }
}
