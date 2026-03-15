"""Roundtrip encode/decode tests for bitmap generated Python modules.

Covers:
- bitmap_wide_fixed: 24-bit fixed-width FSPEC bitmap with fields at bits 0, 8, 16
- nested_bitmap: nested bitmap structs (inline and type-referenced) with
  scaled types, enums, and wrapper structs
"""
import pytest


# ============================================================================
# bitmap_wide_fixed: WideBitmap (24-bit FSPEC) inside WideBitmapMsg
# Fields: alpha (uint8 @ bit 0), beta (uint16 @ bit 8), gamma (uint32 @ bit 16)
# ============================================================================


class TestWideBitmapAllPresent:
    """All three fields present in the wide bitmap."""

    def test_roundtrip_all_fields(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0x42
        bm = WideBitmap()
        bm.alpha = 0xAA
        bm.beta = 0x1234
        bm.gamma = 0xDEADBEEF
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.header == 0x42
        assert msg2.bitmap_data.alpha == 0xAA
        assert msg2.bitmap_data.beta == 0x1234
        assert msg2.bitmap_data.gamma == 0xDEADBEEF

    def test_double_encode_stability(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0x10
        bm = WideBitmap()
        bm.alpha = 100
        bm.beta = 2000
        bm.gamma = 300000
        msg.bitmap_data = bm

        data1 = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


class TestWideBitmapPartialPresence:
    """Only some fields present."""

    def test_only_alpha(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0x01
        bm = WideBitmap()
        bm.alpha = 0x55
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.bitmap_data.alpha == 0x55
        assert msg2.bitmap_data.beta is None
        assert msg2.bitmap_data.gamma is None

    def test_only_gamma(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0x02
        bm = WideBitmap()
        bm.gamma = 0xCAFEBABE
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.bitmap_data.alpha is None
        assert msg2.bitmap_data.beta is None
        assert msg2.bitmap_data.gamma == 0xCAFEBABE

    def test_alpha_and_beta(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0x03
        bm = WideBitmap()
        bm.alpha = 0x11
        bm.beta = 0x2222
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.bitmap_data.alpha == 0x11
        assert msg2.bitmap_data.beta == 0x2222
        assert msg2.bitmap_data.gamma is None


class TestWideBitmapNoFields:
    """No fields present in the bitmap."""

    def test_empty_bitmap(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0xFF
        bm = WideBitmap()
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.header == 0xFF
        assert msg2.bitmap_data.alpha is None
        assert msg2.bitmap_data.beta is None
        assert msg2.bitmap_data.gamma is None


class TestWideBitmapBoundary:
    """Boundary values for wide bitmap fields."""

    def test_max_values(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0xFF
        bm = WideBitmap()
        bm.alpha = 0xFF
        bm.beta = 0xFFFF
        bm.gamma = 0xFFFFFFFF
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.bitmap_data.alpha == 0xFF
        assert msg2.bitmap_data.beta == 0xFFFF
        assert msg2.bitmap_data.gamma == 0xFFFFFFFF

    def test_zero_values(self):
        from bitmap_wide_fixed import WideBitmapMsg
        from bitmap_wide_fixed.structs import WideBitmap

        msg = WideBitmapMsg()
        msg.header = 0
        bm = WideBitmap()
        bm.alpha = 0
        bm.beta = 0
        bm.gamma = 0
        msg.bitmap_data = bm

        data = msg.encode_bytes()
        msg2 = WideBitmapMsg.decode_bytes(data)
        assert msg2.bitmap_data.alpha == 0
        assert msg2.bitmap_data.beta == 0
        assert msg2.bitmap_data.gamma == 0


# ============================================================================
# nested_bitmap: OuterItems (ext bitmap) -> nested inline bitmap, SubItems
#                (type-ref bitmap), wrapper struct with inner bitmap
# ============================================================================


class TestNestedBitmapIdAndStatus:
    """Test id and status fields on the outer bitmap."""

    def test_id_only(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems

        msg = NestedBitmapMsg()
        msg.header = 0x01
        items = OuterItems()
        items.id = 0xAB
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.header == 0x01
        assert msg2.items.id == 0xAB
        assert msg2.items.status is None

    def test_id_and_status(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems
        from nested_bitmap.types import DeviceStatus

        msg = NestedBitmapMsg()
        msg.header = 0x02
        items = OuterItems()
        items.id = 0x10
        items.status = DeviceStatus.WARNING
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.id == 0x10
        assert msg2.items.status == DeviceStatus.WARNING

    def test_all_status_values(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems
        from nested_bitmap.types import DeviceStatus

        for status in [DeviceStatus.OK, DeviceStatus.WARNING,
                       DeviceStatus.ERROR, DeviceStatus.CRITICAL]:
            msg = NestedBitmapMsg()
            msg.header = 0x03
            items = OuterItems()
            items.id = 1
            items.status = status
            msg.items = items

            data = msg.encode_bytes()
            msg2 = NestedBitmapMsg.decode_bytes(data)
            assert msg2.items.status == status


class TestNestedBitmapInlineNested:
    """Test the inline nested bitmap struct (nested.x, nested.y)."""

    def test_nested_with_both_fields(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems

        msg = NestedBitmapMsg()
        msg.header = 0x04
        items = OuterItems()
        items.id = 0x55

        # Create inline nested bitmap
        nested = type(items).nested_type() if hasattr(type(items), 'nested_type') else None
        if nested is None:
            # Try direct attribute assignment
            from nested_bitmap.structs import OuterItemsNested
            nested = OuterItemsNested()
        nested.x = 0xAA
        nested.y = 0x1234
        items.nested = nested
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.id == 0x55
        assert msg2.items.nested is not None
        assert msg2.items.nested.x == 0xAA
        assert msg2.items.nested.y == 0x1234

    def test_nested_x_only(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems

        msg = NestedBitmapMsg()
        msg.header = 0x05
        items = OuterItems()
        items.id = 0x11

        try:
            from nested_bitmap.structs import OuterItemsNested
            nested = OuterItemsNested()
        except ImportError:
            nested = type(items).nested_type()
        nested.x = 0xCC
        items.nested = nested
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.nested.x == 0xCC
        assert msg2.items.nested.y is None


class TestNestedBitmapSubItems:
    """Test the type-referenced SubItems bitmap (alpha, beta, temp)."""

    def test_sub_items_all_fields(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems, SubItems
        from nested_bitmap.types import ScaledTemp

        msg = NestedBitmapMsg()
        msg.header = 0x06
        items = OuterItems()
        items.id = 0x22

        sub = SubItems()
        sub.alpha = 0xDD
        sub.beta = 0x5678
        sub.temp = ScaledTemp(250)  # raw value; scaled-temp with scale=0.1, offset=-40
        items.sub_items = sub
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.sub_items is not None
        assert msg2.items.sub_items.alpha == 0xDD
        assert msg2.items.sub_items.beta == 0x5678
        # temp is a scaled type: value = raw * 0.1 + (-40)
        # We check the raw roundtrips correctly
        assert msg2.items.sub_items.temp is not None
        assert msg2.items.sub_items.temp.raw == 250

    def test_sub_items_partial(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems, SubItems

        msg = NestedBitmapMsg()
        msg.header = 0x07
        items = OuterItems()
        items.id = 0x33

        sub = SubItems()
        sub.alpha = 0xEE
        items.sub_items = sub
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.sub_items.alpha == 0xEE
        assert msg2.items.sub_items.beta is None
        assert msg2.items.sub_items.temp is None


class TestNestedBitmapWrapper:
    """Test the wrapper struct (tag + inner bitmap with p, q)."""

    def test_wrapper_roundtrip(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems

        msg = NestedBitmapMsg()
        msg.header = 0x08
        items = OuterItems()
        items.id = 0x44

        # Build wrapper struct with tag and inner bitmap
        try:
            from nested_bitmap.structs import OuterItemsWrapper, OuterItemsWrapperInner
            wrapper = OuterItemsWrapper()
            wrapper.tag = 0xBB
            inner = OuterItemsWrapperInner()
            inner.p = 0x11
            inner.q = 0x2222
            wrapper.inner = inner
        except ImportError:
            # Alternate naming conventions
            wrapper = type(items).wrapper_type()
            wrapper.tag = 0xBB
            inner = type(wrapper).inner_type()
            inner.p = 0x11
            inner.q = 0x2222
            wrapper.inner = inner

        items.wrapper = wrapper
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.items.wrapper is not None
        assert msg2.items.wrapper.tag == 0xBB
        assert msg2.items.wrapper.inner is not None
        assert msg2.items.wrapper.inner.p == 0x11
        assert msg2.items.wrapper.inner.q == 0x2222


class TestNestedBitmapNoFields:
    """No optional fields present in outer bitmap."""

    def test_empty_outer_items(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems

        msg = NestedBitmapMsg()
        msg.header = 0x09
        items = OuterItems()
        msg.items = items

        data = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data)
        assert msg2.header == 0x09
        assert msg2.items.id is None
        assert msg2.items.status is None
        assert msg2.items.nested is None
        assert msg2.items.sub_items is None
        assert msg2.items.wrapper is None


class TestNestedBitmapDoubleEncode:
    """Double encode stability tests."""

    def test_full_message_double_encode(self):
        from nested_bitmap import NestedBitmapMsg
        from nested_bitmap.structs import OuterItems, SubItems
        from nested_bitmap.types import DeviceStatus

        msg = NestedBitmapMsg()
        msg.header = 0x0A
        items = OuterItems()
        items.id = 0x77
        items.status = DeviceStatus.ERROR

        sub = SubItems()
        sub.alpha = 0x88
        sub.beta = 0x9999
        items.sub_items = sub
        msg.items = items

        data1 = msg.encode_bytes()
        msg2 = NestedBitmapMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2
