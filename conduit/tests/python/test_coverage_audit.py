"""Audit-driven tests for Python codegen coverage gaps.

This file addresses coverage gaps identified by the Conduit library audit,
targeting areas of the 13 generated Python modules that have C++ roundtrip
tests but lack equivalent Python runtime tests.

Coverage areas:
- Wire encoding roundtrips (BCD, BCD_S, sign-magnitude, CB2)
- Boundary type value roundtrips (odd-width, signed, scaled, enum)
- Struct features: alignment, present-when conditional fields
- Arrays/choices: choice dispatch, nested choices, deep nesting, count-from arrays
- String features: type wrappers, terminated strings, max-length strings, inline strings
- Field scale: signed scale, offset, and plain field roundtrips
- Protocol metadata: ProtocolDescriptor, Constants, TypeInfo
- Session: encode_wrap, decode_frame, format_message, sequence, reset
- Mixed endian: value verification
- Constraint module: boundary values
"""
import struct
import pytest


# ============================================================================
# Wire encoding roundtrips
# ============================================================================


class TestWireEncodingRoundtrip:
    """Tests for BCD, BCD_S, sign-magnitude, CB2 encodings in wire_encodings module."""

    def test_bcd_positive_value(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 1234
        msg.bcd_hdg = 180
        msg.sm_offset = 500
        msg.cb2_val = -100
        msg.bnr_val = 0x1234
        msg.inline_bcd = 999
        msg.inline_bnrs = -200

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 1234
        assert msg2.bcd_hdg == 180
        assert msg2.sm_offset == 500
        assert msg2.cb2_val == -100
        assert msg2.bnr_val == 0x1234
        assert msg2.inline_bcd == 999
        assert msg2.inline_bnrs == -200

    def test_bcd_zero_values(self):
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
        assert msg2.sm_offset == 0
        assert msg2.cb2_val == 0
        assert msg2.bnr_val == 0
        assert msg2.inline_bcd == 0
        assert msg2.inline_bnrs == 0

    def test_bcd_negative_values(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = -180
        msg.sm_offset = -500
        msg.cb2_val = -32768
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = -500

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_hdg == -180
        assert msg2.sm_offset == -500
        assert msg2.cb2_val == -32768
        assert msg2.inline_bnrs == -500

    def test_wire_encoding_double_encode(self):
        """Encode -> decode -> encode should produce identical bytes."""
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 5678
        msg.bcd_hdg = -90
        msg.sm_offset = 1000
        msg.cb2_val = 2000
        msg.bnr_val = 0xABCD
        msg.inline_bcd = 123
        msg.inline_bnrs = -100

        data1 = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_cb2_positive_max(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 32767  # max positive i16
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.cb2_val == 32767


# ============================================================================
# Boundary type value roundtrips
# ============================================================================


class TestBoundaryTypeValues:
    """Test BoundaryMsg and OddWidthMsg with specific values, not just truncation errors."""

    def test_boundary_msg_min_values(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 0
        msg.small = 0
        msg.medium = 0
        msg.byte_val = 0
        msg.word = 0
        msg.dword = 0
        msg.qword = 0
        msg.signed_byte = -128
        msg.signed_word = -32768
        msg.signed_dword = -(2**31)
        msg.signed_qword = -(2**63)
        msg.temp = ScaledTemp(0)
        msg.level = NybbleEnum.OFF

        data = msg.encode_bytes()
        msg2 = BoundaryMsg.decode_bytes(data)
        assert msg2.flag == 0
        assert msg2.small == 0
        assert msg2.medium == 0
        assert msg2.signed_byte == -128
        assert msg2.signed_word == -32768
        assert msg2.signed_dword == -(2**31)
        assert msg2.signed_qword == -(2**63)
        assert msg2.level == NybbleEnum.OFF

    def test_boundary_msg_max_values(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 7  # 3-bit max
        msg.medium = 127  # 7-bit max
        msg.byte_val = 255
        msg.word = 65535
        msg.dword = 0xFFFFFFFF
        msg.qword = 0xFFFFFFFFFFFFFFFF
        msg.signed_byte = 127
        msg.signed_word = 32767
        msg.signed_dword = 2**31 - 1
        msg.signed_qword = 2**63 - 1
        msg.temp = ScaledTemp(65535)
        msg.level = NybbleEnum.HIGH

        data = msg.encode_bytes()
        msg2 = BoundaryMsg.decode_bytes(data)
        assert msg2.flag == 1
        assert msg2.small == 7
        assert msg2.medium == 127
        assert msg2.byte_val == 255
        assert msg2.word == 65535
        assert msg2.dword == 0xFFFFFFFF
        assert msg2.qword == 0xFFFFFFFFFFFFFFFF
        assert msg2.signed_byte == 127
        assert msg2.signed_word == 32767
        assert msg2.signed_dword == 2**31 - 1
        assert msg2.signed_qword == 2**63 - 1
        assert msg2.level == NybbleEnum.HIGH

    def test_boundary_msg_double_encode(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 5
        msg.medium = 100
        msg.byte_val = 0xAB
        msg.word = 0x1234
        msg.dword = 0xDEADBEEF
        msg.qword = 0x0102030405060708
        msg.signed_byte = -42
        msg.signed_word = -1000
        msg.signed_dword = -100000
        msg.signed_qword = -999999999
        msg.temp = ScaledTemp(4000)
        msg.level = NybbleEnum.MID

        data1 = msg.encode_bytes()
        msg2 = BoundaryMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_odd_width_msg_roundtrip(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0xFFF  # 12-bit max
        msg.u20 = 0xFFFFF  # 20-bit max
        msg.s12 = -2048  # 12-bit signed min

        data = msg.encode_bytes()
        msg2 = OddWidthMsg.decode_bytes(data)
        assert msg2.u12 == 0xFFF
        assert msg2.u20 == 0xFFFFF
        assert msg2.s12 == -2048

    def test_odd_width_msg_zero(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0
        msg.u20 = 0
        msg.s12 = 0

        data = msg.encode_bytes()
        msg2 = OddWidthMsg.decode_bytes(data)
        assert msg2.u12 == 0
        assert msg2.u20 == 0
        assert msg2.s12 == 0

    def test_odd_width_msg_positive_signed(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 1234
        msg.u20 = 567890
        msg.s12 = 2047  # 12-bit signed max

        data = msg.encode_bytes()
        msg2 = OddWidthMsg.decode_bytes(data)
        assert msg2.u12 == 1234
        assert msg2.u20 == 567890
        assert msg2.s12 == 2047

    def test_scaled_temp_value_property(self):
        """ScaledTemp.value should apply scale/offset correctly."""
        from boundary_types.types import ScaledTemp

        t = ScaledTemp(400)  # raw=400, value = 400 * 0.1 + (-40) = 0.0
        assert abs(t.value - 0.0) < 0.01

        t2 = ScaledTemp(1400)  # 1400 * 0.1 + (-40) = 100.0
        assert abs(t2.value - 100.0) < 0.01

    def test_scaled_temp_value_setter(self):
        from boundary_types.types import ScaledTemp

        t = ScaledTemp()
        t.value = 25.0  # (25 - (-40)) / 0.1 = 650
        assert t.raw == 650


# ============================================================================
# Struct features: alignment, present-when, reserved fields
# ============================================================================


class TestStructFeaturesExtended:
    """Extended tests for struct_features generated code."""

    def test_aligned_message_roundtrip(self):
        from struct_features import AlignedMessage

        msg = AlignedMessage()
        msg.flag = 42
        msg.data = 0xDEADBEEF

        data = msg.encode_bytes()
        msg2 = AlignedMessage.decode_bytes(data)
        assert msg2.flag == 42
        assert msg2.data == 0xDEADBEEF

    def test_aligned_message_size(self):
        """AlignedMessage should pad flag (1 byte) to 2-byte alignment = 2 + 4 = 6 bytes."""
        from struct_features import AlignedMessage

        msg = AlignedMessage()
        msg.flag = 1
        msg.data = 0x12345678
        data = msg.encode_bytes()
        assert len(data) == 6

    def test_aligned_message_double_encode(self):
        from struct_features import AlignedMessage

        msg = AlignedMessage()
        msg.flag = 0xFF
        msg.data = 0xCAFEBABE

        data1 = msg.encode_bytes()
        msg2 = AlignedMessage.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_conditional_message_present_when_true(self):
        from struct_features import ConditionalMessage

        msg = ConditionalMessage()
        msg.has_extra = 1
        msg.base_value = 0x1234
        msg.extra_value = 0x5678

        data = msg.encode_bytes()
        msg2 = ConditionalMessage.decode_bytes(data)
        assert msg2.has_extra == 1
        assert msg2.base_value == 0x1234
        assert msg2.extra_value == 0x5678

    def test_conditional_message_present_when_false(self):
        from struct_features import ConditionalMessage

        msg = ConditionalMessage()
        msg.has_extra = 0
        msg.base_value = 0xABCD

        data = msg.encode_bytes()
        msg2 = ConditionalMessage.decode_bytes(data)
        assert msg2.has_extra == 0
        assert msg2.base_value == 0xABCD
        assert msg2.extra_value is None

    def test_conditional_message_size_varies(self):
        from struct_features import ConditionalMessage

        # With extra: 1 (has_extra) + 2 (base_value) + 2 (extra_value) = 5
        msg_with = ConditionalMessage()
        msg_with.has_extra = 1
        msg_with.base_value = 100
        msg_with.extra_value = 200
        data_with = msg_with.encode_bytes()

        # Without extra: 1 + 2 = 3
        msg_without = ConditionalMessage()
        msg_without.has_extra = 0
        msg_without.base_value = 100
        data_without = msg_without.encode_bytes()

        assert len(data_with) == 5
        assert len(data_without) == 3

    def test_gps_coord_standalone(self):
        from struct_features.structs import GpsCoord

        coord = GpsCoord()
        coord.latitude = 0x12345678
        coord.longitude = 0xABCDEF01

        data = coord.encode_bytes()
        coord2 = GpsCoord.decode_bytes(data)
        assert coord2.latitude == 0x12345678
        assert coord2.longitude == 0xABCDEF01

    def test_constrained_message_reserved_field(self):
        """ConstrainedMessage has a reserved field (skip_bits(8)) after value."""
        from struct_features import ConstrainedMessage
        from struct_features.structs import GpsCoord

        from struct_features.constants import Constants as SFConstants
        msg = ConstrainedMessage()
        msg.magic = SFConstants.MAGIC  # 0xCAFE - must match equals constraint
        msg.version = SFConstants.VERSION  # 3 - must match equals constraint
        msg.value = 500  # must be in range [10, 1000]
        msg.position = GpsCoord()
        msg.position.latitude = 1000
        msg.position.longitude = 2000

        data = msg.encode_bytes()
        # magic(2) + version(1) + value(2) + reserved(1) + lat(4) + lon(4) = 14
        assert len(data) == 14

        msg2 = ConstrainedMessage.decode_bytes(data)
        assert msg2.magic == SFConstants.MAGIC
        assert msg2.version == SFConstants.VERSION
        assert msg2.value == 500
        assert msg2.position.latitude == 1000
        assert msg2.position.longitude == 2000


# ============================================================================
# Arrays and choices: extended coverage
# ============================================================================


class TestArraysChoicesExtended:
    """Extended tests for arrays_choices generated code."""

    def test_count_from_array_msg_roundtrip(self):
        from arrays_choices.messages import CountFromArrayMsg
        from arrays_choices.structs import Point

        msg = CountFromArrayMsg()
        msg.num_items = 2
        msg.items = []
        for i in range(2):
            p = Point()
            p.x = i * 100
            p.y = i * 200
            msg.items.append(p)

        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 2
        assert len(msg2.items) == 2
        assert msg2.items[0].x == 0
        assert msg2.items[0].y == 0
        assert msg2.items[1].x == 100
        assert msg2.items[1].y == 200

    def test_count_from_array_empty(self):
        from arrays_choices.messages import CountFromArrayMsg

        msg = CountFromArrayMsg()
        msg.num_items = 0
        msg.items = []

        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 0
        assert len(msg2.items) == 0

    def test_choice_msg_type_a(self):
        from arrays_choices.messages import ChoiceMsg
        from arrays_choices.structs import TypeABody, SubX
        from arrays_choices.constants import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5  # 1 (sub_type) + 4 (SubX.val)
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0x12345678
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == Constants.TYPE_A
        assert isinstance(msg2.body, TypeABody)
        assert msg2.body.sub_type == Constants.SUB_X
        assert isinstance(msg2.body.sub_body, SubX)
        assert msg2.body.sub_body.val == 0x12345678

    def test_choice_msg_type_b(self):
        from arrays_choices.messages import ChoiceMsg
        from arrays_choices.structs import TypeBBody
        from arrays_choices.constants import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        msg.length = 4
        body = TypeBBody()
        body.tag = 0xDEADBEEF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == Constants.TYPE_B
        assert isinstance(msg2.body, TypeBBody)
        assert msg2.body.tag == 0xDEADBEEF

    def test_choice_msg_fallback(self):
        from arrays_choices.messages import ChoiceMsg
        from arrays_choices.structs import FallbackBody

        msg = ChoiceMsg()
        msg.msg_type = 99  # neither TYPE_A nor TYPE_B
        msg.length = 4  # FallbackBody = 4 bytes (u32)
        body = FallbackBody()
        body.raw = 0x12345678
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == 99
        assert isinstance(msg2.body, FallbackBody)
        assert msg2.body.raw == 0x12345678

    def test_nested_choice_msg_typed_sub_x(self):
        from arrays_choices.messages import (
            NestedChoiceMsg, NestedChoiceMsgTyped, NestedChoiceMsgTypedAlpha
        )
        from arrays_choices.constants import Constants

        msg = NestedChoiceMsg()
        msg.msg_type = 1  # Typed
        msg.sub_type = Constants.SUB_X  # Alpha
        msg.body = NestedChoiceMsgTyped()
        msg.body.detail = NestedChoiceMsgTypedAlpha()
        msg.body.detail.a_val = 0xBEEF

        data = msg.encode_bytes()
        msg2 = NestedChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == 1
        assert msg2.sub_type == Constants.SUB_X
        assert isinstance(msg2.body, NestedChoiceMsgTyped)
        assert isinstance(msg2.body.detail, NestedChoiceMsgTypedAlpha)
        assert msg2.body.detail.a_val == 0xBEEF

    def test_nested_choice_msg_simple(self):
        from arrays_choices.messages import NestedChoiceMsg, NestedChoiceMsgSimple

        msg = NestedChoiceMsg()
        msg.msg_type = 2  # Simple
        msg.sub_type = 0
        msg.body = NestedChoiceMsgSimple()
        msg.body.data = 0xCAFEBABE

        data = msg.encode_bytes()
        msg2 = NestedChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == 2
        assert isinstance(msg2.body, NestedChoiceMsgSimple)
        assert msg2.body.data == 0xCAFEBABE

    def test_deep_nested_msg_full_path(self):
        from arrays_choices.messages import (
            DeepNestedMsg, DeepNestedMsgL1, DeepNestedMsgL1L2, DeepNestedMsgL1L2L3
        )
        from arrays_choices.constants import Constants

        msg = DeepNestedMsg()
        msg.type_a = 1
        msg.type_b = Constants.SUB_X
        msg.type_c = Constants.SUB_Y
        msg.outer = DeepNestedMsgL1()
        msg.outer.mid = DeepNestedMsgL1L2()
        msg.outer.mid.inner = DeepNestedMsgL1L2L3()
        msg.outer.mid.inner.value = 0xDEAD1234

        data = msg.encode_bytes()
        msg2 = DeepNestedMsg.decode_bytes(data)
        assert msg2.type_a == 1
        assert msg2.type_b == Constants.SUB_X
        assert msg2.type_c == Constants.SUB_Y
        assert msg2.outer.mid.inner.value == 0xDEAD1234

    def test_deep_nested_msg_no_outer(self):
        from arrays_choices.messages import DeepNestedMsg
        from arrays_choices.bit_io import DecodeError

        msg = DeepNestedMsg()
        msg.type_a = 0  # no outer choice matches -> should raise on decode
        msg.type_b = 0
        msg.type_c = 0

        data = msg.encode_bytes()
        # The new codegen raises DecodeError when no choice case matches
        import pytest
        with pytest.raises(DecodeError, match="no case matched"):
            DeepNestedMsg.decode_bytes(data)


# ============================================================================
# String features: type wrappers, terminated, max-length
# ============================================================================


class TestStringFeaturesExtended:
    """Extended tests for string_features generated code."""

    def test_string_msg_roundtrip(self):
        from string_features import StringMsg
        from string_features.types import NameStr, LabelStr, BoundedStr, PackedStr, TermStr

        msg = StringMsg()
        msg.id = 42
        msg.name = NameStr("Hello")
        msg.label = LabelStr("World")
        msg.bounded = BoundedStr("Test data here")
        msg.packed = PackedStr("Pack")
        msg.term = TermStr("Terminated")

        data = msg.encode_bytes()
        msg2 = StringMsg.decode_bytes(data)
        assert msg2.id == 42
        assert msg2.name == NameStr("Hello")
        assert msg2.label == LabelStr("World")
        assert msg2.bounded == BoundedStr("Test data here")
        assert msg2.packed == PackedStr("Pack")
        assert msg2.term == TermStr("Terminated")

    def test_name_str_null_padded(self):
        """NameStr uses null padding (pad=0) and right-trims nulls on decode."""
        from string_features.types import NameStr

        s = NameStr("Hi")
        from string_features import BitWriter, BitReader
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 20
        # Remaining bytes should be null
        assert data[2:] == b'\x00' * 18

        r = BitReader(data)
        s2 = NameStr.decode(r)
        assert s2.value == "Hi"

    def test_label_str_space_padded(self):
        """LabelStr uses space padding (pad=32=' ') and trims both sides."""
        from string_features.types import LabelStr

        s = LabelStr("Test")
        from string_features import BitWriter, BitReader
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 16
        # Remaining bytes should be spaces
        assert data[4:] == b' ' * 12

    def test_label_str_trim_both(self):
        """LabelStr with padding should trim on decode."""
        from string_features.types import LabelStr
        from string_features import BitWriter, BitReader

        # Write a space-padded string
        s = LabelStr("ABC")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        r = BitReader(data)
        s2 = LabelStr.decode(r)
        assert s2.value == "ABC"

    def test_string_type_equality(self):
        from string_features.types import NameStr, LabelStr

        assert NameStr("hello") == NameStr("hello")
        assert NameStr("hello") != NameStr("world")
        assert LabelStr("test") == LabelStr("test")

    def test_string_type_wire_size(self):
        from string_features.types import NameStr, LabelStr, BoundedStr, PackedStr, TermStr

        assert NameStr.WIRE_SIZE == 20
        assert LabelStr.WIRE_SIZE == 16
        assert BoundedStr.WIRE_SIZE == 32
        assert PackedStr.WIRE_SIZE == 8
        assert TermStr.WIRE_SIZE == 64

    def test_max_len_msg_roundtrip(self):
        from string_features import MaxLenMsg

        msg = MaxLenMsg()
        msg.id = 1
        msg.data = "Short string"

        data = msg.encode_bytes()
        msg2 = MaxLenMsg.decode_bytes(data)
        assert msg2.id == 1
        assert msg2.data == "Short string"

    def test_max_len_msg_empty_string(self):
        from string_features import MaxLenMsg

        msg = MaxLenMsg()
        msg.id = 2
        msg.data = ""

        data = msg.encode_bytes()
        msg2 = MaxLenMsg.decode_bytes(data)
        assert msg2.id == 2
        assert msg2.data == ""

    def test_inline_string_msg_roundtrip(self):
        from string_features import InlineStringMsg
        from string_features.types import NameStr

        msg = InlineStringMsg()
        msg.id = 5
        msg.inline_name = NameStr("Inline")
        msg.prefix_str = 42

        data = msg.encode_bytes()
        msg2 = InlineStringMsg.decode_bytes(data)
        assert msg2.id == 5
        assert msg2.inline_name == NameStr("Inline")
        assert msg2.prefix_str == 42


# ============================================================================
# Field scale roundtrips
# ============================================================================


class TestFieldScaleExtended:
    """Extended tests for field_scale generated code."""

    def test_scale_msg_positive(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 100.0
        msg.temp = 25.0
        msg.plain = 42

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert abs(msg2.fl - 100.0) < 0.26  # 0.25 step
        assert abs(msg2.temp - 25.0) < 0.02  # 0.01 step
        assert msg2.plain == 42

    def test_scale_msg_negative(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = -100.0
        msg.temp = -40.0  # offset is -40, so raw=0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert abs(msg2.fl - -100.0) < 0.26
        assert abs(msg2.temp - -40.0) < 0.02
        assert msg2.plain == 0

    def test_scale_msg_zero(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 0.0
        msg.temp = -40.0  # raw=0 -> -40.0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert abs(msg2.fl) < 0.26
        assert abs(msg2.temp - -40.0) < 0.02

    def test_scale_msg_double_encode(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 50.0
        msg.temp = 0.0
        msg.plain = 100

        data1 = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ============================================================================
# Mixed endian value verification
# ============================================================================


class TestMixedEndianValues:
    """Verify that mixed endian encoding produces correct byte patterns."""

    def test_mixed_msg_byte_pattern(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0x1234
        msg.le16 = 0x5678
        msg.be32 = 0xDEADBEEF
        msg.le32 = 0xCAFEBABE

        data = msg.encode_bytes()
        # BE16: 0x12, 0x34
        assert data[0] == 0x12
        assert data[1] == 0x34
        # LE16: 0x78, 0x56
        assert data[2] == 0x78
        assert data[3] == 0x56
        # BE32: 0xDE, 0xAD, 0xBE, 0xEF
        assert data[4] == 0xDE
        assert data[5] == 0xAD
        assert data[6] == 0xBE
        assert data[7] == 0xEF
        # LE32: 0xBE, 0xBA, 0xFE, 0xCA
        assert data[8] == 0xBE
        assert data[9] == 0xBA
        assert data[10] == 0xFE
        assert data[11] == 0xCA

    def test_mixed_msg_size(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0
        msg.le16 = 0
        msg.be32 = 0
        msg.le32 = 0

        data = msg.encode_bytes()
        assert len(data) == 12  # 2 + 2 + 4 + 4


# ============================================================================
# Constraint module: boundary values
# ============================================================================


class TestConstraintModule:
    """Tests for the constraints generated code."""

    def test_constraint_msg_max_values(self):
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0xBEEF  # equals constraint - must match
        msg.percent = 100   # max=100
        msg.deferred_val = 0xFFFF
        msg.payload = 0xFFFFFFFF

        data = msg.encode_bytes()
        msg2 = ConstraintMsg.decode_bytes(data)
        assert msg2.magic == 0xBEEF
        assert msg2.percent == 100
        assert msg2.deferred_val == 0xFFFF
        assert msg2.payload == 0xFFFFFFFF

    def test_constraint_msg_min_values(self):
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0xBEEF  # equals constraint - must match
        msg.percent = 0     # min=0
        msg.deferred_val = 0
        msg.payload = 0

        data = msg.encode_bytes()
        msg2 = ConstraintMsg.decode_bytes(data)
        assert msg2.magic == 0xBEEF
        assert msg2.percent == 0
        assert msg2.deferred_val == 0
        assert msg2.payload == 0

    def test_constraint_msg_magic_violation(self):
        """Decoding with wrong magic should raise ConstraintError."""
        from constraints import ConstraintMsg
        from constraints.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xDEAD, True)  # wrong magic
        w.write_u8(50)
        w.write_u16(100, True)
        w.write_u32(0, True)
        with pytest.raises(ConstraintError, match="magic"):
            ConstraintMsg.decode_bytes(w.to_bytes())

    def test_constraint_msg_percent_violation(self):
        """Decoding with percent > 100 should raise ConstraintError."""
        from constraints import ConstraintMsg
        from constraints.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xBEEF, True)  # correct magic
        w.write_u8(101)            # exceeds max=100
        w.write_u16(100, True)
        w.write_u32(0, True)
        with pytest.raises(ConstraintError, match="percent"):
            ConstraintMsg.decode_bytes(w.to_bytes())

    def test_constraint_msg_double_encode(self):
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0xBEEF
        msg.percent = 75
        msg.deferred_val = 500
        msg.payload = 0x12345678

        data1 = msg.encode_bytes()
        msg2 = ConstraintMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_constraint_msg_wire_size(self):
        """ConstraintMsg: magic(2) + percent(1) + deferred_val(2) + payload(4) = 9 bytes."""
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0
        msg.percent = 0
        msg.deferred_val = 0
        msg.payload = 0

        data = msg.encode_bytes()
        assert len(data) == 9


# ============================================================================
# Protocol metadata
# ============================================================================


class TestProtocolMetadata:
    """Test ProtocolDescriptor, Constants, and TypeInfo for all generated modules."""

    def test_all_types_protocol_descriptor(self):
        from all_types.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'all_types'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_session_protocol_descriptor(self):
        from session_protocol.protocol import ProtocolDescriptor, TypeInfo

        assert ProtocolDescriptor.NAME == 'session_test'
        assert ProtocolDescriptor.VERSION == '2.0'
        assert len(ProtocolDescriptor.TYPES) >= 0

    def test_arrays_choices_constants(self):
        from arrays_choices.constants import Constants

        assert Constants.TYPE_A == 1
        assert Constants.TYPE_B == 2
        assert Constants.SUB_X == 10
        assert Constants.SUB_Y == 20

    def test_boundary_types_descriptor(self):
        from boundary_types.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'boundary_types'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_mixed_endian_descriptor(self):
        from mixed_endian.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'mixed_endian'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_wire_encodings_descriptor(self):
        from wire_encodings.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'wire_encodings'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_constraints_descriptor(self):
        from constraints.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'constraints'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_struct_features_descriptor(self):
        from struct_features.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'struct_features'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_field_scale_descriptor(self):
        from field_scale.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'field_scale'
        assert ProtocolDescriptor.VERSION == '2.0'

    def test_string_features_descriptor(self):
        from string_features.protocol import ProtocolDescriptor

        assert ProtocolDescriptor.NAME == 'string_features'
        assert ProtocolDescriptor.VERSION == '2.0'


# ============================================================================
# Session: encode_wrap, decode_frame, format_message, sequence, reset
# ============================================================================


class TestSessionExtendedCoverage:
    """Extended session tests covering encode_wrap, format_message, reset."""

    def test_packet_session_encode_wrap_ping(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import PingBody

        session = PacketSession()
        ping = PingBody()
        ping.timestamp = 12345

        result = session.encode_wrap(0x0ad7bb3ecc473399, ping)
        assert result is not None
        assert 'bytes' in result
        assert 'type_id' in result
        assert result['type_id'] == 0x0ad7bb3ecc473399
        assert len(result['bytes']) > 0

    def test_packet_session_encode_wrap_data(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import DataBody

        session = PacketSession()
        data_msg = DataBody()
        data_msg.channel = 5
        data_msg.payload_a = 100
        data_msg.payload_b = 200

        result = session.encode_wrap(0x29d16b9e73f85835, data_msg)
        assert result is not None
        assert result['type_id'] == 0x29d16b9e73f85835

    def test_packet_session_encode_wrap_unknown(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        result = session.encode_wrap(0xDEADBEEF, None)
        assert result is None

    def test_packet_session_sequence_counter(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import PingBody, Packet

        session = PacketSession()
        ping = PingBody()
        ping.timestamp = 1

        r1 = session.encode_wrap(0x0ad7bb3ecc473399, ping)
        frame1 = Packet.decode_bytes(r1['bytes'])

        r2 = session.encode_wrap(0x0ad7bb3ecc473399, ping)
        frame2 = Packet.decode_bytes(r2['bytes'])

        assert frame2.seq == frame1.seq + 1

    def test_packet_session_reset(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import PingBody, Packet

        session = PacketSession()
        ping = PingBody()
        ping.timestamp = 1

        # Advance sequence
        for _ in range(5):
            session.encode_wrap(0x0ad7bb3ecc473399, ping)

        session.reset()

        r = session.encode_wrap(0x0ad7bb3ecc473399, ping)
        frame = Packet.decode_bytes(r['bytes'])
        assert frame.seq == 0

    def test_packet_session_decode_frame(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import PingBody, Packet

        session = PacketSession()

        # Build a frame
        ping = PingBody()
        ping.timestamp = 9999
        frame = Packet.wrap(ping)
        frame.seq = 0
        frame_bytes = frame.encode_bytes()

        messages = session.decode_frame(frame_bytes)
        assert messages is not None
        assert len(messages) == 1
        assert messages[0]['type_name'] == 'PingBody'
        assert messages[0]['payload'].timestamp == 9999

    def test_packet_session_decode_frame_invalid(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        result = session.decode_frame(b"\x00\x01\x02\x03")
        # Should return None or empty on decode failure
        assert result is None or result == []

    def test_packet_session_format_message(self):
        from session_protocol.sessions import PacketSession
        from session_protocol.messages import PingBody

        session = PacketSession()
        ping = PingBody()
        ping.timestamp = 42

        formatted = session.format_message(0x0ad7bb3ecc473399, ping)
        assert 'PingBody' in formatted
        assert '42' in formatted

    def test_packet_session_type_name(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        assert session.type_name(0x0ad7bb3ecc473399) == 'PingBody'
        assert session.type_name(0x29d16b9e73f85835) == 'DataBody'
        assert session.type_name(0xcc431e5e357bc2e6) == 'AckBody'
        assert session.type_name(0xDEADBEEF) == 'unknown'

    def test_packet_session_leaf_type_ids(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 3
        assert 0x0ad7bb3ecc473399 in ids
        assert 0x29d16b9e73f85835 in ids
        assert 0xcc431e5e357bc2e6 in ids

    def test_packet_session_is_receive_only(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        assert session.is_receive_only(0xcc431e5e357bc2e6) is True  # AckBody
        assert session.is_receive_only(0x0ad7bb3ecc473399) is False  # PingBody
        assert session.is_receive_only(0x29d16b9e73f85835) is False  # DataBody

    def test_packet_session_sync_pattern(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        sync = session.sync_pattern()
        assert sync == b'\xde\xad'

    def test_packet_session_protocol_name(self):
        from session_protocol.sessions import PacketSession

        session = PacketSession()
        assert session.protocol_name() == 'session_test'


# ============================================================================
# BitReader / BitWriter edge cases
# ============================================================================


class TestBitIoEdgeCases:
    """Edge cases for the generated BitReader/BitWriter classes."""

    def test_write_read_single_bit(self):
        from all_types import BitReader, BitWriter

        w = BitWriter()
        w.write_bits(1, 1)
        w.write_bits(0, 1)
        w.write_bits(1, 1)
        w.write_bits(0, 5)  # pad to byte boundary
        data = w.to_bytes()

        r = BitReader(data)
        assert r.read_bits(1) == 1
        assert r.read_bits(1) == 0
        assert r.read_bits(1) == 1

    def test_write_read_u64(self):
        from all_types import BitReader, BitWriter

        w = BitWriter()
        w.write_u64(0x0102030405060708, True)
        data = w.to_bytes()

        r = BitReader(data)
        assert r.read_u64(True) == 0x0102030405060708

    def test_write_read_signed_bits(self):
        from all_types import BitReader, BitWriter

        for val in [-1, -128, 0, 1, 127]:
            w = BitWriter()
            w.write_signed_bits(val, 8)
            data = w.to_bytes()
            r = BitReader(data)
            assert r.read_signed_bits(8) == val

    def test_write_read_string(self):
        from all_types import BitReader, BitWriter

        w = BitWriter()
        w.write_string("Hello", 10, 0)
        data = w.to_bytes()

        r = BitReader(data)
        s = r.read_string(10)
        assert s.startswith("Hello")

    def test_skip_bits(self):
        from all_types import BitReader, BitWriter

        w = BitWriter()
        w.write_u8(0xAA)
        w.write_u8(0xBB)
        w.write_u8(0xCC)
        data = w.to_bytes()

        r = BitReader(data)
        r.skip_bits(8)  # skip 0xAA
        assert r.read_u8() == 0xBB

    def test_align_to(self):
        """Test byte-level alignment matching AlignedMessage pattern (align_to(2))."""
        from all_types import BitReader, BitWriter

        w = BitWriter()
        w.write_u8(0x42)        # 1 byte
        w.align_to(2)           # align to 2-byte boundary -> pad 1 byte
        w.write_u32(0xDEADBEEF, True)
        data = w.to_bytes()
        assert len(data) == 6   # 1 + 1 pad + 4

        r = BitReader(data)
        assert r.read_u8() == 0x42
        r.align_to(2)
        assert r.read_u32(True) == 0xDEADBEEF


# ============================================================================
# Repr tests
# ============================================================================


class TestReprOutput:
    """Test __repr__ for all major message types."""

    def test_wire_encoding_repr(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 100
        r = repr(msg)
        assert 'WireEncodingMsg' in r
        assert '100' in r

    def test_boundary_msg_repr(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 3
        msg.medium = 50
        msg.byte_val = 0
        msg.word = 0
        msg.dword = 0
        msg.qword = 0
        msg.signed_byte = 0
        msg.signed_word = 0
        msg.signed_dword = 0
        msg.signed_qword = 0
        msg.temp = ScaledTemp(0)
        msg.level = NybbleEnum.LOW
        r = repr(msg)
        assert 'BoundaryMsg' in r

    def test_odd_width_repr(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 100
        r = repr(msg)
        assert 'OddWidthMsg' in r
        assert '100' in r

    def test_aligned_message_repr(self):
        from struct_features import AlignedMessage

        msg = AlignedMessage()
        msg.flag = 1
        msg.data = 42
        r = repr(msg)
        assert 'AlignedMessage' in r

    def test_choice_msg_repr(self):
        from arrays_choices.messages import ChoiceMsg

        msg = ChoiceMsg()
        msg.msg_type = 1
        msg.length = 0
        r = repr(msg)
        assert 'ChoiceMsg' in r

    def test_constraint_msg_repr(self):
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0xBEEF
        r = repr(msg)
        assert 'ConstraintMsg' in r
        assert '48879' in r  # 0xBEEF = 48879

    def test_scale_msg_repr(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 3.14
        msg.temp = 0.0
        msg.plain = 42
        r = repr(msg)
        assert 'ScaleMsg' in r

    def test_string_msg_repr(self):
        from string_features import StringMsg

        msg = StringMsg()
        msg.id = 1
        r = repr(msg)
        assert 'StringMsg' in r

    def test_conditional_message_repr(self):
        from struct_features import ConditionalMessage

        msg = ConditionalMessage()
        msg.has_extra = 0
        msg.base_value = 10
        r = repr(msg)
        assert 'ConditionalMessage' in r
        assert 'None' in r  # extra_value is None
