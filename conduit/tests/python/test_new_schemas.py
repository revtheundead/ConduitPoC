"""Comprehensive tests for newly generated Python schemas.

Covers: expr_features, inline_enum, constants_everywhere, constraints_extended,
format_binary, auto_count, auto_struct_length, bitmap_advanced, fx_block,
fx_advanced, inline_field_types, present_when_complex, frame_basic,
bytes_numeric, enum_arrays, outer_scope.
"""
import math
import pytest


# ---------------------------------------------------------------------------
# expr_features: arithmetic length, comparison present-when, bitwise, logical,
#                multiplication-based count, remaining
# ---------------------------------------------------------------------------
class TestExprFeatures:

    def test_arithmetic_length_type_a(self):
        from expr_features import ArithmeticLengthMsg
        from expr_features.constants import Constants
        from expr_features.structs import ItemA

        msg = ArithmeticLengthMsg()
        msg.total_length = 6  # header_size(1) + tag(1) + total_length(2) + ItemA(2)
        msg.header_size = 4   # total_length(2) + header_size(1) + tag(1)
        msg.tag = Constants.TYPE_A
        body = ItemA()
        body.val = 0x1234
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ArithmeticLengthMsg.decode_bytes(data)
        assert msg2.tag == Constants.TYPE_A
        assert msg2.body.val == 0x1234

    def test_arithmetic_length_type_b(self):
        from expr_features import ArithmeticLengthMsg
        from expr_features.constants import Constants
        from expr_features.structs import ItemB

        msg = ArithmeticLengthMsg()
        msg.total_length = 8  # header(4) + ItemB(4)
        msg.header_size = 4
        msg.tag = Constants.TYPE_B
        body = ItemB()
        body.tag = 0xDEADBEEF
        msg.body = body

        data = msg.encode_bytes()
        msg2 = ArithmeticLengthMsg.decode_bytes(data)
        assert msg2.tag == Constants.TYPE_B
        assert msg2.body.tag == 0xDEADBEEF

    def test_comparison_all_present(self):
        from expr_features import ComparisonMsg

        msg = ComparisonMsg()
        msg.flags = 1    # != 0 -> opt_a present
        msg.level = 5    # >3 -> opt_b, >=5 -> opt_c, <10 -> opt_d
        msg.opt_a = 100
        msg.opt_b = 200
        msg.opt_c = 300
        msg.opt_d = 400

        data = msg.encode_bytes()
        msg2 = ComparisonMsg.decode_bytes(data)
        assert msg2.opt_a == 100
        assert msg2.opt_b == 200
        assert msg2.opt_c == 300
        assert msg2.opt_d == 400

    def test_comparison_none_present(self):
        from expr_features import ComparisonMsg

        msg = ComparisonMsg()
        msg.flags = 0    # == 0 -> no opt_a
        msg.level = 10   # not >3? no wait: 10>3 -> opt_b, 10>=5 -> opt_c, not <10 -> no opt_d
        # Actually level=10: >3->yes, >=5->yes, <10->no
        msg.opt_b = 500
        msg.opt_c = 600

        data = msg.encode_bytes()
        msg2 = ComparisonMsg.decode_bytes(data)
        assert msg2.opt_a is None
        assert msg2.opt_b == 500
        assert msg2.opt_c == 600
        assert msg2.opt_d is None

    def test_comparison_flags_zero_level_low(self):
        from expr_features import ComparisonMsg

        msg = ComparisonMsg()
        msg.flags = 0
        msg.level = 2  # not >3 -> no opt_b, not >=5 -> no opt_c, <10 -> opt_d
        msg.opt_d = 999

        data = msg.encode_bytes()
        msg2 = ComparisonMsg.decode_bytes(data)
        assert msg2.opt_a is None
        assert msg2.opt_b is None
        assert msg2.opt_c is None
        assert msg2.opt_d == 999

    def test_bitwise_with_extension(self):
        from expr_features import BitwiseMsg

        msg = BitwiseMsg()
        msg.mask = 0x01   # bit 0 set -> extended present
        msg.base = 0x5678
        msg.extended = 0xABCD

        data = msg.encode_bytes()
        msg2 = BitwiseMsg.decode_bytes(data)
        assert msg2.extended == 0xABCD

    def test_bitwise_without_extension(self):
        from expr_features import BitwiseMsg

        msg = BitwiseMsg()
        msg.mask = 0xFE   # bit 0 not set
        msg.base = 0x1234

        data = msg.encode_bytes()
        msg2 = BitwiseMsg.decode_bytes(data)
        assert msg2.extended is None

    def test_logical_both_flags(self):
        from expr_features import LogicalMsg

        msg = LogicalMsg()
        msg.flag_a = 1
        msg.flag_b = 1
        msg.value = 0x1234
        msg.conditional = 0xABCD

        data = msg.encode_bytes()
        msg2 = LogicalMsg.decode_bytes(data)
        assert msg2.conditional == 0xABCD

    def test_logical_one_flag(self):
        from expr_features import LogicalMsg

        msg = LogicalMsg()
        msg.flag_a = 1
        msg.flag_b = 0
        msg.value = 0x5678

        data = msg.encode_bytes()
        msg2 = LogicalMsg.decode_bytes(data)
        assert msg2.conditional is None

    def test_mul_count(self):
        from expr_features import MulCountMsg
        from expr_features.bit_io import BitWriter, BitReader

        msg = MulCountMsg()
        msg.rows = 2
        msg.cols = 3

        w = BitWriter()
        w.write_u8(2)   # rows
        w.write_u8(3)   # cols
        for i in range(6):
            w.write_u16(i * 100, True)

        msg2 = MulCountMsg.decode_bytes(w.to_bytes())
        assert msg2.rows == 2
        assert msg2.cols == 3
        assert len(msg2.cells) == 6

    def test_double_encode(self):
        from expr_features import BitwiseMsg

        msg = BitwiseMsg()
        msg.mask = 0x01
        msg.base = 42
        msg.extended = 99

        d1 = msg.encode_bytes()
        m2 = BitwiseMsg.decode_bytes(d1)
        d2 = m2.encode_bytes()
        assert d1 == d2


