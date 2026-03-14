"""Roundtrip encode/decode tests for FX block generated Python modules.

Covers:
- bitmap_fx: bitmap-presence struct inside an FX-extended message
- empty_fx: FX block with no extension fields
- fx_ia5_string: FX block with IA5-encoded string and uint16
- fx_string: FX block with ASCII string, bytes, and uint16
"""
import pytest


# ============================================================================
# bitmap_fx: BitmapItems (FSPEC bitmap) inside Category message
# ============================================================================


class TestBitmapFxAllPresent:
    """All three bitmap items are present."""

    def test_roundtrip_all_items(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem010, DataItem020, DataItem030

        msg = Category()
        items = BitmapItems()
        item010 = DataItem010()
        item010.sac = 0xAA
        item010.sic = 0xBB
        items.item010 = item010

        item020 = DataItem020()
        item020.code = 0x1234
        items.item020 = item020

        item030 = DataItem030()
        item030.value = 0xDEADBEEF
        items.item030 = item030

        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010.sac == 0xAA
        assert msg2.items.item010.sic == 0xBB
        assert msg2.items.item020.code == 0x1234
        assert msg2.items.item030.value == 0xDEADBEEF

    def test_double_encode_stability(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem010, DataItem020, DataItem030

        msg = Category()
        items = BitmapItems()
        item010 = DataItem010()
        item010.sac = 1
        item010.sic = 2
        items.item010 = item010
        item020 = DataItem020()
        item020.code = 300
        items.item020 = item020
        item030 = DataItem030()
        item030.value = 400
        items.item030 = item030
        msg.items = items

        data1 = msg.encode_bytes()
        msg2 = Category.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestBitmapFxPartialPresence:
    """Only some bitmap items are present."""

    def test_only_item010(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem010

        msg = Category()
        items = BitmapItems()
        item010 = DataItem010()
        item010.sac = 0x10
        item010.sic = 0x20
        items.item010 = item010
        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010.sac == 0x10
        assert msg2.items.item010.sic == 0x20
        assert msg2.items.item020 is None
        assert msg2.items.item030 is None

    def test_only_item030(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem030

        msg = Category()
        items = BitmapItems()
        item030 = DataItem030()
        item030.value = 0xCAFEBABE
        items.item030 = item030
        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010 is None
        assert msg2.items.item020 is None
        assert msg2.items.item030.value == 0xCAFEBABE


class TestBitmapFxNoItems:
    """No bitmap items present."""

    def test_empty_bitmap(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems

        msg = Category()
        items = BitmapItems()
        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010 is None
        assert msg2.items.item020 is None
        assert msg2.items.item030 is None


class TestBitmapFxBoundaryValues:
    """Boundary values for bitmap field types."""

    def test_max_values(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem010, DataItem020, DataItem030

        msg = Category()
        items = BitmapItems()
        item010 = DataItem010()
        item010.sac = 0xFF
        item010.sic = 0xFF
        items.item010 = item010
        item020 = DataItem020()
        item020.code = 0xFFFF
        items.item020 = item020
        item030 = DataItem030()
        item030.value = 0xFFFFFFFF
        items.item030 = item030
        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010.sac == 0xFF
        assert msg2.items.item010.sic == 0xFF
        assert msg2.items.item020.code == 0xFFFF
        assert msg2.items.item030.value == 0xFFFFFFFF

    def test_zero_values(self):
        from bitmap_fx import Category
        from bitmap_fx.structs import BitmapItems, DataItem010, DataItem020, DataItem030

        msg = Category()
        items = BitmapItems()
        item010 = DataItem010()
        item010.sac = 0
        item010.sic = 0
        items.item010 = item010
        item020 = DataItem020()
        item020.code = 0
        items.item020 = item020
        item030 = DataItem030()
        item030.value = 0
        items.item030 = item030
        msg.items = items

        data = msg.encode_bytes()
        msg2 = Category.decode_bytes(data)
        assert msg2.items.item010.sac == 0
        assert msg2.items.item010.sic == 0
        assert msg2.items.item020.code == 0
        assert msg2.items.item030.value == 0


# ============================================================================
# empty_fx: EmptyFxMsg has header + FX with no extension fields
# ============================================================================


class TestEmptyFxRoundtrip:
    """EmptyFxMsg has a header byte and an empty FX block."""

    def test_header_roundtrip(self):
        from empty_fx import EmptyFxMsg

        msg = EmptyFxMsg()
        msg.header = 0x42

        data = msg.encode_bytes()
        msg2 = EmptyFxMsg.decode_bytes(data)
        assert msg2.header == 0x42

    def test_header_zero(self):
        from empty_fx import EmptyFxMsg

        msg = EmptyFxMsg()
        msg.header = 0

        data = msg.encode_bytes()
        msg2 = EmptyFxMsg.decode_bytes(data)
        assert msg2.header == 0

    def test_header_max(self):
        from empty_fx import EmptyFxMsg

        msg = EmptyFxMsg()
        msg.header = 0xFF

        data = msg.encode_bytes()
        msg2 = EmptyFxMsg.decode_bytes(data)
        assert msg2.header == 0xFF

    def test_double_encode_stability(self):
        from empty_fx import EmptyFxMsg

        msg = EmptyFxMsg()
        msg.header = 0xAB

        data1 = msg.encode_bytes()
        msg2 = EmptyFxMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ============================================================================
# fx_ia5_string: FxIa5Msg with header + FX(label:ia5 string 8, code:uint16)
# ============================================================================


class TestFxIa5StringWithExtension:
    """FxIa5Msg with FX items present."""

    def test_roundtrip_with_fx(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0x10
        msg.label = "HELLO"
        msg.code = 0xABCD

        data = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data)
        assert msg2.header == 0x10
        assert msg2.label.rstrip() == "HELLO"
        assert msg2.code == 0xABCD

    def test_full_length_label(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0x20
        msg.label = "ABCDEFGH"  # exactly 8 chars
        msg.code = 1000

        data = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data)
        assert msg2.label == "ABCDEFGH"
        assert msg2.code == 1000

    def test_empty_label(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0x30
        msg.label = ""
        msg.code = 0

        data = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data)
        assert msg2.label.rstrip() == ""
        assert msg2.code == 0

    def test_double_encode_stability(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0x77
        msg.label = "TEST"
        msg.code = 9999

        data1 = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestFxIa5StringNoExtension:
    """FxIa5Msg without FX items (header only)."""

    def test_no_fx_items(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0xFF

        data = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data)
        assert msg2.header == 0xFF
        assert msg2.label is None
        assert msg2.code is None


class TestFxIa5StringBoundary:
    """Boundary values for FxIa5Msg fields."""

    def test_code_max_value(self):
        from fx_ia5_string import FxIa5Msg

        msg = FxIa5Msg()
        msg.header = 0x01
        msg.label = "MAX"
        msg.code = 0xFFFF

        data = msg.encode_bytes()
        msg2 = FxIa5Msg.decode_bytes(data)
        assert msg2.code == 0xFFFF


# ============================================================================
# fx_string: FxStringMsg with header + FX(label:string 10, payload:bytes 4,
#            extra:uint16)
# ============================================================================


class TestFxStringWithExtension:
    """FxStringMsg with FX items present."""

    def test_roundtrip_with_fx(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0x55
        msg.label = "TestLabel"
        msg.payload = b'\xDE\xAD\xBE\xEF'
        msg.extra = 0x1234

        data = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data)
        assert msg2.header == 0x55
        assert msg2.label.rstrip('\x00') == "TestLabel"
        assert msg2.payload == b'\xDE\xAD\xBE\xEF'
        assert msg2.extra == 0x1234

    def test_full_length_label(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0xAA
        msg.label = "0123456789"  # exactly 10 chars
        msg.payload = b'\x01\x02\x03\x04'
        msg.extra = 42

        data = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data)
        assert msg2.label == "0123456789"

    def test_empty_strings_and_zero_payload(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0x00
        msg.label = ""
        msg.payload = b'\x00\x00\x00\x00'
        msg.extra = 0

        data = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data)
        assert msg2.label.rstrip('\x00') == ""
        assert msg2.payload == b'\x00\x00\x00\x00'
        assert msg2.extra == 0

    def test_double_encode_stability(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0x33
        msg.label = "Stable"
        msg.payload = b'\xCA\xFE\xBA\xBE'
        msg.extra = 0x7FFF

        data1 = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestFxStringNoExtension:
    """FxStringMsg without FX items."""

    def test_no_fx_items(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0xBB

        data = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data)
        assert msg2.header == 0xBB
        assert msg2.label is None
        assert msg2.payload is None
        assert msg2.extra is None


class TestFxStringBoundary:
    """Boundary values for FxStringMsg fields."""

    def test_extra_max_value(self):
        from fx_string import FxStringMsg

        msg = FxStringMsg()
        msg.header = 0x01
        msg.label = "X"
        msg.payload = b'\xFF\xFF\xFF\xFF'
        msg.extra = 0xFFFF

        data = msg.encode_bytes()
        msg2 = FxStringMsg.decode_bytes(data)
        assert msg2.extra == 0xFFFF
        assert msg2.payload == b'\xFF\xFF\xFF\xFF'
