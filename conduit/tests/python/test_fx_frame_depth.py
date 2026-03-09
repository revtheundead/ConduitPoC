"""Deep FX block and frame tests matching C++ test_fx_edge_cases.cpp and
test_frame_roundtrip.cpp depth. Covers: FX truncation, FX wire format sizes,
frame roundtrip, frame wire layout verification, frame decode errors,
frame constants, and double-encode idempotency.
"""
import pytest


# ---------------------------------------------------------------------------
# FX truncation: partial item data (C++ test_fx_edge_cases.cpp)
# ---------------------------------------------------------------------------

class TestFxTruncation:

    def test_fx_decode_partial_item1(self):
        """FX=1 with partial item1 data should fail."""
        from fx_block.bit_io import BitWriter
        from fx_block import FxMessage

        w = BitWriter()
        w.write_u8(0x00)      # header
        w.write_bits(1, 1)    # FX bit = 1
        w.write_u8(0xAA)      # only 1 byte of item1 (needs 2)
        data = w.to_bytes()

        with pytest.raises(Exception):
            FxMessage.decode_bytes(data)

    def test_fx_decode_single_byte(self):
        """Single byte buffer (just header, no FX bit) should fail."""
        from fx_block import FxMessage
        with pytest.raises(Exception):
            FxMessage.decode_bytes(bytes([0x42]))

    def test_fx_decode_empty(self):
        """Empty buffer should fail."""
        from fx_block import FxMessage
        with pytest.raises(Exception):
            FxMessage.decode_bytes(b'')


# ---------------------------------------------------------------------------
# FX no items roundtrip
# ---------------------------------------------------------------------------

