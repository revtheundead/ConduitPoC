"""Integration tests for passthrough-mode message logging in the Python binding.

These tests use the production conduit_cabi library (no test-specific
session registry), exercising the passthrough path: Java/Python register a
session object, frame messages on the language side, then submit them to
the C ABI which only sees raw bytes.

What we're verifying here:

* ``set_message_log_config(enabled=True, include_raw_bytes=True)`` produces
  a hex dump in the log file (the regression for the bug just fixed).
* ``Transceiver.send()`` writes a SEND log entry **before** invoking the
  transport, so a peer that's never connected still produces a record of
  the send attempt.  This is the contract the C++ Transceiver provides
  unconditionally; the Java/Python passthrough wrappers must match it.
"""

from __future__ import annotations

import os
import sys
import tempfile

import pytest

# Use the production CABI library (not the test-specific one with pre-
# registered native sessions) — we register our own passthrough session
# at runtime, which doesn't need a special build.
from conftest import resolve_native_lib  # noqa: E402

_PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", ".."))

_BINDINGS_DIR = os.path.join(_PROJECT_ROOT, "bindings", "python")
if _BINDINGS_DIR not in sys.path:
    sys.path.insert(0, _BINDINGS_DIR)

# Other test files in this suite (test_transceiver_cabi.py, etc.) point
# CONDUIT_CABI_LIB at a test-specific shared lib that has pre-registered
# native sessions but no passthrough support.  Force the env var to the
# production library and clear the cached handle so _get_lib reloads.
_CABI_LIB = resolve_native_lib("CONDUIT_CABI_LIB", "conduit_cabi")
if not os.path.isfile(_CABI_LIB):
    pytest.skip(
        f"libconduit_cabi not built (looked for {_CABI_LIB}); "
        "build with -DCONDUIT_BUILD_CABI=ON or set CONDUIT_CABI_LIB",
        allow_module_level=True)
os.environ["CONDUIT_CABI_LIB"] = _CABI_LIB

import conduit.transceiver as _xcvr_mod  # noqa: E402
_xcvr_mod._lib = None  # force reload against the production library

from conduit.transceiver import Transceiver, ConduitError  # noqa: E402
from conduit.types import (  # noqa: E402
    TcpClientConfig, TcpServerConfig, MessageLogMode, MessageLogOutput,
)


# ---------------------------------------------------------------------------
# Minimal passthrough session — frames a 4-byte big-endian length header
# followed by the body.  Just enough for the binding to wrap/unwrap.
# ---------------------------------------------------------------------------

class _Msg:
    TYPE_ID = 0x01
    TYPE_NAME = "Msg"

    def __init__(self, value: int = 0) -> None:
        self.value = value

    def encode_bytes(self) -> bytes:
        return self.value.to_bytes(4, "big")

    @classmethod
    def decode_bytes(cls, raw: bytes) -> "_Msg":
        return cls(int.from_bytes(raw, "big"))

    def __repr__(self) -> str:
        return f"_Msg(value={self.value})"


class _Session:
    def sync_pattern(self) -> bytes:
        return b""

    def min_frame_header_size(self) -> int:
        return 4

    def leaf_type_ids(self) -> list[int]:
        return [_Msg.TYPE_ID]

    def type_name(self, type_id: int) -> str:
        return _Msg.TYPE_NAME if type_id == _Msg.TYPE_ID else "unknown"

    def is_receive_only(self, type_id: int) -> bool:
        return False

    def extract_frame_length(self, header: bytes) -> int:
        return int.from_bytes(header[:4], "big")

    def encode_wrap(self, type_id: int, msg: _Msg) -> dict:
        body = msg.encode_bytes()
        framed = (len(body) + 4).to_bytes(4, "big") + body
        return {"bytes": bytes(framed), "auto_fields": []}

    def decode_frame(self, raw: bytes) -> list[dict]:
        return []

    def format_message(self, type_id: int, msg: _Msg) -> str:
        return repr(msg)

    def format_outbound(self, type_id: int, msg: _Msg, auto_fields) -> str:
        return repr(msg)


