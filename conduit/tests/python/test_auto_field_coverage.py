"""Roundtrip encode/decode tests for auto field generated Python modules.

Covers:
- auto_seq (auto_sequence.bmdl.xml): frame with auto-increment sequence,
  auto-id, sync constant, BodyA message
- auto_struct_length (auto_struct_length.bmdl.xml): TlvMsg with auto-length
  field for data bytes, tag + len + data + suffix
"""
import pytest


# ============================================================================
# auto_seq: Frame with sync(0xBEEF), seq(auto-increment), tag(auto-id),
#           BodyA message (data: uint16)
# ============================================================================


class TestAutoSeqBodyA:
    """BodyA standalone message roundtrip."""

    def test_roundtrip(self):
        from auto_seq import BodyA

        msg = BodyA()
        msg.data = 0x1234

        data = msg.encode_bytes()
        msg2 = BodyA.decode_bytes(data)
        assert msg2.data == 0x1234

    def test_zero_value(self):
        from auto_seq import BodyA

        msg = BodyA()
        msg.data = 0

        data = msg.encode_bytes()
        msg2 = BodyA.decode_bytes(data)
        assert msg2.data == 0

    def test_max_value(self):
        from auto_seq import BodyA

        msg = BodyA()
        msg.data = 0xFFFF

        data = msg.encode_bytes()
        msg2 = BodyA.decode_bytes(data)
        assert msg2.data == 0xFFFF

    def test_double_encode(self):
        from auto_seq import BodyA

        msg = BodyA()
        msg.data = 0xABCD

        data1 = msg.encode_bytes()
        msg2 = BodyA.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_wire_size(self):
        from auto_seq import BodyA

        msg = BodyA()
        msg.data = 1

        data = msg.encode_bytes()
        assert len(data) == 2  # uint16 = 2 bytes


class TestAutoSeqConstants:
    """Constants defined in auto_seq protocol."""

    def test_sync_constant(self):
        from auto_seq.constants import Constants

        assert Constants.SYNC == 0xBEEF


class TestAutoSeqBodyAMetadata:
    """BodyA metadata (TYPE_ID, ID_VALUE)."""

    def test_id_value(self):
        from auto_seq import BodyA

        assert hasattr(BodyA, 'ID_VALUE')
        assert BodyA.ID_VALUE == 1

    def test_type_id_exists(self):
        from auto_seq import BodyA

        assert hasattr(BodyA, 'TYPE_ID')


class TestAutoSeqFrameSession:
    """Frame session for auto_seq protocol."""

    def test_session_creation(self):
        from auto_seq import FrameSession

        session = FrameSession()
        assert session is not None

    def test_initial_seq_is_zero(self):
        from auto_seq import FrameSession

        session = FrameSession()
        assert session._seq == 0

    def test_sync_pattern(self):
        from auto_seq import FrameSession

        session = FrameSession()
        sync = session.sync_pattern()
        assert sync == b'\xBE\xEF'

    def test_encode_wrap_body_a(self):
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        msg = BodyA()
        msg.data = 0x5678

        result = session.encode_wrap(BodyA.TYPE_ID, msg)
        assert result is not None
        assert 'bytes' in result

    def test_encode_wrap_decode_frame_roundtrip(self):
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        msg = BodyA()
        msg.data = 0x9999

        wrapped = session.encode_wrap(BodyA.TYPE_ID, msg)
        assert wrapped is not None

        messages = session.decode_frame(wrapped['bytes'])
        assert messages is not None
        assert len(messages) == 1
        decoded = messages[0]['payload']
        assert decoded.data == 0x9999

    def test_sequence_increments(self):
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        msg = BodyA()
        msg.data = 1

        r1 = session.encode_wrap(BodyA.TYPE_ID, msg)
        r2 = session.encode_wrap(BodyA.TYPE_ID, msg)
        # Frames should differ due to sequence increment
        assert r1['bytes'] != r2['bytes']

    def test_reset_clears_sequence(self):
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        msg = BodyA()
        msg.data = 1

        session.encode_wrap(BodyA.TYPE_ID, msg)
        session.encode_wrap(BodyA.TYPE_ID, msg)
        session.reset()
        assert session._seq == 0

    def test_encode_wrap_unknown_type(self):
        from auto_seq import FrameSession

        session = FrameSession()
        result = session.encode_wrap(0xDEADDEADDEADDEAD, object())
        assert result is None

    def test_multiple_roundtrips(self):
        """Multiple encode_wrap/decode_frame cycles should all succeed."""
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        for val in [0, 100, 0xFFFF, 0x8000, 1]:
            msg = BodyA()
            msg.data = val

            wrapped = session.encode_wrap(BodyA.TYPE_ID, msg)
            messages = session.decode_frame(wrapped['bytes'])
            assert len(messages) == 1
            assert messages[0]['payload'].data == val

    def test_leaf_type_ids(self):
        from auto_seq import FrameSession, BodyA

        session = FrameSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 1
        assert BodyA.TYPE_ID in ids


