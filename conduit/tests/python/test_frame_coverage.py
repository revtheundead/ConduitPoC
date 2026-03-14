"""Encode/decode roundtrip tests for frame variant generated Python modules.

Covers: frame_collision, frame_count, frame_len_arith, frame_len_offset,
frame_payload_length, frame_payload_length_from, frame_timestamp.
"""
import pytest


# ===========================================================================
# frame_collision: frame field name collides with message field name
# ===========================================================================


class TestFrameCollision:
    """TestFrame has auto='id' field named 'cat'; BadMsg also has field 'cat'.
    Tests that the collision is handled correctly during encode/decode.
    """

    def test_bad_msg_roundtrip(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0xAB

        frame = TestFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TestFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, BadMsg)
        assert decoded.payload.cat == 0xAB

    def test_frame_cat_is_message_id(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0x55

        frame = TestFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # First byte is the frame 'cat' field (auto=id), which should be 1
        assert encoded[0] == 1  # BadMsg ID_VALUE = 1

    def test_bad_msg_zero_cat(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0

        frame = TestFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TestFrame.decode_bytes(encoded)
        assert decoded.payload.cat == 0

    def test_bad_msg_max_cat(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 255

        frame = TestFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TestFrame.decode_bytes(encoded)
        assert decoded.payload.cat == 255

    def test_collision_double_encode(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0x42

        frame = TestFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_collision_encode_decode_idempotent(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0x7F

        frame = TestFrame.wrap(msg)
        data1 = frame.encode_bytes()
        decoded = TestFrame.decode_bytes(data1)
        frame2 = TestFrame.wrap(decoded.payload)
        data2 = frame2.encode_bytes()
        assert data1 == data2

    def test_collision_wire_layout(self):
        from frame_collision import BadMsg, TestFrame

        msg = BadMsg()
        msg.cat = 0xEE

        frame = TestFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [cat/id:1][length:2][payload.cat:1] = 4 bytes
        assert len(encoded) == 4
        assert encoded[0] == 1      # frame cat = message id = 1
        # length is total frame size = 4
        assert (encoded[1] << 8 | encoded[2]) == 4
        assert encoded[3] == 0xEE   # payload cat field

    def test_decode_unknown_id_raises(self):
        from frame_collision import TestFrame

        # Construct bytes with unknown id (cat=99)
        data = bytes([99, 0, 4, 0x00])
        with pytest.raises(Exception):
            TestFrame.decode_bytes(data)


# ===========================================================================
# frame_count: frame with count(payload) auto-field and count="*" payload
# ===========================================================================


class TestFrameCount:
    """CountFrame has auto count(payload) and length fields with multi-message payload."""

    def test_single_item_roundtrip(self):
        from frame_count import DataItem, CountFrame

        item = DataItem()
        item.value = 1000

        frame = CountFrame.wrap(item)
        encoded = frame.encode_bytes()

        decoded = CountFrame.decode_bytes(encoded)
        assert decoded.count == 1
        assert len(decoded.payload) == 1
        assert decoded.payload[0].value == 1000

    def test_multiple_items_roundtrip(self):
        from frame_count import DataItem, CountFrame

        items = []
        for i in range(5):
            item = DataItem()
            item.value = (i + 1) * 100
            items.append(item)

        frame = CountFrame()
        frame.msg_type = 1
        frame.payload = items

        encoded = frame.encode_bytes()

        decoded = CountFrame.decode_bytes(encoded)
        assert decoded.count == 5
        assert len(decoded.payload) == 5
        for i in range(5):
            assert decoded.payload[i].value == (i + 1) * 100

    def test_empty_payload(self):
        from frame_count import CountFrame

        frame = CountFrame()
        frame.msg_type = 1
        frame.payload = []

        encoded = frame.encode_bytes()

        decoded = CountFrame.decode_bytes(encoded)
        assert decoded.count == 0
        assert len(decoded.payload) == 0

    def test_count_field_auto_populated(self):
        from frame_count import DataItem, CountFrame

        items = []
        for _ in range(3):
            item = DataItem()
            item.value = 42
            items.append(item)

        frame = CountFrame()
        frame.msg_type = 1
        frame.payload = items

        encoded = frame.encode_bytes()
        # Wire: [msg-type:1][count:1][length:2][3 * value:2] = 10 bytes
        # count byte should be 3
        assert encoded[1] == 3

    def test_max_value_items(self):
        from frame_count import DataItem, CountFrame

        item = DataItem()
        item.value = 0xFFFF

        frame = CountFrame.wrap(item)
        encoded = frame.encode_bytes()

        decoded = CountFrame.decode_bytes(encoded)
        assert decoded.payload[0].value == 0xFFFF

    def test_count_double_encode(self):
        from frame_count import DataItem, CountFrame

        item = DataItem()
        item.value = 500

        frame = CountFrame.wrap(item)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_count_encode_decode_idempotent(self):
        from frame_count import DataItem, CountFrame

        items = []
        for i in range(3):
            item = DataItem()
            item.value = i * 1000
            items.append(item)

        frame = CountFrame()
        frame.msg_type = 1
        frame.payload = items

        data1 = frame.encode_bytes()
        decoded = CountFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2


# ===========================================================================
# frame_len_arith: frame with length arithmetic (wire value = total * 2)
# ===========================================================================


class TestFrameLenArith:
    """ArithFrame has auto='length * 2' -- the wire length value is multiplied."""

    def test_data_msg_roundtrip(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0x1234

        frame = ArithFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ArithFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, DataMsg)
        assert decoded.payload.value == 0x1234

    def test_length_field_is_doubled(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0x0000

        frame = ArithFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][length:2][value:2] = 5 bytes total
        # auto='length * 2' means wire length = 5 * 2 = 10
        wire_length = (encoded[1] << 8) | encoded[2]
        assert wire_length == 10

    def test_data_msg_zero_value(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0

        frame = ArithFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ArithFrame.decode_bytes(encoded)
        assert decoded.payload.value == 0

    def test_data_msg_max_value(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0xFFFF

        frame = ArithFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ArithFrame.decode_bytes(encoded)
        assert decoded.payload.value == 0xFFFF

    def test_arith_double_encode(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0x5678

        frame = ArithFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_arith_encode_decode_idempotent(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 42

        frame = ArithFrame.wrap(msg)
        data1 = frame.encode_bytes()
        decoded = ArithFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2

    def test_arith_wire_layout(self):
        from frame_len_arith import DataMsg, ArithFrame

        msg = DataMsg()
        msg.value = 0xABCD

        frame = ArithFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][length:2][value:2] = 5 bytes
        assert len(encoded) == 5
        assert encoded[0] == 1  # msg-type = DataMsg ID = 1


# ===========================================================================
# frame_len_offset: frame with length offset (wire value = total - 3)
# ===========================================================================


class TestFrameLenOffset:
    """OffsetFrame has auto='length - 3' -- the wire length is offset from total."""

    def test_ping_roundtrip(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 12345

        frame = OffsetFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = OffsetFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, Ping)
        assert decoded.payload.seq == 12345

    def test_length_field_has_offset(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 0

        frame = OffsetFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][length:2][seq:2] = 5 bytes total
        # auto='length - 3' means wire length = 5 - 3 = 2
        wire_length = (encoded[1] << 8) | encoded[2]
        assert wire_length == 2

    def test_ping_zero_seq(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 0

        frame = OffsetFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = OffsetFrame.decode_bytes(encoded)
        assert decoded.payload.seq == 0

    def test_ping_max_seq(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 0xFFFF

        frame = OffsetFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = OffsetFrame.decode_bytes(encoded)
        assert decoded.payload.seq == 0xFFFF

    def test_offset_double_encode(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 9999

        frame = OffsetFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_offset_encode_decode_idempotent(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 500

        frame = OffsetFrame.wrap(msg)
        data1 = frame.encode_bytes()
        decoded = OffsetFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2

    def test_offset_wire_layout(self):
        from frame_len_offset import Ping, OffsetFrame

        msg = Ping()
        msg.seq = 0x3039  # 12345

        frame = OffsetFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][length:2][seq:2] = 5 bytes
        assert len(encoded) == 5
        assert encoded[0] == 1  # msg-type = Ping ID = 1
        # seq in big-endian
        assert encoded[3] == 0x30
        assert encoded[4] == 0x39


# ===========================================================================
# frame_payload_length: frame with auto='length(payload)' field
# ===========================================================================


class TestFramePayloadLength:
    """PayloadFrame has auto='length(payload)' -- length of just the payload bytes."""

    def test_simple_data_roundtrip(self):
        from frame_payload_length import SimpleData, PayloadFrame

        msg = SimpleData()
        msg.a = 0xAB
        msg.b = 0xCDEF

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = PayloadFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, SimpleData)
        assert decoded.payload.a == 0xAB
        assert decoded.payload.b == 0xCDEF

    def test_large_data_roundtrip(self):
        from frame_payload_length import LargeData, PayloadFrame

        msg = LargeData()
        msg.x = 1000
        msg.y = 2000
        msg.z = 3000

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = PayloadFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, LargeData)
        assert decoded.payload.x == 1000
        assert decoded.payload.y == 2000
        assert decoded.payload.z == 3000

    def test_payload_length_field_simple_data(self):
        from frame_payload_length import SimpleData, PayloadFrame

        msg = SimpleData()
        msg.a = 0
        msg.b = 0

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][payload-len:2][a:1][b:2] = 6 bytes total
        # payload-len should be 3 (a:1 + b:2)
        payload_len = (encoded[1] << 8) | encoded[2]
        assert payload_len == 3

    def test_payload_length_field_large_data(self):
        from frame_payload_length import LargeData, PayloadFrame

        msg = LargeData()
        msg.x = 0
        msg.y = 0
        msg.z = 0

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][payload-len:2][x:2][y:2][z:2] = 9 bytes total
        # payload-len should be 6 (x:2 + y:2 + z:2)
        payload_len = (encoded[1] << 8) | encoded[2]
        assert payload_len == 6

    def test_simple_data_max_values(self):
        from frame_payload_length import SimpleData, PayloadFrame

        msg = SimpleData()
        msg.a = 0xFF
        msg.b = 0xFFFF

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = PayloadFrame.decode_bytes(encoded)
        assert decoded.payload.a == 0xFF
        assert decoded.payload.b == 0xFFFF

    def test_large_data_max_values(self):
        from frame_payload_length import LargeData, PayloadFrame

        msg = LargeData()
        msg.x = 0xFFFF
        msg.y = 0xFFFF
        msg.z = 0xFFFF

        frame = PayloadFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = PayloadFrame.decode_bytes(encoded)
        assert decoded.payload.x == 0xFFFF
        assert decoded.payload.y == 0xFFFF
        assert decoded.payload.z == 0xFFFF

    def test_different_messages_have_different_ids(self):
        from frame_payload_length import SimpleData, LargeData

        assert SimpleData.ID_VALUE == 1
        assert LargeData.ID_VALUE == 2
        assert SimpleData.ID_VALUE != LargeData.ID_VALUE

    def test_payload_length_double_encode(self):
        from frame_payload_length import SimpleData, PayloadFrame

        msg = SimpleData()
        msg.a = 42
        msg.b = 1234

        frame = PayloadFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_payload_length_encode_decode_idempotent(self):
        from frame_payload_length import LargeData, PayloadFrame

        msg = LargeData()
        msg.x = 111
        msg.y = 222
        msg.z = 333

        frame = PayloadFrame.wrap(msg)
        data1 = frame.encode_bytes()
        decoded = PayloadFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2

    def test_decode_unknown_id_raises(self):
        from frame_payload_length import PayloadFrame

        # msg-type=99 is not defined
        data = bytes([99, 0, 3, 0x00, 0x00, 0x00])
        with pytest.raises(Exception):
            PayloadFrame.decode_bytes(data)


# ===========================================================================
# frame_payload_length_from: frame with payload length-from="body-size"
# ===========================================================================


class TestFramePayloadLengthFrom:
    """ExprFrame uses length-from='body-size' to determine payload length."""

    def test_ping_roundtrip(self):
        from frame_payload_length_from import Ping, ExprFrame

        msg = Ping()
        msg.seq = 42

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ExprFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, Ping)
        assert decoded.payload.seq == 42

    def test_data_roundtrip(self):
        from frame_payload_length_from import Data, ExprFrame

        msg = Data()
        msg.x = 10
        msg.y = 20
        msg.z = 30

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ExprFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, Data)
        assert decoded.payload.x == 10
        assert decoded.payload.y == 20
        assert decoded.payload.z == 30

    def test_body_size_field_ping(self):
        from frame_payload_length_from import Ping, ExprFrame

        msg = Ping()
        msg.seq = 0

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][body-size:2][seq:2] = 5 bytes total
        # body-size should be 2 (seq:2)
        body_size = (encoded[1] << 8) | encoded[2]
        assert body_size == 2

    def test_body_size_field_data(self):
        from frame_payload_length_from import Data, ExprFrame

        msg = Data()
        msg.x = 0
        msg.y = 0
        msg.z = 0

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][body-size:2][x:1][y:1][z:1] = 6 bytes total
        # body-size should be 3 (x:1 + y:1 + z:1)
        body_size = (encoded[1] << 8) | encoded[2]
        assert body_size == 3

    def test_ping_max_seq(self):
        from frame_payload_length_from import Ping, ExprFrame

        msg = Ping()
        msg.seq = 0xFFFF

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ExprFrame.decode_bytes(encoded)
        assert decoded.payload.seq == 0xFFFF

    def test_data_max_values(self):
        from frame_payload_length_from import Data, ExprFrame

        msg = Data()
        msg.x = 0xFF
        msg.y = 0xFF
        msg.z = 0xFF

        frame = ExprFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = ExprFrame.decode_bytes(encoded)
        assert decoded.payload.x == 0xFF
        assert decoded.payload.y == 0xFF
        assert decoded.payload.z == 0xFF

    def test_different_messages_have_different_ids(self):
        from frame_payload_length_from import Ping, Data

        assert Ping.ID_VALUE == 1
        assert Data.ID_VALUE == 2
        assert Ping.ID_VALUE != Data.ID_VALUE

    def test_payload_length_from_double_encode(self):
        from frame_payload_length_from import Ping, ExprFrame

        msg = Ping()
        msg.seq = 7777

        frame = ExprFrame.wrap(msg)
        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_payload_length_from_encode_decode_idempotent(self):
        from frame_payload_length_from import Data, ExprFrame

        msg = Data()
        msg.x = 1
        msg.y = 2
        msg.z = 3

        frame = ExprFrame.wrap(msg)
        data1 = frame.encode_bytes()
        decoded = ExprFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2

    def test_decode_unknown_id_raises(self):
        from frame_payload_length_from import ExprFrame

        # msg-type=99 is not defined
        data = bytes([99, 0, 2, 0x00, 0x00])
        with pytest.raises(Exception):
            ExprFrame.decode_bytes(data)


# ===========================================================================
# frame_timestamp: frame with auto='timestamp' field
# ===========================================================================


class TestFrameTimestamp:
    """TsFrame has auto='timestamp' (uint32) alongside auto=id and auto=length."""

    def test_ping_roundtrip(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 12345

        frame = TsFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert isinstance(decoded.payload, Ping)
        assert decoded.payload.seq == 12345

    def test_timestamp_field_exists(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 0

        frame = TsFrame.wrap(msg)
        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][ts:4][length:2][seq:2] = 9 bytes
        assert len(encoded) == 9
        assert encoded[0] == 1  # msg-type = Ping ID = 1

    def test_timestamp_is_readable(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 100

        frame = TsFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        # The ts field should be a uint32 value (auto=timestamp)
        assert isinstance(decoded.ts, int)
        assert 0 <= decoded.ts <= 0xFFFFFFFF

    def test_timestamp_set_explicitly(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 50

        frame = TsFrame.wrap(msg)
        frame.ts = 1000000

        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert decoded.ts == 1000000
        assert decoded.payload.seq == 50

    def test_timestamp_zero(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 1

        frame = TsFrame.wrap(msg)
        frame.ts = 0

        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert decoded.ts == 0
        assert decoded.payload.seq == 1

    def test_timestamp_max(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 1

        frame = TsFrame.wrap(msg)
        frame.ts = 0xFFFFFFFF

        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert decoded.ts == 0xFFFFFFFF
        assert decoded.payload.seq == 1

    def test_ping_zero_seq(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 0

        frame = TsFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert decoded.payload.seq == 0

    def test_ping_max_seq(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 0xFFFF

        frame = TsFrame.wrap(msg)
        encoded = frame.encode_bytes()

        decoded = TsFrame.decode_bytes(encoded)
        assert decoded.payload.seq == 0xFFFF

    def test_timestamp_double_encode(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 42

        frame = TsFrame.wrap(msg)
        frame.ts = 999999

        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second

    def test_timestamp_encode_decode_idempotent(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 8888

        frame = TsFrame.wrap(msg)
        frame.ts = 123456789

        data1 = frame.encode_bytes()
        decoded = TsFrame.decode_bytes(data1)
        data2 = decoded.encode_bytes()
        assert data1 == data2

    def test_timestamp_wire_layout(self):
        from frame_timestamp import Ping, TsFrame

        msg = Ping()
        msg.seq = 0x3039  # 12345

        frame = TsFrame.wrap(msg)
        frame.ts = 0x01020304

        encoded = frame.encode_bytes()

        # Wire: [msg-type:1][ts:4][length:2][seq:2] = 9 bytes
        assert len(encoded) == 9
        assert encoded[0] == 1  # msg-type
        # ts in big-endian
        assert encoded[1] == 0x01
        assert encoded[2] == 0x02
        assert encoded[3] == 0x03
        assert encoded[4] == 0x04
        # seq in big-endian
        assert encoded[7] == 0x30
        assert encoded[8] == 0x39

    def test_decode_unknown_id_raises(self):
        from frame_timestamp import TsFrame

        # msg-type=99 is not defined
        data = bytes([99, 0, 0, 0, 0, 0, 9, 0x00, 0x00])
        with pytest.raises(Exception):
            TsFrame.decode_bytes(data)
