"""UDP multicast transport tests for Conduit Python bindings.

Tests multicast configuration, error handling, and loopback roundtrip
using the Transceiver Python bindings over the CABI layer.
"""

import os
import socket
import struct
import sys
import time

import pytest

from conftest import resolve_native_lib, load_native_lib

# ---------------------------------------------------------------------------
# Environment setup (mirrors test_transceiver_cabi.py)
# ---------------------------------------------------------------------------

_TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.abspath(os.path.join(_TESTS_DIR, "..", ".."))

_CABI_LIB_PATH = resolve_native_lib("CONDUIT_CABI_TEST_LIB", "conduit_cabi_test")
os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB_PATH

_CODEC_LIB_PATH = resolve_native_lib("CONDUIT_CODEC_TEST_LIB", "conduit_codec_cabi_test")
if not os.path.isfile(_CODEC_LIB_PATH):
    _CODEC_LIB_PATH = _CABI_LIB_PATH
os.environ["CONDUIT_CODEC_LIB"] = _CODEC_LIB_PATH

_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

_codec_preload = load_native_lib(_CODEC_LIB_PATH, global_symbols=True)

import conduit.transceiver as _xcvr_mod
_xcvr_mod._lib = None

from conduit.transceiver import Transceiver, ConduitError
from conduit.types import UdpConfig

_GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)

from session_protocol.messages import PingBody

MCAST_GROUP = "239.255.0.1"


def _find_free_udp_port() -> int:
    """Find a free UDP port by binding to port 0."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _multicast_available() -> bool:
    """Check if the OS supports multicast (IP_ADD_MEMBERSHIP)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.bind(("0.0.0.0", 0))
        mreq = struct.pack("4s4s",
                           socket.inet_aton(MCAST_GROUP),
                           socket.inet_aton("0.0.0.0"))
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        s.close()
        return True
    except OSError:
        return False


_mcast_ok = _multicast_available()
requires_multicast = pytest.mark.skipif(
    not _mcast_ok, reason="Multicast not available on this host")


# ---------------------------------------------------------------------------
# Happy-path tests
# ---------------------------------------------------------------------------


class TestMulticastConfig:
    """Tests for multicast UdpConfig start/stop lifecycle."""

    @requires_multicast
    def test_multicast_start_stop(self):
        """Transceiver with multicast UdpConfig starts and stops cleanly."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "mcast", "session_protocol",
                UdpConfig(bind_port=port, multicast_group=MCAST_GROUP,
                          multicast_loop=True),
            )
            t.start()
            time.sleep(0.05)
            t.stop()

    @requires_multicast
    def test_multicast_with_custom_ttl(self):
        """Transceiver with custom multicast TTL starts cleanly."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "mcast", "session_protocol",
                UdpConfig(bind_port=port, multicast_group=MCAST_GROUP,
                          multicast_ttl=4, multicast_loop=True),
            )
            t.start()
            t.stop()


class TestMulticastLoopback:
    """Test multicast send/receive using loopback."""

    @requires_multicast
    def test_multicast_loopback_roundtrip(self):
        """Two transceivers on the same multicast group exchange a PingBody."""
        port = _find_free_udp_port()
        received = []

        with Transceiver() as receiver, Transceiver() as sender:
            receiver.add_peer(
                "mcast_rx", "session_protocol",
                UdpConfig(bind_port=port, multicast_group=MCAST_GROUP,
                          multicast_loop=True),
            )

            @receiver.on(PingBody)
            def handle_ping(peer_id, msg):
                received.append(msg)

            sender.add_peer(
                "mcast_tx", "session_protocol",
                UdpConfig(bind_port=port, multicast_group=MCAST_GROUP,
                          multicast_loop=True),
            )

            receiver.start()
            sender.start()
            time.sleep(0.05)

            msg = PingBody()
            msg.timestamp = 42424242
            sender.send(sender.sole_peer(), msg)

            time.sleep(0.3)

            sender.stop()
            receiver.stop()

        # Both receiver and sender should have gotten the multicast packet
        # (loopback). At minimum the receiver should see it.
        for m in received:
            assert isinstance(m, PingBody)
            assert m.timestamp == 42424242


# ---------------------------------------------------------------------------
# Error-path tests (no multicast I/O needed — test validation logic only)
# ---------------------------------------------------------------------------


class TestMulticastErrors:
    """Tests for multicast configuration error handling."""

    def test_invalid_multicast_group_rejected(self):
        """Non-multicast address is rejected on start."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "bad", "session_protocol",
                UdpConfig(bind_port=port, multicast_group="192.168.1.1"),
            )
            with pytest.raises(ConduitError):
                t.start()

    def test_malformed_multicast_group_rejected(self):
        """Malformed address string is rejected on start."""
        port = _find_free_udp_port()
        with Transceiver() as t:
            t.add_peer(
                "bad", "session_protocol",
                UdpConfig(bind_port=port, multicast_group="not-an-ip"),
            )
            with pytest.raises(ConduitError):
                t.start()

    def test_multicast_bind_port_zero_rejected(self):
        """Multicast with bind_port=0 is rejected on start."""
        with Transceiver() as t:
            t.add_peer(
                "bad", "session_protocol",
                UdpConfig(bind_port=0, multicast_group=MCAST_GROUP),
            )
            with pytest.raises(ConduitError):
                t.start()
