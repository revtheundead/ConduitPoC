"""Tests for session functionality in the Conduit generated Python codecs."""
import pytest

from session_protocol import (
    PacketSession,
    PingBody,
    DataBody,
    AckBody,
    Constants as SessionConstants,
    ProtocolDescriptor,
    TypeInfo,
)
from choice_protocol import (
    FrameSession,
    AlphaBody,
    BetaBody,
    Constants as ChoiceConstants,
    ProtocolDescriptor as ChoiceProtocolDescriptor,
)


# ---------------------------------------------------------------------------
# PacketSession
# ---------------------------------------------------------------------------
class TestPacketSessionCreation:

    def test_can_instantiate(self):
        session = PacketSession()
        assert session is not None

    def test_initial_seq_is_zero(self):
        session = PacketSession()
        assert session._seq == 0


class TestPacketSessionLeafTypes:

    def test_leaf_type_ids_returns_three_types(self):
        session = PacketSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 3

    def test_leaf_type_ids_contains_ping(self):
        session = PacketSession()
        ids = session.leaf_type_ids()
        assert PingBody.TYPE_ID in ids

    def test_leaf_type_ids_contains_data(self):
        session = PacketSession()
        ids = session.leaf_type_ids()
        assert DataBody.TYPE_ID in ids

    def test_leaf_type_ids_contains_ack(self):
        session = PacketSession()
        ids = session.leaf_type_ids()
        assert AckBody.TYPE_ID in ids

    def test_leaf_type_ids_exact_values(self):
        session = PacketSession()
        ids = session.leaf_type_ids()
        assert 0x0ad7bb3ecc473399 in ids
        assert 0x29d16b9e73f85835 in ids
        assert 0xcc431e5e357bc2e6 in ids


class TestPacketSessionTypeName:

    def test_type_name_ping(self):
        session = PacketSession()
        assert session.type_name(0x0ad7bb3ecc473399) == "PingBody"

    def test_type_name_data(self):
        session = PacketSession()
        assert session.type_name(0x29d16b9e73f85835) == "DataBody"

    def test_type_name_ack(self):
        session = PacketSession()
        assert session.type_name(0xcc431e5e357bc2e6) == "AckBody"

    def test_type_name_unknown(self):
        session = PacketSession()
        assert session.type_name(0xDEAD) == "unknown"

    def test_type_name_zero(self):
        session = PacketSession()
        assert session.type_name(0) == "unknown"


class TestPacketSessionProtocolName:

    def test_protocol_name(self):
        session = PacketSession()
        assert session.protocol_name() == "session_test"


class TestPacketSessionReceiveOnly:

    def test_ack_is_receive_only(self):
        session = PacketSession()
        assert session.is_receive_only(AckBody.TYPE_ID) is True
        assert session.is_receive_only(0xcc431e5e357bc2e6) is True

    def test_ping_is_not_receive_only(self):
        session = PacketSession()
        assert session.is_receive_only(PingBody.TYPE_ID) is False
        assert session.is_receive_only(0x0ad7bb3ecc473399) is False

    def test_data_is_not_receive_only(self):
        session = PacketSession()
        assert session.is_receive_only(DataBody.TYPE_ID) is False
        assert session.is_receive_only(0x29d16b9e73f85835) is False

    def test_unknown_is_not_receive_only(self):
        session = PacketSession()
        assert session.is_receive_only(0xFFFF) is False


class TestPacketSessionSyncPattern:

    def test_sync_pattern_value(self):
        session = PacketSession()
        assert session.sync_pattern() == b"\xde\xad"

    def test_sync_pattern_length(self):
        session = PacketSession()
        assert len(session.sync_pattern()) == 2

    def test_sync_matches_constant(self):
        sync = PacketSession().sync_pattern()
        expected = SessionConstants.SYNC.to_bytes(2, byteorder="big")
        assert sync == expected


class TestPacketSessionFrameHeader:

    def test_min_frame_header_size(self):
        session = PacketSession()
        assert session.min_frame_header_size() == 7


class TestPacketSessionReset:

    def test_reset_clears_sequence_counter(self):
        session = PacketSession()
        session._seq = 42
        session.reset()
        assert session._seq == 0

    def test_reset_after_multiple_increments(self):
        session = PacketSession()
        session._seq = 100
        session.reset()
        assert session._seq == 0
        session._seq = 5
        session.reset()
        assert session._seq == 0


