"""Roundtrip encode/decode tests for inline struct generated Python modules.

Covers:
- inline_struct: frame-based protocol with BodyX (uint32) and BodyY (uint16)
  messages, frame has sync/seq/length/msg-type/payload
- inline_struct_overlap: two messages with same-named inline structs (items)
  and same-named inline arrays (entries) with different fields
- inline_case_collision: two messages with same-named choice cases (TypeA, TypeB)
  but different field layouts
"""
import pytest


# ============================================================================
# inline_struct: Frame protocol with BodyX and BodyY messages
# ============================================================================


class TestInlineStructBodyX:
    """BodyX message with x_data (uint32)."""

    def test_body_x_roundtrip(self):
        from inline_struct import BodyX

        msg = BodyX()
        msg.x_data = 0xDEADBEEF

        data = msg.encode_bytes()
        msg2 = BodyX.decode_bytes(data)
        assert msg2.x_data == 0xDEADBEEF

    def test_body_x_zero(self):
        from inline_struct import BodyX

        msg = BodyX()
        msg.x_data = 0

        data = msg.encode_bytes()
        msg2 = BodyX.decode_bytes(data)
        assert msg2.x_data == 0

    def test_body_x_max(self):
        from inline_struct import BodyX

        msg = BodyX()
        msg.x_data = 0xFFFFFFFF

        data = msg.encode_bytes()
        msg2 = BodyX.decode_bytes(data)
        assert msg2.x_data == 0xFFFFFFFF

    def test_body_x_double_encode(self):
        from inline_struct import BodyX

        msg = BodyX()
        msg.x_data = 0x12345678

        data1 = msg.encode_bytes()
        msg2 = BodyX.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_body_x_wire_size(self):
        from inline_struct import BodyX

        msg = BodyX()
        msg.x_data = 1

        data = msg.encode_bytes()
        assert len(data) == 4  # uint32 = 4 bytes


class TestInlineStructBodyY:
    """BodyY message with y_data (uint16)."""

    def test_body_y_roundtrip(self):
        from inline_struct import BodyY

        msg = BodyY()
        msg.y_data = 0xABCD

        data = msg.encode_bytes()
        msg2 = BodyY.decode_bytes(data)
        assert msg2.y_data == 0xABCD

    def test_body_y_zero(self):
        from inline_struct import BodyY

        msg = BodyY()
        msg.y_data = 0

        data = msg.encode_bytes()
        msg2 = BodyY.decode_bytes(data)
        assert msg2.y_data == 0

    def test_body_y_max(self):
        from inline_struct import BodyY

        msg = BodyY()
        msg.y_data = 0xFFFF

        data = msg.encode_bytes()
        msg2 = BodyY.decode_bytes(data)
        assert msg2.y_data == 0xFFFF

    def test_body_y_double_encode(self):
        from inline_struct import BodyY

        msg = BodyY()
        msg.y_data = 0x5678

        data1 = msg.encode_bytes()
        msg2 = BodyY.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_body_y_wire_size(self):
        from inline_struct import BodyY

        msg = BodyY()
        msg.y_data = 1

        data = msg.encode_bytes()
        assert len(data) == 2  # uint16 = 2 bytes


class TestInlineStructFrameSession:
    """Test frame session for inline_struct protocol."""

    def test_session_exists(self):
        from inline_struct import FrameSession

        session = FrameSession()
        assert session is not None

    def test_body_x_type_id(self):
        from inline_struct import BodyX

        assert hasattr(BodyX, 'TYPE_ID')
        assert hasattr(BodyX, 'ID_VALUE')
        assert BodyX.ID_VALUE == 1

    def test_body_y_type_id(self):
        from inline_struct import BodyY

        assert hasattr(BodyY, 'TYPE_ID')
        assert hasattr(BodyY, 'ID_VALUE')
        assert BodyY.ID_VALUE == 2

    def test_encode_wrap_body_x(self):
        from inline_struct import FrameSession, BodyX

        session = FrameSession()
        msg = BodyX()
        msg.x_data = 0xCAFEBABE

        result = session.encode_wrap(BodyX.TYPE_ID, msg)
        assert result is not None
        assert 'bytes' in result

    def test_encode_wrap_decode_frame_body_x(self):
        from inline_struct import FrameSession, BodyX

        session = FrameSession()
        msg = BodyX()
        msg.x_data = 0x11223344

        wrapped = session.encode_wrap(BodyX.TYPE_ID, msg)
        assert wrapped is not None

        messages = session.decode_frame(wrapped['bytes'])
        assert messages is not None
        assert len(messages) == 1
        decoded = messages[0]['payload']
        assert decoded.x_data == 0x11223344

    def test_encode_wrap_decode_frame_body_y(self):
        from inline_struct import FrameSession, BodyY

        session = FrameSession()
        msg = BodyY()
        msg.y_data = 0x5566

        wrapped = session.encode_wrap(BodyY.TYPE_ID, msg)
        assert wrapped is not None

        messages = session.decode_frame(wrapped['bytes'])
        assert messages is not None
        assert len(messages) == 1
        decoded = messages[0]['payload']
        assert decoded.y_data == 0x5566

    def test_sequence_increments(self):
        from inline_struct import FrameSession, BodyX

        session = FrameSession()
        msg = BodyX()
        msg.x_data = 1

        r1 = session.encode_wrap(BodyX.TYPE_ID, msg)
        r2 = session.encode_wrap(BodyX.TYPE_ID, msg)
        # Frames should have different bytes due to sequence increment
        assert r1['bytes'] != r2['bytes']


