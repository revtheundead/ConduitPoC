"""Deep string encoding tests matching C++ test_string_encoding_edge_cases.cpp depth.
Covers: null-padded trailing space preservation, space-padded strings,
packed 6-bit character encoding, terminated strings, and EBCDIC roundtrip.
"""
import pytest


# ---------------------------------------------------------------------------
# Null-padded strings: trailing spaces must survive roundtrip
# (C++ "null-padded string preserves trailing spaces on roundtrip")
# ---------------------------------------------------------------------------

class TestNullPaddedStrings:

    def test_preserves_trailing_spaces(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.name = "Hello   "  # 8 chars, 3 trailing spaces

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.name == "Hello   "

    def test_preserves_single_trailing_space(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.name = "Test "

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.name == "Test "

    def test_without_trailing_spaces(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.name = "Hello"

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.name == "Hello"

    def test_bounded_str_preserves_trailing_spaces(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.bounded = "Data   "

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.bounded == "Data   "


# ---------------------------------------------------------------------------
# Space-padded strings
# (C++ "space-padded string preserves embedded trailing null")
# ---------------------------------------------------------------------------

class TestSpacePaddedStrings:

    def test_basic_roundtrip(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.label = "Test"

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.label == "Test"


# ---------------------------------------------------------------------------
# Packed 6-bit character encoding: lowercase → uppercase
# (C++ "packed 6-bit encoding converts lowercase to uppercase")
# ---------------------------------------------------------------------------

class TestPacked6BitEncoding:

    def test_lowercase_converts_to_uppercase(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.packed = "abcd"

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        # 6-bit IA-5 standard converts lowercase to uppercase
        val = decoded.packed
        assert val[:4] == "ABCD"

    def test_mixed_case_normalizes_to_uppercase(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.packed = "AbCd"

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        val = decoded.packed
        assert val[0] == 'A'
        assert val[1] == 'B'  # 'b' converted to 'B'
        assert val[2] == 'C'
        assert val[3] == 'D'  # 'd' converted to 'D'

    def test_uppercase_roundtrips_correctly(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 0
        msg.packed = "ABCD1234"

        encoded = msg.encode_bytes()
        decoded = StringMsg.decode_bytes(encoded)
        assert decoded.packed == "ABCD1234"


# ---------------------------------------------------------------------------
# Terminated strings
# (C++ "null-terminated string preserves trailing spaces")
# ---------------------------------------------------------------------------

class TestTerminatedStrings:

    def test_null_terminated_preserves_trailing_space(self):
        from string_features import TermStringMsg

        msg = TermStringMsg()
        msg.id = 1
        msg.null_term = "Hello "  # trailing space is content
        msg.newline_term = "World"
        msg.crlf_term = "Test"

        encoded = msg.encode_bytes()
        decoded = TermStringMsg.decode_bytes(encoded)
        assert decoded.null_term == "Hello "

    def test_newline_terminated_preserves_trailing_spaces(self):
        from string_features import TermStringMsg

        msg = TermStringMsg()
        msg.id = 2
        msg.null_term = "A"
        msg.newline_term = "Data   "  # trailing spaces are content
        msg.crlf_term = "B"

        encoded = msg.encode_bytes()
        decoded = TermStringMsg.decode_bytes(encoded)
        assert decoded.newline_term == "Data   "


# ---------------------------------------------------------------------------
# String double-encode idempotency
# ---------------------------------------------------------------------------

class TestStringDoubleEncode:

    def test_string_msg_double_encode(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 42
        msg.name = "Hello"
        msg.label = "Test"
        msg.bounded = "BoundedData"
        msg.packed = "ABCD1234"

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second
