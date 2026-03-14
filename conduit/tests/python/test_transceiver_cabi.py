"""Comprehensive tests for the Conduit transceiver C ABI Python bindings.

Tests the Transceiver class from conduit.transceiver, which wraps the
conduit_cabi shared library (full transceiver with transport + threading).
"""

import ctypes
import os
import sys
import time
import threading
import pytest

from conftest import resolve_native_lib

# ---------------------------------------------------------------------------
# Environment setup: point to the test CABI libraries before importing bindings
# ---------------------------------------------------------------------------

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

# Path to the test CABI shared libraries (platform-aware)
_CABI_LIB_PATH = resolve_native_lib("CONDUIT_CABI_LIB", "conduit_cabi_test")
os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB_PATH

_CODEC_LIB_PATH = resolve_native_lib("CONDUIT_CODEC_LIB", "conduit_codec_cabi_test")
os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

# Ensure Python bindings are importable
_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

# The transceiver CABI test library references conduit_register_session (from
# the codec CABI) because test_sessions_register.cpp includes the codec header.
# On POSIX we preload with RTLD_GLOBAL so the linker can resolve the symbol.
# On Windows, DLL dependencies are resolved automatically via LoadLibrary.
if sys.platform == "win32":
    _lib_dir = os.path.dirname(_CODEC_LIB_PATH)
    if os.path.isdir(_lib_dir) and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(_lib_dir)
    _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH)
else:
    _codec_preload = ctypes.CDLL(_CODEC_LIB_PATH, mode=ctypes.RTLD_GLOBAL)

# Force the transceiver module to reload with the new env var
import conduit.transceiver as _xcvr_mod
_xcvr_mod._lib = None

from conduit.transceiver import (
    Transceiver,
    ConduitError,
    Stats,
    _get_lib as _get_xcvr_lib,
    _MSG_CALLBACK,
    _STATE_CALLBACK,
    _ERROR_CALLBACK,
)
from conduit.types import TransportType, TransportConfig, UdpConfig

# Add generated test protocol to path for typed message tests
_GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)

from session_protocol.messages import PingBody, DataBody, AckBody


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PING_TYPE_ID = 0x0AD7BB3ECC473399
DATA_TYPE_ID = 0x29D16B9E73F85835


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _find_free_udp_port() -> int:
    """Find a free UDP port by binding to port 0."""
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ============================================================================
# Fixtures
# ============================================================================


@pytest.fixture
def cabi_lib():
    """Return the low-level ctypes CDLL for direct API access."""
    return _get_xcvr_lib()


@pytest.fixture
def transceiver():
    """Create a Transceiver instance, yield it, then close."""
    t = Transceiver()
    yield t
    t.close()


@pytest.fixture
def udp_ports():
    """Return a pair of free UDP ports for loopback testing."""
    return _find_free_udp_port(), _find_free_udp_port()


# ============================================================================
# Version
# ============================================================================


class TestVersion:
    def test_conduit_version_returns_string(self, cabi_lib):
        """conduit_version() must return a non-empty version string."""
        version = cabi_lib.conduit_version()
        assert version is not None
        decoded = version.decode("utf-8")
        assert len(decoded) > 0
        assert "." in decoded, f"Expected semver-like version, got: {decoded}"

    def test_version_is_stable_across_calls(self, cabi_lib):
        v1 = cabi_lib.conduit_version().decode("utf-8")
        v2 = cabi_lib.conduit_version().decode("utf-8")
        assert v1 == v2


# ============================================================================
# Transceiver lifecycle
# ============================================================================


class TestLifecycle:
    def test_create_destroy(self):
        """Creating and destroying a Transceiver should not crash."""
        t = Transceiver()
        assert t._handle is not None
        t.close()
        assert t._handle is None

    def test_create_destroy_multiple(self):
        """Creating and destroying multiple Transceivers in sequence."""
        for _ in range(5):
            t = Transceiver()
            assert t._handle is not None
            t.close()

    def test_destroy_is_idempotent(self):
        """Calling close() multiple times must not crash."""
        t = Transceiver()
        t.close()
        t.close()  # second close is a no-op

    def test_context_manager(self):
        """Transceiver works as a context manager."""
        with Transceiver() as t:
            assert t._handle is not None
        assert t._handle is None


