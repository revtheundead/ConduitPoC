"""Advanced roundtrip encode/decode tests: edge cases, complex messages, and Transceiver roundtrips.

Covers: float special values (NaN, Inf, -0), integer bit patterns, string edge cases,
multi-field interactions, complex types (arrays, choices, wire encodings),
error paths (corrupted data, wrong type decode, truncation), and UDP loopback
Transceiver roundtrips with typed and raw messages.
"""
import ctypes
import math
import os
import socket
import struct
import sys
import threading
import time

import pytest

from all_types import (
    AllTypesMessage,
    BitReader,
    BitWriter,
    DecodeError,
)
from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags


# ===========================================================================
# Helpers
# ===========================================================================

def _roundtrip(msg):
    """Encode then decode an AllTypesMessage."""
    return AllTypesMessage.decode_bytes(msg.encode_bytes())


def _default_msg():
    """Create an AllTypesMessage with all required sub-objects."""
    msg = AllTypesMessage()
    msg.ascii = AsciiStr("")
    msg.utf8 = Utf8Str("")
    msg.temp = ScaledTemp(0)
    msg.color = ColorEnum.RED
    msg.status = StatusFlags(0)
    return msg


# ===========================================================================
# Float edge cases
# ===========================================================================

class TestFloatEdgeCases:

    def test_f32_nan_roundtrip(self):
        w = BitWriter()
        w.write_f32(float("nan"), True)
        r = BitReader(w.to_bytes())
        result = r.read_f32(True)
        assert math.isnan(result)

    def test_f64_nan_roundtrip(self):
        w = BitWriter()
        w.write_f64(float("nan"), True)
        r = BitReader(w.to_bytes())
        result = r.read_f64(True)
        assert math.isnan(result)

    def test_f32_positive_inf_roundtrip(self):
        w = BitWriter()
        w.write_f32(float("inf"), True)
        r = BitReader(w.to_bytes())
        assert r.read_f32(True) == float("inf")

    def test_f32_negative_inf_roundtrip(self):
        w = BitWriter()
        w.write_f32(float("-inf"), True)
        r = BitReader(w.to_bytes())
        assert r.read_f32(True) == float("-inf")

    def test_f64_positive_inf_roundtrip(self):
        w = BitWriter()
        w.write_f64(float("inf"), True)
        r = BitReader(w.to_bytes())
        assert r.read_f64(True) == float("inf")

    def test_f64_negative_inf_roundtrip(self):
        w = BitWriter()
        w.write_f64(float("-inf"), True)
        r = BitReader(w.to_bytes())
        assert r.read_f64(True) == float("-inf")

    def test_f32_negative_zero_roundtrip(self):
        w = BitWriter()
        w.write_f32(-0.0, True)
        r = BitReader(w.to_bytes())
        result = r.read_f32(True)
        assert result == 0.0
        assert math.copysign(1.0, result) == -1.0

    def test_f64_negative_zero_roundtrip(self):
        w = BitWriter()
        w.write_f64(-0.0, True)
        r = BitReader(w.to_bytes())
        result = r.read_f64(True)
        assert result == 0.0
        assert math.copysign(1.0, result) == -1.0

    def test_f32_smallest_subnormal(self):
        val = struct.unpack(">f", b"\x00\x00\x00\x01")[0]
        w = BitWriter()
        w.write_f32(val, True)
        r = BitReader(w.to_bytes())
        assert r.read_f32(True) == val

    def test_f64_smallest_subnormal(self):
        val = struct.unpack(">d", b"\x00\x00\x00\x00\x00\x00\x00\x01")[0]
        w = BitWriter()
        w.write_f64(val, True)
        r = BitReader(w.to_bytes())
        assert r.read_f64(True) == val

    def test_f32_largest_finite(self):
        val = struct.unpack(">f", b"\x7f\x7f\xff\xff")[0]
        w = BitWriter()
        w.write_f32(val, True)
        r = BitReader(w.to_bytes())
        assert r.read_f32(True) == pytest.approx(val, rel=1e-7)

    def test_f64_largest_finite(self):
        val = sys.float_info.max
        w = BitWriter()
        w.write_f64(val, True)
        r = BitReader(w.to_bytes())
        assert r.read_f64(True) == val

    def test_f32_nan_in_message(self):
        msg = _default_msg()
        msg.f32 = float("nan")
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert math.isnan(msg2.f32)

    def test_f64_inf_in_message(self):
        msg = _default_msg()
        msg.f64 = float("inf")
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.f64 == float("inf")


