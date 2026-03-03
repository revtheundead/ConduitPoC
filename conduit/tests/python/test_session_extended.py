"""Extended session tests covering direction-qualified messages, sentry-link
auto-increment wrap-around, frame error cases, float special values, and
wire encoding overflow — matching C++ test_generated_session.cpp and
test_frame_roundtrip.cpp / test_wire_encoding_roundtrip.cpp coverage."""
import math
import struct
import pytest

from direction_qualified import (
    FrameSession as DirFrameSession,
    UplinkPayload,
    DownlinkPayload,
    CommonPayload,
    Frame as DirFrame,
)
from direction_qualified.bit_io import BitWriter as DirBitWriter

from sentry_link import (
    FrameSession as SentryFrameSession,
    HeartbeatBody,
    SensorBody,
    ConfigBody,
    AlertBody,
    DeviceStatus,
)

from session_protocol import (
    PacketSession,
    PingBody,
    DataBody,
    AckBody,
    Packet,
)

from all_types import AllTypesMessage

from wire_encodings import WireEncodingMsg
from wire_encodings.bit_io import DecodeError as WireDecodeError, ConduitError as WireConduitError


# ---------------------------------------------------------------------------
# Direction-qualified session tests
# Matches C++ test_generated_session.cpp direction tests
# ---------------------------------------------------------------------------

class TestDirectionSessionCreation:

    def test_can_instantiate(self):
        session = DirFrameSession()
        assert session is not None

    def test_initial_seq_is_zero(self):
        session = DirFrameSession()
        assert session._seq == 0


class TestDirectionLeafTypes:

    def test_leaf_type_ids_returns_three_types(self):
        session = DirFrameSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 3

    def test_contains_uplink(self):
        session = DirFrameSession()
        assert UplinkPayload.TYPE_ID in session.leaf_type_ids()

    def test_contains_downlink(self):
        session = DirFrameSession()
        assert DownlinkPayload.TYPE_ID in session.leaf_type_ids()

    def test_contains_common(self):
        session = DirFrameSession()
        assert CommonPayload.TYPE_ID in session.leaf_type_ids()


class TestDirectionTypeName:

    def test_type_name_uplink(self):
        session = DirFrameSession()
        assert session.type_name(UplinkPayload.TYPE_ID) == "UplinkPayload"

    def test_type_name_downlink(self):
        session = DirFrameSession()
        assert session.type_name(DownlinkPayload.TYPE_ID) == "DownlinkPayload"

    def test_type_name_common(self):
        session = DirFrameSession()
        assert session.type_name(CommonPayload.TYPE_ID) == "CommonPayload"

    def test_type_name_unknown(self):
        session = DirFrameSession()
        assert session.type_name(0xDEAD) == "unknown"


class TestDirectionProtocol:

    def test_protocol_name(self):
        session = DirFrameSession()
        assert session.protocol_name() == "direction_qualified"


class TestDirectionReceiveOnly:

    def test_downlink_is_receive_only(self):
        session = DirFrameSession()
        assert session.is_receive_only(DownlinkPayload.TYPE_ID) is True

    def test_uplink_is_not_receive_only(self):
        session = DirFrameSession()
        assert session.is_receive_only(UplinkPayload.TYPE_ID) is False

    def test_common_is_not_receive_only(self):
        session = DirFrameSession()
        assert session.is_receive_only(CommonPayload.TYPE_ID) is False


class TestDirectionSyncPattern:

    def test_sync_pattern_is_empty(self):
        session = DirFrameSession()
        assert session.sync_pattern() == b''

    def test_min_frame_header_size_is_1(self):
        session = DirFrameSession()
        assert session.min_frame_header_size() == 1


class TestDirectionDecodeSharedDiscriminator:
    """When two message types share the same discriminator value (tag=1 for
    both UplinkPayload and DownlinkPayload), the decoder should prefer the
    receive variant (DownlinkPayload)."""

    def test_decode_produces_receive_variant(self):
        # Build wire: tag=1 (shared ID for uplink/downlink) + uint32 rx-data
        w = DirBitWriter()
        w.write_u8(DownlinkPayload.ID_VALUE)  # tag = 1
        w.write_u32(0xAABBCCDD, True)
        data = w.to_bytes()

        decoded = DirFrame.decode_bytes(data)
        assert isinstance(decoded.payload, DownlinkPayload)

    def test_decode_never_produces_send_variant(self):
        w = DirBitWriter()
        w.write_u8(1)  # shared ID
        w.write_u32(0x12345678, True)
        data = w.to_bytes()

        decoded = DirFrame.decode_bytes(data)
        assert not isinstance(decoded.payload, UplinkPayload), \
            "Decoder should not produce send-only variant for shared discriminator"


