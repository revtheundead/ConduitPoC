"""Tests for array and choice type encode/decode in the Conduit generated Python codecs."""
import struct
import pytest

from arrays_choices import (
    BitReader,
    BitWriter,
    DecodeError,
    Constants,
)
from arrays_choices.structs import Point, SubX, SubY, TypeABody, TypeBBody, FallbackBody
from arrays_choices.messages import FixedArrayMsg, CountFromArrayMsg, ChoiceMsg


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
class TestArraysChoicesConstants:

    def test_type_a_value(self):
        assert Constants.TYPE_A == 1

    def test_type_b_value(self):
        assert Constants.TYPE_B == 2

    def test_sub_x_value(self):
        assert Constants.SUB_X == 10

    def test_sub_y_value(self):
        assert Constants.SUB_Y == 20


# ---------------------------------------------------------------------------
# Point struct
# ---------------------------------------------------------------------------
class TestPointStruct:

    def test_default_values(self):
        p = Point()
        assert p.x == 0
        assert p.y == 0

    def test_roundtrip(self):
        p = Point()
        p.x = 100
        p.y = 200
        data = p.encode_bytes()
        p2 = Point.decode_bytes(data)
        assert p2.x == 100
        assert p2.y == 200

    def test_max_values(self):
        p = Point()
        p.x = 0xFFFF
        p.y = 0xFFFF
        data = p.encode_bytes()
        p2 = Point.decode_bytes(data)
        assert p2.x == 0xFFFF
        assert p2.y == 0xFFFF

    def test_zero_values(self):
        p = Point()
        p.x = 0
        p.y = 0
        data = p.encode_bytes()
        p2 = Point.decode_bytes(data)
        assert p2.x == 0
        assert p2.y == 0

    def test_wire_size(self):
        """Each Point has two u16 fields = 4 bytes."""
        p = Point()
        p.x = 1
        p.y = 2
        data = p.encode_bytes()
        assert len(data) == 4

    def test_encode_big_endian(self):
        p = Point()
        p.x = 0x1234
        p.y = 0x5678
        data = p.encode_bytes()
        assert data == bytes([0x12, 0x34, 0x56, 0x78])

    def test_repr(self):
        p = Point()
        p.x = 10
        p.y = 20
        assert "10" in repr(p)
        assert "20" in repr(p)


# ---------------------------------------------------------------------------
# FixedArrayMsg
# ---------------------------------------------------------------------------
class TestFixedArrayMsg:

    def test_roundtrip_with_three_points(self):
        msg = FixedArrayMsg()
        for i in range(3):
            p = Point()
            p.x = (i + 1) * 10
            p.y = (i + 1) * 20
            msg.points.append(p)

        data = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data)
        assert len(msg2.points) == 3
        assert msg2.points[0].x == 10
        assert msg2.points[0].y == 20
        assert msg2.points[1].x == 20
        assert msg2.points[1].y == 40
        assert msg2.points[2].x == 30
        assert msg2.points[2].y == 60

    def test_wire_size(self):
        """3 Points * 4 bytes each = 12 bytes."""
        msg = FixedArrayMsg()
        for _ in range(3):
            msg.points.append(Point())
        data = msg.encode_bytes()
        assert len(data) == 12

    def test_decode_always_reads_three(self):
        """Regardless of what we put in, decode reads exactly 3 Points."""
        data = bytes(12)  # 3 Points of zeros
        msg = FixedArrayMsg.decode_bytes(data)
        assert len(msg.points) == 3

    def test_idempotent(self):
        msg = FixedArrayMsg()
        for i in range(3):
            p = Point()
            p.x = i * 100
            p.y = i * 200
            msg.points.append(p)
        data1 = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_max_value_points(self):
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

    def test_zero_array(self):
        """All zeros should decode fine."""
        msg = FixedArrayMsg()
        for _ in range(3):
            msg.points.append(Point())
        data = msg.encode_bytes()
        msg2 = FixedArrayMsg.decode_bytes(data)
        for pt in msg2.points:
            assert pt.x == 0
            assert pt.y == 0


