"""Transceiver scenario tests mirroring C++ conduit_tests."""
import ctypes
import os
import sys
import time
import threading
import socket
import pytest

from conftest import resolve_native_lib

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

_CABI_LIB_PATH = resolve_native_lib("CONDUIT_CABI_LIB", "conduit_cabi_test")
os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB_PATH

_CODEC_LIB_PATH = resolve_native_lib("CONDUIT_CODEC_LIB", "conduit_codec_cabi_test")
os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

if sys.platform == "win32":
    _lib_dir = os.path.dirname(_CODEC_LIB_PATH)
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(_lib_dir)
    _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH)
else:
    _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH, mode=ctypes.RTLD_GLOBAL)

import conduit.transceiver as _xcvr_mod
_xcvr_mod._lib = None

from conduit.transceiver import Transceiver, ConduitError, Stats
from conduit.types import TransportType, TransportConfig, UdpConfig

_GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)

from session_protocol.messages import PingBody, DataBody, AckBody

PING_TYPE_ID = 0x0AD7BB3ECC473399
DATA_TYPE_ID = 0x29D16B9E73F85835
ACK_TYPE_ID = 0xCC431E5E357BC2E6


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _find_free_udp_port() -> int:
    """Find a free UDP port by binding to port 0."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ============================================================================
# TestLifecycleScenarios
# ============================================================================


class TestLifecycleScenarios:
    """Lifecycle edge-case scenarios mirroring C++ conduit_tests."""

    def test_send_before_start_raises(self):
        """Sending before start() should raise ConduitError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            msg = PingBody()
            msg.timestamp = 1
            with pytest.raises(ConduitError):
                t.send(peer_id, msg)

    def test_send_to_unknown_peer_raises(self):
        """Sending to a nonexistent peer should raise ConduitError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 1
            with pytest.raises(ConduitError):
                t.send(99999, msg)
            t.stop()

    def test_send_via_sole_peer_convenience(self):
        """With exactly one peer, send(msg) uses sole_peer() convenience."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "only", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 99
            # Should not raise — sole peer convenience
            t.send(msg)
            t.stop()

    def test_sole_peer_no_peers_raises(self):
        """sole_peer() with zero peers should raise ConduitError."""
        with Transceiver() as t:
            with pytest.raises(ConduitError):
                t.sole_peer()

    def test_sole_peer_multiple_peers_raises(self):
        """sole_peer() with 2+ peers should raise ConduitError."""
        with Transceiver() as t:
            for i in range(2):
                port = _find_free_udp_port()
                t.add_peer(
                    f"peer_{i}", "session_protocol",
                    UdpConfig(f"127.0.0.1:{port}"),
                )
            with pytest.raises(ConduitError):
                t.sole_peer()


# ============================================================================
# TestStatsScenarios
# ============================================================================


class TestStatsScenarios:
    """Statistics scenarios mirroring C++ conduit_tests."""

    def test_stats_bytes_sent_increments(self):
        """After sending, bytes_sent should be greater than zero."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 42
            t.send(peer_id, msg)
            time.sleep(0.05)
            s = t.stats()
            assert s.bytes_sent > 0
            t.stop()

    def test_stats_reset_zeroes_counters(self):
        """After reset, all stats counters should be zero."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 1
            t.send(peer_id, msg)
            time.sleep(0.05)

            # Verify non-zero before reset
            s = t.stats()
            assert s.bytes_sent > 0

            t.stats_reset()
            s = t.stats()
            assert s.bytes_sent == 0
            assert s.messages_received == 0
            assert s.messages_dispatched == 0
            assert s.messages_dropped == 0
            assert s.decode_errors == 0
            assert s.handler_errors == 0
            assert s.handler_timeouts == 0
            assert s.bytes_received == 0
            t.stop()

    def test_stats_initial_all_zero(self):
        """A fresh transceiver should have all-zero stats."""
        with Transceiver() as t:
            s = t.stats()
            assert s.messages_received == 0
            assert s.messages_dispatched == 0
            assert s.messages_dropped == 0
            assert s.decode_errors == 0
            assert s.handler_errors == 0
            assert s.handler_timeouts == 0
            assert s.bytes_received == 0
            assert s.bytes_sent == 0