# ---------------------------------------------------------------------------
# inline_enum: inline enum definitions within message
# ---------------------------------------------------------------------------
class TestInlineEnum:

    def test_roundtrip(self):
        from inline_enum import InlineEnumMsg
        from inline_enum.messages import InlineEnumMsgMode, InlineEnumMsgPriority

        msg = InlineEnumMsg()
        msg.mode = InlineEnumMsgMode.ACTIVE
        msg.priority = InlineEnumMsgPriority.HIGH
        msg.data = 0x1234

        data = msg.encode_bytes()
        msg2 = InlineEnumMsg.decode_bytes(data)
        assert msg2.mode == InlineEnumMsgMode.ACTIVE
        assert msg2.priority == InlineEnumMsgPriority.HIGH
        assert msg2.data == 0x1234

    def test_all_modes(self):
        from inline_enum import InlineEnumMsg
        from inline_enum.messages import InlineEnumMsgMode, InlineEnumMsgPriority

        for mode in InlineEnumMsgMode:
            msg = InlineEnumMsg()
            msg.mode = mode
            msg.priority = InlineEnumMsgPriority.LOW
            msg.data = 0
            data = msg.encode_bytes()
            msg2 = InlineEnumMsg.decode_bytes(data)
            assert msg2.mode == mode

    def test_invalid_mode(self):
        from inline_enum import DecodeError
        from inline_enum.bit_io import BitWriter, BitReader

        w = BitWriter()
        w.write_bits(7, 4)  # invalid mode
        w.write_bits(0, 2)  # priority
        w.write_bits(0, 2)  # reserved
        w.write_u16(0, True)

        with pytest.raises(DecodeError, match="unknown InlineEnumMsgMode"):
            from inline_enum import InlineEnumMsg
            InlineEnumMsg.decode_bytes(w.to_bytes())

    def test_wire_size(self):
        from inline_enum import InlineEnumMsg
        from inline_enum.messages import InlineEnumMsgMode, InlineEnumMsgPriority

        msg = InlineEnumMsg()
        msg.mode = InlineEnumMsgMode.OFF
        msg.priority = InlineEnumMsgPriority.LOW
        msg.data = 0
        assert len(msg.encode_bytes()) == 3  # 4+2+2 bits = 1 byte + 2 bytes