# ---------------------------------------------------------------------------
# CountFromArrayMsg
# ---------------------------------------------------------------------------
class TestCountFromArrayMsg:

    def test_roundtrip_with_dynamic_count(self):
        msg = CountFromArrayMsg()
        msg.num_items = 2
        for i in range(2):
            p = Point()
            p.x = (i + 1) * 5
            p.y = (i + 1) * 10
            msg.items.append(p)

        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 2
        assert len(msg2.items) == 2
        assert msg2.items[0].x == 5
        assert msg2.items[0].y == 10
        assert msg2.items[1].x == 10
        assert msg2.items[1].y == 20

    def test_zero_items(self):
        msg = CountFromArrayMsg()
        msg.num_items = 0
        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 0
        assert len(msg2.items) == 0

    def test_single_item(self):
        msg = CountFromArrayMsg()
        msg.num_items = 1
        p = Point()
        p.x = 42
        p.y = 84
        msg.items.append(p)
        data = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data)
        assert msg2.num_items == 1
        assert len(msg2.items) == 1
        assert msg2.items[0].x == 42
        assert msg2.items[0].y == 84

    def test_wire_size(self):
        """1 byte count + N * 4 bytes per Point."""
        msg = CountFromArrayMsg()
        msg.num_items = 3
        for _ in range(3):
            msg.items.append(Point())
        data = msg.encode_bytes()
        assert len(data) == 1 + 3 * 4

    def test_idempotent(self):
        msg = CountFromArrayMsg()
        msg.num_items = 2
        for i in range(2):
            p = Point()
            p.x = i
            p.y = i + 100
            msg.items.append(p)
        data1 = msg.encode_bytes()
        msg2 = CountFromArrayMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# ChoiceMsg with TypeABody
# ---------------------------------------------------------------------------
class TestChoiceMsgTypeA:

    def test_roundtrip(self):
        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5
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

    def test_with_sub_y(self):
        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_A
        msg.length = 5
        body = TypeABody()
        body.sub_type = Constants.SUB_Y
        body.sub_body = SubY()
        body.sub_body.a = 100
        body.sub_body.b = 200
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert isinstance(msg2.body, TypeABody)
        assert msg2.body.sub_type == Constants.SUB_Y
        assert isinstance(msg2.body.sub_body, SubY)
        assert msg2.body.sub_body.a == 100
        assert msg2.body.sub_body.b == 200


# ---------------------------------------------------------------------------
# ChoiceMsg with TypeBBody
# ---------------------------------------------------------------------------
class TestChoiceMsgTypeB:

    def test_roundtrip(self):
        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        msg.length = 4
        body = TypeBBody()
        body.tag = 0xCAFEBABE
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == Constants.TYPE_B
        assert isinstance(msg2.body, TypeBBody)
        assert msg2.body.tag == 0xCAFEBABE

    def test_tag_zero(self):
        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        msg.length = 4
        body = TypeBBody()
        body.tag = 0
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.body.tag == 0

    def test_tag_max(self):
        msg = ChoiceMsg()
        msg.msg_type = Constants.TYPE_B
        msg.length = 4
        body = TypeBBody()
        body.tag = 0xFFFFFFFF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.body.tag == 0xFFFFFFFF


# ---------------------------------------------------------------------------
# ChoiceMsg with FallbackBody
# ---------------------------------------------------------------------------
class TestChoiceMsgFallback:

    def test_roundtrip_unknown_type(self):
        msg = ChoiceMsg()
        msg.msg_type = 99  # not TYPE_A or TYPE_B
        msg.length = 4
        body = FallbackBody()
        body.raw = 0xDEAD1234
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert msg2.msg_type == 99
        assert isinstance(msg2.body, FallbackBody)
        assert msg2.body.raw == 0xDEAD1234

    def test_fallback_zero(self):
        msg = ChoiceMsg()
        msg.msg_type = 255
        msg.length = 4
        body = FallbackBody()
        body.raw = 0
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data)
        assert isinstance(msg2.body, FallbackBody)
        assert msg2.body.raw == 0

    def test_multiple_unknown_types(self):
        """Different unknown type codes should all fall through to FallbackBody."""
        for type_code in [0, 3, 50, 128, 255]:
            msg = ChoiceMsg()
            msg.msg_type = type_code
            msg.length = 4
            body = FallbackBody()
            body.raw = type_code
            msg.body = body

            data = msg.encode_bytes()
            msg2 = ChoiceMsg.decode_bytes(data)
            assert isinstance(msg2.body, FallbackBody)
            assert msg2.body.raw == type_code


# ---------------------------------------------------------------------------
# TypeABody with SubX choice
# ---------------------------------------------------------------------------
class TestTypeABodySubX:

    def test_roundtrip(self):
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0xDEADBEEF

        data = body.encode_bytes()
        body2 = TypeABody.decode_bytes(data)
        assert body2.sub_type == Constants.SUB_X
        assert isinstance(body2.sub_body, SubX)
        assert body2.sub_body.val == 0xDEADBEEF

    def test_sub_x_zero_val(self):
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0

        data = body.encode_bytes()
        body2 = TypeABody.decode_bytes(data)
        assert body2.sub_body.val == 0

    def test_sub_x_max_val(self):
        body = TypeABody()
        body.sub_type = Constants.SUB_X
        body.sub_body = SubX()
        body.sub_body.val = 0xFFFFFFFF

        data = body.encode_bytes()
        body2 = TypeABody.decode_bytes(data)
        assert body2.sub_body.val == 0xFFFFFFFF


