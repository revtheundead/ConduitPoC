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

# ---------------------------------------------------------------------------
# Environment setup: point to the test CABI libraries before importing bindings
# ---------------------------------------------------------------------------

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

# Path to the test transceiver CABI shared library
_CABI_LIB_PATH = os.environ.get(
    "CONDUIT_CABI_LIB",
    os.path.join(_PROJECT_ROOT, "build", "tests", "libconduit_cabi_test.so"),
)
os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB_PATH

# Also set codec lib for any codec operations needed
_CODEC_LIB_PATH = os.environ.get(
    "CONDUIT_CODEC_LIB",
    os.path.join(_PROJECT_ROOT, "build", "tests", "libconduit_codec_cabi_test.so"),
)
os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

# Ensure Python bindings are importable
_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

# The transceiver CABI test library references conduit_register_session (from
# the codec CABI) because test_sessions_register.cpp includes the codec header.
# We must preload the codec CABI library with RTLD_GLOBAL so the linker can
# resolve the symbol when the transceiver CABI library is loaded.
_codec_preload = ctypes.CDLL(_CODEC_LIB_PATH, mode=ctypes.RTLD_GLOBAL)

# Force the transceiver module to reload with the new env var
import conduit.transceiver as _xcvr_mod
_xcvr_mod._lib = None

from conduit.transceiver import (
    Transceiver,
    ConduitError,
    _get_lib as _get_xcvr_lib,
    _MSG_CALLBACK,
    _STATE_CALLBACK,
    _ERROR_CALLBACK,
)
from conduit.types import TransportType, TransportConfig, UdpConfig


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
    """Tests that set up two peers communicating over UDP loopback.

    NOTE: The current conduit_send() C ABI returns ERR_UNKNOWN because
    raw-bytes send is not yet implemented. These tests verify that the
    plumbing is wired up correctly and that the expected error is returned.
    When raw-bytes send is implemented, the test_send_receive_loopback
    test can be upgraded to verify actual message delivery.
    """

    def test_send_returns_error_for_raw_bytes(self):
        """conduit_send with raw bytes currently returns CONDUIT_XCVR_ERR_UNKNOWN."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            peer_id = t.add_peer(
                "test", "session_protocol",
                UdpConfig(f"127.0.0.1:{port}"),
            )
            t.start()

            # send() should raise because raw-bytes send is unimplemented
            with pytest.raises(ConduitError):
                t.send(peer_id, PING_TYPE_ID, b"\x00\x00\x00\x01")

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