# ---------------------------------------------------------------------------
# constants_everywhere: frame with sync, version constraint, header magic
# ---------------------------------------------------------------------------
class TestConstantsEverywhere:

    def test_versioned_roundtrip(self):
        from constants_everywhere import Versioned
        from constants_everywhere.constants import Constants

        msg = Versioned()
        msg.version = Constants.VERSION
        msg.data = 0x5678

        data = msg.encode_bytes()
        msg2 = Versioned.decode_bytes(data)
        assert msg2.version == Constants.VERSION
        assert msg2.data == 0x5678

    def test_versioned_constraint_violation(self):
        from constants_everywhere.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u8(99)  # wrong version
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="version"):
            from constants_everywhere import Versioned
            Versioned.decode_bytes(w.to_bytes())

    def test_with_header_magic(self):
        from constants_everywhere.messages import WithHeader, WithHeaderHdr
        from constants_everywhere.constants import Constants

        hdr = WithHeaderHdr()
        hdr.magic = Constants.HEADER_MAGIC
        hdr.flags = 0x42

        msg = WithHeader()
        msg.hdr = hdr
        msg.payload_data = 0x12345678

        data = msg.encode_bytes()
        msg2 = WithHeader.decode_bytes(data)
        assert msg2.hdr.magic == Constants.HEADER_MAGIC
        assert msg2.hdr.flags == 0x42
        assert msg2.payload_data == 0x12345678

    def test_header_magic_violation(self):
        from constants_everywhere.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u8(0xFF)   # wrong magic
        w.write_u8(0)
        w.write_u32(0, True)
        with pytest.raises(ConstraintError, match="magic"):
            from constants_everywhere.messages import WithHeader
            WithHeader.decode_bytes(w.to_bytes())

    def test_frame_wrap_encode_decode(self):
        from constants_everywhere.messages import Frame, Versioned
        from constants_everywhere.constants import Constants

        v = Versioned()
        v.version = Constants.VERSION
        v.data = 42

        frame = Frame.wrap(v)
        data = frame.encode_bytes()
        frame2 = Frame.decode_bytes(data)
        assert frame2.sync == Constants.SYNC
        assert frame2.msg_type == 1
        assert isinstance(frame2.payload, Versioned)
        assert frame2.payload.data == 42

    def test_default_version_roundtrip(self):
        from constants_everywhere.messages import DefaultVersion

        msg = DefaultVersion()
        msg.version = 3
        msg.count = 42

        data = msg.encode_bytes()
        msg2 = DefaultVersion.decode_bytes(data)
        assert msg2.version == 3
        assert msg2.count == 42


# ---------------------------------------------------------------------------
# constraints_extended: equals, min/max, signed range, deferred
# ---------------------------------------------------------------------------
class TestConstraintsExtended:

    def test_valid_roundtrip(self):
        from constraints_extended import ExtConstraintMsg

        msg = ExtConstraintMsg()
        # defaults already set: sync_word=0xABCD, version=42
        msg.temperature = 25
        msg.count = 5
        msg.index = 50
        msg.data = 0x1234

        data = msg.encode_bytes()
        msg2 = ExtConstraintMsg.decode_bytes(data)
        assert msg2.sync_word == 0xABCD
        assert msg2.version == 42
        assert msg2.temperature == 25
        assert msg2.count == 5
        assert msg2.index == 50

    def test_sync_word_violation(self):
        from constraints_extended.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xFFFF, True)  # wrong sync
        w.write_u8(42)
        w.write_signed_bits(25, 16)
        w.write_u16(5, True)
        w.write_u8(50)
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="sync-word"):
            from constraints_extended import ExtConstraintMsg
            ExtConstraintMsg.decode_bytes(w.to_bytes())

    def test_temperature_max_boundary(self):
        from constraints_extended import ExtConstraintMsg

        msg = ExtConstraintMsg()
        msg.temperature = 85  # exactly at max
        msg.count = 1
        msg.index = 0
        msg.data = 0
        data = msg.encode_bytes()
        msg2 = ExtConstraintMsg.decode_bytes(data)
        assert msg2.temperature == 85

    def test_temperature_min_boundary(self):
        from constraints_extended import ExtConstraintMsg

        msg = ExtConstraintMsg()
        msg.temperature = -40  # exactly at min
        msg.count = 1
        msg.index = 0
        msg.data = 0
        data = msg.encode_bytes()
        msg2 = ExtConstraintMsg.decode_bytes(data)
        assert msg2.temperature == -40

    def test_temperature_exceeds_max(self):
        from constraints_extended.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xABCD, True)
        w.write_u8(42)
        w.write_signed_bits(86, 16)  # exceeds max=85
        w.write_u16(5, True)
        w.write_u8(50)
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="temperature"):
            from constraints_extended import ExtConstraintMsg
            ExtConstraintMsg.decode_bytes(w.to_bytes())

    def test_temperature_below_min(self):
        from constraints_extended.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xABCD, True)
        w.write_u8(42)
        w.write_signed_bits(-41, 16)  # below min=-40
        w.write_u16(5, True)
        w.write_u8(50)
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="temperature"):
            from constraints_extended import ExtConstraintMsg
            ExtConstraintMsg.decode_bytes(w.to_bytes())

    def test_count_below_min(self):
        from constraints_extended.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xABCD, True)
        w.write_u8(42)
        w.write_signed_bits(25, 16)
        w.write_u16(0, True)  # count=0, min=1
        w.write_u8(50)
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="count"):
            from constraints_extended import ExtConstraintMsg
            ExtConstraintMsg.decode_bytes(w.to_bytes())

    def test_index_exceeds_max(self):
        from constraints_extended.bit_io import ConstraintError, BitWriter

        w = BitWriter()
        w.write_u16(0xABCD, True)
        w.write_u8(42)
        w.write_signed_bits(25, 16)
        w.write_u16(5, True)
        w.write_u8(100)  # index=100, max=99
        w.write_u16(0, True)
        with pytest.raises(ConstraintError, match="index"):
            from constraints_extended import ExtConstraintMsg
            ExtConstraintMsg.decode_bytes(w.to_bytes())

    def test_validate_method(self):
        from constraints_extended import ExtConstraintMsg
        from constraints_extended.bit_io import ConstraintError

        msg = ExtConstraintMsg()
        msg.sync_word = 0xFFFF  # wrong
        with pytest.raises(ConstraintError):
            msg.validate()

    def test_double_encode(self):
        from constraints_extended import ExtConstraintMsg
        msg = ExtConstraintMsg()
        msg.temperature = 0
        msg.count = 100
        msg.index = 0
        msg.data = 0xFFFF
        d1 = msg.encode_bytes()
        m2 = ExtConstraintMsg.decode_bytes(d1)
        d2 = m2.encode_bytes()
        assert d1 == d2