# ---------------------------------------------------------------------------
# TypeABody with SubY choice
# ---------------------------------------------------------------------------
class TestTypeABodySubY:

    def test_roundtrip(self):
        body = TypeABody()
        body.sub_type = Constants.SUB_Y
        body.sub_body = SubY()
        body.sub_body.a = 0x1111
        body.sub_body.b = 0x2222

        data = body.encode_bytes()
        body2 = TypeABody.decode_bytes(data)
        assert body2.sub_type == Constants.SUB_Y
        assert isinstance(body2.sub_body, SubY)
        assert body2.sub_body.a == 0x1111
        assert body2.sub_body.b == 0x2222

    def test_sub_y_max(self):
        body = TypeABody()
        body.sub_type = Constants.SUB_Y
        body.sub_body = SubY()
        body.sub_body.a = 0xFFFF
        body.sub_body.b = 0xFFFF

        data = body.encode_bytes()
        body2 = TypeABody.decode_bytes(data)
        assert body2.sub_body.a == 0xFFFF
        assert body2.sub_body.b == 0xFFFF


# ---------------------------------------------------------------------------
# SubX standalone
# ---------------------------------------------------------------------------
class TestSubXStruct:

    def test_default(self):
        s = SubX()
        assert s.val == 0

    def test_roundtrip(self):
        s = SubX()
        s.val = 42
        data = s.encode_bytes()
        s2 = SubX.decode_bytes(data)
        assert s2.val == 42

    def test_wire_size(self):
        s = SubX()
        s.val = 0
        assert len(s.encode_bytes()) == 4


# ---------------------------------------------------------------------------
# SubY standalone
# ---------------------------------------------------------------------------
class TestSubYStruct:

    def test_default(self):
        s = SubY()
        assert s.a == 0
        assert s.b == 0

    def test_roundtrip(self):
        s = SubY()
        s.a = 300
        s.b = 400
        data = s.encode_bytes()
        s2 = SubY.decode_bytes(data)
        assert s2.a == 300
        assert s2.b == 400

    def test_wire_size(self):
        s = SubY()
        assert len(s.encode_bytes()) == 4


# ---------------------------------------------------------------------------
# TypeBBody standalone
# ---------------------------------------------------------------------------
class TestTypeBBodyStruct:

    def test_default(self):
        b = TypeBBody()
        assert b.tag == 0

    def test_roundtrip(self):
        b = TypeBBody()
        b.tag = 0xABCDEF01
        data = b.encode_bytes()
        b2 = TypeBBody.decode_bytes(data)
        assert b2.tag == 0xABCDEF01


# ---------------------------------------------------------------------------
# FallbackBody standalone
# ---------------------------------------------------------------------------
class TestFallbackBodyStruct:

    def test_default(self):
        f = FallbackBody()
        assert f.raw == 0

    def test_roundtrip(self):
        f = FallbackBody()
        f.raw = 0x99887766
        data = f.encode_bytes()
        f2 = FallbackBody.decode_bytes(data)
        assert f2.raw == 0x99887766


# ---------------------------------------------------------------------------
# ChoiceMsg idempotent
# ---------------------------------------------------------------------------
class TestChoiceMsgIdempotent:

    @pytest.mark.parametrize("type_code,body_factory,body_len", [
        (1, lambda: _make_type_a_body(Constants.SUB_X), 5),  # sub_type(1) + SubX(4)
        (1, lambda: _make_type_a_body(Constants.SUB_Y), 5),  # sub_type(1) + SubY(4)
        (2, lambda: _make_type_b_body(), 4),                  # TypeBBody(4)
        (99, lambda: _make_fallback_body(), 4),               # FallbackBody(4)
    ])
    def test_encode_decode_encode_stable(self, type_code, body_factory, body_len):
        msg = ChoiceMsg()
        msg.msg_type = type_code
        msg.length = body_len
        msg.body = body_factory()

        data1 = msg.encode_bytes()
        msg2 = ChoiceMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_type_a_body(sub_type):
    body = TypeABody()
    body.sub_type = sub_type
    if sub_type == Constants.SUB_X:
        body.sub_body = SubX()
        body.sub_body.val = 0x1234
    elif sub_type == Constants.SUB_Y:
        body.sub_body = SubY()
        body.sub_body.a = 10
        body.sub_body.b = 20
    return body


def _make_type_b_body():
    body = TypeBBody()
    body.tag = 0xABCD
    return body


def _make_fallback_body():
    body = FallbackBody()
    body.raw = 0xFFFF
    return body
