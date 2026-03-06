"""Tests for error paths in the Conduit generated Python codecs.

Covers: truncated data, invalid enum values, constraint violations, empty input,
and encode-decode identity invariants.
"""
import pytest

from all_types import (
    AllTypesMessage,
    BitReader,
    BitWriter,
    DecodeError,
    ConduitError,
)
from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags


# ---------------------------------------------------------------------------
# Truncated data
# ---------------------------------------------------------------------------
class TestTruncatedData:

    def test_all_types_message_one_byte(self):
        with pytest.raises(DecodeError, match="underflow"):
            AllTypesMessage.decode_bytes(b"\x42")

    def test_all_types_message_two_bytes(self):
        with pytest.raises(DecodeError, match="underflow"):
            AllTypesMessage.decode_bytes(b"\x00\x01")

    def test_all_types_message_ten_bytes(self):
        with pytest.raises(DecodeError):
            AllTypesMessage.decode_bytes(bytes(10))

    def test_ping_body_too_short(self):
        from session_protocol import PingBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            PingBody.decode_bytes(b"\x12\x34")  # needs 4 bytes

    def test_data_body_too_short(self):
        from session_protocol import DataBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            DataBody.decode_bytes(b"\x05\xAA\xBB")  # needs 9 bytes

    def test_ack_body_too_short(self):
        from session_protocol import AckBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            AckBody.decode_bytes(b"\x12")  # needs 2 bytes

    def test_alpha_body_too_short(self):
        from choice_protocol import AlphaBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            AlphaBody.decode_bytes(b"\x00\x01")  # needs 4 bytes

    def test_beta_body_too_short(self):
        from choice_protocol import BetaBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            BetaBody.decode_bytes(b"\x00\x01")  # needs 5 bytes

    def test_point_too_short(self):
        from arrays_choices.structs import Point
        from arrays_choices import DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            Point.decode_bytes(b"\x00")  # needs 4 bytes

    def test_sub_x_too_short(self):
        from arrays_choices.structs import SubX
        from arrays_choices import DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            SubX.decode_bytes(b"\x00\x01")  # needs 4 bytes

    def test_fixed_array_msg_too_short(self):
        from arrays_choices.messages import FixedArrayMsg
        from arrays_choices import DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            FixedArrayMsg.decode_bytes(bytes(8))  # needs 12 bytes

    def test_wire_encoding_msg_too_short(self):
        from wire_encodings import WireEncodingMsg, DecodeError as DE

        with pytest.raises(DE):
            WireEncodingMsg.decode_bytes(bytes(5))  # needs more

    def test_boundary_msg_too_short(self):
        from boundary_types import BoundaryMsg, DecodeError as DE

        with pytest.raises(DE):
            BoundaryMsg.decode_bytes(bytes(10))  # needs much more

    def test_odd_width_msg_too_short(self):
        from boundary_types import OddWidthMsg, DecodeError as DE

        with pytest.raises(DE):
            OddWidthMsg.decode_bytes(bytes(3))  # needs ~5.5 bytes

    def test_mixed_msg_too_short(self):
        from mixed_endian import MixedMsg, DecodeError as DE

        with pytest.raises(DE):
            MixedMsg.decode_bytes(bytes(8))  # needs 12 bytes

    def test_scale_msg_too_short(self):
        from field_scale import ScaleMsg, DecodeError as DE

        with pytest.raises(DE):
            ScaleMsg.decode_bytes(bytes(2))  # needs more


# ---------------------------------------------------------------------------
# Empty data
# ---------------------------------------------------------------------------
class TestEmptyData:

    def test_all_types_empty_bytes(self):
        with pytest.raises(DecodeError, match="underflow"):
            AllTypesMessage.decode_bytes(b"")

    def test_ping_body_empty(self):
        from session_protocol import PingBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            PingBody.decode_bytes(b"")

    def test_data_body_empty(self):
        from session_protocol import DataBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            DataBody.decode_bytes(b"")

    def test_ack_body_empty(self):
        from session_protocol import AckBody, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            AckBody.decode_bytes(b"")

    def test_point_empty(self):
        from arrays_choices.structs import Point
        from arrays_choices import DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            Point.decode_bytes(b"")

    def test_bit_reader_empty(self):
        r = BitReader(b"")
        assert r.remaining_bits() == 0
        assert r.remaining_bytes() == 0
        with pytest.raises(DecodeError, match="underflow"):
            r.read_u8()

    def test_fixed_array_empty(self):
        from arrays_choices.messages import FixedArrayMsg
        from arrays_choices import DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            FixedArrayMsg.decode_bytes(b"")

    def test_mixed_msg_empty(self):
        from mixed_endian import MixedMsg, DecodeError as DE

        with pytest.raises(DE, match="underflow"):
            MixedMsg.decode_bytes(b"")