class TestPacketSessionFormatMessage:

    def test_format_ping(self):
        session = PacketSession()
        ping = PingBody()
        ping.timestamp = 123
        result = session.format_message(PingBody.TYPE_ID, ping)
        assert "123" in result
        assert "PingBody" in result

    def test_format_data(self):
        session = PacketSession()
        data_body = DataBody()
        data_body.channel = 5
        result = session.format_message(DataBody.TYPE_ID, data_body)
        assert "5" in result
        assert "DataBody" in result

    def test_format_ack(self):
        session = PacketSession()
        ack = AckBody()
        ack.acked_seq = 999
        result = session.format_message(AckBody.TYPE_ID, ack)
        assert "999" in result

    def test_format_unknown_type(self):
        session = PacketSession()
        result = session.format_message(0xDEAD, None)
        assert result == ""


# ---------------------------------------------------------------------------
# FrameSession (choice_protocol)
# ---------------------------------------------------------------------------
class TestFrameSessionCreation:

    def test_can_instantiate(self):
        session = FrameSession()
        assert session is not None


class TestFrameSessionLeafTypes:

    def test_leaf_type_ids_returns_two_types(self):
        session = FrameSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 2

    def test_contains_alpha(self):
        session = FrameSession()
        ids = session.leaf_type_ids()
        assert AlphaBody.TYPE_ID in ids
        assert 0x24395aaf5388c853 in ids

    def test_contains_beta(self):
        session = FrameSession()
        ids = session.leaf_type_ids()
        assert BetaBody.TYPE_ID in ids
        assert 0x8b968b89bfd16bff in ids


class TestFrameSessionTypeName:

    def test_type_name_alpha(self):
        session = FrameSession()
        assert session.type_name(0x24395aaf5388c853) == "AlphaBody"

    def test_type_name_beta(self):
        session = FrameSession()
        assert session.type_name(0x8b968b89bfd16bff) == "BetaBody"

    def test_type_name_unknown(self):
        session = FrameSession()
        assert session.type_name(0) == "unknown"


class TestFrameSessionProtocolName:

    def test_protocol_name(self):
        session = FrameSession()
        assert session.protocol_name() == "choice_test"


class TestFrameSessionReceiveOnly:

    def test_beta_is_receive_only(self):
        session = FrameSession()
        assert session.is_receive_only(BetaBody.TYPE_ID) is True

    def test_alpha_is_not_receive_only(self):
        session = FrameSession()
        assert session.is_receive_only(AlphaBody.TYPE_ID) is False


class TestFrameSessionSyncPattern:

    def test_sync_pattern_value(self):
        session = FrameSession()
        assert session.sync_pattern() == b"\xbe\xef"

    def test_sync_matches_constant(self):
        sync = FrameSession().sync_pattern()
        expected = ChoiceConstants.SYNC.to_bytes(2, byteorder="big")
        assert sync == expected


class TestFrameSessionFrameHeader:

    def test_min_frame_header_size(self):
        session = FrameSession()
        assert session.min_frame_header_size() == 5


class TestFrameSessionReset:

    def test_reset_clears_seq(self):
        session = FrameSession()
        session._seq = 10
        session.reset()
        assert session._seq == 0


# ---------------------------------------------------------------------------
# Protocol Descriptors
# ---------------------------------------------------------------------------
class TestProtocolDescriptor:

    def test_session_protocol_name(self):
        assert ProtocolDescriptor.NAME == "session_test"

    def test_session_protocol_version(self):
        assert ProtocolDescriptor.VERSION == "2.0"

    def test_session_protocol_types_count(self):
        assert len(ProtocolDescriptor.TYPES) == 3

    def test_find_by_id_ping(self):
        info = ProtocolDescriptor.find_by_id(0x0ad7bb3ecc473399)
        assert info is not None
        assert info.type_name == "PingBody"

    def test_find_by_id_unknown(self):
        info = ProtocolDescriptor.find_by_id(0xDEAD)
        assert info is None

    def test_find_by_name_ping(self):
        info = ProtocolDescriptor.find_by_name("PingBody")
        assert info is not None
        assert info.type_id == 0x0ad7bb3ecc473399

    def test_find_by_name_unknown(self):
        info = ProtocolDescriptor.find_by_name("NonExistent")
        assert info is None


