"""Deep frame tests matching C++ test_frame_roundtrip.cpp depth.
Covers: frame_config (config field), frame_footer (checksum footer),
frame_direction (direction-qualified messages), frame_array (array payload).
"""
import pytest


# ---------------------------------------------------------------------------
# frame_footer: FooterFrame with checksum footer field
# (C++ "frame_footer: Data roundtrip with footer")
# ---------------------------------------------------------------------------

class TestFrameFooter:

    def test_data_roundtrip_with_footer(self):
        from frame_footer import Data, FooterFrame

        data_msg = Data()
        data_msg.value = 0xABCD

        frame = FooterFrame.wrap(data_msg)
        frame.checksum = 0x42

        encoded = frame.encode_bytes()

        # Wire: [msg_type:1][length:2][value:2][checksum:1] = 6 bytes
        assert len(encoded) == 6
        assert encoded[0] == 1          # msg_type
        assert encoded[5] == 0x42       # checksum (footer)

        decoded = FooterFrame.decode_bytes(encoded)
        assert decoded.msg_type == 1
        assert decoded.length == 6
        assert decoded.checksum == 0x42
        assert isinstance(decoded.payload, Data)
        assert decoded.payload.value == 0xABCD

    def test_ack_roundtrip_with_footer(self):
        from frame_footer import Ack, FooterFrame

        ack = Ack()
        ack.seq = 99

        frame = FooterFrame.wrap(ack)
        frame.checksum = 0xFF

        encoded = frame.encode_bytes()
        # Wire: [msg_type:1][length:2][seq:1][checksum:1] = 5 bytes
        assert len(encoded) == 5

        decoded = FooterFrame.decode_bytes(encoded)
        assert decoded.checksum == 0xFF
        assert isinstance(decoded.payload, Ack)
        assert decoded.payload.seq == 99

    def test_footer_double_encode(self):
        from frame_footer import Data, FooterFrame

        data_msg = Data()
        data_msg.value = 12345
        frame = FooterFrame.wrap(data_msg)
        frame.checksum = 0x42

        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# frame_direction: DirFrame with direction-qualified messages
# (C++ "frame_direction: CommandResponse roundtrip (receive direction)")
# ---------------------------------------------------------------------------

class TestFrameDirection:

    def test_command_response_roundtrip(self):
        from frame_direction import CommandResponse, DirFrame

        msg = CommandResponse()
        msg.status = 1
        msg.detail = 500

        frame = DirFrame.wrap(msg)
        assert frame.msg_type == 1  # ID_VALUE = 1

        encoded = frame.encode_bytes()
        decoded = DirFrame.decode_bytes(encoded)

        # Decode should produce CommandResponse (receive direction)
        assert isinstance(decoded.payload, CommandResponse)
        assert decoded.payload.status == 1
        assert decoded.payload.detail == 500

    def test_heartbeat_roundtrip_bidirectional(self):
        from frame_direction import Heartbeat, DirFrame

        msg = Heartbeat()
        msg.seq = 9999

        frame = DirFrame.wrap(msg)
        assert frame.msg_type == 2

        encoded = frame.encode_bytes()
        decoded = DirFrame.decode_bytes(encoded)

        assert isinstance(decoded.payload, Heartbeat)
        assert decoded.payload.seq == 9999

    def test_message_constants_distinct(self):
        from frame_direction import CommandRequest, CommandResponse, Heartbeat

        # Same ID_VALUE but different TYPE_ID
        assert CommandRequest.ID_VALUE == 1
        assert CommandResponse.ID_VALUE == 1
        assert Heartbeat.ID_VALUE == 2
        assert CommandRequest.TYPE_ID != CommandResponse.TYPE_ID

    def test_direction_double_encode(self):
        from frame_direction import Heartbeat, DirFrame

        msg = Heartbeat()
        msg.seq = 42
        frame = DirFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# frame_array: ArrayFrame with count="*" array payload
