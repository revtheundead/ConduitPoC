"""Comprehensive tests for the Conduit codec C ABI Python bindings.

Tests the CodecSession and CodecFramer classes from conduit.codec_binding,
which wrap the conduit_codec_cabi shared library.
"""

import os
import sys
import struct
import pytest

from conftest import resolve_native_lib

# ---------------------------------------------------------------------------
# Environment setup: point to the test CABI library before importing bindings
# ---------------------------------------------------------------------------

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

# Path to the test codec CABI shared library (platform-aware)
_CODEC_LIB_PATH = resolve_native_lib("CONDUIT_CODEC_LIB", "conduit_codec_cabi_test")
os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

# Ensure Python bindings are importable
_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

# Ensure generated protocol modules are importable
_GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)

# Force the codec_binding module to reload with the new env var (in case
# the module was already imported with a stale _lib singleton).
import conduit.codec_binding as _codec_mod
_codec_mod._lib = None

from conduit.codec_binding import (
    CodecSession,
    CodecFramer,
    ConduitCodecError,
    DecodedMessage,
)

# Import the generated Python session_protocol module for building frames
from session_protocol.messages import PingBody, DataBody, AckBody, Packet
from session_protocol.constants import Constants as SessionConstants

# ---------------------------------------------------------------------------
# Constants (must match the C++ generated code)
# ---------------------------------------------------------------------------

PING_TYPE_ID = 0x0AD7BB3ECC473399
DATA_TYPE_ID = 0x29D16B9E73F85835
ACK_TYPE_ID = 0xCC431E5E357BC2E6

SYNC_BYTES = struct.pack(">H", 0xDEAD)  # 2-byte sync pattern


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_ping_frame(timestamp: int = 1000, seq: int = 0) -> bytes:
    """Build a valid Packet frame containing a PingBody using the generated Python codec."""
    ping = PingBody()
    ping.timestamp = timestamp
    frame = Packet.wrap(ping)
    frame.seq = seq
    return frame.encode_bytes()


def _make_data_frame(channel: int = 5, payload_a: int = 100,
                     payload_b: int = 200, seq: int = 0) -> bytes:
    """Build a valid Packet frame containing a DataBody."""
    data = DataBody()
    data.channel = channel
    data.payload_a = payload_a
    data.payload_b = payload_b
    frame = Packet.wrap(data)
    frame.seq = seq
    return frame.encode_bytes()


def _make_ack_frame(acked_seq: int = 42, seq: int = 0) -> bytes:
    """Build a valid Packet frame containing an AckBody."""
    ack = AckBody()
    ack.acked_seq = acked_seq
    frame = Packet.wrap(ack)
    frame.seq = seq
    return frame.encode_bytes()


# ============================================================================
# Fixtures
# ============================================================================


@pytest.fixture
def codec_lib():
    """Return the low-level ctypes CDLL for direct version checks."""
    return _codec_mod._get_lib()


@pytest.fixture
def session():
    """Create a CodecSession for session_protocol, yield it, then close."""
    s = CodecSession("session_protocol")
    yield s
    s.close()


@pytest.fixture
def framer(session):
    """Create a CodecFramer attached to the session_protocol session."""
    f = CodecFramer(session)
    yield f
    f.close()


# ============================================================================
# Version
# ============================================================================


class TestVersion:
    def test_conduit_codec_version_returns_string(self, codec_lib):
        """conduit_codec_version() must return a non-empty version string."""
        version = codec_lib.conduit_codec_version()
        assert version is not None
        decoded = version.decode("utf-8")
        assert len(decoded) > 0
        # Basic semver-like check: contains at least one dot
        assert "." in decoded, f"Expected semver-like version, got: {decoded}"

    def test_version_is_stable_across_calls(self, codec_lib):
        v1 = codec_lib.conduit_codec_version().decode("utf-8")
        v2 = codec_lib.conduit_codec_version().decode("utf-8")
        assert v1 == v2


# ============================================================================
# Session lifecycle
# ============================================================================