class TestInlineStructConstants:
    """Test constants defined in the inline_struct protocol."""

    def test_sync_constant(self):
        from inline_struct.constants import Constants

        assert Constants.SYNC == 0xCAFE


# ============================================================================
# inline_struct_overlap: MsgFoo and MsgBar with same-named inline structs,
# MsgAlpha and MsgBeta with same-named inline arrays
# ============================================================================


class TestInlineStructOverlapMsgFoo:
    """MsgFoo: count (uint8) + items inline struct (foo_x: uint32, foo_y: uint16)."""

    def test_roundtrip(self):
        from inline_struct_overlap import MsgFoo

        msg = MsgFoo()
        msg.count = 5
        msg.items.foo_x = 0xDEADBEEF
        msg.items.foo_y = 0x1234

        data = msg.encode_bytes()
        msg2 = MsgFoo.decode_bytes(data)
        assert msg2.count == 5
        assert msg2.items.foo_x == 0xDEADBEEF
        assert msg2.items.foo_y == 0x1234

    def test_zero_values(self):
        from inline_struct_overlap import MsgFoo

        msg = MsgFoo()
        msg.count = 0
        msg.items.foo_x = 0
        msg.items.foo_y = 0

        data = msg.encode_bytes()
        msg2 = MsgFoo.decode_bytes(data)
        assert msg2.count == 0
        assert msg2.items.foo_x == 0
        assert msg2.items.foo_y == 0

    def test_max_values(self):
        from inline_struct_overlap import MsgFoo

        msg = MsgFoo()
        msg.count = 0xFF
        msg.items.foo_x = 0xFFFFFFFF
        msg.items.foo_y = 0xFFFF

        data = msg.encode_bytes()
        msg2 = MsgFoo.decode_bytes(data)
        assert msg2.count == 0xFF
        assert msg2.items.foo_x == 0xFFFFFFFF
        assert msg2.items.foo_y == 0xFFFF

    def test_double_encode(self):
        from inline_struct_overlap import MsgFoo

        msg = MsgFoo()
        msg.count = 10
        msg.items.foo_x = 0xCAFE
        msg.items.foo_y = 0xBABE

        data1 = msg.encode_bytes()
        msg2 = MsgFoo.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_wire_size(self):
        from inline_struct_overlap import MsgFoo

        msg = MsgFoo()
        msg.count = 1
        msg.items.foo_x = 1
        msg.items.foo_y = 1

        data = msg.encode_bytes()
        # count(1) + foo_x(4) + foo_y(2) = 7
        assert len(data) == 7


class TestInlineStructOverlapMsgBar:
    """MsgBar: count (uint8) + items inline struct (bar_a, bar_b, bar_c: uint8)."""

    def test_roundtrip(self):
        from inline_struct_overlap import MsgBar

        msg = MsgBar()
        msg.count = 3
        msg.items.bar_a = 0xAA
        msg.items.bar_b = 0xBB
        msg.items.bar_c = 0xCC

        data = msg.encode_bytes()
        msg2 = MsgBar.decode_bytes(data)
        assert msg2.count == 3
        assert msg2.items.bar_a == 0xAA
        assert msg2.items.bar_b == 0xBB
        assert msg2.items.bar_c == 0xCC

    def test_zero_values(self):
        from inline_struct_overlap import MsgBar

        msg = MsgBar()
        msg.count = 0
        msg.items.bar_a = 0
        msg.items.bar_b = 0
        msg.items.bar_c = 0

        data = msg.encode_bytes()
        msg2 = MsgBar.decode_bytes(data)
        assert msg2.count == 0
        assert msg2.items.bar_a == 0
        assert msg2.items.bar_b == 0
        assert msg2.items.bar_c == 0

    def test_double_encode(self):
        from inline_struct_overlap import MsgBar

        msg = MsgBar()
        msg.count = 7
        msg.items.bar_a = 0x11
        msg.items.bar_b = 0x22
        msg.items.bar_c = 0x33

        data1 = msg.encode_bytes()
        msg2 = MsgBar.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_wire_size(self):
        from inline_struct_overlap import MsgBar

        msg = MsgBar()
        msg.count = 1
        msg.items.bar_a = 1
        msg.items.bar_b = 1
        msg.items.bar_c = 1

        data = msg.encode_bytes()
        # count(1) + bar_a(1) + bar_b(1) + bar_c(1) = 4
        assert len(data) == 4