# ============================================================================
# auto_struct_length: TlvMsg with tag, auto-length(data), data(bytes), suffix
# ============================================================================


class TestAutoStructLengthBasic:
    """Basic TlvMsg roundtrip tests."""

    def test_roundtrip(self):
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

    def test_empty_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0x01
        msg.data = b''
        msg.suffix = 0x00

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.tag == 0x01
        assert msg2.len == 0
        assert msg2.data == b''
        assert msg2.suffix == 0x00

    def test_single_byte_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xAA
        msg.data = b'\xBB'
        msg.suffix = 0xCC

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.len == 1
        assert msg2.data == b'\xBB'


class TestAutoStructLengthAutoField:
    """Verify auto-length is computed correctly."""

    def test_auto_length_matches_data_size(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0x10
        msg.data = b'\xDE\xAD\xBE\xEF'
        msg.suffix = 0x20

        data = msg.encode_bytes()
        # tag(1) + len(1) + data(4) + suffix(1) = 7
        assert len(data) == 7
        # The length byte at position 1 should be 4 (data size only)
        assert data[1] == 4

    def test_auto_length_for_various_sizes(self):
        from auto_struct_length import TlvMsg

        for size in [0, 1, 5, 10, 50, 100, 255]:
            msg = TlvMsg()
            msg.tag = 0x01
            msg.data = bytes(range(size % 256)) * (size // 256) + bytes(range(size % 256))
            msg.data = bytes(size)  # size zero-bytes
            msg.suffix = 0x02

            data = msg.encode_bytes()
            msg2 = TlvMsg.decode_bytes(data)
            assert msg2.len == size
            assert len(msg2.data) == size

    def test_auto_length_max_uint8(self):
        """Max auto-length for uint8 is 255."""
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xFF
        msg.data = bytes(255)
        msg.suffix = 0xFF

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.len == 255
        assert len(msg2.data) == 255


class TestAutoStructLengthBoundary:
    """Boundary values for tag and suffix."""

    def test_tag_zero(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0
        msg.data = b'\x01'
        msg.suffix = 0

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.tag == 0

    def test_tag_max(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xFF
        msg.data = b'\x01'
        msg.suffix = 0xFF

        data = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data)
        assert msg2.tag == 0xFF
        assert msg2.suffix == 0xFF


class TestAutoStructLengthDoubleEncode:
    """Encode -> decode -> encode produces same bytes."""

    def test_double_encode_with_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0x10
        msg.data = bytes(range(10))
        msg.suffix = 0x20

        data1 = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_double_encode_empty_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xAA
        msg.data = b''
        msg.suffix = 0xBB

        data1 = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2

    def test_double_encode_max_data(self):
        from auto_struct_length import TlvMsg

        msg = TlvMsg()
        msg.tag = 0xCC
        msg.data = b'\xFF' * 100
        msg.suffix = 0xDD

        data1 = msg.encode_bytes()
        msg2 = TlvMsg.decode_bytes(data1)
        data2 = msg2.encode_bytes()
        assert data1 == data2