class TestDirectionEncodeWrap:

    def test_encode_wrap_uplink_succeeds(self):
        session = DirFrameSession()
        uplink = UplinkPayload()
        uplink.tx_data = 0x1234
        result = session.encode_wrap(UplinkPayload.TYPE_ID, uplink)
        assert result is not None
        assert 'bytes' in result

    def test_encode_wrap_downlink_succeeds(self):
        """encode_wrap should succeed even for receive-only (direction is documentary)."""
        session = DirFrameSession()
        downlink = DownlinkPayload()
        downlink.rx_data = 0xDEADBEEF
        result = session.encode_wrap(DownlinkPayload.TYPE_ID, downlink)
        assert result is not None

    def test_encode_wrap_common_roundtrip(self):
        session = DirFrameSession()
        common = CommonPayload()
        common.common_data = 42
        wrapped = session.encode_wrap(CommonPayload.TYPE_ID, common)
        assert wrapped is not None

        messages = session.decode_frame(wrapped['bytes'])
        assert messages is not None
        assert len(messages) == 1
        assert messages[0]['type_name'] == 'CommonPayload'
        decoded = messages[0]['payload']
        assert decoded.common_data == 42

    def test_encode_wrap_unknown_type_returns_none(self):
        session = DirFrameSession()
        assert session.encode_wrap(0x1111111111111111, object()) is None


class TestDirectionWrapDiscriminator:

    def test_wrap_uplink_sets_correct_tag(self):
        uplink = UplinkPayload()
        uplink.tx_data = 100
        frame = DirFrame.wrap(uplink)
        assert frame.tag == UplinkPayload.ID_VALUE

    def test_wrap_downlink_sets_correct_tag(self):
        downlink = DownlinkPayload()
        downlink.rx_data = 200
        frame = DirFrame.wrap(downlink)
        assert frame.tag == DownlinkPayload.ID_VALUE

    def test_wrap_common_sets_correct_tag(self):
        common = CommonPayload()
        common.common_data = 7
        frame = DirFrame.wrap(common)
        assert frame.tag == CommonPayload.ID_VALUE


class TestDirectionDecodeFrame:

    def test_extracts_receive_variant_from_shared_discriminator(self):
        w = DirBitWriter()
        w.write_u8(DownlinkPayload.ID_VALUE)
        w.write_u32(0xCAFEBABE, True)
        data = w.to_bytes()

        session = DirFrameSession()
        messages = session.decode_frame(data)
        assert messages is not None
        assert len(messages) == 1
        assert messages[0]['type_name'] == 'DownlinkPayload'
        assert isinstance(messages[0]['payload'], DownlinkPayload)


# ---------------------------------------------------------------------------
# Sentry-link session tests (8-bit auto-increment wrap-around)
# Matches C++ "auto-increment 8-bit wrap-around" test
# ---------------------------------------------------------------------------

class TestSentryLinkSessionCreation:

    def test_can_instantiate(self):
        session = SentryFrameSession()
        assert session is not None

    def test_initial_seq_is_zero(self):
        session = SentryFrameSession()
        assert session._seq == 0


class TestSentryLinkLeafTypes:

    def test_leaf_type_ids_returns_four_types(self):
        session = SentryFrameSession()
        ids = session.leaf_type_ids()
        assert len(ids) == 4

    def test_contains_heartbeat(self):
        session = SentryFrameSession()
        assert HeartbeatBody.TYPE_ID in session.leaf_type_ids()

    def test_contains_sensor(self):
        session = SentryFrameSession()
        assert SensorBody.TYPE_ID in session.leaf_type_ids()

    def test_contains_config(self):
        session = SentryFrameSession()
        assert ConfigBody.TYPE_ID in session.leaf_type_ids()

    def test_contains_alert(self):
        session = SentryFrameSession()
        assert AlertBody.TYPE_ID in session.leaf_type_ids()


class TestSentryLinkProtocol:

    def test_protocol_name(self):
        session = SentryFrameSession()
        assert session.protocol_name() == "sentry_link"

    def test_sync_pattern(self):
        session = SentryFrameSession()
        assert session.sync_pattern() == b'\xaa\x55'

    def test_min_frame_header_size(self):
        session = SentryFrameSession()
        assert session.min_frame_header_size() == 6


class TestSentryLinkReceiveOnly:

    def test_heartbeat_is_receive_only(self):
        session = SentryFrameSession()
        assert session.is_receive_only(HeartbeatBody.TYPE_ID) is True

    def test_sensor_is_receive_only(self):
        session = SentryFrameSession()
        assert session.is_receive_only(SensorBody.TYPE_ID) is True

    def test_alert_is_receive_only(self):
        session = SentryFrameSession()
        assert session.is_receive_only(AlertBody.TYPE_ID) is True

    def test_config_is_not_receive_only(self):
        session = SentryFrameSession()
        assert session.is_receive_only(ConfigBody.TYPE_ID) is False