class TestInlineStructOverlapDistinctTypes:
    """MsgFoo.items and MsgBar.items should be distinct types despite same name."""

    def test_items_are_different_types(self):
        from inline_struct_overlap import MsgFoo, MsgBar

        foo = MsgFoo()
        bar = MsgBar()
        assert type(foo.items) is not type(bar.items)


class TestInlineStructOverlapMsgAlpha:
    """MsgAlpha: tag (uint8) + entries array (count=2, alpha_val: uint32)."""

    def test_roundtrip(self):
        from inline_struct_overlap import MsgAlpha
        from inline_struct_overlap.messages import MsgAlphaEntries

        msg = MsgAlpha()
        msg.tag = 0x42
        e0 = MsgAlphaEntries()
        e0.alpha_val = 0x11111111
        e1 = MsgAlphaEntries()
        e1.alpha_val = 0x22222222
        msg.entries = [e0, e1]

        data = msg.encode_bytes()
        msg2 = MsgAlpha.decode_bytes(data)
        assert msg2.tag == 0x42
        assert msg2.entries[0].alpha_val == 0x11111111
        assert msg2.entries[1].alpha_val == 0x22222222

    def test_double_encode(self):
        from inline_struct_overlap import MsgAlpha
        from inline_struct_overlap.messages import MsgAlphaEntries

        msg = MsgAlpha()
        msg.tag = 0xAA
        e0 = MsgAlphaEntries()
        e0.alpha_val = 0xCAFEBABE
        e1 = MsgAlphaEntries()
        e1.alpha_val = 0xDEADBEEF
        msg.entries = [e0, e1]

        data1 = msg.encode_bytes()
        msg2 = MsgAlpha.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_wire_size(self):
        from inline_struct_overlap import MsgAlpha
        from inline_struct_overlap.messages import MsgAlphaEntries

        msg = MsgAlpha()
        msg.tag = 1
        e0 = MsgAlphaEntries()
        e0.alpha_val = 1
        e1 = MsgAlphaEntries()
        e1.alpha_val = 1
        msg.entries = [e0, e1]

        data = msg.encode_bytes()
        # tag(1) + 2 * alpha_val(4) = 9
        assert len(data) == 9


class TestInlineStructOverlapMsgBeta:
    """MsgBeta: tag (uint8) + entries array (count=3, beta_val: uint16)."""

    def test_roundtrip(self):
        from inline_struct_overlap import MsgBeta
        from inline_struct_overlap.messages import MsgBetaEntries

        msg = MsgBeta()
        msg.tag = 0x55
        e0 = MsgBetaEntries()
        e0.beta_val = 0x1111
        e1 = MsgBetaEntries()
        e1.beta_val = 0x2222
        e2 = MsgBetaEntries()
        e2.beta_val = 0x3333
        msg.entries = [e0, e1, e2]

        data = msg.encode_bytes()
        msg2 = MsgBeta.decode_bytes(data)
        assert msg2.tag == 0x55
        assert msg2.entries[0].beta_val == 0x1111
        assert msg2.entries[1].beta_val == 0x2222
        assert msg2.entries[2].beta_val == 0x3333

    def test_double_encode(self):
        from inline_struct_overlap import MsgBeta
        from inline_struct_overlap.messages import MsgBetaEntries

        msg = MsgBeta()
        msg.tag = 0xBB
        e0 = MsgBetaEntries()
        e0.beta_val = 100
        e1 = MsgBetaEntries()
        e1.beta_val = 200
        e2 = MsgBetaEntries()
        e2.beta_val = 300
        msg.entries = [e0, e1, e2]

        data1 = msg.encode_bytes()
        msg2 = MsgBeta.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_wire_size(self):
        from inline_struct_overlap import MsgBeta
        from inline_struct_overlap.messages import MsgBetaEntries

        msg = MsgBeta()
        msg.tag = 1
        e0 = MsgBetaEntries()
        e0.beta_val = 1
        e1 = MsgBetaEntries()
        e1.beta_val = 1
        e2 = MsgBetaEntries()
        e2.beta_val = 1
        msg.entries = [e0, e1, e2]

        data = msg.encode_bytes()
        # tag(1) + 3 * beta_val(2) = 7
        assert len(data) == 7