# ---------------------------------------------------------------------------
# Invalid enum values
# ---------------------------------------------------------------------------
class TestInvalidEnumValues:

    @pytest.mark.parametrize("invalid_value", [0, 4, 10, 99, 128, 255])
    def test_color_enum_invalid(self, invalid_value):
        w = BitWriter()
        w.write_bits(invalid_value, 8)
        r = BitReader(w.to_bytes())
        with pytest.raises(DecodeError, match="unknown ColorEnum value"):
            ColorEnum.decode(r)

    def test_color_enum_valid_values_succeed(self):
        for valid in [1, 2, 3]:
            w = BitWriter()
            w.write_bits(valid, 8)
            r = BitReader(w.to_bytes())
            c = ColorEnum.decode(r)
            assert c == valid

    @pytest.mark.parametrize("invalid_value", [4, 5, 6, 7, 8, 9, 10, 15])
    def test_nybble_enum_invalid(self, invalid_value):
        from boundary_types.types import NybbleEnum
        from boundary_types import BitReader as BR, BitWriter as BW, DecodeError as DE

        w = BW()
        w.write_bits(invalid_value, 4)
        r = BR(w.to_bytes())
        with pytest.raises(DE, match="unknown NybbleEnum value"):
            NybbleEnum.decode(r)

    def test_nybble_enum_valid_values_succeed(self):
        from boundary_types.types import NybbleEnum
        from boundary_types import BitReader as BR, BitWriter as BW

        for valid in [0, 1, 2, 3]:
            w = BW()
            w.write_bits(valid, 4)
            r = BR(w.to_bytes())
            n = NybbleEnum.decode(r)
            assert n == valid


# ---------------------------------------------------------------------------
# Error hierarchy
# ---------------------------------------------------------------------------
class TestErrorHierarchy:

    def test_decode_error_is_conduit_error(self):
        assert issubclass(DecodeError, ConduitError)

    def test_encode_error_is_conduit_error(self):
        from all_types import EncodeError
        assert issubclass(EncodeError, ConduitError)

    def test_constraint_error_is_conduit_error(self):
        from all_types import ConstraintError
        assert issubclass(ConstraintError, ConduitError)

    def test_decode_error_is_exception(self):
        assert issubclass(DecodeError, Exception)


# ---------------------------------------------------------------------------
# BitReader exhaustion
# ---------------------------------------------------------------------------
class TestBitReaderExhaustion:

    def test_read_past_end_u8(self):
        r = BitReader(b"\x42")
        r.read_u8()  # consume all
        with pytest.raises(DecodeError, match="underflow"):
            r.read_u8()

    def test_read_past_end_u16(self):
        r = BitReader(b"\x42\x43")
        r.read_u16(True)  # consume all
        with pytest.raises(DecodeError, match="underflow"):
            r.read_u8()

    def test_read_past_end_bits(self):
        r = BitReader(b"\xFF")
        r.read_bits(8)  # consume all
        with pytest.raises(DecodeError, match="underflow"):
            r.read_bits(1)

    def test_remaining_bits_accurate(self):
        r = BitReader(b"\xFF\xFF")
        assert r.remaining_bits() == 16
        r.read_bits(5)
        assert r.remaining_bits() == 11
        r.read_bits(11)
        assert r.remaining_bits() == 0

    def test_remaining_bytes_accurate(self):
        r = BitReader(b"\xFF\xFF\xFF")
        assert r.remaining_bytes() == 3
        r.read_u8()
        assert r.remaining_bytes() == 2

    def test_skip_bits_past_end(self):
        r = BitReader(b"\xFF")
        with pytest.raises(DecodeError, match="underflow"):
            r.skip_bits(16)

    def test_read_string_past_end(self):
        r = BitReader(b"AB")
        with pytest.raises(DecodeError, match="underflow"):
            r.read_string(5)


# ---------------------------------------------------------------------------
# Encode then decode identity
# ---------------------------------------------------------------------------
class TestEncodeDecodeIdentity:

    def test_ping_identity(self):
        from session_protocol import PingBody

        for ts in [0, 1, 0x12345678, 0xFFFFFFFF]:
            ping = PingBody()
            ping.timestamp = ts
            data = ping.encode_bytes()
            ping2 = PingBody.decode_bytes(data)
            assert ping2.timestamp == ts

    def test_data_body_identity(self):
        from session_protocol import DataBody

        db = DataBody()
        db.channel = 255
        db.payload_a = 0
        db.payload_b = 0xFFFFFFFF
        data = db.encode_bytes()
        db2 = DataBody.decode_bytes(data)
        assert db2.channel == 255
        assert db2.payload_a == 0
        assert db2.payload_b == 0xFFFFFFFF

    def test_ack_body_identity(self):
        from session_protocol import AckBody

        for seq in [0, 1, 0x7FFF, 0xFFFF]:
            ack = AckBody()
            ack.acked_seq = seq
            data = ack.encode_bytes()
            ack2 = AckBody.decode_bytes(data)
            assert ack2.acked_seq == seq

    def test_all_types_double_encode(self, all_types_msg):
        """Encode -> decode -> encode should produce identical bytes."""
        data1 = all_types_msg.encode_bytes()
        msg2 = AllTypesMessage.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_point_identity(self):
        from arrays_choices.structs import Point

        for x, y in [(0, 0), (1, 1), (0xFFFF, 0xFFFF), (0x1234, 0x5678)]:
            p = Point()
            p.x = x
            p.y = y
            data = p.encode_bytes()
            p2 = Point.decode_bytes(data)
            assert p2.x == x
            assert p2.y == y

    def test_mixed_msg_identity(self):
        from mixed_endian import MixedMsg

        msg = MixedMsg()
        msg.be16 = 0x1234
        msg.le16 = 0x5678
        msg.be32 = 0xDEADBEEF
        msg.le32 = 0xCAFEBABE
        data1 = msg.encode_bytes()
        msg2 = MixedMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2