class TestChoiceProtocolDescriptor:

    def test_name(self):
        assert ChoiceProtocolDescriptor.NAME == "choice_test"

    def test_types_count(self):
        assert len(ChoiceProtocolDescriptor.TYPES) == 2


# ---------------------------------------------------------------------------
# Message body encode/decode (without framing)
# ---------------------------------------------------------------------------
class TestPingBodyCodec:

    def test_roundtrip(self):
        ping = PingBody()
        ping.timestamp = 0x12345678
        data = ping.encode_bytes()
        ping2 = PingBody.decode_bytes(data)
        assert ping2.timestamp == 0x12345678

    def test_type_id(self):
        assert PingBody.TYPE_ID == 0x0ad7bb3ecc473399

    def test_type_name(self):
        assert PingBody.TYPE_NAME == "PingBody"

    def test_id_value(self):
        assert PingBody.ID_VALUE == 1

    def test_default_timestamp(self):
        ping = PingBody()
        assert ping.timestamp == 0


class TestDataBodyCodec:

    def test_roundtrip(self):
        db = DataBody()
        db.channel = 5
        db.payload_a = 0xAABBCCDD
        db.payload_b = 0x11223344
        data = db.encode_bytes()
        db2 = DataBody.decode_bytes(data)
        assert db2.channel == 5
        assert db2.payload_a == 0xAABBCCDD
        assert db2.payload_b == 0x11223344

    def test_type_id(self):
        assert DataBody.TYPE_ID == 0x29d16b9e73f85835

    def test_type_name(self):
        assert DataBody.TYPE_NAME == "DataBody"

    def test_id_value(self):
        assert DataBody.ID_VALUE == 2


class TestAckBodyCodec:

    def test_roundtrip(self):
        ack = AckBody()
        ack.acked_seq = 0x1234
        data = ack.encode_bytes()
        ack2 = AckBody.decode_bytes(data)
        assert ack2.acked_seq == 0x1234

    def test_type_id(self):
        assert AckBody.TYPE_ID == 0xcc431e5e357bc2e6

    def test_type_name(self):
        assert AckBody.TYPE_NAME == "AckBody"

    def test_id_value(self):
        assert AckBody.ID_VALUE == 3

    def test_max_seq(self):
        ack = AckBody()
        ack.acked_seq = 0xFFFF
        data = ack.encode_bytes()
        ack2 = AckBody.decode_bytes(data)
        assert ack2.acked_seq == 0xFFFF


class TestAlphaBodyCodec:

    def test_roundtrip(self):
        alpha = AlphaBody()
        alpha.x = 100
        alpha.y = 200
        data = alpha.encode_bytes()
        alpha2 = AlphaBody.decode_bytes(data)
        assert alpha2.x == 100
        assert alpha2.y == 200

    def test_type_id(self):
        assert AlphaBody.TYPE_ID == 0x24395aaf5388c853

    def test_max_values(self):
        alpha = AlphaBody()
        alpha.x = 0xFFFF
        alpha.y = 0xFFFF
        data = alpha.encode_bytes()
        alpha2 = AlphaBody.decode_bytes(data)
        assert alpha2.x == 0xFFFF
        assert alpha2.y == 0xFFFF


class TestBetaBodyCodec:

    def test_roundtrip(self):
        beta = BetaBody()
        beta.payload_size = 42
        beta.tag = 0xDEADBEEF
        data = beta.encode_bytes()
        beta2 = BetaBody.decode_bytes(data)
        assert beta2.payload_size == 42
        assert beta2.tag == 0xDEADBEEF

    def test_type_id(self):
        assert BetaBody.TYPE_ID == 0x8b968b89bfd16bff


# ---------------------------------------------------------------------------
# Session Constants
# ---------------------------------------------------------------------------
class TestSessionConstants:

    def test_session_sync(self):
        assert SessionConstants.SYNC == 0xDEAD

    def test_choice_sync(self):
        assert ChoiceConstants.SYNC == 0xBEEF
