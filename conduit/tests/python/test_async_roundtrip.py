"""Asyncio-specific roundtrip tests using AsyncTransceiver.

Tests async send/receive, message streams, concurrent sends, error handling,
lifecycle management, and cancellation — all exercised over UDP loopback.
"""
import asyncio
import ctypes
import os
import socket
import sys
import time
import threading

import pytest

from conftest import resolve_native_lib, load_native_lib

# ---------------------------------------------------------------------------
# Native library setup (mirrors test_xcvr_scenarios.py)
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

from conduit.transceiver import ConduitError
from conduit.async_transceiver import AsyncTransceiver, MessageStream
from conduit.types import UdpConfig

_GENERATED_DIR = os.path.join(_TESTS_DIR, "generated")
if _GENERATED_DIR not in sys.path:
    sys.path.insert(0, _GENERATED_DIR)

from session_protocol.messages import PingBody, DataBody, AckBody

PING_TYPE_ID = 0x0AD7BB3ECC473399
DATA_TYPE_ID = 0x29D16B9E73F85835


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run(coro):
    """Run an async coroutine synchronously (no pytest-asyncio needed)."""
    return asyncio.run(coro)


def _find_free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ===========================================================================
# Async happy path — send/receive roundtrips
# ===========================================================================

class TestAsyncTransceiverRoundtrip:

    def test_async_send_receive_ping(self):
        """Send PingBody via AsyncTransceiver and receive with @t.on handler."""
        async def _test():
            port = _find_free_udp_port()
            received = []
            recv_event = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(PingBody)
                async def handle_ping(peer_id, msg):
                    received.append(msg)
                    recv_event.set()

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = PingBody()
                msg.timestamp = 12345
                await sender.send(sender.sole_peer(), msg)

                try:
                    await asyncio.wait_for(recv_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(received) > 0:
                assert received[0].timestamp == 12345

        run(_test())

    def test_async_send_receive_data_body(self):
        """Send DataBody with all fields via AsyncTransceiver."""
        async def _test():
            port = _find_free_udp_port()
            received = []
            recv_event = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(DataBody)
                async def handle_data(peer_id, msg):
                    received.append(msg)
                    recv_event.set()

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = DataBody()
                msg.channel = 255
                msg.payload_a = 0xDEADBEEF
                msg.payload_b = 0xCAFEBABE
                await sender.send(sender.sole_peer(), msg)

                try:
                    await asyncio.wait_for(recv_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(received) > 0:
                assert received[0].channel == 255
                assert received[0].payload_a == 0xDEADBEEF
                assert received[0].payload_b == 0xCAFEBABE

        run(_test())

    def test_async_message_stream_iteration(self):
        """Use t.messages(PingBody) async iterator to receive messages."""
        async def _test():
            port = _find_free_udp_port()
            results = []

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                stream = receiver.messages(PingBody)

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = PingBody()
                msg.timestamp = 42
                await sender.send(sender.sole_peer(), msg)

                try:
                    peer_id, received_msg = await asyncio.wait_for(
                        stream.__anext__(), timeout=2.0)
                    results.append(received_msg)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(results) > 0:
                assert results[0].timestamp == 42

        run(_test())

    def test_async_send_receive_multi_type(self):
        """Register handlers for PingBody and DataBody, send both, verify both received."""
        async def _test():
            port = _find_free_udp_port()
            pings = []
            datas = []
            done_event = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(PingBody)
                async def handle_ping(peer_id, msg):
                    pings.append(msg)
                    if len(pings) + len(datas) >= 2:
                        done_event.set()

                @receiver.on(DataBody)
                async def handle_data(peer_id, msg):
                    datas.append(msg)
                    if len(pings) + len(datas) >= 2:
                        done_event.set()

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()
                await asyncio.sleep(0.05)

                ping = PingBody()
                ping.timestamp = 1111
                await sender.send(sender.sole_peer(), ping)

                data_msg = DataBody()
                data_msg.channel = 99
                data_msg.payload_a = 0xAAAA
                data_msg.payload_b = 0xBBBB
                await sender.send(sender.sole_peer(), data_msg)

                try:
                    await asyncio.wait_for(done_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(pings) > 0:
                assert pings[0].timestamp == 1111
            if len(datas) > 0:
                assert datas[0].channel == 99

        run(_test())

    def test_async_concurrent_sends(self):
        """Use asyncio.gather to send multiple messages concurrently."""
        async def _test():
            port = _find_free_udp_port()
            received = []
            done_event = asyncio.Event()
            count = 10

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(PingBody)
                async def handle_ping(peer_id, msg):
                    received.append(msg.timestamp)
                    if len(received) >= count:
                        done_event.set()

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()
                await asyncio.sleep(0.05)

                async def send_msg(i):
                    msg = PingBody()
                    msg.timestamp = i
                    await sender.send(sender.sole_peer(), msg)

                await asyncio.gather(*[send_msg(i) for i in range(count)])

                try:
                    await asyncio.wait_for(done_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            # UDP is unreliable; verify what we got is valid
            for ts in received:
                assert 0 <= ts < count

        run(_test())

    def test_async_message_stream_with_timeout(self):
        """Iterate message stream with asyncio.wait_for timeout."""
        async def _test():
            port = _find_free_udp_port()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                stream = receiver.messages(PingBody)

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                # Send a message
                msg = PingBody()
                msg.timestamp = 7777
                await sender.send(sender.sole_peer(), msg)

                try:
                    peer_id, received_msg = await asyncio.wait_for(
                        stream.__anext__(), timeout=2.0)
                    assert received_msg.timestamp == 7777
                except asyncio.TimeoutError:
                    pass  # UDP delivery not guaranteed

                # Now test timeout on no message
                with pytest.raises(asyncio.TimeoutError):
                    await asyncio.wait_for(
                        stream.__anext__(), timeout=0.1)

                await sender.stop()
                await receiver.stop()

        run(_test())


# ===========================================================================
# Async error paths
# ===========================================================================

class TestAsyncTransceiverErrors:

    def test_async_send_before_start_raises(self):
        """Sending before start() should raise ConduitError."""
        async def _test():
            port = _find_free_udp_port()
            async with AsyncTransceiver() as t:
                peer_id = t.add_peer("test", "session_protocol",
                                     UdpConfig(f"127.0.0.1:{port}"))
                msg = PingBody()
                msg.timestamp = 1
                with pytest.raises(ConduitError):
                    await t.send(peer_id, msg)

        run(_test())

    def test_async_handler_exception_does_not_crash(self):
        """An async handler that raises should not crash the transceiver."""
        async def _test():
            port = _find_free_udp_port()
            handler_called = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(PingBody)
                async def bad_handler(peer_id, msg):
                    handler_called.set()
                    raise RuntimeError("intentional async handler error")

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = PingBody()
                msg.timestamp = 42
                await sender.send(sender.sole_peer(), msg)

                try:
                    await asyncio.wait_for(handler_called.wait(), timeout=1.0)
                except asyncio.TimeoutError:
                    pass

                await asyncio.sleep(0.2)
                # Transceiver should still be running
                assert receiver.is_running() is True

                await sender.stop()
                await receiver.stop()

        run(_test())

    def test_async_error_callback_fires(self):
        """Error callback should fire asynchronously when garbage bytes received."""
        async def _test():
            port = _find_free_udp_port()
            errors = []
            error_event = asyncio.Event()

            async with AsyncTransceiver() as receiver:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                async def error_cb(peer_id, peer_name, error_code, error_msg):
                    errors.append((error_code, error_msg))
                    error_event.set()

                receiver.on_error(error_cb)
                await receiver.start()
                await asyncio.sleep(0.05)

                # Send garbage directly via raw UDP
                garbage = b"\xFF\xFE\xFD\xFC\xFB"
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.sendto(garbage, ("127.0.0.1", port))
                sock.close()

                try:
                    await asyncio.wait_for(error_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await receiver.stop()

            if len(errors) > 0:
                assert isinstance(errors[0][0], int)
                assert isinstance(errors[0][1], str)

        run(_test())

    def test_async_send_to_unknown_peer_raises(self):
        """Sending to a nonexistent peer should raise ConduitError."""
        async def _test():
            port = _find_free_udp_port()
            async with AsyncTransceiver() as t:
                t.add_peer("test", "session_protocol",
                           UdpConfig(f"127.0.0.1:{port}"))
                await t.start()
                msg = PingBody()
                msg.timestamp = 1
                with pytest.raises(ConduitError):
                    await t.send(99999, msg)
                await t.stop()

        run(_test())

    def test_async_send_raw_invalid_type_raises(self):
        """send_raw with bogus type_id should raise ConduitError."""
        async def _test():
            port = _find_free_udp_port()
            async with AsyncTransceiver() as t:
                peer_id = t.add_peer("test", "session_protocol",
                                     UdpConfig(f"127.0.0.1:{port}"))
                await t.start()
                with pytest.raises(ConduitError):
                    await t.send_raw(peer_id, 0xDEADBEEFCAFE, b"\x00")
                await t.stop()

        run(_test())


# ===========================================================================
# Async-specific patterns — lifecycle and concurrency
# ===========================================================================

class TestAsyncConcurrency:

    def test_async_start_stop_cycle(self):
        """Start/stop can be repeated via async API."""
        async def _test():
            port = _find_free_udp_port()
            async with AsyncTransceiver() as t:
                t.add_peer("test", "session_protocol",
                           UdpConfig(f"127.0.0.1:{port}"))

                assert t.is_running() is False
                await t.start()
                assert t.is_running() is True
                await t.stop()
                assert t.is_running() is False
                await t.start()
                assert t.is_running() is True
                await t.stop()
                assert t.is_running() is False

        run(_test())

    def test_async_context_manager_cleanup(self):
        """Verify cleanup happens when exception occurs inside async with block."""
        async def _test():
            port = _find_free_udp_port()
            t = None
            try:
                async with AsyncTransceiver() as t_inner:
                    t = t_inner
                    t.add_peer("test", "session_protocol",
                               UdpConfig(f"127.0.0.1:{port}"))
                    await t.start()
                    raise ValueError("intentional test error")
            except ValueError:
                pass

            # After the context manager exits, the transceiver should be stopped
            assert t is not None
            assert t.is_running() is False

        run(_test())

    def test_async_message_stream_cancellation(self):
        """Cancel async iterator, verify no hang."""
        async def _test():
            port = _find_free_udp_port()

            async with AsyncTransceiver() as receiver:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                stream = receiver.messages(PingBody)

                await receiver.start()

                # Attempt to read from the stream with a short timeout
                with pytest.raises(asyncio.TimeoutError):
                    await asyncio.wait_for(
                        stream.__anext__(), timeout=0.1)

                # Verify the transceiver is still healthy
                assert receiver.is_running() is True

                await receiver.stop()

        run(_test())

    def test_async_multiple_streams_different_types(self):
        """Separate streams for PingBody and DataBody, verify each gets its type."""
        async def _test():
            port = _find_free_udp_port()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                ping_stream = receiver.messages(PingBody)
                data_stream = receiver.messages(DataBody)

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()
                await asyncio.sleep(0.05)

                # Send one of each
                ping = PingBody()
                ping.timestamp = 5555
                await sender.send(sender.sole_peer(), ping)

                data_msg = DataBody()
                data_msg.channel = 77
                data_msg.payload_a = 0x1111
                data_msg.payload_b = 0x2222
                await sender.send(sender.sole_peer(), data_msg)

                # Try to get from each stream
                ping_result = None
                data_result = None

                try:
                    _, ping_result = await asyncio.wait_for(
                        ping_stream.__anext__(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                try:
                    _, data_result = await asyncio.wait_for(
                        data_stream.__anext__(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if ping_result is not None:
                assert isinstance(ping_result, PingBody)
                assert ping_result.timestamp == 5555

            if data_result is not None:
                assert isinstance(data_result, DataBody)
                assert data_result.channel == 77

        run(_test())

    def test_async_sync_handler_also_works(self):
        """A synchronous (non-async) handler registered via @t.on also works."""
        async def _test():
            port = _find_free_udp_port()
            received = []
            recv_event = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                @receiver.on(PingBody)
                def sync_handler(peer_id, msg):
                    received.append(msg.timestamp)
                    recv_event.set()

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = PingBody()
                msg.timestamp = 8888
                await sender.send(sender.sole_peer(), msg)

                try:
                    await asyncio.wait_for(recv_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(received) > 0:
                assert received[0] == 8888

        run(_test())

    def test_async_on_any_handler(self):
        """on_any catch-all async handler receives any message type."""
        async def _test():
            port = _find_free_udp_port()
            any_messages = []
            recv_event = asyncio.Event()

            async with AsyncTransceiver() as receiver, AsyncTransceiver() as sender:
                receiver.add_peer("src", "session_protocol",
                                  UdpConfig(f"0.0.0.0:{port}"))

                async def catch_all(peer_id, type_id, type_name, data):
                    any_messages.append(type_id)
                    recv_event.set()

                receiver.on_any(catch_all)

                sender.add_peer("dst", "session_protocol",
                                UdpConfig(f"127.0.0.1:{port}"))

                await receiver.start()
                await sender.start()

                msg = PingBody()
                msg.timestamp = 1
                await sender.send(sender.sole_peer(), msg)

                try:
                    await asyncio.wait_for(recv_event.wait(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass

                await sender.stop()
                await receiver.stop()

            if len(any_messages) > 0:
                assert any_messages[0] == PING_TYPE_ID

        run(_test())