# (C++ "frame_array: single record roundtrip")
# ---------------------------------------------------------------------------

class TestFrameArray:

    def test_single_record_roundtrip(self):
        from frame_array import ArrayFrame, Record

        frame = ArrayFrame()
        rec = Record()
        rec.key = 1
        rec.value = 1000
        frame.payload = [rec]
        frame.msg_type = 1

        encoded = frame.encode_bytes()
        # Wire: [msg_type:1][length:2][key:1][value:2] = 6 bytes
        assert len(encoded) == 6

        decoded = ArrayFrame.decode_bytes(encoded)
        assert len(decoded.payload) == 1
        assert decoded.payload[0].key == 1
        assert decoded.payload[0].value == 1000

    def test_multiple_records_roundtrip(self):
        from frame_array import ArrayFrame, Record

        frame = ArrayFrame()
        frame.msg_type = 1
        records = []
        for i in range(5):
            rec = Record()
            rec.key = i
            rec.value = i * 100
            records.append(rec)
        frame.payload = records

        encoded = frame.encode_bytes()
        # Wire: [msg_type:1][length:2] + 5 * [key:1][value:2] = 3 + 15 = 18 bytes
        assert len(encoded) == 18

        decoded = ArrayFrame.decode_bytes(encoded)
        assert len(decoded.payload) == 5
        for i in range(5):
            assert decoded.payload[i].key == i
            assert decoded.payload[i].value == i * 100

    def test_empty_payload_roundtrip(self):
        from frame_array import ArrayFrame

        frame = ArrayFrame()
        frame.msg_type = 1
        frame.payload = []

        encoded = frame.encode_bytes()
        # Wire: [msg_type:1][length:2] = 3 bytes (header only)
        assert len(encoded) == 3

        decoded = ArrayFrame.decode_bytes(encoded)
        assert len(decoded.payload) == 0

    def test_array_double_encode(self):
        from frame_array import ArrayFrame, Record

        frame = ArrayFrame()
        frame.msg_type = 1
        rec = Record()
        rec.key = 7
        rec.value = 777
        frame.payload = [rec]

        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# frame_config: ConfigFrame with auto="config(system-id)"
# (C++ "frame_config: config field set during encode")
# ---------------------------------------------------------------------------

class TestFrameConfig:

    def test_direct_frame_encode_decode(self):
        from frame_config import ConfigFrame, Ping

        ping = Ping()
        ping.seq = 300

        frame = ConfigFrame.wrap(ping)
        frame.system_id = 10

        encoded = frame.encode_bytes()
        # Wire: [system-id:1][msg-type:1][length:2][seq:2] = 6 bytes
        assert len(encoded) == 6
        assert encoded[0] == 10   # system-id from config
        assert encoded[1] == 1    # msg-type = Ping::ID_VALUE

        decoded = ConfigFrame.decode_bytes(encoded)
        assert decoded.system_id == 10
        assert decoded.msg_type == 1
        assert decoded.length == 6
        assert isinstance(decoded.payload, Ping)
        assert decoded.payload.seq == 300

    def test_pong_roundtrip(self):
        from frame_config import ConfigFrame, Pong

        pong = Pong()
        pong.seq = 200

        frame = ConfigFrame.wrap(pong)
        frame.system_id = 42

        encoded = frame.encode_bytes()
        assert encoded[0] == 42   # system-id
        assert encoded[1] == 2    # msg-type = Pong::ID_VALUE

        decoded = ConfigFrame.decode_bytes(encoded)
        assert decoded.system_id == 42
        assert isinstance(decoded.payload, Pong)
        assert decoded.payload.seq == 200

    def test_config_double_encode(self):
        from frame_config import ConfigFrame, Ping

        ping = Ping()
        ping.seq = 100
        frame = ConfigFrame.wrap(ping)
        frame.system_id = 7

        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second