class TestSentryLinkEncodeDecodeRoundtrip:

    def test_heartbeat_roundtrip(self):
        session = SentryFrameSession()
        hb = HeartbeatBody()
        hb.timestamp = 1000
        hb.uptime_hours = 48
        hb.status = DeviceStatus.ONLINE
        hb.cpu_load = 75

        wrapped = session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        assert wrapped is not None

        messages = session.decode_frame(wrapped['bytes'])
        assert messages is not None
        assert len(messages) == 1
        assert messages[0]['type_name'] == 'HeartbeatBody'
        decoded = messages[0]['payload']
        assert decoded.timestamp == 1000
        assert decoded.uptime_hours == 48
        assert decoded.status == DeviceStatus.ONLINE
        assert decoded.cpu_load == 75


class TestSentryLinkAutoIncrement:

    def test_8bit_wrap_around(self):
        """Sequence should wrap at 256 (8-bit counter)."""
        session = SentryFrameSession()
        hb = HeartbeatBody()
        hb.status = DeviceStatus.ONLINE

        # Send 260 messages
        for i in range(260):
            result = session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
            assert result is not None, f"encodeWrap should succeed at iteration {i}"

        # Counter should be 260 (wrapping is applied per-frame via bitmask)
        assert session._seq == 260

        # Verify the sequence byte wraps modulo 256
        session.reset()
        for i in range(256):
            session.encode_wrap(HeartbeatBody.TYPE_ID, hb)

        # Counter is now 256; masked to 8 bits = 0
        wrap_result = session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        frame_bytes = wrap_result['bytes']
        # Frame layout: sync(2) + msg_type(1) + length(2) + sequence(1)
        # Sequence byte is at offset 5
        assert frame_bytes[5] == 0, \
            "Sequence byte should wrap to 0 at 256th message"

    def test_per_session_not_per_type(self):
        """Sequence counter is shared across message types."""
        session = SentryFrameSession()
        hb = HeartbeatBody()
        hb.status = DeviceStatus.ONLINE
        alert = AlertBody()

        session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        assert session._seq == 1

        session.encode_wrap(AlertBody.TYPE_ID, alert)
        assert session._seq == 2, \
            "Sequence counter should be shared across message types"

        session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        assert session._seq == 3


class TestSentryLinkReset:

    def test_reset_clears_sequence(self):
        session = SentryFrameSession()
        hb = HeartbeatBody()
        hb.status = DeviceStatus.ONLINE

        session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        session.encode_wrap(HeartbeatBody.TYPE_ID, hb)
        assert session._seq == 2

        session.reset()
        assert session._seq == 0


# ---------------------------------------------------------------------------
# Frame error cases
# Matches C++ "decode_frame with truncated data" and "unknown ID" tests
# ---------------------------------------------------------------------------

class TestFrameErrorCases:

    def test_packet_decode_truncated_throws(self):
        """Decoding less than minimum header should raise."""
        truncated = bytes([0xDE, 0xAD, 0x00])
        with pytest.raises(Exception):
            Packet.decode_bytes(truncated)

    def test_session_decode_truncated_handles_gracefully(self):
        """Session decode_frame should not crash on truncated input."""
        session = PacketSession()
        truncated = bytes([0xDE, 0xAD, 0x00])
        # Should either return None/empty or raise — but not crash
        try:
            result = session.decode_frame(truncated)
            # If it doesn't throw, result should be valid
            assert result is None or isinstance(result, list)
        except Exception:
            pass  # Raising is acceptable

    def test_id_wire_byte_matches_ping_id_value(self):
        ping = PingBody()
        ping.timestamp = 0
        frame = Packet.wrap(ping)
        frame.seq = 0
        encoded = frame.encode_bytes()
        # msg_id byte is at offset 4 (sync:2 + seq:2 + msgId:1)
        wire_id = encoded[4]
        assert wire_id == PingBody.ID_VALUE, \
            "Wire msg-id byte should match PingBody.ID_VALUE"

    def test_id_wire_byte_matches_data_id_value(self):
        data = DataBody()
        frame = Packet.wrap(data)
        encoded = frame.encode_bytes()
        assert encoded[4] == DataBody.ID_VALUE

    def test_id_wire_byte_matches_ack_id_value(self):
        ack = AckBody()
        frame = Packet.wrap(ack)
        encoded = frame.encode_bytes()
        assert encoded[4] == AckBody.ID_VALUE


# ---------------------------------------------------------------------------
# Float special value roundtrip tests
# Matches C++ test_generated_roundtrip.cpp float NaN/Infinity tests
# ---------------------------------------------------------------------------

