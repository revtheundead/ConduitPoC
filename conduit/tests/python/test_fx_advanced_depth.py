"""Deep FxAdvanced and FxChoice tests matching C++ test_fx_edge_cases.cpp depth.
Covers: nested FX blocks (outer extent only, both extents, inner triggers outer),
FxAdvanced wire format sizes, FxChoice (choice inside FX) roundtrip.
"""
import pytest


# ---------------------------------------------------------------------------
# FxAdvanced: no FX items (C++ "FxAdvanced no FX items roundtrip")
# ---------------------------------------------------------------------------

class TestFxAdvancedNoItems:

    def test_roundtrip_preserves_header(self):
        from fx_advanced import FxAdvancedMsg

        msg = FxAdvancedMsg()
        msg.header = 0xBB
        msg.scaled_temp = None
        msg.status = None
        msg.label = None
        msg.sub = None
        msg.values = None

        encoded = msg.encode_bytes()
        decoded = FxAdvancedMsg.decode_bytes(encoded)
        assert decoded.header == 0xBB
        assert decoded.scaled_temp is None
        assert decoded.status is None
        assert decoded.label is None
        assert decoded.sub is None
        assert decoded.values is None


# ---------------------------------------------------------------------------
# FxAdvanced: outer extent only (C++ "FxAdvanced outer extent only")
# ---------------------------------------------------------------------------