# ---------------------------------------------------------------------------
# Constraint message (no runtime checks in generated code, but test structure)
# ---------------------------------------------------------------------------
class TestConstraintMessage:

    def test_constraint_msg_roundtrip(self):
        from constraints import ConstraintMsg

        msg = ConstraintMsg()
        msg.magic = 0xBEEF  # must match equals constraint
        msg.percent = 50
        msg.deferred_val = 1000
        msg.payload = 0x12345678

        data = msg.encode_bytes()
        msg2 = ConstraintMsg.decode_bytes(data)
        assert msg2.magic == 0xBEEF
        assert msg2.percent == 50
        assert msg2.deferred_val == 1000
        assert msg2.payload == 0x12345678

    def test_constraint_msg_empty_fails(self):
        from constraints import ConstraintMsg, DecodeError as CDE

        with pytest.raises(CDE, match="underflow"):
            ConstraintMsg.decode_bytes(b"")


# ---------------------------------------------------------------------------
# Struct features (struct_features module)
# ---------------------------------------------------------------------------
class TestStructFeaturesErrors:

    def test_constrained_message_roundtrip(self):
        from struct_features import ConstrainedMessage
        from struct_features.structs import GpsCoord
        from struct_features.constants import Constants

        msg = ConstrainedMessage()
        # magic and version must match constraint equals values
        msg.magic = Constants.MAGIC  # 0xCAFE
        msg.version = Constants.VERSION  # 3
        msg.value = 500  # constraint: 10..1000
        msg.position = GpsCoord()
        msg.position.latitude = 0x12345678
        msg.position.longitude = 0xDEADBEEF

        data = msg.encode_bytes()
        msg2 = ConstrainedMessage.decode_bytes(data)
        assert msg2.magic == Constants.MAGIC
        assert msg2.version == Constants.VERSION
        assert msg2.value == 500
        assert msg2.position.latitude == 0x12345678
        assert msg2.position.longitude == 0xDEADBEEF

    def test_constrained_message_truncated(self):
        from struct_features import ConstrainedMessage, DecodeError as SFDE
        from struct_features.bit_io import ConstraintError

        # decode of all-zeros will fail on constraint (magic != MAGIC)
        with pytest.raises((SFDE, ConstraintError)):
            ConstrainedMessage.decode_bytes(bytes(13))

    def test_conditional_message_with_extra(self):
        from struct_features import ConditionalMessage

        msg = ConditionalMessage()
        msg.has_extra = 1
        msg.base_value = 100
        msg.extra_value = 200

        data = msg.encode_bytes()
        msg2 = ConditionalMessage.decode_bytes(data)
        assert msg2.has_extra == 1
        assert msg2.base_value == 100
        assert msg2.extra_value == 200

    def test_conditional_message_without_extra(self):
        from struct_features import ConditionalMessage

        msg = ConditionalMessage()
        msg.has_extra = 0
        msg.base_value = 42

        data = msg.encode_bytes()
        msg2 = ConditionalMessage.decode_bytes(data)
        assert msg2.has_extra == 0
        assert msg2.base_value == 42
        assert msg2.extra_value is None

    def test_conditional_message_variable_size(self):
        """With extra, message is 5 bytes; without, 3 bytes."""
        from struct_features import ConditionalMessage

        msg_with = ConditionalMessage()
        msg_with.has_extra = 1
        msg_with.base_value = 100
        msg_with.extra_value = 200
        data_with = msg_with.encode_bytes()

        msg_without = ConditionalMessage()
        msg_without.has_extra = 0
        msg_without.base_value = 100
        data_without = msg_without.encode_bytes()

        assert len(data_with) == 5
        assert len(data_without) == 3

    def test_gps_coord_truncated(self):
        from struct_features.structs import GpsCoord
        from struct_features import DecodeError as SFDE

        with pytest.raises(SFDE):
            GpsCoord.decode_bytes(bytes(4))  # needs 8 bytes