def _make_all_types_msg():
    """Create an AllTypesMessage with all type-wrapper fields initialized."""
    from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags
    msg = AllTypesMessage()
    msg.ascii = AsciiStr('')
    msg.utf8 = Utf8Str('')
    msg.temp = ScaledTemp(0)
    msg.color = ColorEnum.RED
    msg.status = StatusFlags(0)
    return msg


class TestFloat32SpecialValues:

    def test_nan_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f32 = float('nan')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert math.isnan(decoded.f32), "Decoded f32 should be NaN"

    def test_positive_infinity_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f32 = float('inf')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert decoded.f32 == float('inf')

    def test_negative_infinity_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f32 = float('-inf')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert decoded.f32 == float('-inf')

    def test_negative_zero_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f32 = -0.0
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        # Check sign bit is preserved
        assert struct.pack('>f', decoded.f32) == struct.pack('>f', -0.0), \
            "Decoded f32 should preserve negative zero"


class TestFloat64SpecialValues:

    def test_nan_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f64 = float('nan')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert math.isnan(decoded.f64), "Decoded f64 should be NaN"

    def test_positive_infinity_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f64 = float('inf')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert decoded.f64 == float('inf')

    def test_negative_infinity_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f64 = float('-inf')
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert decoded.f64 == float('-inf')

    def test_negative_zero_roundtrip(self):
        msg = _make_all_types_msg()
        msg.f64 = -0.0
        encoded = msg.encode_bytes()
        decoded = AllTypesMessage.decode_bytes(encoded)
        assert struct.pack('>d', decoded.f64) == struct.pack('>d', -0.0), \
            "Decoded f64 should preserve negative zero"


# ---------------------------------------------------------------------------
# Wire encoding overflow tests
# Matches C++ test_wire_encoding_roundtrip.cpp
# ---------------------------------------------------------------------------

class TestWireEncodingRoundtrip:

    def test_basic_roundtrip(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 1234
        msg.bcd_hdg = 90
        msg.sm_offset = -100
        msg.cb2_val = -500
        msg.bnr_val = 1000
        msg.inline_bcd = 567
        msg.inline_bnrs = 200

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 1234
        assert decoded.bcd_hdg == 90
        assert decoded.sm_offset == -100
        assert decoded.cb2_val == -500
        assert decoded.bnr_val == 1000
        assert decoded.inline_bcd == 567
        assert decoded.inline_bnrs == 200

    def test_bcd_zero_values(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = 0
        msg.sm_offset = 0
        msg.cb2_val = 0
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = 0

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 0
        assert decoded.bcd_hdg == 0
        assert decoded.sm_offset == 0

    def test_bcd_max_values(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 9999  # Max for 16-bit BCD (4 digits)
        msg.inline_bcd = 999  # Max for 12-bit BCD (3 digits)

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 9999
        assert decoded.inline_bcd == 999

    def test_sign_magnitude_positive_values(self):
        msg = WireEncodingMsg()
        msg.sm_offset = 100
        msg.inline_bnrs = 32767

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.sm_offset == 100
        assert decoded.inline_bnrs == 32767

    def test_negative_bcd_heading_roundtrip(self):
        msg = WireEncodingMsg()
        msg.bcd_hdg = -180

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_hdg == -180


class TestWireEncodingWireFormat:

    def test_bcd_altitude_wire_format(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 9876
        encoded = msg.encode_bytes()
        # First two bytes should be BCD 9876 = 0x98 0x76
        assert encoded[0] == 0x98
        assert encoded[1] == 0x76

    def test_tight_packing_size_14_bytes(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 1234
        msg.bcd_hdg = 90
        msg.sm_offset = 100
        msg.cb2_val = -1
        msg.bnr_val = 1
        msg.inline_bcd = 123
        msg.inline_bnrs = 50

        encoded = msg.encode_bytes()
        # Total bits: 16+13+16+16+16+12+16 = 105 bits = ceil(105/8) = 14 bytes
        assert len(encoded) == 14, "Wire format should be tightly packed to 14 bytes"


class TestWireEncodingOverflow:

    def test_bcd_overflow_16bit(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 10000  # Exceeds 4-digit BCD (0-9999)
        with pytest.raises(Exception):
            msg.encode_bytes()

    def test_bcd_overflow_12bit(self):
        msg = WireEncodingMsg()
        msg.inline_bcd = 1000  # Exceeds 3-digit BCD (0-999)
        with pytest.raises(Exception):
            msg.encode_bytes()

    def test_decode_truncated_buffer_fails(self):
        with pytest.raises(Exception):
            WireEncodingMsg.decode_bytes(bytes([0x12, 0x34]))

    def test_decode_empty_buffer_fails(self):
        with pytest.raises(Exception):
            WireEncodingMsg.decode_bytes(b'')