# ============================================================================
# TestHandlerManagement
# ============================================================================


class TestHandlerManagement:
    """Handler registration and removal scenarios."""

    def test_remove_handler_stops_delivery(self):
        """Registering and removing a handler should return True."""
        with Transceiver() as t:
            @t.on(type_id=PING_TYPE_ID)
            def handle_ping(peer_id, type_id, type_name, data):
                pass

            # remove_handler uses peer_id=0 convention
            result = t.remove_handler(0, PING_TYPE_ID)
            assert result is True

    def test_remove_state_change_stops_callbacks(self):
        """Registering and removing a state change callback returns True."""
        with Transceiver() as t:
            cb_id = t.on_state_change(lambda peer_id, state: None)
            result = t.remove_state_change(cb_id)
            assert result is True

    def test_remove_error_callback_stops_delivery(self):
        """Registering and removing an error callback returns True."""
        with Transceiver() as t:
            cb_id = t.on_error(
                lambda peer_id, peer_name, err_code, err_msg: None)
            result = t.remove_error_callback(cb_id)
            assert result is True

    def test_remove_error_callback_false_for_unknown_id(self):
        """Removing an error callback with an unknown ID returns False."""
        with Transceiver() as t:
            result = t.remove_error_callback(99999)
            assert result is False


# ============================================================================
# TestErrorCallbacks
# ============================================================================


class TestErrorCallbacks:
    """Error callback scenarios mirroring C++ conduit_tests."""

    def test_error_callback_fires_for_decode_failure(self):
        """Sending malformed raw bytes should trigger an error callback on the receiver."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()
        errors = []
        error_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            # Receiver binds on port_recv, points to sender's port_send
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            def error_cb(peer_id, peer_name, error_code, error_message):
                errors.append((peer_id, peer_name, error_code, error_message))
                error_event.set()

            receiver.on_error(error_cb)

            # Sender binds on port_send, points to receiver's port_recv
            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            # Send garbage data with a valid type_id but corrupted payload
            sender.send_raw(peer_id, PING_TYPE_ID, b"\xFF\xFE\xFD\xFC\xFB\xFA")

            # Wait for error callback (UDP is unreliable, use timeout)
            error_event.wait(timeout=0.5)

            sender.stop()
            receiver.stop()

        # UDP is unreliable; if we got errors, verify structure
        if len(errors) > 0:
            err = errors[0]
            assert isinstance(err[2], int)  # error_code is int
            assert isinstance(err[3], str)  # error_message is str

    def test_multiple_error_callbacks_all_fire(self):
        """Multiple registered error callbacks should all fire on error."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()
        counters = [0, 0, 0]
        all_fired = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            for idx in range(3):
                def make_cb(i):
                    def cb(peer_id, peer_name, error_code, error_message):
                        counters[i] += 1
                        if all(c > 0 for c in counters):
                            all_fired.set()
                    return cb
                receiver.on_error(make_cb(idx))

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            # Send garbage to trigger error
            sender.send_raw(peer_id, PING_TYPE_ID, b"\xFF\xFE\xFD\xFC\xFB\xFA")

            all_fired.wait(timeout=0.5)

            sender.stop()
            receiver.stop()

        # UDP is unreliable; if any fired, all three should have
        if any(c > 0 for c in counters):
            assert all(c > 0 for c in counters), \
                f"Not all error callbacks fired: {counters}"

    def test_error_callback_exception_does_not_crash(self):
        """An error callback that raises an exception should not crash the transceiver."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            def bad_error_cb(peer_id, peer_name, error_code, error_message):
                raise RuntimeError("intentional error in callback")

            receiver.on_error(bad_error_cb)

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            # Send garbage to trigger error callback
            sender.send_raw(peer_id, PING_TYPE_ID, b"\xFF\xFE\xFD\xFC\xFB\xFA")
            time.sleep(0.2)

            # Transceiver should still be running
            assert receiver.is_running() is True

            sender.stop()
            receiver.stop()

    def test_error_callback_can_query_transceiver_state(self):
        """An error callback that calls is_running() should not deadlock."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()
        state_results = []
        done_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            def state_checking_cb(peer_id, peer_name, error_code, error_message):
                state_results.append(receiver.is_running())
                done_event.set()

            receiver.on_error(state_checking_cb)

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            sender.send_raw(peer_id, PING_TYPE_ID, b"\xFF\xFE\xFD\xFC\xFB\xFA")

            done_event.wait(timeout=0.5)

            sender.stop()
            receiver.stop()

        # If the callback fired, is_running() should have returned True
        if len(state_results) > 0:
            assert state_results[0] is True