class TestFxAdvancedOuterExtentOnly:

    def test_outer_extent_fields_present(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.types import FxStatus

        msg = FxAdvancedMsg()
        msg.header = 0x11
        msg.scaled_temp = 100.0
        msg.status = FxStatus.ACTIVE
        msg.label = "Test"
        msg.sub = None
        msg.values = None

        encoded = msg.encode_bytes()
        decoded = FxAdvancedMsg.decode_bytes(encoded)
        assert decoded.header == 0x11
        assert decoded.scaled_temp is not None
        assert abs(decoded.scaled_temp - 100.0) < 0.02
        assert decoded.status == FxStatus.ACTIVE
        assert decoded.label == "Test"
        # Inner FX items should NOT be present
        assert decoded.sub is None
        assert decoded.values is None


# ---------------------------------------------------------------------------
# FxAdvanced: both extents populated (C++ "FxAdvanced both extents populated")
# ---------------------------------------------------------------------------

class TestFxAdvancedBothExtents:

    def test_both_extents_roundtrip(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.types import FxStatus, Uint8
        from fx_advanced.structs import FxSubStruct

        msg = FxAdvancedMsg()
        msg.header = 0x22
        msg.scaled_temp = -100.0
        msg.status = FxStatus.COMPLETE
        msg.label = "Full"

        sub = FxSubStruct()
        sub.a = 0xAA
        sub.b = 0xBB
        msg.sub = sub
        msg.values = [Uint8(1), Uint8(2), Uint8(3), Uint8(4)]

        encoded = msg.encode_bytes()
        decoded = FxAdvancedMsg.decode_bytes(encoded)
        assert decoded.header == 0x22

        # Outer extent
        assert decoded.scaled_temp is not None
        assert abs(decoded.scaled_temp - (-100.0)) < 0.02
        assert decoded.status == FxStatus.COMPLETE
        assert decoded.label == "Full"

        # Inner extent
        assert decoded.sub is not None
        assert decoded.sub.a == 0xAA
        assert decoded.sub.b == 0xBB
        assert decoded.values is not None
        assert len(decoded.values) == 4
        vals = [v.value if hasattr(v, 'value') else v for v in decoded.values]
        assert vals == [1, 2, 3, 4]


# ---------------------------------------------------------------------------
# FxAdvanced: inner extent triggers outer
# (C++ "FxAdvanced nested extent only triggers full outer")
# ---------------------------------------------------------------------------

class TestFxAdvancedInnerTriggersOuter:

    def test_setting_inner_fx_triggers_outer(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.structs import FxSubStruct

        msg = FxAdvancedMsg()
        msg.header = 0x33

        # Set only inner FX items
        sub = FxSubStruct()
        sub.a = 0x11
        sub.b = 0x22
        msg.sub = sub

        encoded = msg.encode_bytes()
        decoded = FxAdvancedMsg.decode_bytes(encoded)
        assert decoded.header == 0x33

        # Outer extent items should also be present (all-or-nothing per extent)
        assert decoded.scaled_temp is not None
        assert decoded.status is not None
        assert decoded.label is not None

        # Inner extent items
        assert decoded.sub is not None
        assert decoded.sub.a == 0x11
        assert decoded.sub.b == 0x22
        assert decoded.values is not None


# ---------------------------------------------------------------------------
# FxAdvanced: wire format sizes
# (C++ "FxAdvanced wire format: no items = 2 bytes")
# ---------------------------------------------------------------------------

class TestFxAdvancedWireFormat:

    def test_no_items_is_2_bytes(self):
        from fx_advanced import FxAdvancedMsg

        msg = FxAdvancedMsg()
        msg.header = 0x00
        msg.scaled_temp = None
        msg.status = None
        msg.label = None
        msg.sub = None
        msg.values = None

        encoded = msg.encode_bytes()
        # header(8) + FX=0(1) = 9 bits -> 2 bytes
        assert len(encoded) == 2

    def test_outer_extent_is_larger_than_empty(self):
        from fx_advanced import FxAdvancedMsg

        msg_empty = FxAdvancedMsg()
        msg_empty.header = 0x00
        msg_empty.scaled_temp = None
        msg_empty.status = None
        msg_empty.label = None
        msg_empty.sub = None
        msg_empty.values = None
        empty_bytes = msg_empty.encode_bytes()

        msg_outer = FxAdvancedMsg()
        msg_outer.header = 0x00
        msg_outer.scaled_temp = 0.0
        outer_bytes = msg_outer.encode_bytes()

        assert len(outer_bytes) > len(empty_bytes)

    def test_both_extents_larger_than_outer_only(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.structs import FxSubStruct

        msg_outer = FxAdvancedMsg()
        msg_outer.header = 0x00
        msg_outer.scaled_temp = 0.0

        outer_bytes = msg_outer.encode_bytes()

        msg_both = FxAdvancedMsg()
        msg_both.header = 0x00
        msg_both.scaled_temp = 0.0
        sub = FxSubStruct()
        sub.a = 0
        sub.b = 0
        msg_both.sub = sub

        both_bytes = msg_both.encode_bytes()
        assert len(both_bytes) > len(outer_bytes)


# ---------------------------------------------------------------------------
# FxAdvanced: decode empty buffer fails
# ---------------------------------------------------------------------------

class TestFxAdvancedErrors:

    def test_decode_empty_buffer_fails(self):
        from fx_advanced import FxAdvancedMsg

        with pytest.raises(Exception):
            FxAdvancedMsg.decode_bytes(b'')


# ---------------------------------------------------------------------------
# FxChoice: CaseA roundtrip (C++ "FxChoice: CaseA roundtrip")
# ---------------------------------------------------------------------------

class TestFxChoiceCaseA:

    def test_case_a_roundtrip(self):
        from fx_choice import FxChoiceMsg
        from fx_choice.structs import CaseABody
        from fx_choice.types import TagType

        msg = FxChoiceMsg()
        msg.header = 0x42
        msg.item1 = 1000
        msg.tag = TagType.CASE_A
        body = CaseABody()
        body.x = 0xABCD
        msg.payload = body
        msg.item3 = 99

        encoded = msg.encode_bytes()
        decoded = FxChoiceMsg.decode_bytes(encoded)
        assert decoded.header == 0x42
        assert decoded.item1 == 1000
        assert decoded.tag == TagType.CASE_A
        assert isinstance(decoded.payload, CaseABody)
        assert decoded.payload.x == 0xABCD
        assert decoded.item3 == 99


# ---------------------------------------------------------------------------
# FxChoice: CaseB roundtrip (C++ "FxChoice: CaseB roundtrip")
# ---------------------------------------------------------------------------

class TestFxChoiceCaseB:

    def test_case_b_roundtrip(self):
        from fx_choice import FxChoiceMsg
        from fx_choice.structs import CaseBBody
        from fx_choice.types import TagType

        msg = FxChoiceMsg()
        msg.header = 0x55
        msg.item1 = 2000
        msg.tag = TagType.CASE_B
        body = CaseBBody()
        body.y = 0xDEADBEEF
        msg.payload = body
        msg.item3 = 7

        encoded = msg.encode_bytes()
        decoded = FxChoiceMsg.decode_bytes(encoded)
        assert decoded.header == 0x55
        assert decoded.tag == TagType.CASE_B
        assert isinstance(decoded.payload, CaseBBody)
        assert decoded.payload.y == 0xDEADBEEF
        assert decoded.item3 == 7


# ---------------------------------------------------------------------------
# FxChoice: no FX items (C++ "FxChoice: no FX items roundtrip")
# ---------------------------------------------------------------------------

class TestFxChoiceNoItems:

    def test_no_fx_items_roundtrip(self):
        from fx_choice import FxChoiceMsg

        msg = FxChoiceMsg()
        msg.header = 0x00

        encoded = msg.encode_bytes()
        decoded = FxChoiceMsg.decode_bytes(encoded)
        assert decoded.header == 0x00
        assert decoded.item1 is None
        assert decoded.tag is None
        assert decoded.payload is None
        assert decoded.item3 is None


# ---------------------------------------------------------------------------
# FxAdvanced: double-encode idempotency
# ---------------------------------------------------------------------------

class TestFxAdvancedDoubleEncode:

    def test_full_message_double_encode(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.types import FxStatus, Uint8
        from fx_advanced.structs import FxSubStruct

        msg = FxAdvancedMsg()
        msg.header = 0x10
        msg.scaled_temp = -10.0
        msg.status = FxStatus.ACTIVE
        msg.label = "hello"
        msg.sub = FxSubStruct()
        msg.sub.a = 11
        msg.sub.b = 22
        msg.values = [Uint8(1), Uint8(2), Uint8(3), Uint8(4)]

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second