class TestSessionLifecycle:
    def test_create_session_protocol(self):
        """Creating a session with a known name must succeed."""
        s = CodecSession("session_protocol")
        assert s._handle is not None
        s.close()

    def test_create_choice_protocol(self):
        """Creating a session with choice_protocol must succeed."""
        s = CodecSession("choice_protocol")
        assert s._handle is not None
        s.close()

    def test_create_sentry_link(self):
        """Creating a session with sentry_link must succeed."""
        s = CodecSession("sentry_link")
        assert s._handle is not None
        s.close()

    def test_create_direction_qualified(self):
        """Creating a session with direction_qualified must succeed."""
        s = CodecSession("direction_qualified")
        assert s._handle is not None
        s.close()

    def test_create_unknown_session_raises(self):
        """Creating a session with an unregistered name must raise ConduitCodecError."""
        with pytest.raises(ConduitCodecError) as exc_info:
            CodecSession("nonexistent_session_xyz")
        assert exc_info.value.code == -4  # CONDUIT_ERR_UNKNOWN_SESSION

    def test_destroy_is_idempotent(self):
        """Calling close() multiple times must not crash."""
        s = CodecSession("session_protocol")
        s.close()
        s.close()  # second close should be a no-op

    def test_context_manager(self):
        """CodecSession works as a context manager."""
        with CodecSession("session_protocol") as s:
            assert s._handle is not None
        # After exiting the context, the handle should be cleared
        assert s._handle is None

    def test_operations_after_close_raise(self):
        """Using decode_frame after close must raise."""
        s = CodecSession("session_protocol")
        s.close()
        with pytest.raises(ConduitCodecError):
            s.decode_frame(b"\x00" * 16)

    def test_encode_after_close_raises(self):
        """Using encode_message after close must raise."""
        s = CodecSession("session_protocol")
        s.close()
        with pytest.raises(ConduitCodecError):
            s.encode_message(PING_TYPE_ID, b"\x00" * 4)


# ============================================================================
# Introspection
# ============================================================================


class TestIntrospection:
    def test_leaf_type_ids_returns_three_types(self, session):
        """session_protocol has 3 leaf types: PingBody, DataBody, AckBody."""
        ids = session.leaf_type_ids()
        assert len(ids) == 3
        assert PING_TYPE_ID in ids
        assert DATA_TYPE_ID in ids
        assert ACK_TYPE_ID in ids

    def test_type_name_ping(self, session):
        assert session.type_name(PING_TYPE_ID) == "PingBody"

    def test_type_name_data(self, session):
        assert session.type_name(DATA_TYPE_ID) == "DataBody"

    def test_type_name_ack(self, session):
        assert session.type_name(ACK_TYPE_ID) == "AckBody"

    def test_type_name_unknown_returns_empty(self, session):
        """An unknown type_id must return an empty string."""
        assert session.type_name(0xDEADDEADDEADDEAD) == ""

    def test_protocol_name(self, session):
        assert session.protocol_name() == "session_test"

    def test_is_receive_only_ack_true(self, session):
        """AckBody is marked receive-only."""
        assert session.is_receive_only(ACK_TYPE_ID) is True

    def test_is_receive_only_ping_false(self, session):
        """PingBody is NOT receive-only."""
        assert session.is_receive_only(PING_TYPE_ID) is False

    def test_is_receive_only_data_false(self, session):
        """DataBody is NOT receive-only."""
        assert session.is_receive_only(DATA_TYPE_ID) is False

    def test_is_receive_only_unknown_false(self, session):
        """An unknown type_id should return False for is_receive_only."""
        assert session.is_receive_only(0x0000000000000000) is False

    def test_introspection_after_close_returns_defaults(self):
        """Introspection methods on a closed session return safe defaults."""
        s = CodecSession("session_protocol")
        s.close()
        assert s.leaf_type_ids() == []
        assert s.type_name(PING_TYPE_ID) == ""
        assert s.protocol_name() == ""
        assert s.is_receive_only(PING_TYPE_ID) is False


# ============================================================================
# Decode frame
# ============================================================================