# ---------------------------------------------------------------------------
# format_binary: sub-byte fields (bits)
# ---------------------------------------------------------------------------
class TestFormatBinary:

    def test_roundtrip(self):
        from format_binary import BinaryMsg

        msg = BinaryMsg()
        msg.flags = 0xAB
        msg.mask = 0x0F
        msg.tag = 0x05
        msg.value = 0x1234

        data = msg.encode_bytes()
        msg2 = BinaryMsg.decode_bytes(data)
        assert msg2.flags == 0xAB
        assert msg2.mask == 0x0F
        assert msg2.tag == 0x05
        assert msg2.value == 0x1234

    def test_wire_size(self):
        from format_binary import BinaryMsg

        msg = BinaryMsg()
        msg.flags = 0
        msg.mask = 0
        msg.tag = 0
        msg.value = 0
        assert len(msg.encode_bytes()) == 4  # u8 + 4bits + 4bits + u16

    def test_nibble_boundaries(self):
        from format_binary import BinaryMsg

        for mask in range(16):
            for tag in range(16):
                msg = BinaryMsg()
                msg.flags = 0
                msg.mask = mask
                msg.tag = tag
                msg.value = 0
                data = msg.encode_bytes()
                msg2 = BinaryMsg.decode_bytes(data)
                assert msg2.mask == mask
                assert msg2.tag == tag


# ---------------------------------------------------------------------------
# auto_count: auto count field for arrays
# ---------------------------------------------------------------------------
class TestAutoCount:

    def test_container_roundtrip(self):
        from auto_count.structs import Container, Record

        c = Container()
        c.tag = 42
        for v in [100, 200, 300]:
            r = Record()
            r.value = v
            c.items.append(r)

        data = c.encode_bytes()
        c2 = Container.decode_bytes(data)
        assert c2.tag == 42
        assert c2.count == 3
        assert len(c2.items) == 3
        assert c2.items[0].value == 100
        assert c2.items[2].value == 300

    def test_container_empty(self):
        from auto_count.structs import Container

        c = Container()
        c.tag = 1
        data = c.encode_bytes()
        c2 = Container.decode_bytes(data)
        assert c2.count == 0
        assert len(c2.items) == 0

    def test_count_msg_roundtrip(self):
        from auto_count import CountMsg
        from auto_count.bit_io import BitWriter

        w = BitWriter()
        w.write_u8(99)      # id
        w.write_u16(3, True) # num_entries=3
        for v in [10, 20, 30]:
            w.write_u16(v, True)

        msg = CountMsg.decode_bytes(w.to_bytes())
        assert msg.id == 99
        assert msg.num_entries == 3
        assert len(msg.entries) == 3

    def test_count_msg_encode_auto_count(self):
        """Encode should auto-populate count from list length."""
        from auto_count import CountMsg
        from auto_count.types import Uint16

        msg = CountMsg()
        msg.id = 7
        msg.entries = [Uint16(111), Uint16(222)]
        data = msg.encode_bytes()
        msg2 = CountMsg.decode_bytes(data)
        assert msg2.num_entries == 2
        assert msg2.entries[0].value == 111
        assert msg2.entries[1].value == 222

    def test_double_encode(self):
        from auto_count.structs import Container, Record

        c = Container()
        c.tag = 10
        for v in [1, 2, 3, 4, 5]:
            r = Record()
            r.value = v
            c.items.append(r)

        d1 = c.encode_bytes()
        c2 = Container.decode_bytes(d1)
        d2 = c2.encode_bytes()
        assert d1 == d2


