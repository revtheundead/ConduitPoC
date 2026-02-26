"""Tests for specific wire format byte patterns in the Conduit generated Python codecs.

Each test verifies that encoding produces the exact expected byte sequence, and that
decoding from a known byte sequence yields the exact expected field values.
"""
import struct
import pytest

from all_types import AllTypesMessage, BitReader, BitWriter
from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags


# ---------------------------------------------------------------------------
# PingBody wire format
# ---------------------------------------------------------------------------
class TestPingBodyWireFormat:

    def test_timestamp_0x12345678(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 0x12345678
        data = ping.encode_bytes()
        assert data == bytes([0x12, 0x34, 0x56, 0x78])

    def test_timestamp_zero(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 0
        data = ping.encode_bytes()
        assert data == bytes([0x00, 0x00, 0x00, 0x00])

    def test_timestamp_max(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 0xFFFFFFFF
        data = ping.encode_bytes()
        assert data == bytes([0xFF, 0xFF, 0xFF, 0xFF])

    def test_timestamp_decode_from_known_bytes(self):
        from session_protocol import PingBody

        data = bytes([0xAB, 0xCD, 0xEF, 0x01])
        ping = PingBody.decode_bytes(data)
        assert ping.timestamp == 0xABCDEF01

    def test_wire_size(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 0
        data = ping.encode_bytes()
        assert len(data) == 4


# ---------------------------------------------------------------------------
# DataBody wire format
# ---------------------------------------------------------------------------
class TestDataBodyWireFormat:

    def test_exact_bytes(self):
        from session_protocol import DataBody

        db = DataBody()
        db.channel = 0x05
        db.payload_a = 0xAABBCCDD
        db.payload_b = 0x11223344
        data = db.encode_bytes()
        expected = bytes([0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44])
        assert data == expected

    def test_all_zeros(self):
        from session_protocol import DataBody

        db = DataBody()
        data = db.encode_bytes()
        assert data == bytes(9)

    def test_wire_size(self):
        from session_protocol import DataBody

        db = DataBody()
        data = db.encode_bytes()
        assert len(data) == 9

    def test_decode_from_known_bytes(self):
        from session_protocol import DataBody

        data = bytes([0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02])
        db = DataBody.decode_bytes(data)
        assert db.channel == 0xFF
        assert db.payload_a == 0x00000001
        assert db.payload_b == 0x00000002


# ---------------------------------------------------------------------------
# AckBody wire format
# ---------------------------------------------------------------------------
class TestAckBodyWireFormat:

    def test_exact_bytes(self):
        from session_protocol import AckBody

        ack = AckBody()
        ack.acked_seq = 0x1234
        data = ack.encode_bytes()
        assert data == bytes([0x12, 0x34])

    def test_wire_size(self):
        from session_protocol import AckBody

        ack = AckBody()
        data = ack.encode_bytes()
        assert len(data) == 2


# ---------------------------------------------------------------------------
# Big-endian multi-byte field verification
# ---------------------------------------------------------------------------
class TestBigEndianWireFormat:

    def test_u16_be_0xABCD(self):
        w = BitWriter()
        w.write_u16(0xABCD, True)
        data = w.to_bytes()
        assert data == bytes([0xAB, 0xCD])

    def test_u32_be_0xDEADBEEF(self):
        w = BitWriter()
        w.write_u32(0xDEADBEEF, True)
        data = w.to_bytes()
        assert data == bytes([0xDE, 0xAD, 0xBE, 0xEF])

    def test_u64_be(self):
        w = BitWriter()
        w.write_u64(0x0102030405060708, True)
        data = w.to_bytes()
        assert data == bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08])

    def test_decode_u16_be(self):
        r = BitReader(bytes([0x12, 0x34]))
        assert r.read_u16(True) == 0x1234

    def test_decode_u32_be(self):
        r = BitReader(bytes([0xCA, 0xFE, 0xBA, 0xBE]))
        assert r.read_u32(True) == 0xCAFEBABE


# ---------------------------------------------------------------------------
# Little-endian byte order verification
# ---------------------------------------------------------------------------
class TestLittleEndianWireFormat:

    def test_u16_le_0x1234(self):
        w = BitWriter()
        w.write_u16(0x1234, False)
        data = w.to_bytes()
        assert data == bytes([0x34, 0x12])

    def test_u32_le_0x12345678(self):
        w = BitWriter()
        w.write_u32(0x12345678, False)
        data = w.to_bytes()
        assert data == bytes([0x78, 0x56, 0x34, 0x12])

    def test_decode_u16_le(self):
        r = BitReader(bytes([0x34, 0x12]))
        assert r.read_u16(False) == 0x1234

    def test_decode_u32_le(self):
        r = BitReader(bytes([0x78, 0x56, 0x34, 0x12]))
        assert r.read_u32(False) == 0x12345678

    def test_le_vs_be_different_wire(self):
        """Same value should produce different wire bytes for LE vs BE."""
        value = 0x1234
        w_be = BitWriter()
        w_be.write_u16(value, True)
        w_le = BitWriter()
        w_le.write_u16(value, False)
        assert w_be.to_bytes() != w_le.to_bytes()
        assert w_be.to_bytes() == bytes([0x12, 0x34])
        assert w_le.to_bytes() == bytes([0x34, 0x12])


# ---------------------------------------------------------------------------
# Mixed endian wire format
# ---------------------------------------------------------------------------
class TestMixedEndianWireFormat:

    def test_mixed_msg_exact_bytes(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0x1234
        msg.le16 = 0x5678
        msg.be32 = 0xDEADBEEF
        msg.le32 = 0xCAFEBABE
        data = msg.encode_bytes()

        expected = bytes([
            0x12, 0x34,                         # be16
            0x78, 0x56,                         # le16
            0xDE, 0xAD, 0xBE, 0xEF,            # be32
            0xBE, 0xBA, 0xFE, 0xCA,            # le32
        ])
        assert data == expected

    def test_mixed_msg_wire_size(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        data = msg.encode_bytes()
        assert len(data) == 12

    def test_mixed_msg_decode_known_bytes(self):
        from mixed_endian import MixedMsg

        data = bytes([
            0xAB, 0xCD,              # be16 = 0xABCD
            0x34, 0x12,              # le16 = 0x1234
            0x00, 0x00, 0x00, 0x01,  # be32 = 1
            0x02, 0x00, 0x00, 0x00,  # le32 = 2
        ])
        msg = MixedMsg.decode_bytes(data)
        assert msg.be16 == 0xABCD
        assert msg.le16 == 0x1234
        assert msg.be32 == 1
        assert msg.le32 == 2


# ---------------------------------------------------------------------------
# Bool encoding
# ---------------------------------------------------------------------------
class TestBoolWireFormat:

    def test_true_encodes_to_0x01(self):
        w = BitWriter()
        w.write_bits(1 if True else 0, 8)
        data = w.to_bytes()
        assert data == bytes([0x01])

    def test_false_encodes_to_0x00(self):
        w = BitWriter()
        w.write_bits(1 if False else 0, 8)
        data = w.to_bytes()
        assert data == bytes([0x00])

    def test_nonzero_decodes_as_true(self):
        for val in [1, 2, 127, 255]:
            r = BitReader(bytes([val]))
            assert (r.read_u8() != 0) is True

    def test_zero_decodes_as_false(self):
        r = BitReader(bytes([0]))
        assert (r.read_u8() != 0) is False


# ---------------------------------------------------------------------------
# String padding verification
# ---------------------------------------------------------------------------
class TestStringPaddingWireFormat:

    def test_ascii_null_padding(self):
        """AsciiStr 'Hi' in 10-byte field: 'H','i',0x00*8."""
        s = AsciiStr("Hi")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 10
        assert data[0] == ord("H")
        assert data[1] == ord("i")
        for i in range(2, 10):
            assert data[i] == 0x00, f"byte {i} should be 0x00, got {data[i]:#x}"

    def test_ascii_empty_all_nulls(self):
        s = AsciiStr("")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert data == bytes(10)

    def test_utf8_space_padding(self):
        """Utf8Str 'AB' in 16-byte field: 'A','B',0x20*14."""
        s = Utf8Str("AB")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 16
        assert data[0] == ord("A")
        assert data[1] == ord("B")
        for i in range(2, 16):
            assert data[i] == 0x20, f"byte {i} should be 0x20, got {data[i]:#x}"

    def test_utf8_empty_all_spaces(self):
        s = Utf8Str("")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert data == bytes([0x20] * 16)

    def test_ascii_exact_length_no_padding(self):
        """Exactly 10 characters fill the field with no padding."""
        s = AsciiStr("0123456789")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert data == b"0123456789"


# ---------------------------------------------------------------------------
# String features wire format
# ---------------------------------------------------------------------------
class TestStringFeaturesWireFormat:

    def test_name_str_null_padded(self):
        from string_features.types import NameStr

        s = NameStr("abc")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 20
        assert data[:3] == b"abc"
        assert data[3:] == bytes(17)

    def test_label_str_space_padded(self):
        from string_features.types import LabelStr

        s = LabelStr("XY")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 16
        assert data[:2] == b"XY"
        assert data[2:] == b" " * 14

    def test_bounded_str_null_padded(self):
        from string_features.types import BoundedStr

        s = BoundedStr("test")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 32
        assert data[:4] == b"test"
        assert data[4:] == bytes(28)

    def test_packed_str_null_padded(self):
        from string_features.types import PackedStr

        s = PackedStr("PK")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 8
        assert data[:2] == b"PK"
        assert data[2:] == bytes(6)


# ---------------------------------------------------------------------------
# ColorEnum wire format
# ---------------------------------------------------------------------------
class TestColorEnumWireFormat:

    def test_red_encodes_to_0x01(self):
        w = BitWriter()
        ColorEnum.RED.encode(w)
        assert w.to_bytes() == bytes([0x01])

    def test_green_encodes_to_0x02(self):
        w = BitWriter()
        ColorEnum.GREEN.encode(w)
        assert w.to_bytes() == bytes([0x02])

    def test_blue_encodes_to_0x03(self):
        w = BitWriter()
        ColorEnum.BLUE.encode(w)
        assert w.to_bytes() == bytes([0x03])

    def test_decode_0x01_is_red(self):
        r = BitReader(bytes([0x01]))
        assert ColorEnum.decode(r) == ColorEnum.RED

    def test_decode_0x02_is_green(self):
        r = BitReader(bytes([0x02]))
        assert ColorEnum.decode(r) == ColorEnum.GREEN

    def test_decode_0x03_is_blue(self):
        r = BitReader(bytes([0x03]))
        assert ColorEnum.decode(r) == ColorEnum.BLUE


# ---------------------------------------------------------------------------
# StatusFlags wire format
# ---------------------------------------------------------------------------
class TestStatusFlagsWireFormat:

    def test_no_flags_is_0x00(self):
        sf = StatusFlags(0)
        w = BitWriter()
        sf.encode(w)
        assert w.to_bytes() == bytes([0x00])

    def test_active_only_is_0x01(self):
        sf = StatusFlags(0b00000001)
        w = BitWriter()
        sf.encode(w)
        assert w.to_bytes() == bytes([0x01])

    def test_error_only_is_0x02(self):
        sf = StatusFlags(0b00000010)
        w = BitWriter()
        sf.encode(w)
        assert w.to_bytes() == bytes([0x02])

    def test_ready_only_is_0x04(self):
        sf = StatusFlags(0b00000100)
        w = BitWriter()
        sf.encode(w)
        assert w.to_bytes() == bytes([0x04])

    def test_all_flags_is_0x07(self):
        sf = StatusFlags(0b00000111)
        w = BitWriter()
        sf.encode(w)
        assert w.to_bytes() == bytes([0x07])

    def test_decode_0x05_active_and_ready(self):
        r = BitReader(bytes([0x05]))
        sf = StatusFlags.decode(r)
        assert sf.active is True
        assert sf.error is False
        assert sf.ready is True


# ---------------------------------------------------------------------------
# ScaledTemp wire format
# ---------------------------------------------------------------------------
class TestScaledTempWireFormat:

    def test_raw_0_encodes_to_zero_bytes(self):
        t = ScaledTemp(0)
        w = BitWriter()
        t.encode(w)
        assert w.to_bytes() == bytes([0x00, 0x00])

    def test_raw_4000_wire_bytes(self):
        t = ScaledTemp(4000)
        w = BitWriter()
        t.encode(w)
        data = w.to_bytes()
        # 4000 = 0x0FA0
        assert data == bytes([0x0F, 0xA0])

    def test_decode_from_known_bytes(self):
        r = BitReader(bytes([0x0F, 0xA0]))
        t = ScaledTemp.decode(r)
        assert t.raw == 4000
        assert t.value == pytest.approx(0.0, abs=1e-10)

    def test_raw_ffff_wire_bytes(self):
        t = ScaledTemp(0xFFFF)
        w = BitWriter()
        t.encode(w)
        assert w.to_bytes() == bytes([0xFF, 0xFF])


# ---------------------------------------------------------------------------
# Float32 wire format
# ---------------------------------------------------------------------------
class TestFloat32WireFormat:

    def test_float_zero_wire(self):
        w = BitWriter()
        w.write_f32(0.0, True)
        assert w.to_bytes() == bytes(4)

    def test_float_one_wire(self):
        w = BitWriter()
        w.write_f32(1.0, True)
        data = w.to_bytes()
        assert data == struct.pack(">f", 1.0)

    def test_float_negative_wire(self):
        w = BitWriter()
        w.write_f32(-1.5, True)
        data = w.to_bytes()
        assert data == struct.pack(">f", -1.5)

    def test_decode_float_from_known(self):
        known = struct.pack(">f", 3.14)
        r = BitReader(known)
        val = r.read_f32(True)
        assert val == pytest.approx(3.14, rel=1e-5)


# ---------------------------------------------------------------------------
# Float64 wire format
# ---------------------------------------------------------------------------
class TestFloat64WireFormat:

    def test_double_zero_wire(self):
        w = BitWriter()
        w.write_f64(0.0, True)
        assert w.to_bytes() == bytes(8)

    def test_double_pi_wire(self):
        import math
        w = BitWriter()
        w.write_f64(math.pi, True)
        data = w.to_bytes()
        assert data == struct.pack(">d", math.pi)

    def test_decode_double_from_known(self):
        known = struct.pack(">d", -2.718281828)
        r = BitReader(known)
        val = r.read_f64(True)
        assert val == pytest.approx(-2.718281828, rel=1e-9)


# ---------------------------------------------------------------------------
# Signed integer wire format
# ---------------------------------------------------------------------------
class TestSignedIntWireFormat:

    def test_i8_minus_one(self):
        w = BitWriter()
        w.write_signed_bits(-1, 8)
        assert w.to_bytes() == bytes([0xFF])

    def test_i8_minus_128(self):
        w = BitWriter()
        w.write_signed_bits(-128, 8)
        assert w.to_bytes() == bytes([0x80])

    def test_i16_minus_one(self):
        w = BitWriter()
        w.write_signed_bits(-1, 16)
        assert w.to_bytes() == bytes([0xFF, 0xFF])

    def test_i16_minus_32768(self):
        w = BitWriter()
        w.write_signed_bits(-32768, 16)
        assert w.to_bytes() == bytes([0x80, 0x00])

    def test_i32_minus_one(self):
        w = BitWriter()
        w.write_signed_bits(-1, 32)
        assert w.to_bytes() == bytes([0xFF, 0xFF, 0xFF, 0xFF])

    def test_decode_i8_minus_one(self):
        r = BitReader(bytes([0xFF]))
        assert r.read_signed_bits(8) == -1

    def test_decode_i16_minus_32768(self):
        r = BitReader(bytes([0x80, 0x00]))
        assert r.read_signed_bits(16) == -32768


# ---------------------------------------------------------------------------
# Odd-width field wire format
# ---------------------------------------------------------------------------
class TestOddWidthWireFormat:

    def test_12bit_max(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0xFFF
        msg.u20 = 0xFFFFF
        msg.s12 = -2048
        data = msg.encode_bytes()
        # u12(12 bits) + u20(20 bits) + s12(12 bits) = 44 bits = 5.5 bytes -> 6 bytes
        assert len(data) == 6

    def test_12bit_zero(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0
        msg.u20 = 0
        msg.s12 = 0
        data = msg.encode_bytes()
        assert data == bytes(6)


# ---------------------------------------------------------------------------
# Field scale wire format
# ---------------------------------------------------------------------------
class TestFieldScaleWireFormat:

    def test_scale_msg_wire_size(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 0.0
        msg.temp = -40.0
        msg.plain = 0
        data = msg.encode_bytes()
        # fl: 12 bits + temp: 16 bits + plain: 8 bits = 36 bits = 4.5 -> 5 bytes
        assert len(data) == 5

    def test_scale_msg_known_values(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 2.5    # raw = 10
        msg.temp = 25.0  # raw = 6500 = 0x1964
        msg.plain = 0x63  # 99

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(2.5)
        assert msg2.temp == pytest.approx(25.0)
        assert msg2.plain == 0x63


# ---------------------------------------------------------------------------
# Point struct wire format
# ---------------------------------------------------------------------------
class TestPointWireFormat:

    def test_exact_bytes(self):
        from arrays_choices.structs import Point

        p = Point()
        p.x = 0x0100
        p.y = 0x0200
        data = p.encode_bytes()
        assert data == bytes([0x01, 0x00, 0x02, 0x00])

    def test_wire_size(self):
        from arrays_choices.structs import Point

        p = Point()
        data = p.encode_bytes()
        assert len(data) == 4


# ---------------------------------------------------------------------------
# FixedArrayMsg wire format
# ---------------------------------------------------------------------------
class TestFixedArrayMsgWireFormat:

    def test_three_points_exact_bytes(self):
        from arrays_choices.messages import FixedArrayMsg
        from arrays_choices.structs import Point

        msg = FixedArrayMsg()
        for i in range(3):
            p = Point()
            p.x = i + 1
            p.y = (i + 1) * 256
            msg.points.append(p)

        data = msg.encode_bytes()
        expected = bytes([
            0x00, 0x01, 0x01, 0x00,  # Point(1, 256)
            0x00, 0x02, 0x02, 0x00,  # Point(2, 512)
            0x00, 0x03, 0x03, 0x00,  # Point(3, 768)
        ])
        assert data == expected


# ---------------------------------------------------------------------------
# ChoiceMsg wire format
# ---------------------------------------------------------------------------
class TestChoiceMsgWireFormat:

    def test_type_a_wire_format(self):
        from arrays_choices.messages import ChoiceMsg
        from arrays_choices.structs import TypeABody, SubX
        from arrays_choices import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A  # 1
        msg.length = 0x0005
        body = TypeABody()
        body.sub_type = Constants.SUB_X  # 10
        body.sub_body = SubX()
        body.sub_body.val = 0x00000001
        msg.body = body

        data = msg.encode_bytes()
        expected = bytes([
            0x01,              # msg_type = 1
            0x00, 0x05,        # length = 5
            0x0A,              # sub_type = 10
            0x00, 0x00, 0x00, 0x01,  # SubX val = 1
        ])
        assert data == expected


# ---------------------------------------------------------------------------
# AlphaBody / BetaBody wire format
# ---------------------------------------------------------------------------
class TestChoiceProtocolWireFormat:

    def test_alpha_body_wire(self):
        from choice_protocol import AlphaBody

        alpha = AlphaBody()
        alpha.x = 0x0100
        alpha.y = 0x0200
        data = alpha.encode_bytes()
        assert data == bytes([0x01, 0x00, 0x02, 0x00])
        assert len(data) == 4

    def test_beta_body_wire(self):
        from choice_protocol import BetaBody

        beta = BetaBody()
        beta.payload_size = 0x0A
        beta.tag = 0xDEAD0000
        data = beta.encode_bytes()
        assert data == bytes([0x0A, 0xDE, 0xAD, 0x00, 0x00])
        assert len(data) == 5


# ---------------------------------------------------------------------------
# BCD wire encoding
# ---------------------------------------------------------------------------
class TestBCDWireFormat:

    def test_bcd_encode_1234(self):
        """BCD encode of 1234 into 16 bits."""
        w = BitWriter()
        w.write_bcd(1234, 16)
        data = w.to_bytes()
        r = BitReader(data)
        val = r.read_bcd(16)
        assert val == 1234

    def test_bcd_signed_negative(self):
        """BCD signed encode of -90 into 13 bits."""
        w = BitWriter()
        w.write_bcd_signed(-90, 13)
        data = w.to_bytes()
        r = BitReader(data)
        val = r.read_bcd_signed(13)
        assert val == -90

    def test_sign_magnitude_negative(self):
        """Sign-magnitude encode of -500 into 16 bits."""
        w = BitWriter()
        w.write_sign_magnitude(-500, 16)
        data = w.to_bytes()
        r = BitReader(data)
        val = r.read_sign_magnitude(16)
        assert val == -500

    def test_sign_magnitude_positive(self):
        w = BitWriter()
        w.write_sign_magnitude(1000, 16)
        data = w.to_bytes()
        r = BitReader(data)
        val = r.read_sign_magnitude(16)
        assert val == 1000

    def test_sign_magnitude_zero(self):
        w = BitWriter()
        w.write_sign_magnitude(0, 16)
        data = w.to_bytes()
        r = BitReader(data)
        val = r.read_sign_magnitude(16)
        assert val == 0


# ---------------------------------------------------------------------------
# Wire encoding message wire format
# ---------------------------------------------------------------------------
class TestWireEncodingMsgWireFormat:

    def test_wire_size(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        # bcd_alt(16) + bcd_hdg(13) + sm_offset(16) + cb2_val(16) +
        # bnr_val(16) + inline_bcd(12) + inline_bnrs(16) = 105 bits
        # However the actual encoded size depends on bit-level alignment
        data = msg.encode_bytes()
        # 105 bits = 13.125 bytes -> 14 bytes
        assert len(data) == 14

    def test_all_zeros(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 0
        assert msg2.bcd_hdg == 0
        assert msg2.sm_offset == 0
        assert msg2.cb2_val == 0
        assert msg2.bnr_val == 0
        assert msg2.inline_bcd == 0
        assert msg2.inline_bnrs == 0


# ---------------------------------------------------------------------------
# Bit-level operations
# ---------------------------------------------------------------------------
class TestBitLevelWireFormat:

    def test_single_bit_write_read(self):
        w = BitWriter()
        w.write_bits(1, 1)
        w.write_bits(0, 1)
        w.write_bits(1, 1)
        w.write_bits(0, 5)
        data = w.to_bytes()
        assert data == bytes([0b10100000])

    def test_3bit_field(self):
        w = BitWriter()
        w.write_bits(0b101, 3)
        w.write_bits(0b010, 3)
        w.write_bits(0, 2)
        data = w.to_bytes()
        assert data == bytes([0b10101000])

    def test_mixed_bit_widths(self):
        """1-bit flag + 3-bit small + 4-bit padding = 1 byte."""
        w = BitWriter()
        w.write_bits(1, 1)    # flag = 1
        w.write_bits(5, 3)    # small = 5 (101)
        w.write_bits(0, 4)    # padding
        data = w.to_bytes()
        # 1_101_0000 = 0xD0
        assert data == bytes([0xD0])

    def test_read_mixed_bit_widths(self):
        r = BitReader(bytes([0xD0]))
        assert r.read_bits(1) == 1
        assert r.read_bits(3) == 5
        assert r.read_bits(4) == 0


# ---------------------------------------------------------------------------
# Boundary types wire format
# ---------------------------------------------------------------------------
class TestBoundaryMsgWireFormat:

    def test_wire_size_with_max_values(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp as BScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 7
        msg.medium = 127
        msg.byte_val = 255
        msg.word = 0xFFFF
        msg.dword = 0xFFFFFFFF
        msg.qword = 0xFFFFFFFFFFFFFFFF
        msg.signed_byte = -1
        msg.signed_word = -1
        msg.signed_dword = -1
        msg.signed_qword = -1
        msg.temp = BScaledTemp(0xFFFF)
        msg.level = NybbleEnum.HIGH
        data = msg.encode_bytes()
        # 1+3+7+8+16+32+64+8+16+32+64+16+4 = 271 bits = 33.875 -> 34 bytes
        assert len(data) == 34

    def test_all_zeros_wire_size(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp as BScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 0
        msg.small = 0
        msg.medium = 0
        msg.byte_val = 0
        msg.word = 0
        msg.dword = 0
        msg.qword = 0
        msg.signed_byte = 0
        msg.signed_word = 0
        msg.signed_dword = 0
        msg.signed_qword = 0
        msg.temp = BScaledTemp(0)
        msg.level = NybbleEnum.OFF
        data = msg.encode_bytes()
        assert len(data) == 34