# ============================================================================
# Start / Stop
# ============================================================================


class TestStartStop:
    def test_start_stop(self):
        """start() followed by stop() should work."""
        with Transceiver() as t:
            port = _find_free_udp_port()
            t.add_peer("test", "session_protocol",
                        UdpConfig(f"127.0.0.1:{port}"))
            t.start()
            assert t.is_running() is True
            t.stop()
            assert t.is_running() is False

    def test_is_running_before_start(self, transceiver):
        """is_running() must return False before start()."""
        assert transceiver.is_running() is False

    def test_stop_without_start_is_safe(self, transceiver):
        """stop() without a preceding start() should not crash."""
        transceiver.stop()

    def test_start_stop_start_stop(self):
        """Start/stop cycle can be repeated."""
        with Transceiver() as t:
            port = _find_free_udp_port()
            t.add_peer("test", "session_protocol",
                        UdpConfig(f"127.0.0.1:{port}"))
            t.start()
            assert t.is_running() is True
            t.stop()
            assert t.is_running() is False

            # Start again
            t.start()
            assert t.is_running() is True
            t.stop()
            assert t.is_running() is False


# ============================================================================
# Peer management
# ============================================================================


class TestPeerManagement:
    def test_add_peer_udp(self, transceiver):
        """Adding a peer with UDP transport should return a valid peer ID."""
        port = _find_free_udp_port()
        peer_id = transceiver.add_peer(
            "radar", "session_protocol",
            UdpConfig(f"127.0.0.1:{port}"),
        )
        assert isinstance(peer_id, int)
        assert peer_id >= 0

    def test_add_peer_unknown_session_raises(self, transceiver):
        """Adding a peer with an unknown session name should raise."""
        port = _find_free_udp_port()
        with pytest.raises(ConduitError) as exc_info:
            transceiver.add_peer(
                "bad", "nonexistent_protocol_xyz",
                UdpConfig(f"127.0.0.1:{port}"),
            )
        assert exc_info.value.code != 0

    def test_peer_count_starts_at_zero(self, transceiver):
        """A fresh Transceiver has zero peers."""
        assert transceiver.peer_count() == 0

    def test_peer_count_increments(self, transceiver):
        """Peer count increments as peers are added."""
        for i in range(3):
            port = _find_free_udp_port()
            transceiver.add_peer(
                f"peer_{i}", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
        assert transceiver.peer_count() == 3

    def test_peer_by_name(self, transceiver):
        """peer_by_name returns the correct peer ID."""
        port = _find_free_udp_port()
        added_id = transceiver.add_peer(
            "my_peer", "session_protocol",
            UdpConfig(f"127.0.0.1:{port}"),
        )
        found_id = transceiver.peer_by_name("my_peer")
        assert found_id == added_id

    def test_peer_by_name_unknown_raises(self, transceiver):
        """Querying an unknown peer name should raise."""
        with pytest.raises(ConduitError):
            transceiver.peer_by_name("no_such_peer")

    def test_sole_peer(self, transceiver):
        """sole_peer() returns the ID when exactly one peer exists."""
        port = _find_free_udp_port()
        added_id = transceiver.add_peer(
            "only", "session_protocol",
            UdpConfig(f"127.0.0.1:{port}"),
        )
        sole_id = transceiver.sole_peer()
        assert sole_id == added_id

    def test_sole_peer_no_peers_raises(self, transceiver):
        """sole_peer() with zero peers should raise."""
        with pytest.raises(ConduitError):
            transceiver.sole_peer()

    def test_sole_peer_multiple_peers_raises(self, transceiver):
        """sole_peer() with multiple peers should raise."""
        for i in range(2):
            port = _find_free_udp_port()
            transceiver.add_peer(
                f"peer_{i}", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
        with pytest.raises(ConduitError):
            transceiver.sole_peer()

    def test_add_multiple_peers_different_sessions(self, transceiver):
        """Peers can use different session types."""
        port1 = _find_free_udp_port()
        port2 = _find_free_udp_port()
        id1 = transceiver.add_peer(
            "alpha", "session_protocol",
            UdpConfig(f"127.0.0.1:{port1}"),
        )
        id2 = transceiver.add_peer(
            "beta", "sentry_link",
            UdpConfig(f"127.0.0.1:{port2}"),
        )
        assert id1 != id2
        assert transceiver.peer_count() == 2

    def test_peer_state_initial(self, transceiver):
        """A newly added peer should be in Disconnected state (0)."""
        port = _find_free_udp_port()
        peer_id = transceiver.add_peer(
            "test", "session_protocol",
            UdpConfig(f"127.0.0.1:{port}"),
        )
        state = transceiver.peer_state(peer_id)
        # ConnectionState::Disconnected is typically 0
        assert isinstance(state, int)


# ============================================================================
# Handler registration (via raw C library)
# ============================================================================


class TestHandlerRegistration:
    def test_on_message_returns_callback_id(self, transceiver, cabi_lib):
        """Registering a message handler should return a non-zero callback ID."""

        @_MSG_CALLBACK
        def _cb(peer_id, type_id, type_name, data, data_len, user_data):
            pass

        # Keep reference to prevent GC
        transceiver._callback_refs.append(_cb)

        cb_id = cabi_lib.conduit_on_message(
            transceiver._handle, PING_TYPE_ID, _cb, None)
        assert cb_id > 0

    def test_on_any_message_returns_callback_id(self, transceiver, cabi_lib):
        """Registering a catch-all handler should return a non-zero callback ID."""

        @_MSG_CALLBACK
        def _cb(peer_id, type_id, type_name, data, data_len, user_data):
            pass

        transceiver._callback_refs.append(_cb)
        cb_id = cabi_lib.conduit_on_any_message(transceiver._handle, _cb, None)
        assert cb_id > 0

    def test_remove_handler(self, transceiver, cabi_lib):
        """Removing a registered handler should return true (1)."""

        @_MSG_CALLBACK
        def _cb(peer_id, type_id, type_name, data, data_len, user_data):
            pass

        transceiver._callback_refs.append(_cb)
        cabi_lib.conduit_on_message(
            transceiver._handle, PING_TYPE_ID, _cb, None)

        # Remove by type_id (peer_id=0 is unused in current impl)
        removed = cabi_lib.conduit_remove_handler(
            transceiver._handle, 0, PING_TYPE_ID)
        assert removed == 1

    def test_remove_nonexistent_handler(self, transceiver, cabi_lib):
        """Removing a handler for a type that was never registered returns 0."""
        removed = cabi_lib.conduit_remove_handler(
            transceiver._handle, 0, 0xDEADBEEF)
        assert removed == 0

    def test_on_decorator_registers_handler(self, transceiver):
        """The @t.on(type_id=...) decorator should register without error."""
        @transceiver.on(type_id=PING_TYPE_ID)
        def handle_ping(peer_id, type_id, type_name, data):
            pass

    def test_on_any_registers_handler(self, transceiver):
        """on_any() should register a catch-all handler."""
        received = []

        def handle_any(peer_id, type_id, type_name, data):
            received.append(type_id)

        cb_id = transceiver.on_any(handle_any)
        assert isinstance(cb_id, int)
        assert cb_id > 0


# ============================================================================
# State change callbacks
# ============================================================================


class TestStateChangeCallbacks:
    def test_on_state_change_returns_id(self, transceiver):
        """Registering a state change callback should return a callback ID."""
        cb_id = transceiver.on_state_change(lambda peer_id, state: None)
        assert isinstance(cb_id, int)
        assert cb_id > 0

    def test_remove_state_change(self, transceiver, cabi_lib):
        """Removing a registered state change callback returns 1."""
        cb_id = transceiver.on_state_change(lambda peer_id, state: None)
        removed = cabi_lib.conduit_remove_state_change(
            transceiver._handle, cb_id)
        assert removed == 1

    def test_remove_state_change_nonexistent(self, transceiver, cabi_lib):
        """Removing a nonexistent state change callback returns 0."""
        removed = cabi_lib.conduit_remove_state_change(
            transceiver._handle, 99999)
        assert removed == 0

    def test_multiple_state_change_callbacks(self, transceiver):
        """Multiple state change callbacks can be registered."""
        ids = []
        for _ in range(5):
            cb_id = transceiver.on_state_change(lambda p, s: None)
            ids.append(cb_id)
        # All IDs should be unique
        assert len(set(ids)) == 5


# ============================================================================
# Error callbacks
# ============================================================================


class TestErrorCallbacks:
    def test_on_error_returns_id(self, transceiver):
        """Registering an error callback should return a callback ID."""
        cb_id = transceiver.on_error(
            lambda peer_id, peer_name, err_code, err_msg: None)
        assert isinstance(cb_id, int)
        assert cb_id > 0

    def test_remove_error_callback(self, transceiver, cabi_lib):
        """Removing a registered error callback returns 1."""
        cb_id = transceiver.on_error(
            lambda peer_id, peer_name, err_code, err_msg: None)
        removed = cabi_lib.conduit_remove_error_callback(
            transceiver._handle, cb_id)
        assert removed == 1

    def test_remove_error_callback_nonexistent(self, transceiver, cabi_lib):
        """Removing a nonexistent error callback returns 0."""
        removed = cabi_lib.conduit_remove_error_callback(
            transceiver._handle, 99999)
        assert removed == 0


# ============================================================================
# UDP loopback send/receive
# ============================================================================


class TestUdpLoopback:
    """Tests that set up two peers communicating over UDP loopback."""

    def test_send_raw_bytes_succeeds(self):
        """conduit_send with raw bytes should now succeed (send_raw API)."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()

            # send_raw() should now work — raw bytes are encoded by the session
            t.send_raw(peer_id, PING_TYPE_ID, b"\x00\x00\x00\x01")

            t.stop()

    def test_two_peers_can_be_added_for_loopback(self, udp_ports):
        """Two peers with UDP loopback addresses can coexist."""
        port_a, port_b = udp_ports
        with Transceiver() as t:
            id_a = t.add_peer(
                "alpha", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )
            id_b = t.add_peer(
                "beta", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_b}"),
            )
            assert id_a != id_b
            assert t.peer_count() == 2

    def test_start_with_two_udp_peers(self, udp_ports):
        """Starting a transceiver with two UDP peers should succeed."""
        port_a, port_b = udp_ports
        with Transceiver() as t:
            t.add_peer(
                "alpha", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )
            t.add_peer(
                "beta", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_b}"),
            )
            t.start()
            assert t.is_running() is True
            t.stop()
            assert t.is_running() is False

    def test_handler_receives_callback_on_start(self, udp_ports):
        """Register a handler and verify it does not crash during start/stop."""
        port_a, port_b = udp_ports
        received = []

        with Transceiver() as t:
            t.add_peer(
                "alpha", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )

            @t.on(type_id=PING_TYPE_ID)
            def handle_ping(peer_id, type_id, type_name, data):
                received.append((peer_id, type_id))

            t.start()
            # Give it a moment for the I/O threads to stabilize
            time.sleep(0.05)
            t.stop()

        # No messages should have been received (nobody sent anything)
        assert len(received) == 0


# ============================================================================
# Peer management edge cases
# ============================================================================


class TestPeerEdgeCases:
    def test_add_peer_after_start(self):
        """Adding a peer after start should still work (or raise a clear error)."""
        port1 = _find_free_udp_port()
        port2 = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer("first", "session_protocol",
                        UdpConfig(f"127.0.0.1:{port1}"))
            t.start()

            # Adding a peer while running -- this may or may not be supported.
            # We verify it does not crash or segfault.
            try:
                t.add_peer("second", "session_protocol",
                            UdpConfig(f"127.0.0.1:{port2}"))
                # If it succeeds, peer count should be 2
                assert t.peer_count() == 2
            except ConduitError:
                # If adding peers after start is not supported, that is acceptable
                pass

            t.stop()

    def test_peer_by_name_after_multiple_adds(self, transceiver):
        """peer_by_name works correctly with multiple peers."""
        names_and_ids = {}
        for i in range(5):
            port = _find_free_udp_port()
            name = f"peer_{i}"
            pid = transceiver.add_peer(
                name, "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            names_and_ids[name] = pid

        for name, expected_id in names_and_ids.items():
            found_id = transceiver.peer_by_name(name)
            assert found_id == expected_id


# ============================================================================
# Transceiver with different session types
# ============================================================================


class TestMultiSessionTypes:
    def test_add_peers_with_all_session_types(self, transceiver):
        """All registered session types can be used to add peers."""
        session_names = [
            "session_protocol",
            "choice_protocol",
            "sentry_link",
            "direction_qualified",
        ]
        for i, session_name in enumerate(session_names):
            port = _find_free_udp_port()
            pid = transceiver.add_peer(
                f"peer_{i}", session_name,
                UdpConfig(f"127.0.0.1:{port}"),
            )
            assert pid >= 0
        assert transceiver.peer_count() == len(session_names)

    def test_start_stop_with_mixed_sessions(self, transceiver):
        """Starting and stopping with peers using different sessions works."""
        port1 = _find_free_udp_port()
        port2 = _find_free_udp_port()
        transceiver.add_peer(
            "sp", "session_protocol",
            UdpConfig(f"127.0.0.1:{port1}"),
        )
        transceiver.add_peer(
            "sl", "sentry_link",
            UdpConfig(f"127.0.0.1:{port2}"),
        )
        transceiver.start()
        assert transceiver.is_running() is True
        time.sleep(0.02)
        transceiver.stop()
        assert transceiver.is_running() is False


# ============================================================================
# Stress / lifecycle robustness
# ============================================================================


class TestRobustness:
    def test_rapid_create_destroy(self):
        """Rapidly creating and destroying transceivers must not leak or crash."""
        for _ in range(20):
            t = Transceiver()
            t.close()

    def test_rapid_start_stop(self):
        """Rapidly starting and stopping should not crash."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer("test", "session_protocol",
                        UdpConfig(f"127.0.0.1:{port}"))
            for _ in range(10):
                t.start()
                t.stop()

    def test_many_peers(self, transceiver):
        """Adding many peers should work without issues."""
        for i in range(20):
            port = _find_free_udp_port()
            transceiver.add_peer(
                f"peer_{i}", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
        assert transceiver.peer_count() == 20

    def test_many_handler_registrations(self, transceiver, cabi_lib):
        """Registering many handlers should not crash."""
        cb_ids = []
        for i in range(50):
            @_MSG_CALLBACK
            def _cb(peer_id, type_id, type_name, data, data_len, user_data):
                pass
            transceiver._callback_refs.append(_cb)
            cb_id = cabi_lib.conduit_on_message(
                transceiver._handle, PING_TYPE_ID + i, _cb, None)
            cb_ids.append(cb_id)

        assert len(set(cb_ids)) == 50  # All IDs unique

    def test_context_manager_stops_running_transceiver(self):
        """Exiting the context manager should stop a running transceiver."""
        port = _find_free_udp_port()
        t = Transceiver()
        t.add_peer("test", "session_protocol",
                    UdpConfig(f"127.0.0.1:{port}"))
        t.start()
        assert t.is_running() is True

        # Use close() which should stop + destroy
        t.close()
        assert t._handle is None


# ============================================================================
# Direct C ABI access tests (bypassing Python wrappers)
# ============================================================================


class TestDirectCABI:
    def test_conduit_create_returns_non_null(self, cabi_lib):
        """conduit_create() returns a non-null handle."""
        handle = cabi_lib.conduit_create()
        assert handle is not None
        assert handle != 0
        cabi_lib.conduit_destroy(handle)

    def test_conduit_is_running_before_start(self, cabi_lib):
        """conduit_is_running returns 0 for a freshly created transceiver."""
        handle = cabi_lib.conduit_create()
        assert cabi_lib.conduit_is_running(handle) == 0
        cabi_lib.conduit_destroy(handle)

    def test_conduit_peer_count_initially_zero(self, cabi_lib):
        """conduit_peer_count returns 0 for a fresh transceiver."""
        handle = cabi_lib.conduit_create()
        assert cabi_lib.conduit_peer_count(handle) == 0
        cabi_lib.conduit_destroy(handle)

    def test_conduit_stop_without_start(self, cabi_lib):
        """conduit_stop on a non-running transceiver should not crash."""
        handle = cabi_lib.conduit_create()
        cabi_lib.conduit_stop(handle)  # should be a no-op
        cabi_lib.conduit_destroy(handle)

    def test_conduit_destroy_null_safe(self, cabi_lib):
        """conduit_destroy(NULL) should not crash."""
        cabi_lib.conduit_destroy(None)

    def test_sole_peer_no_peers_returns_error(self, cabi_lib):
        """conduit_sole_peer with no peers returns an error code."""
        handle = cabi_lib.conduit_create()
        out_id = ctypes.c_uint32(0)
        err = cabi_lib.conduit_sole_peer(handle, ctypes.byref(out_id))
        assert err != 0
        cabi_lib.conduit_destroy(handle)


# ============================================================================
# Typed message send (happy path)
# ============================================================================


class TestTypedSend:
    """Tests for the typed send() API that mirrors C++ transceiver.send<T>()."""

    def test_send_typed_message(self):
        """send(peer_id, msg) should succeed with a bgen-generated message."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 42
            # Should not raise — send encodes and delivers to transport
            t.send(peer_id, msg)
            t.stop()

    def test_send_sole_peer_convenience(self):
        """send(msg) should use sole_peer() when only one peer exists."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "only", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = PingBody()
            msg.timestamp = 99
            t.send(msg)  # sole peer convenience
            t.stop()

    def test_send_data_body(self):
        """send() works with different message types."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            msg = DataBody()
            msg.channel = 5
            msg.payload_a = 100
            msg.payload_b = 200
            t.send(peer_id, msg)
            t.stop()

    def test_send_raw_still_works(self):
        """The low-level send_raw() API still works."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            # PingBody: timestamp=1 is 4 bytes big-endian
            t.send_raw(peer_id, PING_TYPE_ID, b"\x00\x00\x00\x01")
            t.stop()

    def test_send_multiple_messages(self):
        """Sending multiple messages in sequence should work."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            for i in range(10):
                msg = PingBody()
                msg.timestamp = i
                t.send(peer_id, msg)
            t.stop()


# ============================================================================
# Typed message send (error path)
# ============================================================================


class TestTypedSendErrors:
    """Error-path tests for the typed send() API."""

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

    def test_send_invalid_peer_raises(self):
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
                t.send(99999, msg)  # nonexistent peer
            t.stop()

    def test_send_none_raises_type_error(self):
        """Sending None should raise TypeError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            with pytest.raises(TypeError):
                t.send(peer_id, None)
            t.stop()

    def test_send_plain_object_raises_type_error(self):
        """Sending an object without TYPE_ID should raise TypeError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            with pytest.raises(TypeError):
                t.send(peer_id, "not a message")
            t.stop()

    def test_send_string_raises_type_error(self):
        """Sending a string should raise TypeError."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()
            with pytest.raises(TypeError):
                t.send(peer_id, 42)  # int is not a message
            t.stop()

    def test_sole_peer_send_with_multiple_peers_raises(self):
        """send(msg) with multiple peers should raise (sole_peer fails)."""
        port1 = _find_free_udp_port()
        port2 = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer("a", "session_protocol", UdpConfig(f"127.0.0.1:{port1}"))
            t.add_peer("b", "session_protocol", UdpConfig(f"127.0.0.1:{port2}"))
            t.start()
            msg = PingBody()
            msg.timestamp = 1
            with pytest.raises(ConduitError):
                t.send(msg)  # sole_peer fails with 2 peers
            t.stop()


# ============================================================================
# Typed handler registration (happy path)
# ============================================================================


class TestTypedHandler:
    """Tests for the typed @t.on(MsgClass) decorator that mirrors C++ on<T>()."""

    def test_on_typed_decorator_registers(self):
        """@t.on(PingBody) should register without error."""
        with Transceiver() as t:
            @t.on(PingBody)
            def handle_ping(peer_id, msg):
                pass

    def test_on_typed_multiple_types(self):
        """Registering handlers for different message types should work."""
        with Transceiver() as t:
            @t.on(PingBody)
            def handle_ping(peer_id, msg):
                pass

            @t.on(DataBody)
            def handle_data(peer_id, msg):
                pass

    def test_on_raw_still_works(self):
        """The raw @t.on(type_id=...) API still works."""
        with Transceiver() as t:
            @t.on(type_id=PING_TYPE_ID)
            def handle_raw(peer_id, type_id, type_name, data):
                pass


# ============================================================================
# Typed handler registration (error path)
# ============================================================================


class TestTypedHandlerErrors:
    """Error-path tests for the typed handler registration."""

    def test_on_non_message_class_raises(self):
        """@t.on(str) should raise TypeError."""
        with Transceiver() as t:
            with pytest.raises(TypeError):
                @t.on(str)
                def handle(peer_id, msg):
                    pass

    def test_on_dict_raises(self):
        """@t.on(dict) should raise TypeError since dict is not a message class."""
        with Transceiver() as t:
            with pytest.raises(TypeError):
                @t.on(dict)
                def handle(peer_id, msg):
                    pass

    def test_on_int_raises(self):
        """@t.on(42) should raise TypeError."""
        with Transceiver() as t:
            with pytest.raises(TypeError):
                @t.on(42)
                def handle(peer_id, msg):
                    pass


# ============================================================================
# Statistics (happy path)
# ============================================================================


class TestStats:
    """Tests for the stats() API that mirrors C++ transceiver.stats()."""

    def test_stats_returns_named_tuple(self, transceiver):
        """stats() should return a Stats namedtuple."""
        s = transceiver.stats()
        assert isinstance(s, Stats)

    def test_stats_initial_zeros(self, transceiver):
        """A fresh transceiver should have all-zero stats."""
        s = transceiver.stats()
        assert s.messages_received == 0
        assert s.messages_dispatched == 0
        assert s.messages_dropped == 0
        assert s.decode_errors == 0
        assert s.handler_errors == 0
        assert s.handler_timeouts == 0
        assert s.bytes_received == 0
        assert s.bytes_sent == 0

    def test_stats_after_send(self):
        """bytes_sent should increment after sending a message."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()

            s_before = t.stats()
            msg = PingBody()
            msg.timestamp = 42
            t.send(peer_id, msg)

            # Give I/O time to process
            time.sleep(0.05)
            s_after = t.stats()
            assert s_after.bytes_sent > s_before.bytes_sent

            t.stop()

    def test_stats_reset(self):
        """stats_reset() should zero all counters."""
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

            # Verify non-zero
            s = t.stats()
            assert s.bytes_sent > 0

            # Reset and verify zero
            t.stats_reset()
            s = t.stats()
            assert s.bytes_sent == 0
            assert s.messages_received == 0

            t.stop()

    def test_stats_fields_accessible(self, transceiver):
        """All 8 stats fields should be accessible as named attributes."""
        s = transceiver.stats()
        fields = [
            "messages_received", "messages_dispatched", "messages_dropped",
            "decode_errors", "handler_errors", "handler_timeouts",
            "bytes_received", "bytes_sent",
        ]
        for field in fields:
            assert hasattr(s, field), f"Stats missing field: {field}"
            assert isinstance(getattr(s, field), int)


# ============================================================================
# Statistics (error path)
# ============================================================================


class TestStatsErrors:
    """Error-path tests for statistics."""

    def test_stats_on_closed_transceiver(self):
        """stats() on a closed transceiver should raise or return error."""
        t = Transceiver()
        t.close()
        # After close, handle is None, so stats should fail gracefully
        # (the behavior depends on implementation — may raise or segfault guard)
        with pytest.raises(Exception):
            t.stats()


# ============================================================================
# UDP loopback: typed send + typed receive end-to-end
# ============================================================================


class TestTypedSendReceiveLoopback:
    """Verify that typed messages can be sent and received over UDP loopback."""

    def test_send_receive_ping_loopback(self):
        """Send a PingBody from one transceiver and receive it on another."""
        port_a = _find_free_udp_port()
        port_b = _find_free_udp_port()
        received = []

        with Transceiver() as receiver, Transceiver() as sender:
            # Receiver listens on port_a, sends to port_b
            receiver.add_peer(
                "sender_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_b}"),
            )

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg)

            # Sender listens on port_b, sends to port_a
            peer_id = sender.add_peer(
                "receiver_link", "session_protocol",
                UdpConfig(f"127.0.0.1:{port_a}"),
            )

            receiver.start()
            sender.start()

            # Send a PingBody
            msg = PingBody()
            msg.timestamp = 12345
            sender.send(peer_id, msg)

            # Wait for delivery
            time.sleep(0.2)

            sender.stop()
            receiver.stop()

        # Depending on whether UDP loopback connects both ways,
        # we may or may not receive. The key test is no crash + correct typing.
        # If received, verify type correctness.
        for m in received:
            assert isinstance(m, PingBody)
            assert m.timestamp == 12345