class TestFxNoItems:

    def test_roundtrip_preserves_header(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0xBB

        encoded = msg.encode_bytes()
        decoded = FxMessage.decode_bytes(encoded)
        assert decoded.header == 0xBB
        assert decoded.item1 is None, "item1 should not be present"


# ---------------------------------------------------------------------------
# FX with items roundtrip
# ---------------------------------------------------------------------------

class TestFxWithItems:

    def test_roundtrip_preserves_item1(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0x11
        msg.item1 = 0x1234

        encoded = msg.encode_bytes()
        decoded = FxMessage.decode_bytes(encoded)
        assert decoded.header == 0x11
        assert decoded.item1 == 0x1234


# ---------------------------------------------------------------------------
# FX double-encode idempotency
# ---------------------------------------------------------------------------

class TestFxDoubleEncode:

    def test_double_encode_produces_identical_bytes(self):
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0x42
        msg.item1 = 0x5678

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second, \
            "Encoding the same FX message twice should produce identical bytes"


# ---------------------------------------------------------------------------
# FX wire format sizes
# ---------------------------------------------------------------------------

class TestFxWireFormat:

    def test_no_items_is_2_bytes(self):
        """header(8) + FX=0(1) = 9 bits -> 2 bytes."""
        from fx_block import FxMessage

        msg = FxMessage()
        msg.header = 0x00
        encoded = msg.encode_bytes()
        assert len(encoded) == 2

    def test_with_items_is_larger_than_without(self):
        from fx_block import FxMessage

        no_items = FxMessage()
        no_items.header = 0x00
        no_items_bytes = no_items.encode_bytes()

        with_items = FxMessage()
        with_items.header = 0x00
        with_items.item1 = 0
        with_items_bytes = with_items.encode_bytes()

        assert len(with_items_bytes) > len(no_items_bytes)


# ---------------------------------------------------------------------------
# Frame basic: roundtrip depth tests (C++ test_frame_roundtrip.cpp)
# ---------------------------------------------------------------------------

class TestFrameBasicRoundtrip:

    def test_heartbeat_roundtrip_via_frame(self):
        from frame_basic import Heartbeat, SimpleFrame

        hb = Heartbeat()
        hb.timestamp = 12345

        frame = SimpleFrame.wrap(hb)
        encoded = frame.encode_bytes()

        # Verify wire layout: [msg_type:1][length:2][timestamp:2] = 5 bytes total
        assert len(encoded) == 5
        assert encoded[0] == 1        # msg_type = 1
        assert encoded[1] == 0        # length high byte
        assert encoded[2] == 5        # length low byte = 5
        assert encoded[3] == 0x30     # timestamp high (0x3039)
        assert encoded[4] == 0x39     # timestamp low

        decoded = SimpleFrame.decode_bytes(encoded)
        assert decoded.msg_type == 1
        assert decoded.length == 5
        assert isinstance(decoded.payload, Heartbeat)
        assert decoded.payload.timestamp == 12345

    def test_status_roundtrip_via_frame(self):
        from frame_basic import Status, SimpleFrame

        st = Status()
        st.code = 42
        st.detail = 9999

        frame = SimpleFrame.wrap(st)
        encoded = frame.encode_bytes()

        # [msg_type:1][length:2][code:1][detail:2] = 6 bytes
        assert len(encoded) == 6
        assert encoded[0] == 2  # msg_type = 2 (Status)

        decoded = SimpleFrame.decode_bytes(encoded)
        assert decoded.msg_type == 2
        assert decoded.length == 6
        assert isinstance(decoded.payload, Status)
        assert decoded.payload.code == 42
        assert decoded.payload.detail == 9999

    def test_decode_invalid_message_id_throws(self):
        from frame_basic import SimpleFrame

        # Construct bytes with unknown msg_type=99
        data = bytes([99, 0, 5, 0x30, 0x39])
        with pytest.raises(Exception):
            SimpleFrame.decode_bytes(data)


# ---------------------------------------------------------------------------
# Frame basic: constants (C++ test_frame_roundtrip.cpp)
# ---------------------------------------------------------------------------

class TestFrameBasicConstants:

    def test_type_id_constants_are_distinct(self):
        from frame_basic import Heartbeat, Status

        assert Heartbeat.TYPE_ID != 0
        assert Status.TYPE_ID != 0
        assert Heartbeat.TYPE_ID != Status.TYPE_ID

    def test_id_value_constants(self):
        from frame_basic import Heartbeat, Status

        assert Heartbeat.ID_VALUE == 1
        assert Status.ID_VALUE == 2

    def test_type_name_constants(self):
        from frame_basic import Heartbeat, Status

        assert Heartbeat.TYPE_NAME == "Heartbeat"
        assert Status.TYPE_NAME == "Status"


# ---------------------------------------------------------------------------
# Frame basic: double-encode idempotency
# ---------------------------------------------------------------------------

class TestFrameDoubleEncode:

    def test_frame_double_encode_produces_identical_bytes(self):
        from frame_basic import Heartbeat, SimpleFrame

        hb = Heartbeat()
        hb.timestamp = 42
        frame = SimpleFrame.wrap(hb)

        first = frame.encode_bytes()
        second = frame.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# Bitmap advanced: double-encode and basic roundtrip
# ---------------------------------------------------------------------------

class TestBitmapAdvanced:

    def test_basic_roundtrip(self):
        from bitmap_advanced import BitmapAdvancedMsg

        msg = BitmapAdvancedMsg()
        msg.header = 0xBB

        encoded = msg.encode_bytes()
        decoded = BitmapAdvancedMsg.decode_bytes(encoded)
        assert decoded.header == 0xBB

    def test_double_encode_idempotency(self):
        from bitmap_advanced import BitmapAdvancedMsg

        msg = BitmapAdvancedMsg()
        msg.header = 0x42

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second


# ---------------------------------------------------------------------------
# All-types double-encode idempotency
# ---------------------------------------------------------------------------

class TestAllTypesDoubleEncode:

    def test_all_types_message_double_encode(self):
        from all_types import AllTypesMessage
        from all_types.types import AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags

        msg = AllTypesMessage()
        msg.u8 = 0xAB
        msg.u16 = 0x1234
        msg.u32 = 0xDEADBEEF
        msg.u64 = 0x0102030405060708
        msg.i8 = -42
        msg.i16 = -1000
        msg.i32 = -100000
        msg.f32 = 3.14
        msg.f64 = -1.5
        msg.flag = True
        msg.ascii = AsciiStr("Hello")
        msg.utf8 = Utf8Str("World")
        msg.raw = 0xCAFEBABE00000000
        msg.le16 = 0x5678
        msg.le32 = 0xABCD1234
        msg.temp = ScaledTemp(4000)
        msg.hex = 0xFF00FF00
        msg.color = ColorEnum.GREEN
        msg.status = StatusFlags(0b00000101)

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second, \
            "Encoding AllTypesMessage twice should produce identical bytes"


# ---------------------------------------------------------------------------
# Session: double-encode idempotency
# ---------------------------------------------------------------------------

class TestSessionDoubleEncode:

    def test_ping_body_double_encode(self):
        from session_protocol import PingBody

        ping = PingBody()
        ping.timestamp = 0x12345678

        first = ping.encode_bytes()
        second = ping.encode_bytes()
        assert first == second

    def test_data_body_double_encode(self):
        from session_protocol import DataBody

        db = DataBody()
        db.channel = 5
        db.payload_a = 0xAABBCCDD
        db.payload_b = 0x11223344

        first = db.encode_bytes()
        second = db.encode_bytes()
        assert first == second