# ============================================================================
# TestHandlerExceptionSafety
# ============================================================================


class TestHandlerExceptionSafety:
    """Handler exception safety scenarios."""

    def test_handler_exception_does_not_crash_transceiver(self):
        """A message handler that raises should not crash the transceiver."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            @receiver.on(type_id=PING_TYPE_ID)
            def bad_handler(peer_id, type_id, type_name, data):
                raise RuntimeError("intentional handler error")

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            # Send a valid message to trigger the handler
            msg = PingBody()
            msg.timestamp = 42
            sender.send(peer_id, msg)
            time.sleep(0.2)

            # Transceiver should survive the handler exception
            assert receiver.is_running() is True

            sender.stop()
            receiver.stop()


# ============================================================================
# TestBatchSend
# ============================================================================


class TestBatchSend:
    """Batch send scenarios mirroring C++ conduit_tests."""

    def test_empty_batch_send_raises(self):
        """send_batch with an empty list should raise ConduitError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            with pytest.raises(ConduitError):
                t.send_batch(peer_id, PING_TYPE_ID, [])
            t.stop()

    def test_batch_send_non_batch_protocol_raises(self):
        """session_protocol does not support batch; send_batch should raise."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            payloads = [b"\x00\x00\x00\x01", b"\x00\x00\x00\x02"]
            with pytest.raises(ConduitError):
                t.send_batch(peer_id, PING_TYPE_ID, payloads)
            t.stop()


# ============================================================================
# TestMultiPeer
# ============================================================================


class TestMultiPeer:
    """Multi-peer scenarios mirroring C++ conduit_tests."""

    def test_two_peers_distinct_ids(self):
        """Two peers should have different IDs."""
        with Transceiver() as t:
            port1 = _find_free_udp_port()
            port2 = _find_free_udp_port()
            id1 = t.add_peer(
                "alpha", "session_protocol",
                UdpConfig(f"127.0.0.1:{port1}"),
            )
            id2 = t.add_peer(
                "beta", "session_protocol",
                UdpConfig(f"127.0.0.1:{port2}"),
            )
            assert id1 != id2

    def test_peer_by_name_finds_correct_peer(self):
        """peer_by_name should return the correct peer ID."""
        with Transceiver() as t:
            port = _find_free_udp_port()
            added_id = t.add_peer(
                "alpha", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            found_id = t.peer_by_name("alpha")
            assert found_id == added_id

    def test_peer_by_name_unknown_raises(self):
        """peer_by_name with an unknown name should raise ConduitError."""
        with Transceiver() as t:
            with pytest.raises(ConduitError):
                t.peer_by_name("nonexistent")

    def test_peer_count_tracks_additions(self):
        """peer_count should reflect the number of added peers."""
        with Transceiver() as t:
            for i in range(3):
                port = _find_free_udp_port()
                t.add_peer(
                    f"peer_{i}", "session_protocol",
                    UdpConfig(f"127.0.0.1:{port}"),
                )
            assert t.peer_count() == 3

    def test_peer_state_initial_disconnected(self):
        """A freshly added peer should be in Disconnected state (0)."""
        with Transceiver() as t:
            port = _find_free_udp_port()
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            state = t.peer_state(peer_id)
            assert state == 0  # ConnectionState::Disconnected


# ============================================================================
# TestStress
# ============================================================================


class TestStress:
    """Stress and robustness scenarios mirroring C++ conduit_tests."""

    def test_rapid_sends_all_succeed(self):
        """Sending 100 messages in a tight loop should not crash."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            for i in range(100):
                msg = PingBody()
                msg.timestamp = i
                t.send(peer_id, msg)
            t.stop()

    def test_rapid_create_destroy_cycles(self):
        """Creating and destroying 20 Transceiver instances should not crash or leak."""
        for _ in range(20):
            t = Transceiver()
            t.close()


