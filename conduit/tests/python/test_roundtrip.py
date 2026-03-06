"""Roundtrip encode/decode tests for all primitive and complex types in the Conduit codecs."""
import struct
import math
import pytest

from all_types import (
    AllTypesMessage,
    BitReader,
    BitWriter,
    DecodeError,
)
from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags


# ---------------------------------------------------------------------------
# uint8
# ---------------------------------------------------------------------------
class TestUint8Roundtrip:

    @pytest.mark.parametrize("value", [0, 1, 127, 128, 255])
    def test_u8_roundtrip(self, value):
        w = BitWriter()
        w.write_u8(value)
        data = w.to_bytes()
        r = BitReader(data)
        assert r.read_u8() == value

    def test_u8_boundary_zero(self):
        w = BitWriter()
        w.write_u8(0)
        assert w.to_bytes() == b"\x00"

    def test_u8_boundary_max(self):
        w = BitWriter()
        w.write_u8(255)
        assert w.to_bytes() == b"\xff"


# ---------------------------------------------------------------------------
# uint16
# ---------------------------------------------------------------------------
class TestUint16Roundtrip:

    @pytest.mark.parametrize("value", [0, 1, 0x7FFF, 0x8000, 0xFFFF])
    def test_u16_big_endian_roundtrip(self, value):
        w = BitWriter()
        w.write_u16(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u16(True) == value

    @pytest.mark.parametrize("value", [0, 1, 0x1234, 0xFFFF])
    def test_u16_little_endian_roundtrip(self, value):
        w = BitWriter()
        w.write_u16(value, False)
        r = BitReader(w.to_bytes())
        assert r.read_u16(False) == value

    def test_u16_be_mid_value(self):
        w = BitWriter()
        w.write_u16(0x1234, True)
        data = w.to_bytes()
        assert data == bytes([0x12, 0x34])

    def test_u16_le_mid_value(self):
        w = BitWriter()
        w.write_u16(0x1234, False)
        data = w.to_bytes()
        assert data == bytes([0x34, 0x12])


# ---------------------------------------------------------------------------
# uint32
# ---------------------------------------------------------------------------
class TestUint32Roundtrip:

    @pytest.mark.parametrize("value", [0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF])
    def test_u32_big_endian_roundtrip(self, value):
        w = BitWriter()
        w.write_u32(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u32(True) == value

    @pytest.mark.parametrize("value", [0, 0x12345678, 0xFFFFFFFF])
    def test_u32_little_endian_roundtrip(self, value):
        w = BitWriter()
        w.write_u32(value, False)
        r = BitReader(w.to_bytes())
        assert r.read_u32(False) == value


# ---------------------------------------------------------------------------
# uint64
# ---------------------------------------------------------------------------
class TestUint64Roundtrip:

    @pytest.mark.parametrize("value", [0, 1, 0x0102030405060708, 0xFFFFFFFFFFFFFFFF])
    def test_u64_roundtrip(self, value):
        w = BitWriter()
        w.write_u64(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u64(True) == value


# ---------------------------------------------------------------------------
# Signed integers
# ---------------------------------------------------------------------------
class TestSignedIntRoundtrip:

    @pytest.mark.parametrize("value", [-128, -1, 0, 1, 127])
    def test_i8_roundtrip(self, value):
        w = BitWriter()
        w.write_signed_bits(value, 8)
        r = BitReader(w.to_bytes())
        assert r.read_signed_bits(8) == value

    @pytest.mark.parametrize("value", [-32768, -1, 0, 1, 32767])
    def test_i16_roundtrip(self, value):
        w = BitWriter()
        w.write_signed_bits(value, 16)
        r = BitReader(w.to_bytes())
        assert r.read_signed_bits(16) == value

    @pytest.mark.parametrize("value", [-2147483648, -100000, -1, 0, 1, 100000, 2147483647])
    def test_i32_roundtrip(self, value):
        w = BitWriter()
        w.write_signed_bits(value, 32)
        r = BitReader(w.to_bytes())
        assert r.read_signed_bits(32) == value

    def test_i8_negative_boundary(self):
        w = BitWriter()
        w.write_signed_bits(-128, 8)
        data = w.to_bytes()
        assert data == b"\x80"

    def test_i8_positive_boundary(self):
        w = BitWriter()
        w.write_signed_bits(127, 8)
        data = w.to_bytes()
        assert data == b"\x7f"


# ---------------------------------------------------------------------------
# Float32 / Float64
# ---------------------------------------------------------------------------
class TestFloatRoundtrip:

    @pytest.mark.parametrize("value", [0.0, 1.0, -1.5, 3.140000104904175])
    def test_f32_roundtrip(self, value):
        w = BitWriter()
        w.write_f32(value, True)
        r = BitReader(w.to_bytes())
        result = r.read_f32(True)
        assert result == pytest.approx(value, rel=1e-6)

    @pytest.mark.parametrize("value", [0.0, 1.0, -1.5, 3.141592653589793, 1e100, -1e100])
    def test_f64_roundtrip(self, value):
        w = BitWriter()
        w.write_f64(value, True)
        r = BitReader(w.to_bytes())
        result = r.read_f64(True)
        assert result == pytest.approx(value, rel=1e-15)

    def test_f32_large_value(self):
        value = 1.0e30
        w = BitWriter()
        w.write_f32(value, True)
        r = BitReader(w.to_bytes())
        result = r.read_f32(True)
        assert result == pytest.approx(value, rel=1e-6)

    def test_f64_negative_zero(self):
        w = BitWriter()
        w.write_f64(-0.0, True)
        r = BitReader(w.to_bytes())
        result = r.read_f64(True)
        assert result == 0.0
        assert math.copysign(1.0, result) == -1.0  # negative zero


# ---------------------------------------------------------------------------
# Bool
# ---------------------------------------------------------------------------
class TestBoolRoundtrip:

    def test_bool_true(self):
        w = BitWriter()
        w.write_bits(1, 8)
        r = BitReader(w.to_bytes())
        val = r.read_u8()
        assert (val != 0) is True

    def test_bool_false(self):
        w = BitWriter()
        w.write_bits(0, 8)
        r = BitReader(w.to_bytes())
        val = r.read_u8()
        assert (val != 0) is False

    def test_bool_via_message(self):
        msg = AllTypesMessage()
        msg.flag = True
        msg.ascii = AsciiStr("")
        msg.utf8 = Utf8Str("")
        msg.temp = ScaledTemp(0)
        msg.color = ColorEnum.RED
        msg.status = StatusFlags(0)
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.flag is True

        msg.flag = False
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.flag is False


# ---------------------------------------------------------------------------
# AsciiStr
# ---------------------------------------------------------------------------
class TestAsciiStrRoundtrip:

    def test_empty_string(self):
        s = AsciiStr("")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == ""

    def test_short_string(self):
        s = AsciiStr("Hi")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "Hi"

    def test_max_length_string(self):
        s = AsciiStr("ABCDEFGHIJ")  # exactly 10 chars = WIRE_SIZE
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = AsciiStr.decode(r)
        assert s2.value == "ABCDEFGHIJ"

    def test_null_padding(self):
        """Shorter strings should be null-padded on encode, stripped on decode."""
        s = AsciiStr("AB")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 10
        # Remaining bytes should be null
        assert data[2:] == b"\x00" * 8

    def test_equality(self):
        assert AsciiStr("test") == AsciiStr("test")
        assert AsciiStr("a") != AsciiStr("b")


# ---------------------------------------------------------------------------
# Utf8Str
# ---------------------------------------------------------------------------
class TestUtf8StrRoundtrip:

    def test_basic_string(self):
        s = Utf8Str("Hello")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        assert s2.value == "Hello"

    def test_space_padding(self):
        """Utf8Str is padded with spaces (0x20) and stripped on decode."""
        s = Utf8Str("AB")
        w = BitWriter()
        s.encode(w)
        data = w.to_bytes()
        assert len(data) == 16
        # Remaining bytes should be spaces (0x20)
        assert data[2:] == b" " * 14

    def test_empty_string(self):
        s = Utf8Str("")
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        assert s2.value == ""

    def test_max_length(self):
        s = Utf8Str("A" * 16)
        w = BitWriter()
        s.encode(w)
        r = BitReader(w.to_bytes())
        s2 = Utf8Str.decode(r)
        assert s2.value == "A" * 16


# ---------------------------------------------------------------------------
# Raw bytes (u64 field used as raw)
# ---------------------------------------------------------------------------
class TestRawBytesRoundtrip:

    @pytest.mark.parametrize("value", [0, 0xCAFEBABE00000000, 0xFFFFFFFFFFFFFFFF])
    def test_raw_u64_roundtrip(self, value):
        w = BitWriter()
        w.write_u64(value, True)
        r = BitReader(w.to_bytes())
        assert r.read_u64(True) == value


# ---------------------------------------------------------------------------
# ScaledTemp
# ---------------------------------------------------------------------------
class TestScaledTempRoundtrip:

    def test_raw_zero_gives_negative_40(self):
        t = ScaledTemp(0)
        assert t.value == -40.0

    def test_raw_4000_gives_zero(self):
        t = ScaledTemp(4000)
        assert t.value == pytest.approx(0.0, abs=1e-10)

    def test_raw_10000_gives_60(self):
        t = ScaledTemp(10000)
        assert t.value == pytest.approx(60.0, abs=1e-10)

    def test_set_value_zero(self):
        t = ScaledTemp()
        t.value = 0.0
        assert t.raw == 4000

    def test_set_value_neg40(self):
        t = ScaledTemp()
        t.value = -40.0
        assert t.raw == 0

    def test_roundtrip_via_wire(self):
        t = ScaledTemp(4000)
        w = BitWriter()
        t.encode(w)
        r = BitReader(w.to_bytes())
        t2 = ScaledTemp.decode(r)
        assert t2.raw == 4000
        assert t2.value == pytest.approx(0.0, abs=1e-10)

    def test_equality(self):
        assert ScaledTemp(100) == ScaledTemp(100)
        assert ScaledTemp(100) != ScaledTemp(200)


# ---------------------------------------------------------------------------
# ColorEnum
# ---------------------------------------------------------------------------
class TestColorEnumRoundtrip:

    @pytest.mark.parametrize("color", [ColorEnum.RED, ColorEnum.GREEN, ColorEnum.BLUE])
    def test_color_roundtrip(self, color):
        w = BitWriter()
        color.encode(w)
        r = BitReader(w.to_bytes())
        c2 = ColorEnum.decode(r)
        assert c2 == color

    def test_red_value(self):
        assert ColorEnum.RED == 1

    def test_green_value(self):
        assert ColorEnum.GREEN == 2

    def test_blue_value(self):
        assert ColorEnum.BLUE == 3

    def test_invalid_enum_raises(self):
        w = BitWriter()
        w.write_bits(99, 8)
        r = BitReader(w.to_bytes())
        with pytest.raises(DecodeError, match="unknown ColorEnum value"):
            ColorEnum.decode(r)

    def test_invalid_enum_zero_raises(self):
        w = BitWriter()
        w.write_bits(0, 8)
        r = BitReader(w.to_bytes())
        with pytest.raises(DecodeError, match="unknown ColorEnum value"):
            ColorEnum.decode(r)


# ---------------------------------------------------------------------------
# StatusFlags
# ---------------------------------------------------------------------------
class TestStatusFlagsRoundtrip:

    def test_all_flags_set(self):
        sf = StatusFlags(0b00000111)
        assert sf.active is True
        assert sf.error is True
        assert sf.ready is True

    def test_no_flags_set(self):
        sf = StatusFlags(0)
        assert sf.active is False
        assert sf.error is False
        assert sf.ready is False

    def test_individual_active(self):
        sf = StatusFlags(0b00000001)
        assert sf.active is True
        assert sf.error is False
        assert sf.ready is False

    def test_individual_error(self):
        sf = StatusFlags(0b00000010)
        assert sf.active is False
        assert sf.error is True
        assert sf.ready is False

    def test_individual_ready(self):
        sf = StatusFlags(0b00000100)
        assert sf.active is False
        assert sf.error is False
        assert sf.ready is True

    def test_set_flag(self):
        sf = StatusFlags(0)
        sf.active = True
        assert sf.raw == 0b00000001
        sf.error = True
        assert sf.raw == 0b00000011
        sf.ready = True
        assert sf.raw == 0b00000111

    def test_clear_flag(self):
        sf = StatusFlags(0b00000111)
        sf.active = False
        assert sf.raw == 0b00000110
        sf.error = False
        assert sf.raw == 0b00000100
        sf.ready = False
        assert sf.raw == 0b00000000

    def test_roundtrip_via_wire(self):
        sf = StatusFlags(0b00000101)
        w = BitWriter()
        sf.encode(w)
        r = BitReader(w.to_bytes())
        sf2 = StatusFlags.decode(r)
        assert sf2.active is True
        assert sf2.error is False
        assert sf2.ready is True
        assert sf2.raw == 0b00000101

    def test_equality(self):
        assert StatusFlags(5) == StatusFlags(5)
        assert StatusFlags(5) != StatusFlags(3)


# ---------------------------------------------------------------------------
# Little-endian fields
# ---------------------------------------------------------------------------
class TestLittleEndianRoundtrip:

    def test_le16_roundtrip(self):
        w = BitWriter()
        w.write_u16(0x5678, False)
        r = BitReader(w.to_bytes())
        assert r.read_u16(False) == 0x5678

    def test_le32_roundtrip(self):
        w = BitWriter()
        w.write_u32(0xABCD1234, False)
        r = BitReader(w.to_bytes())
        assert r.read_u32(False) == 0xABCD1234

    def test_le16_wire_order(self):
        """0x1234 in little-endian should be stored as [0x34, 0x12]."""
        w = BitWriter()
        w.write_u16(0x1234, False)
        data = w.to_bytes()
        assert data == bytes([0x34, 0x12])

    def test_le32_wire_order(self):
        """0x12345678 in little-endian should be stored as [0x78, 0x56, 0x34, 0x12]."""
        w = BitWriter()
        w.write_u32(0x12345678, False)
        data = w.to_bytes()
        assert data == bytes([0x78, 0x56, 0x34, 0x12])


# ---------------------------------------------------------------------------
# AllTypesMessage full roundtrip
# ---------------------------------------------------------------------------
class TestAllTypesMessageRoundtrip:

    def test_full_roundtrip(self, all_types_msg):
        data = all_types_msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)

        assert msg2.u8 == all_types_msg.u8
        assert msg2.u16 == all_types_msg.u16
        assert msg2.u32 == all_types_msg.u32
        assert msg2.u64 == all_types_msg.u64
        assert msg2.i8 == all_types_msg.i8
        assert msg2.i16 == all_types_msg.i16
        assert msg2.i32 == all_types_msg.i32
        assert msg2.f32 == pytest.approx(all_types_msg.f32, rel=1e-6)
        assert msg2.f64 == pytest.approx(all_types_msg.f64, rel=1e-15)
        assert msg2.flag == all_types_msg.flag
        assert msg2.ascii.value == all_types_msg.ascii.value
        assert msg2.utf8.value == all_types_msg.utf8.value
        assert msg2.raw == all_types_msg.raw
        assert msg2.le16 == all_types_msg.le16
        assert msg2.le32 == all_types_msg.le32
        assert msg2.temp.raw == all_types_msg.temp.raw
        assert msg2.temp.value == pytest.approx(all_types_msg.temp.value, abs=1e-10)
        assert msg2.hex == all_types_msg.hex
        assert msg2.color == all_types_msg.color
        assert msg2.status.raw == all_types_msg.status.raw

    def test_default_values_roundtrip(self):
        """Message with all default values should roundtrip cleanly."""
        msg = AllTypesMessage()
        msg.ascii = AsciiStr("")
        msg.utf8 = Utf8Str("")
        msg.temp = ScaledTemp(0)
        msg.color = ColorEnum.RED
        msg.status = StatusFlags(0)
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.u8 == 0
        assert msg2.u16 == 0
        assert msg2.u32 == 0
        assert msg2.u64 == 0
        assert msg2.i8 == 0
        assert msg2.i16 == 0
        assert msg2.i32 == 0
        assert msg2.f32 == pytest.approx(0.0)
        assert msg2.f64 == pytest.approx(0.0)
        assert msg2.flag is False
        assert msg2.ascii.value == ""
        assert msg2.utf8.value == ""
        assert msg2.raw == 0
        assert msg2.le16 == 0
        assert msg2.le32 == 0
        assert msg2.temp.raw == 0
        assert msg2.color == ColorEnum.RED
        assert msg2.status.raw == 0

    def test_encode_decode_idempotent(self, all_types_msg):
        """Encode -> decode -> encode should produce the same bytes."""
        data1 = all_types_msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_max_unsigned_values(self):
        """Test max values for unsigned fields."""
        msg = AllTypesMessage()
        msg.u8 = 255
        msg.u16 = 65535
        msg.u32 = 0xFFFFFFFF
        msg.u64 = 0xFFFFFFFFFFFFFFFF
        msg.i8 = 0
        msg.i16 = 0
        msg.i32 = 0
        msg.le16 = 0xFFFF
        msg.le32 = 0xFFFFFFFF
        msg.raw = 0xFFFFFFFFFFFFFFFF
        msg.hex = 0xFFFFFFFF
        msg.ascii = AsciiStr("")
        msg.utf8 = Utf8Str("")
        msg.temp = ScaledTemp(0xFFFF)
        msg.color = ColorEnum.BLUE
        msg.status = StatusFlags(0xFF)
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.u8 == 255
        assert msg2.u16 == 65535
        assert msg2.u32 == 0xFFFFFFFF
        assert msg2.u64 == 0xFFFFFFFFFFFFFFFF
        assert msg2.le16 == 0xFFFF
        assert msg2.le32 == 0xFFFFFFFF

    def test_min_signed_values(self):
        """Test minimum values for signed fields."""
        msg = AllTypesMessage()
        msg.i8 = -128
        msg.i16 = -32768
        msg.i32 = -2147483648
        msg.ascii = AsciiStr("")
        msg.utf8 = Utf8Str("")
        msg.temp = ScaledTemp(0)
        msg.color = ColorEnum.RED
        msg.status = StatusFlags(0)
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.i8 == -128
        assert msg2.i16 == -32768
        assert msg2.i32 == -2147483648

    def test_max_signed_values(self):
        """Test maximum values for signed fields."""
        msg = AllTypesMessage()
        msg.i8 = 127
        msg.i16 = 32767
        msg.i32 = 2147483647
        msg.ascii = AsciiStr("")
        msg.utf8 = Utf8Str("")
        msg.temp = ScaledTemp(0)
        msg.color = ColorEnum.RED
        msg.status = StatusFlags(0)
        data = msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data)
        assert msg2.i8 == 127
        assert msg2.i16 == 32767
        assert msg2.i32 == 2147483647


# ---------------------------------------------------------------------------
# Boundary types module
# ---------------------------------------------------------------------------
class TestBoundaryTypesRoundtrip:

    def test_boundary_msg_max_values(self):
        from boundary_types import BoundaryMsg
        from boundary_types.types import ScaledTemp as BScaledTemp, NybbleEnum

        msg = BoundaryMsg()
        msg.flag = 1
        msg.small = 7
        msg.medium = 127
        msg.byte_val = 255
        msg.word = 65535
        msg.dword = 0xFFFFFFFF
        msg.qword = 0xFFFFFFFFFFFFFFFF
        msg.signed_byte = -128
        msg.signed_word = -32768
        msg.signed_dword = -2147483648
        msg.signed_qword = -(2**63)
        msg.temp = BScaledTemp(0)
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
        assert msg2.signed_byte == -128
        assert msg2.signed_word == -32768
        assert msg2.signed_dword == -2147483648
        assert msg2.signed_qword == -(2**63)
        assert msg2.level == NybbleEnum.HIGH

    def test_boundary_msg_zero_values(self):
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
        msg2 = BoundaryMsg.decode_bytes(data)
        assert msg2.flag == 0
        assert msg2.small == 0
        assert msg2.medium == 0
        assert msg2.byte_val == 0
        assert msg2.signed_byte == 0
        assert msg2.signed_qword == 0
        assert msg2.level == NybbleEnum.OFF

    def test_odd_width_msg_max(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0xFFF
        msg.u20 = 0xFFFFF
        msg.s12 = -2048
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

    def test_odd_width_msg_positive_max(self):
        from boundary_types import OddWidthMsg

        msg = OddWidthMsg()
        msg.u12 = 0xFFF
        msg.u20 = 0xFFFFF
        msg.s12 = 2047  # max positive 12-bit signed
        data = msg.encode_bytes()
        msg2 = OddWidthMsg.decode_bytes(data)
        assert msg2.s12 == 2047

    def test_nybble_enum_roundtrip(self):
        from boundary_types.types import NybbleEnum
        from boundary_types import BitReader as BR, BitWriter as BW

        for level in [NybbleEnum.OFF, NybbleEnum.LOW, NybbleEnum.MID, NybbleEnum.HIGH]:
            w = BW()
            level.encode(w)
            r = BR(w.to_bytes())
            decoded = NybbleEnum.decode(r)
            assert decoded == level

    def test_nybble_enum_invalid_raises(self):
        from boundary_types.types import NybbleEnum
        from boundary_types import BitReader as BR, BitWriter as BW, DecodeError as DE

        w = BW()
        w.write_bits(15, 4)  # 15 is not a valid NybbleEnum
        r = BR(w.to_bytes())
        with pytest.raises(DE, match="unknown NybbleEnum value"):
            NybbleEnum.decode(r)


# ---------------------------------------------------------------------------
# Mixed endian
# ---------------------------------------------------------------------------
class TestMixedEndianRoundtrip:

    def test_mixed_msg_roundtrip(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0x1234
        msg.le16 = 0x5678
        msg.be32 = 0xDEADBEEF
        msg.le32 = 0xCAFEBABE

        data = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data)
        assert msg2.be16 == 0x1234
        assert msg2.le16 == 0x5678
        assert msg2.be32 == 0xDEADBEEF
        assert msg2.le32 == 0xCAFEBABE

    def test_mixed_msg_idempotent(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0xAAAA
        msg.le16 = 0xBBBB
        msg.be32 = 0xCCCCCCCC
        msg.le32 = 0xDDDDDDDD

        data1 = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# Field-level scale/offset
# ---------------------------------------------------------------------------
class TestFieldScaleRoundtrip:

    def test_scale_msg_basic(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 2.5       # raw = 2.5 / 0.25 = 10
        msg.temp = 25.0    # raw = (25 + 40) / 0.01 = 6500
        msg.plain = 99

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(2.5)
        assert msg2.temp == pytest.approx(25.0)
        assert msg2.plain == 99

    def test_scale_msg_zero(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 0.0
        msg.temp = -40.0  # raw = 0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(0.0)
        assert msg2.temp == pytest.approx(-40.0)
        assert msg2.plain == 0

    def test_scale_msg_negative(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = -3.0  # raw = -3.0 / 0.25 = -12
        msg.temp = 0.0
        msg.plain = 0

        data = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data)
        assert msg2.fl == pytest.approx(-3.0)

    def test_scale_msg_idempotent(self):
        from field_scale import ScaleMsg

        msg = ScaleMsg()
        msg.fl = 1.75
        msg.temp = 50.0
        msg.plain = 200

        data1 = msg.encode_bytes()
        msg2 = ScaleMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# Wire encoding types (BCD, sign-magnitude, etc.)
# ---------------------------------------------------------------------------
class TestWireEncodingRoundtrip:

    def test_bcd_basic(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 1234
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 1234

    def test_bcd_signed_positive(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 180
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_hdg == 180

    def test_bcd_signed_negative(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = -90
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_hdg == -90

    def test_sign_magnitude(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = -500
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.sm_offset == -500

    def test_twos_complement(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = -100
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.cb2_val == -100

    def test_full_roundtrip(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 9876
        msg.bcd_hdg = -45
        msg.sm_offset = 1000
        msg.cb2_val = -200
        msg.bnr_val = 5000
        msg.inline_bcd = 789
        msg.inline_bnrs = -300

        data = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data)
        assert msg2.bcd_alt == 9876
        assert msg2.bcd_hdg == -45
        assert msg2.sm_offset == 1000
        assert msg2.cb2_val == -200
        assert msg2.bnr_val == 5000
        assert msg2.inline_bcd == 789
        assert msg2.inline_bnrs == -300

    def test_idempotent(self):
        from wire_encodings import WireEncodingMsg

        msg = WireEncodingMsg()
        msg.bcd_alt = 4567
        msg.bcd_hdg = 123
        msg.sm_offset = -999
        msg.cb2_val = 32767
        msg.bnr_val = 60000
        msg.inline_bcd = 111
        msg.inline_bnrs = 0

        data1 = msg.encode_bytes()
        msg2 = WireEncodingMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# String features
# ---------------------------------------------------------------------------
class TestStringFeaturesRoundtrip:

    def test_string_msg_roundtrip(self):
        from string_features import StringMsg
        from string_features.types import NameStr, LabelStr, BoundedStr, PackedStr, TermStr

        msg = StringMsg()
        msg.id = 42
        msg.name = NameStr("TestName")
        msg.label = LabelStr("MyLabel")
        msg.bounded = BoundedStr("BoundedValue")
        msg.packed = PackedStr("Pack")
        msg.term = TermStr("Terminated")

        data = msg.encode_bytes()
        msg2 = StringMsg.decode_bytes(data)
        assert msg2.id == 42
        assert msg2.name.value == "TestName"
        assert msg2.label.value == "MyLabel"
        assert msg2.bounded.value == "BoundedValue"
        assert msg2.packed.value == "Pack"
        assert msg2.term.value == "Terminated"

    def test_string_msg_empty_strings(self):
        from string_features import StringMsg
        from string_features.types import NameStr, LabelStr, BoundedStr, PackedStr, TermStr

        msg = StringMsg()
        msg.id = 0
        msg.name = NameStr("")
        msg.label = LabelStr("")
        msg.bounded = BoundedStr("")
        msg.packed = PackedStr("")
        msg.term = TermStr("")

        data = msg.encode_bytes()
        msg2 = StringMsg.decode_bytes(data)
        assert msg2.name.value == ""
        assert msg2.label.value == ""
        assert msg2.bounded.value == ""
        assert msg2.packed.value == ""
        assert msg2.term.value == ""

    def test_max_len_msg_roundtrip(self):
        from string_features import MaxLenMsg

        msg = MaxLenMsg()
        msg.id = 10
        msg.data = "Short"

        data = msg.encode_bytes()
        msg2 = MaxLenMsg.decode_bytes(data)
        assert msg2.id == 10
        assert msg2.data == "Short"

    def test_max_len_msg_full_data(self):
        from string_features import MaxLenMsg

        msg = MaxLenMsg()
        msg.id = 1
        msg.data = "A" * 16  # max-length constraint is 16

        data = msg.encode_bytes()
        msg2 = MaxLenMsg.decode_bytes(data)
        assert msg2.data == "A" * 16

    def test_max_len_msg_exceeds_constraint(self):
        """Data exceeding max-length=16 should raise ConstraintError on decode."""
        from string_features import MaxLenMsg
        from string_features.bit_io import ConstraintError

        msg = MaxLenMsg()
        msg.id = 1
        msg.data = "A" * 32  # exceeds max-length=16

        data = msg.encode_bytes()
        with pytest.raises(ConstraintError, match="max length"):
            MaxLenMsg.decode_bytes(data)

    def test_name_str_wire_size(self):
        from string_features.types import NameStr
        assert NameStr.WIRE_SIZE == 20

    def test_label_str_wire_size(self):
        from string_features.types import LabelStr
        assert LabelStr.WIRE_SIZE == 16

    def test_bounded_str_wire_size(self):
        from string_features.types import BoundedStr
        assert BoundedStr.WIRE_SIZE == 32