class TestInlineStructOverlapArrayDistinctTypes:
    """MsgAlpha.entries and MsgBeta.entries elements should be distinct types."""

    def test_entries_are_different_types(self):
        from inline_struct_overlap.messages import MsgAlphaEntries, MsgBetaEntries

        alpha_entry = MsgAlphaEntries()
        beta_entry = MsgBetaEntries()
        assert type(alpha_entry) is not type(beta_entry)


# ============================================================================
# inline_case_collision: MsgAlpha and MsgBeta with same-named choice cases
# ============================================================================


class TestInlineCaseCollisionMsgAlpha:
    """MsgAlpha: tag (uint8) + choice payload (TypeA: alpha_val uint32,
    TypeB: alpha_flag uint8)."""

    def test_case_a_roundtrip(self):
        from inline_case_collision import MsgAlpha

        msg = MsgAlpha()
        msg.tag = 1

        # Build TypeA case
        from inline_case_collision.messages import MsgAlphaTypeA
        body = MsgAlphaTypeA()
        body.alpha_val = 0xDEADBEEF
        msg.payload = body

        data = msg.encode_bytes()
        msg2 = MsgAlpha.decode_bytes(data)
        assert msg2.tag == 1
        assert msg2.payload.alpha_val == 0xDEADBEEF

    def test_case_b_roundtrip(self):
        from inline_case_collision import MsgAlpha

        msg = MsgAlpha()
        msg.tag = 2

        from inline_case_collision.messages import MsgAlphaTypeB
        body = MsgAlphaTypeB()
        body.alpha_flag = 0xAA
        msg.payload = body

        data = msg.encode_bytes()
        msg2 = MsgAlpha.decode_bytes(data)
        assert msg2.tag == 2
        assert msg2.payload.alpha_flag == 0xAA

    def test_case_a_double_encode(self):
        from inline_case_collision import MsgAlpha

        msg = MsgAlpha()
        msg.tag = 1

        from inline_case_collision.messages import MsgAlphaTypeA
        body = MsgAlphaTypeA()
        body.alpha_val = 0x12345678
        msg.payload = body

        data1 = msg.encode_bytes()
        msg2 = MsgAlpha.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestInlineCaseCollisionMsgBeta:
    """MsgBeta: tag (uint8) + choice payload (TypeA: beta_x + beta_y uint16,
    TypeB: beta_code uint16)."""

    def test_case_a_roundtrip(self):
        from inline_case_collision import MsgBeta

        msg = MsgBeta()
        msg.tag = 1

        from inline_case_collision.messages import MsgBetaTypeA
        body = MsgBetaTypeA()
        body.beta_x = 0x1111
        body.beta_y = 0x2222
        msg.payload = body

        data = msg.encode_bytes()
        msg2 = MsgBeta.decode_bytes(data)
        assert msg2.tag == 1
        assert msg2.payload.beta_x == 0x1111
        assert msg2.payload.beta_y == 0x2222

    def test_case_b_roundtrip(self):
        from inline_case_collision import MsgBeta

        msg = MsgBeta()
        msg.tag = 2

        from inline_case_collision.messages import MsgBetaTypeB
        body = MsgBetaTypeB()
        body.beta_code = 0xAAAA
        msg.payload = body

        data = msg.encode_bytes()
        msg2 = MsgBeta.decode_bytes(data)
        assert msg2.tag == 2
        assert msg2.payload.beta_code == 0xAAAA

    def test_case_a_double_encode(self):
        from inline_case_collision import MsgBeta

        msg = MsgBeta()
        msg.tag = 1

        from inline_case_collision.messages import MsgBetaTypeA
        body = MsgBetaTypeA()
        body.beta_x = 0xFFFF
        body.beta_y = 0xFFFF
        msg.payload = body

        data1 = msg.encode_bytes()
        msg2 = MsgBeta.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestInlineCaseCollisionDistinctCases:
    """TypeA/TypeB in MsgAlpha vs MsgBeta should be distinct types."""

    def test_type_a_are_different(self):
        from inline_case_collision import MsgAlpha, MsgBeta

        msg_a = MsgAlpha()
        msg_a.tag = 1
        msg_b = MsgBeta()
        msg_b.tag = 1

        # Build both TypeA cases
        from inline_case_collision.messages import MsgAlphaTypeA, MsgBetaTypeA
        body_a = MsgAlphaTypeA()
        body_b = MsgBetaTypeA()

        assert type(body_a) is not type(body_b)