class TestDecodeFrame:
    def test_decode_ping_frame(self, session):
        """Decode a valid PingBody frame and verify the decoded message."""
        timestamp = 12345
        frame_bytes = _make_ping_frame(timestamp=timestamp)

        messages = session.decode_frame(frame_bytes)
        assert len(messages) == 1
        msg = messages[0]
        assert isinstance(msg, DecodedMessage)
        assert msg.type_id == PING_TYPE_ID
        assert msg.type_name == "PingBody"
        # The raw data returned is the entire frame bytes
        assert len(msg.data) > 0

    def test_decode_data_frame(self, session):
        """Decode a valid DataBody frame."""
        frame_bytes = _make_data_frame(channel=7, payload_a=0xAABBCCDD,
                                        payload_b=0x11223344)
        messages = session.decode_frame(frame_bytes)
        assert len(messages) == 1
        msg = messages[0]
        assert msg.type_id == DATA_TYPE_ID
        assert msg.type_name == "DataBody"

    def test_decode_ack_frame(self, session):
        """Decode a valid AckBody frame."""
        frame_bytes = _make_ack_frame(acked_seq=1000)
        messages = session.decode_frame(frame_bytes)
        assert len(messages) == 1
        msg = messages[0]
        assert msg.type_id == ACK_TYPE_ID
        assert msg.type_name == "AckBody"

    def test_decode_invalid_data_raises(self, session):
        """Decoding garbage bytes must raise ConduitCodecError."""
        with pytest.raises(ConduitCodecError) as exc_info:
            session.decode_frame(b"\x00\x01\x02\x03")
        assert exc_info.value.code != 0

    def test_decode_truncated_frame_raises(self, session):
        """Decoding a truncated frame must raise ConduitCodecError."""
        frame_bytes = _make_ping_frame()
        # Truncate the frame to remove payload
        truncated = frame_bytes[:4]
        with pytest.raises(ConduitCodecError):
            session.decode_frame(truncated)

    def test_decode_multiple_frames_sequentially(self, session):
        """Decoding multiple frames in sequence should work independently."""
        for i in range(5):
            frame_bytes = _make_ping_frame(timestamp=i * 100, seq=i)
            messages = session.decode_frame(frame_bytes)
            assert len(messages) == 1
            assert messages[0].type_id == PING_TYPE_ID

    def test_decode_preserves_raw_bytes(self, session):
        """The decoded message's data field should contain the raw frame bytes."""
        frame_bytes = _make_ping_frame(timestamp=9999)
        messages = session.decode_frame(frame_bytes)
        assert len(messages) == 1
        # The raw data should be the full frame bytes
        assert messages[0].data == frame_bytes


# ============================================================================
# Encode message
# ============================================================================


class TestEncodeMessage:
    def test_encode_ping_roundtrip(self, session):
        """Encode a PingBody payload via CABI, then decode the result with Python."""
        ping = PingBody()
        ping.timestamp = 42000
        payload_bytes = ping.encode_bytes()

        wire_bytes = session.encode_message(PING_TYPE_ID, payload_bytes)
        assert len(wire_bytes) > 0

        # The wire bytes should be a complete Packet frame
        decoded_frame = Packet.decode_bytes(wire_bytes)
        assert decoded_frame.sync == SessionConstants.SYNC
        assert decoded_frame.msg_id == PingBody.ID_VALUE
        assert isinstance(decoded_frame.payload, PingBody)
        assert decoded_frame.payload.timestamp == 42000

    def test_encode_data_roundtrip(self, session):
        """Encode a DataBody payload via CABI, then decode with Python."""
        data = DataBody()
        data.channel = 3
        data.payload_a = 0x12345678
        data.payload_b = 0xABCDEF01
        payload_bytes = data.encode_bytes()

        wire_bytes = session.encode_message(DATA_TYPE_ID, payload_bytes)
        assert len(wire_bytes) > 0

        decoded_frame = Packet.decode_bytes(wire_bytes)
        assert decoded_frame.msg_id == DataBody.ID_VALUE
        assert isinstance(decoded_frame.payload, DataBody)
        assert decoded_frame.payload.channel == 3
        assert decoded_frame.payload.payload_a == 0x12345678
        assert decoded_frame.payload.payload_b == 0xABCDEF01

    def test_encode_increments_sequence(self, session):
        """Successive encodes must increment the frame sequence counter."""
        ping = PingBody()
        ping.timestamp = 1

        wire1 = session.encode_message(PING_TYPE_ID, ping.encode_bytes())
        frame1 = Packet.decode_bytes(wire1)

        wire2 = session.encode_message(PING_TYPE_ID, ping.encode_bytes())
        frame2 = Packet.decode_bytes(wire2)

        assert frame2.seq == frame1.seq + 1

    def test_encode_unknown_type_raises(self, session):
        """Encoding with an unknown type_id must raise."""
        with pytest.raises(ConduitCodecError) as exc_info:
            session.encode_message(0xFFFFFFFFFFFFFFFF, b"\x00\x00\x00\x00")
        assert exc_info.value.code != 0

    def test_encode_then_decode_via_cabi(self, session):
        """Full roundtrip: encode via CABI, decode via CABI."""
        ping = PingBody()
        ping.timestamp = 77777
        payload = ping.encode_bytes()

        wire = session.encode_message(PING_TYPE_ID, payload)
        messages = session.decode_frame(wire)
        assert len(messages) == 1
        assert messages[0].type_id == PING_TYPE_ID
        assert messages[0].type_name == "PingBody"

    def test_encode_with_empty_payload(self, session):
        """Encoding with an empty payload for a type that expects fields should
        either produce a valid (zero-valued) frame or raise an error."""
        # PingBody has a u32 timestamp field. An empty payload should cause
        # the C++ decode step (within encode_wrap) to fail.
        with pytest.raises(ConduitCodecError):
            session.encode_message(PING_TYPE_ID, b"")


