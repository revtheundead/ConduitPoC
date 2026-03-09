import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Deep string encoding tests matching C++ test_string_encoding_edge_cases.cpp depth.
 * Covers: null-padded trailing space preservation, space-padded strings,
 * packed 6-bit character encoding, terminated strings, and string double-encode.
 */
public class TestStringEncodingDepth {

    // ========================================================================
    // Null-padded strings: trailing spaces must survive roundtrip
    // (C++ "null-padded string preserves trailing spaces on roundtrip")
    // ========================================================================

    @Test
    @DisplayName("Null-padded: preserves trailing spaces")
    void nullPaddedPreservesTrailingSpaces() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.name = "Hello   ";  // 8 chars, 3 trailing spaces

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("Hello   ", decoded.name);
    }

    @Test
    @DisplayName("Null-padded: preserves single trailing space")
    void nullPaddedPreservesSingleTrailingSpace() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.name = "Test ";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("Test ", decoded.name);
    }

    @Test
    @DisplayName("Null-padded: without trailing spaces roundtrips correctly")
    void nullPaddedWithoutTrailingSpaces() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.name = "Hello";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("Hello", decoded.name);
    }

    @Test
    @DisplayName("Null-padded: bounded-str preserves trailing spaces")
    void boundedStrPreservesTrailingSpaces() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.bounded = "Data   ";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("Data   ", decoded.bounded);
    }

    // ========================================================================
    // Space-padded strings
    // (C++ "space-padded string preserves embedded trailing null")
    // ========================================================================

    @Test
    @DisplayName("Space-padded: basic roundtrip")
    void spacePaddedBasicRoundtrip() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.label = "Test";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("Test", decoded.label);
    }

    // ========================================================================
    // Packed 6-bit character encoding: lowercase → uppercase
    // (C++ "packed 6-bit encoding converts lowercase to uppercase")
    // ========================================================================

    @Test
    @DisplayName("Packed 6-bit: lowercase converts to uppercase")
    void packed6BitLowercaseToUppercase() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.packed = "abcd";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        // 6-bit IA-5 standard converts lowercase to uppercase
        assertTrue(decoded.packed.startsWith("ABCD"),
            "Lowercase input should be converted to uppercase, got: " + decoded.packed);
    }

    @Test
    @DisplayName("Packed 6-bit: mixed case normalizes to uppercase")
    void packed6BitMixedCase() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.packed = "AbCd";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals('A', decoded.packed.charAt(0));
        assertEquals('B', decoded.packed.charAt(1));
        assertEquals('C', decoded.packed.charAt(2));
        assertEquals('D', decoded.packed.charAt(3));
    }

    @Test
    @DisplayName("Packed 6-bit: uppercase roundtrips correctly")
    void packed6BitUppercaseRoundtrip() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 0;
        msg.packed = "ABCD1234";

        byte[] encoded = msg.encodeBytes();
        string_features.StringMsg decoded = string_features.StringMsg.decodeBytes(encoded);
        assertEquals("ABCD1234", decoded.packed);
    }

    // ========================================================================
    // Terminated strings
    // (C++ "null-terminated string preserves trailing spaces")
    // ========================================================================

    @Test
    @DisplayName("Null-terminated: preserves trailing space")
    void nullTerminatedPreservesTrailingSpace() {
        string_features.TermStringMsg msg = new string_features.TermStringMsg();
        msg.id = 1;
        msg.nullTerm = "Hello ";
        msg.newlineTerm = "World";
        msg.crlfTerm = "Test";

        byte[] encoded = msg.encodeBytes();
        string_features.TermStringMsg decoded = string_features.TermStringMsg.decodeBytes(encoded);
        assertEquals("Hello ", decoded.nullTerm);
    }

    @Test
    @DisplayName("Newline-terminated: preserves trailing spaces")
    void newlineTerminatedPreservesTrailingSpaces() {
        string_features.TermStringMsg msg = new string_features.TermStringMsg();
        msg.id = 2;
        msg.nullTerm = "A";
        msg.newlineTerm = "Data   ";
        msg.crlfTerm = "B";

        byte[] encoded = msg.encodeBytes();
        string_features.TermStringMsg decoded = string_features.TermStringMsg.decodeBytes(encoded);
        assertEquals("Data   ", decoded.newlineTerm);
    }

    // ========================================================================
    // String double-encode idempotency
    // ========================================================================

    @Test
    @DisplayName("StringMsg: double-encode produces identical bytes")
    void stringMsgDoubleEncode() {
        string_features.StringMsg msg = new string_features.StringMsg();
        msg.id = 42;
        msg.name = "Hello";
        msg.label = "Test";
        msg.bounded = "BoundedData";
        msg.packed = "ABCD1234";

        byte[] first = msg.encodeBytes();
        byte[] second = msg.encodeBytes();
        assertArrayEquals(first, second);
    }
}
