"""Roundtrip encode/decode tests for EBCDIC string generated Python module.

Covers:
- ebcdic_test: EbcdicMsg with id (uint8), label (EBCDIC string, 8 bytes,
  space-padded, right-trimmed), trailer (uint16)
"""
import pytest


# ============================================================================
# ebcdic_test: EbcdicMsg roundtrip tests
# ============================================================================


class TestEbcdicMsgBasicRoundtrip:
    """Basic encode/decode roundtrip for EBCDIC string fields."""

    def test_simple_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0x42
        msg.label = "HELLO"
        msg.trailer = 0x1234

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.id == 0x42
        assert msg2.label.rstrip() == "HELLO"
        assert msg2.trailer == 0x1234

    def test_full_length_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0x01
        msg.label = "ABCDEFGH"  # exactly 8 chars
        msg.trailer = 0xFFFF

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label == "ABCDEFGH"
        assert msg2.trailer == 0xFFFF

    def test_single_char_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0x02
        msg.label = "X"
        msg.trailer = 100

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label.rstrip() == "X"
        assert msg2.trailer == 100


class TestEbcdicMsgEmptyLabel:
    """Empty label should be space-padded and trimmed back to empty."""

    def test_empty_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0x03
        msg.label = ""
        msg.trailer = 0

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label.rstrip() == ""
        assert msg2.id == 0x03
        assert msg2.trailer == 0


class TestEbcdicMsgBoundaryValues:
    """Boundary values for surrounding numeric fields."""

    def test_id_max(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0xFF
        msg.label = "TEST"
        msg.trailer = 0

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.id == 0xFF

    def test_id_zero(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0
        msg.label = "TEST"
        msg.trailer = 0xFFFF

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.id == 0
        assert msg2.trailer == 0xFFFF

    def test_trailer_max(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 1
        msg.label = "MAX"
        msg.trailer = 0xFFFF

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.trailer == 0xFFFF


class TestEbcdicMsgWireFormat:
    """Verify wire format size: 1 (id) + 8 (label) + 2 (trailer) = 11 bytes."""

    def test_wire_size(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0x10
        msg.label = "SIZE"
        msg.trailer = 0x20

        data = msg.encode_bytes()
        assert len(data) == 11  # 1 + 8 + 2


class TestEbcdicMsgDoubleEncode:
    """Encode -> decode -> encode should produce identical bytes."""

    def test_double_encode_stability(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0xAB
        msg.label = "STABLE"
        msg.trailer = 0xCDEF

        data1 = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_double_encode_empty_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0
        msg.label = ""
        msg.trailer = 0

        data1 = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_double_encode_full_label(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 0xFF
        msg.label = "ABCDEFGH"
        msg.trailer = 0xFFFF

        data1 = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestEbcdicMsgVariousStrings:
    """Test various string content through EBCDIC encoding."""

    def test_numeric_string(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 1
        msg.label = "12345678"
        msg.trailer = 0

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label == "12345678"

    def test_uppercase_alpha(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 2
        msg.label = "ABCDEFGH"
        msg.trailer = 0

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label == "ABCDEFGH"

    def test_mixed_content(self):
        from ebcdic_test import EbcdicMsg

        msg = EbcdicMsg()
        msg.id = 3
        msg.label = "A1B2C3"
        msg.trailer = 0

        data = msg.encode_bytes()
        msg2 = EbcdicMsg.decode_bytes(data)
        assert msg2.label.rstrip() == "A1B2C3"