# ============================================================================
# Session reset
# ============================================================================


class TestSessionReset:
    def test_reset_resets_sequence_counter(self, session):
        """After reset, the sequence counter should restart from 0."""
        ping = PingBody()
        ping.timestamp = 1

        # Encode a few messages to advance the counter
        for _ in range(5):
            session.encode_message(PING_TYPE_ID, ping.encode_bytes())

        session.reset()

        # Now encode again: sequence should be 0
        wire = session.encode_message(PING_TYPE_ID, ping.encode_bytes())
        frame = Packet.decode_bytes(wire)
        assert frame.seq == 0

    def test_reset_is_idempotent(self, session):
        """Calling reset multiple times should not break the session."""
        session.reset()
        session.reset()
        session.reset()

        # Session should still work
        frame_bytes = _make_ping_frame()
        messages = session.decode_frame(frame_bytes)
        assert len(messages) == 1

    def test_reset_on_closed_session_is_noop(self):
        """reset() on a closed session should not crash."""
        s = CodecSession("session_protocol")
        s.close()
        s.reset()  # should not raise


# ============================================================================
# CodecFramer
# ============================================================================


class TestCodecFramer:
    def test_framer_creation(self, session):
        """CodecFramer creation from a valid session must succeed."""
        f = CodecFramer(session)
        assert f._handle is not None
        f.close()

    def test_framer_context_manager(self, session):
        """CodecFramer works as a context manager."""
        with CodecFramer(session) as f:
            assert f._handle is not None
        assert f._handle is None

    def test_framer_close_idempotent(self, session):
        """Calling close() multiple times on a framer must not crash."""
        f = CodecFramer(session)
        f.close()
        f.close()

    def test_feed_complete_frame_extracts(self, framer):
        """Feeding a complete frame should return exactly one frame."""
        frame_bytes = _make_ping_frame(timestamp=5000)
        frames = framer.feed(frame_bytes)
        assert len(frames) == 1
        assert frames[0] == frame_bytes

    def test_feed_partial_then_rest_extracts(self, framer):
        """Feeding a frame in two parts: first returns nothing, second returns the frame."""
        frame_bytes = _make_ping_frame(timestamp=8888)

        # Split at an arbitrary mid-point (after sync but before end)
        split = len(frame_bytes) // 2
        part1 = frame_bytes[:split]
        part2 = frame_bytes[split:]

        frames1 = framer.feed(part1)
        assert len(frames1) == 0, "Partial data should not yield any frames"

        frames2 = framer.feed(part2)
        assert len(frames2) == 1
        assert frames2[0] == frame_bytes

    def test_feed_two_complete_frames(self, framer):
        """Feeding two concatenated frames should return both."""
        frame1 = _make_ping_frame(timestamp=1, seq=0)
        frame2 = _make_data_frame(channel=2, payload_a=3, payload_b=4, seq=1)

        frames = framer.feed(frame1 + frame2)
        assert len(frames) == 2
        assert frames[0] == frame1
        assert frames[1] == frame2

    def test_feed_byte_by_byte(self, framer):
        """Feeding a frame one byte at a time should eventually yield the frame."""
        frame_bytes = _make_ping_frame(timestamp=3333)
        collected = []
        for byte in frame_bytes:
            result = framer.feed(bytes([byte]))
            collected.extend(result)

        assert len(collected) == 1
        assert collected[0] == frame_bytes

    def test_feed_garbage_then_valid_frame(self, framer):
        """The framer should skip garbage bytes and find the sync pattern."""
        garbage = b"\x01\x02\x03\x04\x05"
        frame_bytes = _make_ping_frame(timestamp=7777)

        # Feed garbage followed by a valid frame
        frames = framer.feed(garbage + frame_bytes)
        # The framer should find the sync and extract the frame
        assert len(frames) >= 1
        # The extracted frame should be the valid one
        assert frames[-1] == frame_bytes

    def test_feed_after_close_raises(self, session):
        """Feeding data to a closed framer must raise."""
        f = CodecFramer(session)
        f.close()
        with pytest.raises(ConduitCodecError):
            f.feed(b"\x00" * 16)

    def test_feed_empty_returns_nothing(self, framer):
        """Feeding zero bytes should return an empty list."""
        frames = framer.feed(b"")
        assert frames == []

    def test_framer_multiple_sequential_frames(self, framer):
        """Feed multiple frames sequentially and verify all are extracted."""
        total_frames = []
        for i in range(10):
            frame = _make_ping_frame(timestamp=i * 100, seq=i)
            result = framer.feed(frame)
            total_frames.extend(result)

        assert len(total_frames) == 10

    def test_framer_split_at_sync_boundary(self, framer):
        """Split a frame right after the sync bytes."""
        frame_bytes = _make_ping_frame(timestamp=4444)
        # Sync is 2 bytes
        sync_end = 2
        part1 = frame_bytes[:sync_end]
        part2 = frame_bytes[sync_end:]

        frames1 = framer.feed(part1)
        assert len(frames1) == 0

        frames2 = framer.feed(part2)
        assert len(frames2) == 1
        assert frames2[0] == frame_bytes