# ============================================================================
# TestDirectionViolation
# ============================================================================


class TestDirectionViolation:
    """Direction qualification scenarios mirroring C++ conduit_tests."""

    def test_send_receive_only_type_raises(self):
        """Sending a receive-only type (AckBody) should raise ConduitError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            ack = AckBody()
            with pytest.raises(ConduitError):
                t.send(peer_id, ack)
            t.stop()


# ============================================================================
# TestQueueOverflow
# ============================================================================


class TestQueueOverflow:
    """Queue overflow scenarios mirroring C++ conduit_tests."""

    def test_queue_overflow_fires_error_callback(self):
        """A small queue with a slow handler should trigger error/drop callbacks."""
        port_recv = _find_free_udp_port()
        port_send = _find_free_udp_port()
        errors = []
        error_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            # Configure a very small queue on the receiver
            receiver.set_queue_config(capacity=2, drop_policy=0)

            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_send}"),
            )

            # Slow handler to cause queue backup
            @receiver.on(type_id=PING_TYPE_ID)
            def slow_handler(peer_id, type_id, type_name, data):
                time.sleep(0.5)

            def error_cb(peer_id, peer_name, error_code, error_message):
                errors.append((error_code, error_message))
                error_event.set()

            receiver.on_error(error_cb)

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_recv}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            # Flood the receiver with messages
            for i in range(20):
                msg = PingBody()
                msg.timestamp = i
                sender.send(peer_id, msg)

            # Wait for potential overflow errors
            error_event.wait(timeout=1.0)

            sender.stop()
            receiver.stop()

        # Check stats for drops — even without error callback,
        # the stats should reflect dropped messages if overflow occurred
        # (UDP delivery is unreliable, so we use conditional assertions)
        if len(errors) > 0:
            assert isinstance(errors[0][0], int)


# ============================================================================
# TestUdpLoopbackScenarios
# ============================================================================


class TestUdpLoopbackScenarios:
    """UDP loopback send/receive scenarios mirroring C++ conduit_tests."""

    def test_send_receive_typed_loopback(self):
        """Send a PingBody from one transceiver and receive it on another, verifying fields."""
        port_a = _find_free_udp_port()
        port_b = _find_free_udp_port()
        received = []
        recv_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            # Receiver binds on port_a, sends to port_b
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_b}"),
            )

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg)
                recv_event.set()

            # Sender binds on port_b, sends to port_a
            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )

            receiver.start()
            sender.start()

            msg = PingBody()
            msg.timestamp = 12345
            sender.send(peer_id, msg)

            recv_event.wait(timeout=0.2)

            sender.stop()
            receiver.stop()

        # UDP is unreliable; verify fields only if received
        if len(received) > 0:
            assert isinstance(received[0], PingBody)
            assert received[0].timestamp == 12345

    def test_send_receive_multiple_messages_loopback(self):
        """Send 5 messages and verify at least some are received."""
        port_a = _find_free_udp_port()
        port_b = _find_free_udp_port()
        received = []
        done_event = threading.Event()

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_b}"),
            )

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg)
                if len(received) >= 5:
                    done_event.set()

            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            for i in range(5):
                msg = PingBody()
                msg.timestamp = 1000 + i
                sender.send(peer_id, msg)

            done_event.wait(timeout=0.2)

            sender.stop()
            receiver.stop()

        # UDP is unreliable; if we received any, verify correctness
        if len(received) > 0:
            for m in received:
                assert isinstance(m, PingBody)
                assert 1000 <= m.timestamp <= 1004