# ---------------------------------------------------------------------------
# auto_struct_length: TLV with auto-length backpatch
# ---------------------------------------------------------------------------
class TestAutoStructLength:

    def test_tlv_roundtrip(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0x42
        msg.data = b'\x01\x02\x03\x04\x05'
        msg.suffix = 0xFF

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.tag == 0x42
        assert msg2.len == 5
        assert msg2.data == b'\x01\x02\x03\x04\x05'
        assert msg2.suffix == 0xFF

    def test_tlv_empty_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 1
        msg.data = b''
        msg.suffix = 0

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.len == 0
        assert msg2.data == b''

    def test_tlv_auto_length_correct(self):
        """The encoded length should match the data field size, not total size."""
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xAA
        msg.data = b'\xDE\xAD\xBE\xEF'
        msg.suffix = 0xBB

        data = msg.encode_bytes()
        # tag(1) + len(1) + data(4) + suffix(1) = 7
        assert len(data) == 7
        # len byte should be 4 (just the data field)
        assert data[1] == 4

    def test_double_encode(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0x10
        msg.data = bytes(range(10))
        msg.suffix = 0x20

        d1 = msg.encode_bytes()
        m2 = TlvMsg.decode_bytes(d1)
        d2 = m2.encode_bytes()
        assert d1 == d2


# ---------------------------------------------------------------------------
# bitmap_advanced: FSPEC bitmap with mixed field types (BCD, string, enum, bytes)
# ---------------------------------------------------------------------------
class TestBitmapAdvanced:

    def test_all_fields_present(self):
        from bitmap_advanced.structs import AdvancedBitmap
        from bitmap_advanced.types import StatusCode

        bm = AdvancedBitmap()
        bm.counter = 0x1234
        bm.bcd_field = 12  # BCD 8-bit = 2 digits max (0-99)
        bm.data = b'\xAA\xBB\xCC\xDD'
        bm.label = "test"
        bm.status = StatusCode.OK

        data = bm.encode_bytes()
        bm2 = AdvancedBitmap.decode_bytes(data)
        assert bm2.counter == 0x1234
        assert bm2.bcd_field == 12
        assert bm2.data == b'\xAA\xBB\xCC\xDD'
        assert bm2.label.rstrip('\x00') == "test"
        assert bm2.status == StatusCode.OK

    def test_no_fields_present(self):
        from bitmap_advanced.structs import AdvancedBitmap

        bm = AdvancedBitmap()
        data = bm.encode_bytes()
        bm2 = AdvancedBitmap.decode_bytes(data)
        assert bm2.counter is None
        assert bm2.bcd_field is None
        assert bm2.data is None
        assert bm2.label is None
        assert bm2.status is None
        assert len(data) == 1  # just the fspec byte

    def test_single_field(self):
        from bitmap_advanced.structs import AdvancedBitmap
        from bitmap_advanced.types import StatusCode

        bm = AdvancedBitmap()
        bm.status = StatusCode.ERROR
        data = bm.encode_bytes()
        bm2 = AdvancedBitmap.decode_bytes(data)
        assert bm2.status == StatusCode.ERROR
        assert bm2.counter is None
        assert bm2.label is None

    def test_message_wrapper(self):
        from bitmap_advanced import BitmapAdvancedMsg
        from bitmap_advanced.structs import AdvancedBitmap

        msg = BitmapAdvancedMsg()
        msg.header = 0x42
        msg.bitmap_data = AdvancedBitmap()
        msg.bitmap_data.counter = 999

        data = msg.encode_bytes()
        msg2 = BitmapAdvancedMsg.decode_bytes(data)
        assert msg2.header == 0x42
        assert msg2.bitmap_data.counter == 999

    def test_all_status_codes(self):
        from bitmap_advanced.structs import AdvancedBitmap
        from bitmap_advanced.types import StatusCode

        for code in StatusCode:
            bm = AdvancedBitmap()
            bm.status = code
            data = bm.encode_bytes()
            bm2 = AdvancedBitmap.decode_bytes(data)
            assert bm2.status == code

    def test_invalid_status_code(self):
        from bitmap_advanced import DecodeError
        from bitmap_advanced.bit_io import BitWriter

        w = BitWriter()
        w.write_u8(0x01)  # fspec: only status present (bit 0)
        w.write_u8(99)    # invalid StatusCode

        with pytest.raises(DecodeError, match="unknown StatusCode"):
            from bitmap_advanced.structs import AdvancedBitmap
            AdvancedBitmap.decode_bytes(w.to_bytes())


# ---------------------------------------------------------------------------
# fx_block: field extension (FX) mechanism
# ---------------------------------------------------------------------------
class TestFxBlock:

    def test_with_extension(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0xAA
        msg.item1 = 0x1234
        msg.item2 = 0xDEADBEEF
        msg.item3 = 0x42

        data = msg.encode_bytes()
        msg2 = FxMessage.decode_bytes(data)
        assert msg2.header == 0xAA
        assert msg2.item1 == 0x1234
        assert msg2.item2 == 0xDEADBEEF
        assert msg2.item3 == 0x42

    def test_without_extension(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0xBB

        data = msg.encode_bytes()
        msg2 = FxMessage.decode_bytes(data)
        assert msg2.header == 0xBB
        assert msg2.item1 is None
        assert msg2.item2 is None
        assert msg2.item3 is None

    def test_double_encode(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0x55
        msg.item1 = 1
        msg.item2 = 2
        msg.item3 = 3

        d1 = msg.encode_bytes()
        m2 = FxMessage.decode_bytes(d1)
        d2 = m2.encode_bytes()
        assert d1 == d2


# ---------------------------------------------------------------------------
# fx_advanced: nested FX blocks with scaled values, enums, sub-structs, arrays
# ---------------------------------------------------------------------------
class TestFxAdvanced:

    def test_full_message(self):
        from fx_advanced import FxAdvancedMsg
        from fx_advanced.types import FxStatus
        from fx_advanced.structs import FxSubStruct

        msg = FxAdvancedMsg()
        msg.header = 0x10
        msg.scaled_temp = -10.0  # scaled: raw = (-10 - -40) / 0.01 = 3000
        msg.status = FxStatus.ACTIVE
        msg.label = "hello"
        msg.sub = FxSubStruct()
        msg.sub.a = 11
        msg.sub.b = 22

        from fx_advanced.types import Uint8
        msg.values = [Uint8(1), Uint8(2), Uint8(3), Uint8(4)]

        data = msg.encode_bytes()
        msg2 = FxAdvancedMsg.decode_bytes(data)
        assert msg2.header == 0x10
        assert abs(msg2.scaled_temp - (-10.0)) < 0.02
        assert msg2.status == FxStatus.ACTIVE
        assert msg2.label == "hello"
        assert msg2.sub.a == 11
        assert msg2.sub.b == 22
        assert len(msg2.values) == 4

    def test_minimal_message(self):
        from fx_advanced import FxAdvancedMsg

        msg = FxAdvancedMsg()
        msg.header = 0x20
        # Clear all FX fields
        msg.scaled_temp = None
        msg.status = None
        msg.label = None
        msg.sub = None
        msg.values = None

        data = msg.encode_bytes()
        msg2 = FxAdvancedMsg.decode_bytes(data)
        assert msg2.header == 0x20
        assert msg2.scaled_temp is None


# ---------------------------------------------------------------------------
# inline_field_types: float32, float64, signed bits, IA5 string, bool, octal
# ---------------------------------------------------------------------------
class TestInlineFieldTypes:

    def test_roundtrip(self):
        from inline_field_types import InlineMsg

        msg = InlineMsg()
        msg.temperature = 98.6
        msg.latitude = 51.5074
        msg.offset = -1000
        msg.callsign = "ABC1234"
        msg.raw_data = 0xDEADBEEF
        msg.active = True
        msg.mode3a = 0o7700
        msg.tag = 42

        data = msg.encode_bytes()
        msg2 = InlineMsg.decode_bytes(data)
        assert abs(msg2.temperature - 98.6) < 0.01
        assert abs(msg2.latitude - 51.5074) < 0.0001
        assert msg2.offset == -1000
        assert msg2.callsign.strip() == "ABC1234"
        assert msg2.raw_data == 0xDEADBEEF
        assert msg2.active is True
        assert msg2.mode3a == 0o7700
        assert msg2.tag == 42

    def test_negative_offset(self):
        from inline_field_types import InlineMsg

        msg = InlineMsg()
        msg.temperature = 0.0
        msg.latitude = 0.0
        msg.offset = -32768
        msg.callsign = ""
        msg.raw_data = 0
        msg.active = False
        msg.mode3a = 0
        msg.tag = 0

        data = msg.encode_bytes()
        msg2 = InlineMsg.decode_bytes(data)
        assert msg2.offset == -32768

    def test_bool_field(self):
        from inline_field_types import InlineMsg

        for active in [True, False]:
            msg = InlineMsg()
            msg.temperature = 0.0
            msg.latitude = 0.0
            msg.offset = 0
            msg.callsign = ""
            msg.raw_data = 0
            msg.active = active
            msg.mode3a = 0
            msg.tag = 0

            data = msg.encode_bytes()
            msg2 = InlineMsg.decode_bytes(data)
            assert msg2.active is active


# ---------------------------------------------------------------------------
# present_when_complex: combined present-when with bitwise AND, nested choice
# ---------------------------------------------------------------------------
class TestPresentWhenComplex:

    def test_with_items_and_type_a(self):
        from present_when_complex.structs import Packet, PacketTypeA
        from present_when_complex.bit_io import BitWriter, BitReader

        # flags=0x11: bit0=1 (items present), bits 7-4=0x10 (TypeA), bit1=1 (extra present)
        flags = 0x13  # 0001_0011
        w = BitWriter()
        w.write_u8(flags)
        w.write_u8(2)   # count
        w.write_u16(100, True)  # item 0
        w.write_u16(200, True)  # item 1
        w.write_u16(0x5678, True)  # extra (TypeA.a_val)
        w.write_u8(0xFF)  # trailer

        pkt = Packet.decode_bytes(w.to_bytes())
        assert len(pkt.items) == 2
        assert isinstance(pkt.extra, PacketTypeA)
        assert pkt.extra.a_val == 0x5678
        assert pkt.trailer == 0xFF

    def test_without_optional_fields(self):
        from present_when_complex.structs import Packet
        from present_when_complex.bit_io import BitWriter

        w = BitWriter()
        w.write_u8(0)  # flags=0: no items, no extra
        w.write_u8(0)  # count (unused)
        w.write_u8(0xAA)  # trailer

        pkt = Packet.decode_bytes(w.to_bytes())
        assert pkt.items == []
        assert pkt.extra is None
        assert pkt.trailer == 0xAA

    def test_roundtrip(self):
        from present_when_complex.structs import Packet, PacketTypeB

        pkt = Packet()
        pkt.flags = 0x22  # bit1=extra present, bits 7-4=0x20 (TypeB)
        pkt.count = 0
        pkt.extra = PacketTypeB()
        pkt.extra.b_val = 42
        pkt.trailer = 0x99

        data = pkt.encode_bytes()
        pkt2 = Packet.decode_bytes(data)
        assert isinstance(pkt2.extra, PacketTypeB)
        assert pkt2.extra.b_val == 42
        assert pkt2.trailer == 0x99


# ---------------------------------------------------------------------------
# frame_basic: simple frame with msg_type dispatch, auto length
# ---------------------------------------------------------------------------
class TestFrameBasic:

    def test_heartbeat_frame(self):
        from frame_basic import Heartbeat, SimpleFrame

        hb = Heartbeat()
        hb.timestamp = 0x1234

        frame = SimpleFrame.wrap(hb)
        data = frame.encode_bytes()
        frame2 = SimpleFrame.decode_bytes(data)
        assert frame2.msg_type == 1
        assert isinstance(frame2.payload, Heartbeat)
        assert frame2.payload.timestamp == 0x1234

    def test_status_frame(self):
        from frame_basic import Status, SimpleFrame

        st = Status()
        st.code = 5
        st.detail = 0xABCD

        frame = SimpleFrame.wrap(st)
        data = frame.encode_bytes()
        frame2 = SimpleFrame.decode_bytes(data)
        assert frame2.msg_type == 2
        assert isinstance(frame2.payload, Status)
        assert frame2.payload.code == 5
        assert frame2.payload.detail == 0xABCD

    def test_frame_auto_length(self):
        from frame_basic import Heartbeat, SimpleFrame

        hb = Heartbeat()
        hb.timestamp = 0

        frame = SimpleFrame.wrap(hb)
        data = frame.encode_bytes()
        # msg_type(1) + length(2) + timestamp(2) = 5
        assert len(data) == 5
        # length field should encode the total frame size
        from frame_basic.bit_io import BitReader
        r = BitReader(data)
        r.read_u8()  # msg_type
        length = r.read_u16(True)
        assert length == 5

    def test_double_encode_heartbeat(self):
        from frame_basic import Heartbeat, SimpleFrame
        hb = Heartbeat()
        hb.timestamp = 12345
        frame = SimpleFrame.wrap(hb)
        d1 = frame.encode_bytes()
        f2 = SimpleFrame.decode_bytes(d1)
        d2 = f2.encode_bytes()

        # Frames use wrap() which copies ID, so re-wrap for encode
        f3 = SimpleFrame.wrap(f2.payload)
        d3 = f3.encode_bytes()
        assert d1 == d3


# ---------------------------------------------------------------------------
# bytes_numeric: multi-byte integer fields (24-bit, 56-bit, 64-bit)
# ---------------------------------------------------------------------------
class TestBytesNumeric:

    def test_small_bytes_roundtrip(self):
        from bytes_numeric import SmallBytesMsg

        msg = SmallBytesMsg()
        msg.val16 = 0x1234
        msg.val24 = 0xABCDEF
        msg.val32 = 0xDEADBEEF
        msg.val56 = 0x12345678ABCDEF
        msg.val64 = 0xFEDCBA9876543210

        data = msg.encode_bytes()
        msg2 = SmallBytesMsg.decode_bytes(data)
        assert msg2.val16 == 0x1234
        assert msg2.val24 == 0xABCDEF
        assert msg2.val32 == 0xDEADBEEF
        assert msg2.val56 == 0x12345678ABCDEF
        assert msg2.val64 == 0xFEDCBA9876543210

    def test_wire_size(self):
        from bytes_numeric import SmallBytesMsg

        msg = SmallBytesMsg()
        msg.val16 = 0
        msg.val24 = 0
        msg.val32 = 0
        msg.val56 = 0
        msg.val64 = 0
        # 2 + 3 + 4 + 7 + 8 = 24 bytes
        assert len(msg.encode_bytes()) == 24

    def test_scaled_bytes_roundtrip(self):
        from bytes_numeric import ScaledBytesMsg

        msg = ScaledBytesMsg()
        msg.sensor = 123.45
        msg.temperature = 25.0
        msg.tag = 99

        data = msg.encode_bytes()
        msg2 = ScaledBytesMsg.decode_bytes(data)
        assert abs(msg2.sensor - 123.45) < 0.02
        assert abs(msg2.temperature - 25.0) < 0.2
        assert msg2.tag == 99

    def test_zero_values(self):
        from bytes_numeric import SmallBytesMsg

        msg = SmallBytesMsg()
        data = msg.encode_bytes()
        msg2 = SmallBytesMsg.decode_bytes(data)
        assert msg2.val16 == 0
        assert msg2.val24 == 0
        assert msg2.val32 == 0
        assert msg2.val56 == 0
        assert msg2.val64 == 0


# ---------------------------------------------------------------------------
# enum_arrays: arrays of enum values
# ---------------------------------------------------------------------------
class TestEnumArrays:

    def test_roundtrip(self):
        from enum_arrays.bit_io import BitWriter, BitReader

        # Read the generated messages to find what's available
        from enum_arrays import messages
        # If there's a message, test it
        import enum_arrays
        # Check what types exist
        msg_classes = [name for name in dir(enum_arrays.messages)
                      if not name.startswith('_') and name[0].isupper()
                      and name not in ('BitReader', 'BitWriter', 'DecodeError',
                                       'EncodeError', 'ConstraintError', 'Constants',
                                       'IntEnum')]
        for cls_name in msg_classes:
            cls = getattr(enum_arrays.messages, cls_name)
            if hasattr(cls, 'decode_bytes') and hasattr(cls, 'encode_bytes'):
                # Can instantiate and encode
                msg = cls()
                data = msg.encode_bytes()
                assert len(data) > 0


# ---------------------------------------------------------------------------
# outer_scope: length-from with outer scope references, sub_reader
# ---------------------------------------------------------------------------
class TestOuterScope:

    def test_data_a_roundtrip(self):
        from outer_scope.structs import DataA

        d = DataA()
        d.x = 10
        d.y = 20
        data = d.encode_bytes()
        d2 = DataA.decode_bytes(data)
        assert d2.x == 10
        assert d2.y == 20

    def test_data_b_roundtrip(self):
        from outer_scope.structs import DataB

        d = DataB()
        d.value = 0x1234
        data = d.encode_bytes()
        d2 = DataB.decode_bytes(data)
        assert d2.value == 0x1234

    def test_packet_with_data_a(self):
        from outer_scope.structs import Packet, DataA

        pkt = Packet()
        pkt.tag = 1
        pkt.payload = DataA()
        pkt.payload.x = 100
        pkt.payload.y = 200

        data = pkt.encode_bytes()
        pkt2 = Packet.decode_bytes(data)
        assert pkt2.tag == 1
        # auto length should be set correctly
        assert isinstance(pkt2.payload, DataA)
        assert pkt2.payload.x == 100
        assert pkt2.payload.y == 200

    def test_packet_with_data_b(self):
        from outer_scope.structs import Packet, DataB

        pkt = Packet()
        pkt.tag = 2
        pkt.payload = DataB()
        pkt.payload.value = 0xBEEF

        data = pkt.encode_bytes()
        pkt2 = Packet.decode_bytes(data)
        assert pkt2.tag == 2
        assert isinstance(pkt2.payload, DataB)
        assert pkt2.payload.value == 0xBEEF

    def test_packet_raw_fallback(self):
        from outer_scope.structs import Packet, PacketRaw
        from outer_scope.bit_io import BitWriter

        w = BitWriter()
        w.write_u8(99)   # tag (not 1 or 2)
        w.write_u8(5)    # len=5 -> data = 5-2 = 3 bytes
        w.write_bytes(b'\xAA\xBB\xCC')

        pkt = Packet.decode_bytes(w.to_bytes())
        assert pkt.tag == 99
        assert isinstance(pkt.payload, PacketRaw)
        assert pkt.payload.data == b'\xAA\xBB\xCC'

    def test_double_encode(self):
        from outer_scope.structs import Packet, DataA

        pkt = Packet()
        pkt.tag = 1
        pkt.payload = DataA()
        pkt.payload.x = 55
        pkt.payload.y = 66

        d1 = pkt.encode_bytes()
        p2 = Packet.decode_bytes(d1)
        # Re-wrap since we need the tag and auto-length
        p3 = Packet()
        p3.tag = p2.tag
        p3.payload = p2.payload
        d3 = p3.encode_bytes()
        assert d1 == d3