# ============================================================================
# Cross-session isolation
# ============================================================================


class TestCrossSessionIsolation:
    def test_two_sessions_independent_sequence_counters(self):
        """Two CodecSession instances should have independent state."""
        s1 = CodecSession("session_protocol")
        s2 = CodecSession("session_protocol")

        ping = PingBody()
        ping.timestamp = 1

        # Encode 3 messages on s1
        for _ in range(3):
            s1.encode_message(PING_TYPE_ID, ping.encode_bytes())

        # Encode 1 message on s2 -- sequence should be 0
        wire = s2.encode_message(PING_TYPE_ID, ping.encode_bytes())
        frame = Packet.decode_bytes(wire)
        assert frame.seq == 0

        s1.close()
        s2.close()

    def test_different_session_types(self):
        """Creating different session types should yield different protocol names."""
        s1 = CodecSession("session_protocol")
        s2 = CodecSession("sentry_link")

        assert s1.protocol_name() != s2.protocol_name()
        assert len(s1.leaf_type_ids()) != len(s2.leaf_type_ids())

        s1.close()
        s2.close()


# ============================================================================
# Edge cases and error paths
# ============================================================================


class TestEdgeCases:
    def test_large_timestamp_encodes_correctly(self, session):
        """A PingBody with a large (max u32) timestamp should roundtrip."""
        ping = PingBody()
        ping.timestamp = 0xFFFFFFFF
        payload = ping.encode_bytes()

        wire = session.encode_message(PING_TYPE_ID, payload)
        decoded = Packet.decode_bytes(wire)
        assert decoded.payload.timestamp == 0xFFFFFFFF

    def test_zero_valued_fields(self, session):
        """Messages with all-zero field values should encode/decode correctly."""
        data = DataBody()
        data.channel = 0
        data.payload_a = 0
        data.payload_b = 0
        payload = data.encode_bytes()

        wire = session.encode_message(DATA_TYPE_ID, payload)
        decoded = Packet.decode_bytes(wire)
        assert decoded.payload.channel == 0
        assert decoded.payload.payload_a == 0
        assert decoded.payload.payload_b == 0

    def test_all_message_types_roundtrip(self, session):
        """Every message type in session_protocol should roundtrip through CABI."""
        # PingBody
        ping = PingBody()
        ping.timestamp = 12345
        wire = session.encode_message(PING_TYPE_ID, ping.encode_bytes())
        msgs = session.decode_frame(wire)
        assert msgs[0].type_id == PING_TYPE_ID

        # DataBody
        data = DataBody()
        data.channel = 1
        data.payload_a = 2
        data.payload_b = 3
        wire = session.encode_message(DATA_TYPE_ID, data.encode_bytes())
        msgs = session.decode_frame(wire)
        assert msgs[0].type_id == DATA_TYPE_ID

        # AckBody (receive-only, but encoding should still work at codec level)
        ack = AckBody()
        ack.acked_seq = 99
        wire = session.encode_message(ACK_TYPE_ID, ack.encode_bytes())
        msgs = session.decode_frame(wire)
        assert msgs[0].type_id == ACK_TYPE_ID
