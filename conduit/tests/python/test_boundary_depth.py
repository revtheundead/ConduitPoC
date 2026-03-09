"""Boundary and edge case tests matching C++ test depth.
Covers: unsigned/signed integer boundaries, all-zero values,
double-encode idempotency across message types, choice type dispatch,
fixed array boundaries, and scaled values.
"""
import pytest


# ---------------------------------------------------------------------------
# Unsigned integer boundary values
# ---------------------------------------------------------------------------

class TestUnsignedBoundaries:

    def test_u8_max(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.u8 = 0xFF
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.u8 == 0xFF

    def test_u16_max(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.u16 = 0xFFFF
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.u16 == 0xFFFF

    def test_u32_max(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.u32 = 0xFFFFFFFF
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.u32 == 0xFFFFFFFF

    def test_u64_max(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.u64 = 0xFFFFFFFFFFFFFFFF
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.u64 == 0xFFFFFFFFFFFFFFFF


# ---------------------------------------------------------------------------
# Signed integer boundary values
# ---------------------------------------------------------------------------

class TestSignedBoundaries:

    def test_i8_min(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.i8 = -128
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.i8 == -128

    def test_i8_max(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.i8 = 127
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.i8 == 127

    def test_i16_min(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.i16 = -32768
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.i16 == -32768

    def test_i32_min(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.i32 = -2147483648
        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.i32 == -2147483648


# ---------------------------------------------------------------------------
# All-zero values
# ---------------------------------------------------------------------------

class TestAllZeroValues:

    def test_all_zeros_roundtrip(self):
        from all_types import AllTypesMessage
        msg = AllTypesMessage()
        msg.u8 = 0
        msg.u16 = 0
        msg.u32 = 0
        msg.u64 = 0
        msg.i8 = 0
        msg.i16 = 0
        msg.i32 = 0
        msg.f32 = 0.0
        msg.f64 = 0.0
        msg.flag = False

        decoded = AllTypesMessage.decode_bytes(msg.encode_bytes())
        assert decoded.u8 == 0
        assert decoded.u16 == 0
        assert decoded.u32 == 0
        assert decoded.u64 == 0
        assert decoded.i8 == 0
        assert decoded.i16 == 0
        assert decoded.i32 == 0
        assert decoded.f32 == 0.0
        assert decoded.f64 == 0.0
        assert decoded.flag is False


# ---------------------------------------------------------------------------
# Choice type edge cases
# ---------------------------------------------------------------------------

class TestChoiceEdgeCases:

    def test_choice_msg_type_a_roundtrip(self):
        from arrays_choices import ChoiceMsg, TypeABody, SubX
        from arrays_choices.constants import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        sub_x = SubX()
        sub_x.val = 0xDEADBEEF
        body.sub_body = sub_x
        msg.body = body
        msg.length = 5  # 1 byte sub_type + 4 bytes val

        decoded = ChoiceMsg.decode_bytes(msg.encode_bytes())
        assert isinstance(decoded.body, TypeABody)
        assert isinstance(decoded.body.sub_body, SubX)
        assert decoded.body.sub_body.val == 0xDEADBEEF

    def test_choice_msg_type_b_roundtrip(self):
        from arrays_choices import ChoiceMsg, TypeBBody
        from arrays_choices.constants import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        body = TypeBBody()
        body.tag = 0x12345678
        msg.body = body
        msg.length = 4  # 4 bytes tag

        decoded = ChoiceMsg.decode_bytes(msg.encode_bytes())
        assert isinstance(decoded.body, TypeBBody)
        assert decoded.body.tag == 0x12345678

    def test_choice_msg_double_encode(self):
        from arrays_choices import ChoiceMsg, TypeABody, SubX
        from arrays_choices.constants import Constants

        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        sub_x = SubX()
        sub_x.val = 42
        body.sub_body = sub_x
        msg.body = body
        msg.length = 5

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# Fixed array boundary values
# ---------------------------------------------------------------------------

class TestFixedArrayBoundaries:

    def test_all_max_values(self):
        from arrays_choices import FixedArrayMsg, Point

        msg = FixedArrayMsg()
        msg.points = []
        for _ in range(3):
            p = Point()
            p.x = 0xFFFF
            p.y = 0xFFFF
            msg.points.append(p)
        decoded = FixedArrayMsg.decode_bytes(msg.encode_bytes())
        assert len(decoded.points) == 3
        for p in decoded.points:
            assert p.x == 0xFFFF
            assert p.y == 0xFFFF

    def test_all_zero_values(self):
        from arrays_choices import FixedArrayMsg, Point

        msg = FixedArrayMsg()
        msg.points = []
        for _ in range(3):
            p = Point()
            p.x = 0
            p.y = 0
            msg.points.append(p)
        decoded = FixedArrayMsg.decode_bytes(msg.encode_bytes())
        assert len(decoded.points) == 3
        for p in decoded.points:
            assert p.x == 0
            assert p.y == 0

    def test_wire_size_is_12_bytes(self):
        from arrays_choices import FixedArrayMsg, Point

        msg = FixedArrayMsg()
        msg.points = []
        for i in range(3):
            p = Point()
            p.x = i + 1
            p.y = (i + 1) * 10
            msg.points.append(p)
        encoded = msg.encode_bytes()
        assert len(encoded) == 12, "3 Point structs (2 u16 each) = 12 bytes"


# ---------------------------------------------------------------------------
# Alpha/Beta choice bodies: max field values
# ---------------------------------------------------------------------------

class TestChoiceBodyBoundaries:

    def test_alpha_body_max_values(self):
        from choice_protocol import AlphaBody

        alpha = AlphaBody()
        alpha.x = 0xFFFF
        alpha.y = 0xFFFF
        decoded = AlphaBody.decode_bytes(alpha.encode_bytes())
        assert decoded.x == 0xFFFF
        assert decoded.y == 0xFFFF

    def test_beta_body_max_values(self):
        from choice_protocol import BetaBody

        beta = BetaBody()
        beta.payload_size = 0xFF
        beta.tag = 0xFFFFFFFF
        decoded = BetaBody.decode_bytes(beta.encode_bytes())
        assert decoded.payload_size == 0xFF
        assert decoded.tag == 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Point struct boundary values
# ---------------------------------------------------------------------------

class TestPointBoundaries:

    def test_max_values(self):
        from arrays_choices import Point

        p = Point()
        p.x = 0xFFFF
        p.y = 0xFFFF
        decoded = Point.decode_bytes(p.encode_bytes())
        assert decoded.x == 0xFFFF
        assert decoded.y == 0xFFFF

    def test_zero_values(self):
        from arrays_choices import Point

        p = Point()
        p.x = 0
        p.y = 0
        decoded = Point.decode_bytes(p.encode_bytes())
        assert decoded.x == 0
        assert decoded.y == 0

    def test_double_encode(self):
        from arrays_choices import Point

        p = Point()
        p.x = 100
        p.y = 200
        first = p.encode_bytes()
        second = p.encode_bytes()
        assert first == second