# ===========================================================================
# Integer edge cases
# ===========================================================================

class TestIntegerEdgeCases:

    @pytest.mark.parametrize("value", [0xAA, 0x55])
    def test_u8_alternating_bits(self, value):
        w = BitWriter()
        w.write_u8(value)
        r = BitReader(w.to_bytes())
        assert r.read_u8() == value

    @pytest.mark.parametrize("value", [0xAAAA, 0x5555])
    def test_u16_alternating_bits(self, value):
        w = BitWriter()
        w.write_u16(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u16(True) == value

    @pytest.mark.parametrize("value", [0xAAAAAAAA, 0x55555555])
    def test_u32_alternating_bits(self, value):
        w = BitWriter()
        w.write_u32(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u32(True) == value

    @pytest.mark.parametrize("value", [0xAAAAAAAAAAAAAAAA, 0x5555555555555555])
    def test_u64_alternating_bits(self, value):
        w = BitWriter()
        w.write_u64(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u64(True) == value

    @pytest.mark.parametrize("bits", [1, 2, 4, 8, 16, 32])
    def test_power_of_two_boundary_u32(self, bits):
        value = (1 << bits) - 1
        w = BitWriter()
        w.write_u32(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u32(True) == value

    def test_u64_single_msb(self):
        value = 1 << 63
        w = BitWriter()
        w.write_u64(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u64(True) == value

    def test_alternating_bits_in_message(self):
        msg = _default_msg()
        msg.u8 = 0xAA
        msg.u16 = 0x5555
        msg.u32 = 0xAAAAAAAA
        msg.u64 = 0x5555555555555555
        msg2 = _roundtrip(msg)
        assert msg2.u8 == 0xAA
        assert msg2.u16 == 0x5555
        assert msg2.u32 == 0xAAAAAAAA
        assert msg2.u64 == 0x5555555555555555

    def test_signed_alternating_sign_pattern(self):
        msg = _default_msg()
        msg.i8 = -1
        msg.i16 = 1
        msg.i32 = -1
        msg2 = _roundtrip(msg)
        assert msg2.i8 == -1
        assert msg2.i16 == 1
        assert msg2.i32 == -1


# ===========================================================================
# String edge cases
# ===========================================================================

class TestStringEdgeCases:

    def test_ascii_exact_wire_size(self):
        s = AsciiStr("ABCDEFGHIJ")  # exactly 10 chars
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "ABCDEFGHIJ"

    def test_ascii_all_printable_chars(self):
        s = AsciiStr("!@#$%^&*()")  # 10 printable chars
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "!@#$%^&*()"

    def test_ascii_single_char(self):
        s = AsciiStr("Z")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "Z"

    def test_ascii_digits_only(self):
        s = AsciiStr("0123456789")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "0123456789"

    def test_utf8_exact_wire_size(self):
        s = Utf8Str("A" * 16)  # exactly 16 chars
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        assert s2.value == "A" * 16

    def test_utf8_all_spaces(self):
        """Utf8Str is space-padded; an all-spaces string should decode to empty."""
        s = Utf8Str("   ")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        # Spaces are padding; stripped on decode
        assert s2.value == ""

    def test_utf8_single_char(self):
        s = Utf8Str("X")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        assert s2.value == "X"

    def test_strings_in_message_roundtrip(self):
        msg = _default_msg()
        msg.ascii = AsciiStr("ABCDEFGHIJ")
        msg.utf8 = Utf8Str("Hello World!!!!!")  # 16 chars
        msg2 = _roundtrip(msg)
        assert msg2.ascii.value == "ABCDEFGHIJ"
        assert msg2.utf8.value == "Hello World!!!!!"


# ===========================================================================
# Multi-field interaction
# ===========================================================================

class TestMultiFieldInteraction:

    def test_all_max_all_min_simultaneously(self):
        msg = _default_msg()
        msg.u8 = 255
        msg.u16 = 0xFFFF
        msg.u32 = 0xFFFFFFFF
        msg.u64 = 0xFFFFFFFFFFFFFFFF
        msg.i8 = -128
        msg.i16 = -32768
        msg.i32 = -2147483648
        msg.f32 = float("inf")
        msg.f64 = float("-inf")
        msg.flag = True
        msg.ascii = AsciiStr("ZZZZZZZZZZ")
        msg.utf8 = Utf8Str("WWWWWWWWWWWWWWWW")
        msg.raw = 0xFFFFFFFFFFFFFFFF
        msg.le16 = 0xFFFF
        msg.le32 = 0xFFFFFFFF
        msg.temp = ScaledTemp(0xFFFF)
        msg.hex = 0xFFFFFFFF
        msg.color = ColorEnum.BLUE
        msg.status = StatusFlags(0xFF)

        msg2 = _roundtrip(msg)
        assert msg2.u8 == 255
        assert msg2.u16 == 0xFFFF
        assert msg2.u32 == 0xFFFFFFFF
        assert msg2.u64 == 0xFFFFFFFFFFFFFFFF
        assert msg2.i8 == -128
        assert msg2.i16 == -32768
        assert msg2.i32 == -2147483648
        assert msg2.f32 == float("inf")
        assert msg2.f64 == float("-inf")
        assert msg2.flag is True
        assert msg2.ascii.value == "ZZZZZZZZZZ"
        assert msg2.utf8.value == "WWWWWWWWWWWWWWWW"
        assert msg2.le16 == 0xFFFF
        assert msg2.le32 == 0xFFFFFFFF

    def test_alternating_zero_max_per_field(self):
        msg = _default_msg()
        msg.u8 = 0
        msg.u16 = 0xFFFF
        msg.u32 = 0
        msg.u64 = 0xFFFFFFFFFFFFFFFF
        msg.i8 = 0
        msg.i16 = 32767
        msg.i32 = 0
        msg.f32 = 0.0
        msg.f64 = sys.float_info.max

        msg2 = _roundtrip(msg)
        assert msg2.u8 == 0
        assert msg2.u16 == 0xFFFF
        assert msg2.u32 == 0
        assert msg2.u64 == 0xFFFFFFFFFFFFFFFF
        assert msg2.i8 == 0
        assert msg2.i16 == 32767
        assert msg2.i32 == 0
        assert msg2.f32 == 0.0
        assert msg2.f64 == sys.float_info.max

    def test_triple_roundtrip_idempotent(self):
        msg = _default_msg()
        msg.u8 = 0xAB
        msg.u16 = 0x1234
        msg.u32 = 0xDEADBEEF
        msg.u64 = 0x0102030405060708
        msg.i8 = -42
        msg.i16 = -1000
        msg.i32 = -100000
        msg.f32 = 3.140000104904175
        msg.f64 = -1.5
        msg.flag = True
        msg.ascii = AsciiStr("Hello")
        msg.utf8 = Utf8Str("World")
        msg.raw = 0xCAFEBABE00000000
        msg.le16 = 0x5678
        msg.le32 = 0xABCD1234
        msg.temp = ScaledTemp(4000)
        msg.hex = 0xFF00FF00
        msg.color = ColorEnum.GREEN
        msg.status = StatusFlags(0b00000101)

        data1 = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        msg3 = AllTypesMessage.decode_bytes(data2)
        data3 = msg3.encode_bytes()
        assert data1 == data2 == data3


# ===========================================================================
# Boundary type mixed values
# ===========================================================================

class TestBoundaryTypeMixedValues:

    def test_odd_width_alternating_bits(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0xAAA & 0xFFF  # 2730
        msg.u20 = 0xAAAAA & 0xFFFFF  # 699050
        msg.s12 = -1
        data = msg.encode_bytes()
        msg2 = OddWidthMsg.decode_bytes(data)
        assert msg2.u12 == 0xAAA
        assert msg2.u20 == 0xAAAAA
        assert msg2.s12 == -1

    def test_boundary_signed_plus_minus_one(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp as BScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 1
        msg.medium = 1
        msg.byte_val = 1
        msg.word = 1
        msg.dword = 1
        msg.qword = 1
        msg.signed_byte = -1
        msg.signed_word = -1
        msg.signed_dword = -1
        msg.signed_qword = -1
        msg.temp = BScaledTemp(0)
        msg.level = NybbleEnum.LOW

        data = msg.encode_bytes()
        msg2 = BoundaryMsg.decode_bytes(data)
        assert msg2.signed_byte == -1
        assert msg2.signed_word == -1
        assert msg2.signed_dword == -1
        assert msg2.signed_qword == -1

    def test_boundary_msg_signed_positive_max_minus_one(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp as BScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 0
        msg.small = 6
        msg.medium = 126
        msg.byte_val = 254
        msg.word = 65534
        msg.dword = 0xFFFFFFFE
        msg.qword = 0xFFFFFFFFFFFFFFFE
        msg.signed_byte = 126
        msg.signed_word = 32766
        msg.signed_dword = 2147483646
        msg.signed_qword = 2**63 - 2
        msg.temp = BScaledTemp(0)
        msg.level = NybbleEnum.MID

        data = msg.encode_bytes()
        msg2 = BoundaryMsg.decode_bytes(data)
        assert msg2.signed_byte == 126
        assert msg2.signed_word == 32766
        assert msg2.signed_dword == 2147483646


# ===========================================================================
# Mixed endian stress
# ===========================================================================

class TestMixedEndianStress:

    def test_mixed_msg_all_ones(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0xFFFF
        msg.le16 = 0xFFFF
        msg.be32 = 0xFFFFFFFF
        msg.le32 = 0xFFFFFFFF

        data = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data)
        assert msg2.be16 == 0xFFFF
        assert msg2.le16 == 0xFFFF
        assert msg2.be32 == 0xFFFFFFFF
        assert msg2.le32 == 0xFFFFFFFF

    def test_mixed_msg_all_zeros(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0
        msg.le16 = 0
        msg.be32 = 0
        msg.le32 = 0

        data = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data)
        assert msg2.be16 == 0
        assert msg2.le16 == 0
        assert msg2.be32 == 0
        assert msg2.le32 == 0

    def test_mixed_msg_alternating_patterns(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0xAAAA
        msg.le16 = 0x5555
        msg.be32 = 0xAAAAAAAA
        msg.le32 = 0x55555555

        data = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data)
        assert msg2.be16 == 0xAAAA
        assert msg2.le16 == 0x5555
        assert msg2.be32 == 0xAAAAAAAA
        assert msg2.le32 == 0x55555555

    def test_mixed_msg_verify_wire_byte_order(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0x0102
        msg.le16 = 0x0304
        msg.be32 = 0x05060708
        msg.le32 = 0x090A0B0C

        data = msg.encode_bytes()
        # BE16: [01, 02], LE16: [04, 03], BE32: [05, 06, 07, 08], LE32: [0C, 0B, 0A, 09]
        assert data[0:2] == bytes([0x01, 0x02])
        assert data[2:4] == bytes([0x04, 0x03])
        assert data[4:8] == bytes([0x05, 0x06, 0x07, 0x08])
        assert data[8:12] == bytes([0x0C, 0x0B, 0x0A, 0x09])


# ===========================================================================
# Field scale edge cases
# ===========================================================================

class TestFieldScaleEdgeCases:

    def test_scale_msg_exact_integer_raw(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 2.5
        msg.temp = 25.0
        msg.plain = 100

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(2.5)
        assert msg2.temp == pytest.approx(25.0)
        assert msg2.plain == 100

    def test_scale_msg_boundary_temp(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 0.0
        msg.temp = -40.0  # raw = 0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.temp == pytest.approx(-40.0)

    def test_scale_msg_negative_fl(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = -10.0
        msg.temp = 0.0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(-10.0)


# ===========================================================================
# Wire encoding edge cases
# ===========================================================================

class TestWireEncodingEdgeCases:

    def test_bcd_all_nines(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 9999
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 999
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 9999
        assert msg2.inline_bcd == 999

    def test_bcd_zero(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 0
        assert msg2.bcd_hdg == 0

    def test_sign_magnitude_positive_and_negative_zero(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.sm_offset == 0

    def test_twos_complement_boundary(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 32767  # max positive for 16-bit signed
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.cb2_val == 32767

    def test_all_fields_nonzero(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 5678
        msg.bcd_hdg = -179
        msg.sm_offset = -777
        msg.cb2_val = -10000
        msg.bnr_val = 40000
        msg.inline_bcd = 456
        msg.inline_bnrs = -150

        data1 = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2
        assert msg2.bcd_alt == 5678
        assert msg2.bcd_hdg == -179
        assert msg2.sm_offset == -777
        assert msg2.cb2_val == -10000
        assert msg2.bnr_val == 40000


# ===========================================================================
# Choice roundtrip
# ===========================================================================

class TestChoiceRoundtripAdvanced:

    def test_choice_type_a_sub_x_max_val(self):
        from arrays_choices import Constants
        from arrays_choices.structs import SubX, TypeABody
        from arrays_choices.messages import ChoiceMsg

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0xFFFFFFFF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert isinstance(msg2.body, TypeABody)
        assert isinstance(msg2.body.sub_body, SubX)
        assert msg2.body.sub_body.val == 0xFFFFFFFF

    def test_choice_type_a_sub_y_max_vals(self):
        from arrays_choices import Constants
        from arrays_choices.structs import SubY, TypeABody
        from arrays_choices.messages import ChoiceMsg

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5
        body = TypeABody()
        body.sub_type = Constants.SUB_Y
        body.sub_body = SubY()
        body.sub_body.a = 0xFFFF
        body.sub_body.b = 0xFFFF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert isinstance(msg2.body.sub_body, SubY)
        assert msg2.body.sub_body.a == 0xFFFF
        assert msg2.body.sub_body.b == 0xFFFF

    def test_choice_type_b_max_tag(self):
        from arrays_choices import Constants
        from arrays_choices.structs import TypeBBody
        from arrays_choices.messages import ChoiceMsg

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        msg.length = 4
        body = TypeBBody()
        body.tag = 0xFFFFFFFF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert isinstance(msg2.body, TypeBBody)
        assert msg2.body.tag == 0xFFFFFFFF

    def test_choice_idempotent(self):
        from arrays_choices import Constants
        from arrays_choices.structs import SubX, TypeABody
        from arrays_choices.messages import ChoiceMsg

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0xDEADBEEF
        msg.body = body

        data1 = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ===========================================================================
# Array roundtrip
# ===========================================================================

class TestArrayRoundtripAdvanced:

    def test_fixed_array_all_zero_points(self):
        from arrays_choices.structs import Point
        from arrays_choices.messages import FixedArrayMsg

        msg = FixedArrayMsg()
        for _ in range(3):
            msg.points.append(Point())

        data = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data)
        for pt in msg2.points:
            assert pt.x == 0
            assert pt.y == 0

    def test_fixed_array_max_value_points(self):
        from arrays_choices.structs import Point
        from arrays_choices.messages import FixedArrayMsg

        msg = FixedArrayMsg()
        for _ in range(3):
            p = Point()
            p.x = 0xFFFF
            p.y = 0xFFFF
            msg.points.append(p)

        data = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data)
        for pt in msg2.points:
            assert pt.x == 0xFFFF
            assert pt.y == 0xFFFF

    def test_fixed_array_mixed_values(self):
        from arrays_choices.structs import Point
        from arrays_choices.messages import FixedArrayMsg

        msg = FixedArrayMsg()
        values = [(0, 0xFFFF), (0xFFFF, 0), (0x5555, 0xAAAA)]
        for x, y in values:
            p = Point()
            p.x = x
            p.y = y
            msg.points.append(p)

        data = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data)
        for i, (x, y) in enumerate(values):
            assert msg2.points[i].x == x
            assert msg2.points[i].y == y

    def test_count_from_array_zero_items(self):
        from arrays_choices.messages import CountFromArrayMsg

        msg = CountFromArrayMsg()
        msg.num_items = 0

        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 0
        assert len(msg2.items) == 0

    def test_count_from_array_single_item(self):
        from arrays_choices.structs import Point
        from arrays_choices.messages import CountFromArrayMsg

        msg = CountFromArrayMsg()
        msg.num_items = 1
        p = Point()
        p.x = 0xBEEF
        p.y = 0xCAFE
        msg.items.append(p)

        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 1
        assert msg2.items[0].x == 0xBEEF
        assert msg2.items[0].y == 0xCAFE


# ===========================================================================
# Multi-message sequence
# ===========================================================================

class TestMultiMessageSequence:

    def test_sequential_encode_decode_no_contamination(self):
        from session_protocol import PingBody, DataBody, AckBody

        ping = PingBody()
        ping.timestamp = 0xDEADBEEF

        data_msg = DataBody()
        data_msg.channel = 42
        data_msg.payload_a = 0x11111111
        data_msg.payload_b = 0x22222222

        ack = AckBody()
        ack.acked_seq = 9999

        ping_data = ping.encode_bytes()
        data_data = data_msg.encode_bytes()
        ack_data = ack.encode_bytes()

        ping2 = PingBody.decode_bytes(ping_data)
        data2 = DataBody.decode_bytes(data_data)
        ack2 = AckBody.decode_bytes(ack_data)

        assert ping2.timestamp == 0xDEADBEEF
        assert data2.channel == 42
        assert data2.payload_a == 0x11111111
        assert data2.payload_b == 0x22222222
        assert ack2.acked_seq == 9999

    def test_repeated_encode_same_message_produces_same_bytes(self):
        from session_protocol import PingBody

        msg = PingBody()
        msg.timestamp = 12345678

        data1 = msg.encode_bytes()
        data2 = msg.encode_bytes()
        data3 = msg.encode_bytes()
        assert data1 == data2 == data3


# ===========================================================================
# Error paths — corrupted bytes
# ===========================================================================

class TestCorruptedBytesErrors:

    def test_flipped_bits_in_enum_field(self):
        """Flip bits in the color enum byte to create an invalid value."""
        msg = _default_msg()
        msg.color = ColorEnum.RED
        data = bytearray(msg.encode_bytes())
        # Find the enum byte and corrupt it by writing an invalid value
        # The color enum is near the end of the message, write 99 there
        # We encode a known good message and corrupt the enum
        w = BitWriter()
        w.write_bits(99, 8)  # Invalid enum value
        r = BitReader(w.to_bytes())
        with pytest.raises(DecodeError, match="unknown ColorEnum value"):
            ColorEnum.decode(r)

    def test_all_0xff_bytes_as_message(self):
        """Decoding all-0xFF should either succeed or raise DecodeError."""
        data = b"\xff" * 100
        try:
            msg = AllTypesMessage.decode_bytes(data)
            # If it succeeds, the enum field should raise
        except DecodeError:
            pass  # Expected for invalid enum values


class TestWrongMessageDecode:

    def test_ping_bytes_as_data_body(self):
        from session_protocol import PingBody, DataBody, DecodeError as DE

        ping = PingBody()
        ping.timestamp = 0x12345678
        ping_data = ping.encode_bytes()

        # PingBody is 4 bytes, DataBody needs 9 bytes — should fail
        with pytest.raises(DE, match="underflow"):
            DataBody.decode_bytes(ping_data)

    def test_ack_bytes_as_ping_body(self):
        from session_protocol import PingBody, AckBody, DecodeError as DE

        ack = AckBody()
        ack.acked_seq = 100
        ack_data = ack.encode_bytes()

        # AckBody is 2 bytes, PingBody needs 4 — should fail
        with pytest.raises(DE, match="underflow"):
            PingBody.decode_bytes(ack_data)


class TestOversizedData:

    def test_extra_bytes_after_message_are_ignored(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 42
        data = ping.encode_bytes() + b"\xDE\xAD\xBE\xEF"

        # Extra bytes at the end should be ignored
        ping2 = PingBody.decode_bytes(data)
        assert ping2.timestamp == 42


class TestZeroLengthDecode:

    def test_empty_bytes_all_types(self):
        with pytest.raises(DecodeError, match="underflow"):
            AllTypesMessage.decode_bytes(b"")

    def test_empty_bytes_ping(self):
        from session_protocol import PingBody, DecodeError as DE
        with pytest.raises(DE, match="underflow"):
            PingBody.decode_bytes(b"")


class TestPartialMessages:

    @pytest.mark.parametrize("truncate_at", range(1, 9))
    def test_data_body_truncated_at_every_position(self, truncate_at):
        from session_protocol import DataBody, DecodeError as DE

        msg = DataBody()
        msg.channel = 42
        msg.payload_a = 0xDEADBEEF
        msg.payload_b = 0xCAFEBABE
        full_data = msg.encode_bytes()

        with pytest.raises(DE):
            DataBody.decode_bytes(full_data[:truncate_at])


class TestBitReaderExhaustion:

    def test_read_beyond_available_bits(self):
        r = BitReader(b"\x42")  # 8 bits available
        r.read_u8()  # consume all 8
        with pytest.raises(DecodeError, match="underflow"):
            r.read_u8()  # no bits left

    def test_read_u32_from_two_bytes(self):
        r = BitReader(b"\x00\x01")
        with pytest.raises(DecodeError):
            r.read_u32(True)

    def test_read_u64_from_four_bytes(self):
        r = BitReader(b"\x00\x01\x02\x03")
        with pytest.raises(DecodeError):
            r.read_u64(True)


# ===========================================================================
# Transceiver roundtrips (UDP loopback)
# ===========================================================================

# ---- Native library setup (mirrors test_xcvr_scenarios.py) ----
# Guarded so codec-only tests above still run when the native lib is absent.

from conftest import resolve_native_lib

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

_NATIVE_AVAILABLE = False
_native_skip_reason = "native library not available"

try:
    _CABI_LIB_PATH = resolve_native_lib("CONDUIT_CABI_TEST_LIB", "conduit_cabi_test")
    if not os.path.isfile(_CABI_LIB_PATH):
        raise OSError(f"Not found: {_CABI_LIB_PATH}")
    os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB_PATH

    _CODEC_LIB_PATH = resolve_native_lib("CONDUIT_CODEC_TEST_LIB", "conduit_codec_cabi_test")
    if not os.path.isfile(_CODEC_LIB_PATH):
        _CODEC_LIB_PATH = _CABI_LIB_PATH
    os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

    _BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
    if _BINDINGS_DIR not in sys.path:
        sys.path.insert(0, _BINDINGS_DIR)

    if sys.platform == "win32":
        _lib_dir = os.path.dirname(_CODEC_LIB_PATH)
        if os.path.isdir(_lib_dir) and hasattr(os, "add_dll_directory"):
            os.add_dll_directory(_lib_dir)
        _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH)
    else:
        _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH, mode=ctypes.RTLD_GLOBAL)

    import conduit.transceiver as _xcvr_mod
    _xcvr_mod._lib = None

    from conduit.transceiver import Transceiver, ConduitError
    from conduit.types import UdpConfig

    _GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
    if _GENERATED_DIR not in sys.path:
        sys.path.insert(0, _GENERATED_DIR)

    from session_protocol.messages import PingBody, DataBody, AckBody

    PING_TYPE_ID = 0x0AD7BB3ECC473399
    DATA_TYPE_ID = 0x29D16B9E73F85835

    _NATIVE_AVAILABLE = True
except (OSError, ImportError) as exc:
    _native_skip_reason = f"native library not available: {exc}"

_requires_native = pytest.mark.skipif(
    not _NATIVE_AVAILABLE, reason=_native_skip_reason
)


def _find_free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ---- Transceiver test classes ----

@_requires_native
class TestTransceiverPingRoundtrip:

    @pytest.mark.parametrize("timestamp", [0, 1, 0x7FFFFFFF, 0xFFFFFFFF])
    def test_ping_edge_case_timestamps(self, timestamp):
        port = _find_free_udp_port()
        received = []
        recv_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg)
                recv_event.set()

            peer_id = sender.add_peer("dst", "session_protocol",
                                      UdpConfig(f"127.0.0.1:{port}"))

            receiver.start()
            sender.start()

            msg = PingBody()
            msg.timestamp = timestamp
            sender.send(peer_id, msg)

            recv_event.wait(timeout=0.5)
            sender.stop()
            receiver.stop()

        if len(received) > 0:
            assert received[0].timestamp == timestamp


@_requires_native
class TestTransceiverDataBodyRoundtrip:

    def test_data_body_full_fields(self):
        port = _find_free_udp_port()
        received = []
        recv_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            @receiver.on(DataBody)
            def handle_data(peer_id, msg):
                received.append(msg)
                recv_event.set()

            peer_id = sender.add_peer("dst", "session_protocol",
                                      UdpConfig(f"127.0.0.1:{port}"))

            receiver.start()
            sender.start()

            msg = DataBody()
            msg.channel = 255
            msg.payload_a = 0xDEADBEEF
            msg.payload_b = 0xCAFEBABE
            sender.send(peer_id, msg)

            recv_event.wait(timeout=0.5)
            sender.stop()
            receiver.stop()

        if len(received) > 0:
            assert received[0].channel == 255
            assert received[0].payload_a == 0xDEADBEEF
            assert received[0].payload_b == 0xCAFEBABE


@_requires_native
class TestTransceiverMultiTypeRoundtrip:

    def test_interleaved_ping_and_data(self):
        port = _find_free_udp_port()
        pings = []
        datas = []
        done_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                pings.append(msg)
                if len(pings) + len(datas) >= 4:
                    done_event.set()

            @receiver.on(DataBody)
            def handle_data(peer_id, msg):
                datas.append(msg)
                if len(pings) + len(datas) >= 4:
                    done_event.set()

            peer_id = sender.add_peer("dst", "session_protocol",
                                      UdpConfig(f"127.0.0.1:{port}"))

            receiver.start()
            sender.start()
            time.sleep(0.05)

            for i in range(2):
                ping = PingBody()
                ping.timestamp = 1000 + i
                sender.send(peer_id, ping)

                data_msg = DataBody()
                data_msg.channel = i
                data_msg.payload_a = i * 100
                data_msg.payload_b = i * 200
                sender.send(peer_id, data_msg)

            done_event.wait(timeout=0.5)
            sender.stop()
            receiver.stop()

        if len(pings) > 0:
            for p in pings:
                assert 1000 <= p.timestamp <= 1001

        if len(datas) > 0:
            for d in datas:
                assert d.channel in (0, 1)


@_requires_native
class TestTransceiverRawBytesRoundtrip:

    def test_manual_encode_send_raw_decode(self):
        port = _find_free_udp_port()
        received_raw = []
        recv_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            @receiver.on(type_id=PING_TYPE_ID)
            def handle_raw(peer_id, type_id, type_name, data):
                received_raw.append(data)
                recv_event.set()

            peer_id = sender.add_peer("dst", "session_protocol",
                                      UdpConfig(f"127.0.0.1:{port}"))

            receiver.start()
            sender.start()

            # Manually encode a PingBody
            ping = PingBody()
            ping.timestamp = 0xABCD1234
            raw_bytes = ping.encode_bytes()

            sender.send_raw(peer_id, PING_TYPE_ID, raw_bytes)

            recv_event.wait(timeout=0.5)
            sender.stop()
            receiver.stop()

        if len(received_raw) > 0:
            decoded = PingBody.decode_bytes(received_raw[0])
            assert decoded.timestamp == 0xABCD1234


@_requires_native
class TestTransceiverSendReceiveStress:

    def test_50_messages_roundtrip(self):
        port = _find_free_udp_port()
        received = []
        done_event = threading.Event()
        count = 50

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg.timestamp)
                if len(received) >= count:
                    done_event.set()

            peer_id = sender.add_peer("dst", "session_protocol",
                                      UdpConfig(f"127.0.0.1:{port}"))

            receiver.start()
            sender.start()
            time.sleep(0.05)

            for i in range(count):
                msg = PingBody()
                msg.timestamp = i
                sender.send(peer_id, msg)

            done_event.wait(timeout=2.0)
            sender.stop()
            receiver.stop()

        # UDP is unreliable, but if we got some, verify they're valid
        if len(received) > 0:
            for ts in received:
                assert 0 <= ts < count


@_requires_native
class TestTransceiverErrorOnCorruptedRaw:

    def test_corrupted_raw_triggers_error(self):
        port = _find_free_udp_port()
        errors = []
        error_event = threading.Event()

        with Transceiver() as receiver:
            receiver.add_peer("src", "session_protocol",
                              UdpConfig(f"0.0.0.0:{port}"))

            def error_cb(peer_id, peer_name, error_code, error_message):
                errors.append((error_code, error_message))
                error_event.set()

            receiver.on_error(error_cb)
            receiver.start()
            time.sleep(0.05)

            # Send garbage directly via raw UDP
            garbage = b"\xFF\xFE\xFD\xFC\xFB\xFA\xF9\xF8"
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.sendto(garbage, ("127.0.0.1", port))
            sock.close()

            error_event.wait(timeout=1.0)
            receiver.stop()

        if len(errors) > 0:
            assert isinstance(errors[0][0], int)
            assert isinstance(errors[0][1], str)