def _setup(tmp_path, *, raw: bool, content: bool = True):
    tx = Transceiver()
    tx.set_message_log_config(
        enabled=True,
        mode=MessageLogMode.SEPARATE_DIRECTION,
        output=MessageLogOutput.FILE,
        directory=str(tmp_path),
        prefix="test",
        include_message_content=content,
        include_raw_bytes=raw,
    )
    tx.register_session("fake", _Session())
    peer_id = tx.add_peer(
        "server", "fake", TcpClientConfig("127.0.0.1:1"))  # nothing listening
    tx.start()
    return tx, peer_id


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_send_before_transport_passthrough_writes_log_entry(tmp_path):
    """A failed send (peer never reachable) still produces a SEND log entry.

    This is the contract the C++ Transceiver guarantees by logging before
    transport->send; the Python passthrough wrapper must match it.
    """
    tx, peer_id = _setup(tmp_path, raw=False)
    try:
        with pytest.raises(ConduitError):
            tx.send(peer_id, _Msg(42))
    finally:
        tx.close()

    log_path = tmp_path / "test_sent.log"
    assert log_path.exists(), "passthrough send must log even when peer is down"
    body = log_path.read_text()
    assert "SEND" in body
    assert "type=Msg" in body
    assert "peer=server" in body
    # No hex dump — include_raw_bytes was False
    assert "hex:" not in body


def test_include_raw_bytes_emits_hex_dump_passthrough(tmp_path):
    """include_raw_bytes=True must produce a hex block of the wire frame."""
    tx, peer_id = _setup(tmp_path, raw=True)
    try:
        with pytest.raises(ConduitError):
            tx.send(peer_id, _Msg(0x2A))
    finally:
        tx.close()

    body = (tmp_path / "test_sent.log").read_text()
    assert "hex:" in body
    # Frame is 4-byte length (00 00 00 08) + 4-byte body (00 00 00 2A)
    assert "00 00 00 08 00 00 00 2A" in body


def test_include_raw_bytes_off_omits_hex_dump_passthrough(tmp_path):
    """include_raw_bytes=False must not produce any hex block."""
    tx, peer_id = _setup(tmp_path, raw=False)
    try:
        with pytest.raises(ConduitError):
            tx.send(peer_id, _Msg(0x2A))
    finally:
        tx.close()

    body = (tmp_path / "test_sent.log").read_text()
    assert "hex:" not in body


def test_send_batch_passthrough_concatenates(tmp_path):
    """Passthrough send_batch concatenates pre-framed buffers and forwards.

    Regression for ISession default returning BatchNotSupported even when
    the binding has framed each message individually.
    """
    tx, peer_id = _setup(tmp_path, raw=False)
    try:
        # Two frames, each 8 bytes (4-byte length + 4-byte body).
        frame_a = (8).to_bytes(4, "big") + (1).to_bytes(4, "big")
        frame_b = (8).to_bytes(4, "big") + (2).to_bytes(4, "big")
        # send_batch on a TCP client with no server should fail at the
        # transport layer with SEND_FAILED — but it must NOT fail with
        # BatchNotSupported, because the passthrough session does support
        # batching of pre-framed buffers.
        with pytest.raises(ConduitError) as excinfo:
            tx.send_batch(peer_id, _Msg.TYPE_ID, [frame_a, frame_b])
        assert excinfo.value.code != -7, (
            "passthrough send_batch must not return BatchNotSupported")
    finally:
        tx.close()


def test_send_sole_peer_logs_when_no_real_peers_exist(tmp_path):
    """Regression: tx.send(msg) sole-peer overload used to throw from
    sole_peer() *before* logging, so a TCP server with no clients (or
    a UDP listener with no remote yet) produced no log file at all.
    The fix encodes and logs with peer_id=0 (peer="<no-peer>") before
    propagating the original sole-peer error.
    """
    tx = Transceiver()
    tx.set_message_log_config(
        enabled=True,
        mode=MessageLogMode.SEPARATE_DIRECTION,
        output=MessageLogOutput.FILE,
        directory=str(tmp_path),
        prefix="test",
        include_message_content=True,
        include_raw_bytes=True,
    )
    tx.register_session("fake", _Session())
    # TCP server registration → MultiPeerEntry, peers_ stays empty,
    # so sole_peer() raises PeerNotFound.
    tx.add_peer("srv", "fake", TcpServerConfig("127.0.0.1:0"))
    tx.start()
    try:
        with pytest.raises(ConduitError) as excinfo:
            tx.send(_Msg(0x55))   # sole-peer overload
        # The original sole-peer error must propagate, not be masked.
        assert excinfo.value.code == -4   # PEER_NOT_FOUND
    finally:
        tx.close()

    log_path = tmp_path / "test_sent.log"
    assert log_path.exists(), \
        "send(msg) must log even when sole_peer() fails"
    body = log_path.read_text()
    assert "SEND" in body
    assert "type=Msg" in body
    # Peer is unknown at the binding layer, so the C++ helper records
    # the configured fallback name.
    assert "peer=<no-peer>" in body
    # Wire bytes still threaded through to the hex dump.
    assert "00 00 00 08 00 00 00 55" in body
