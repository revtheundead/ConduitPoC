"""Tests for the AsyncTransceiver wrapper.

These tests verify the asyncio integration layer without requiring
the native C library — they test the Python wrapper logic using mocks.
"""
import asyncio
from unittest.mock import MagicMock, patch
import pytest

from conduit.async_transceiver import AsyncTransceiver, MessageStream


def run(coro):
    """Run an async coroutine synchronously (no pytest-asyncio needed)."""
    return asyncio.run(coro)


# Minimal mock for a bgen-generated message class
class MockMessage:
    TYPE_ID = 0x1234

    def __init__(self, value=0):
        self.value = value

    @staticmethod
    def encode_bytes():
        return b"\x12\x34"

    @classmethod
    def decode_bytes(cls, data):
        return cls(42)


class MockMessage2:
    TYPE_ID = 0x5678

    def __init__(self, value=0):
        self.value = value

    @staticmethod
    def encode_bytes():
        return b"\x56\x78"

    @classmethod
    def decode_bytes(cls, data):
        return cls(99)


@pytest.fixture
def mock_transceiver():
    """Patch the Transceiver class so no native library is needed."""
    with patch("conduit.async_transceiver.Transceiver") as MockXcvr:
        instance = MockXcvr.return_value
        instance.start = MagicMock()
        instance.stop = MagicMock()
        instance.close = MagicMock()
        instance.is_running = MagicMock(return_value=False)
        instance.send = MagicMock()
        instance.send_raw = MagicMock()
        instance.peer_count = MagicMock(return_value=1)
        instance.sole_peer = MagicMock(return_value=1)
        instance.peer_state = MagicMock(return_value=2)
        instance.peer_by_name = MagicMock(return_value=1)
        instance.stats = MagicMock()
        instance.stats_reset = MagicMock()
        instance.set_queue_config = MagicMock()
        instance.set_worker_config = MagicMock()
        instance.set_shutdown_timeout = MagicMock()
        instance.set_message_log_config = MagicMock()
        instance.register_session = MagicMock()
        instance.add_peer = MagicMock(return_value=1)
        instance.on_state_change = MagicMock(return_value=1)
        instance.on_error = MagicMock(return_value=1)
        instance.on_any = MagicMock()
        instance._register_message_handler = MagicMock()
        instance._session = None
        instance._session_handlers = {}
        yield instance


class TestAsyncContextManager:
    def test_async_context_manager(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                assert t is not None
                assert t._loop is not None
            mock_transceiver.close.assert_called_once()
        run(_test())

    def test_async_context_manager_stops_if_started(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                await t.start()
                mock_transceiver.start.assert_called_once()
            mock_transceiver.stop.assert_called_once()
            mock_transceiver.close.assert_called_once()
        run(_test())


class TestAsyncLifecycle:
    def test_start_stop(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                await t.start()
                mock_transceiver.start.assert_called_once()
                await t.stop()
                mock_transceiver.stop.assert_called_once()
        run(_test())

    def test_is_running(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                assert not t.is_running()
                mock_transceiver.is_running.assert_called()
        run(_test())


class TestAsyncSend:
    def test_send_typed(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                msg = MockMessage(10)
                await t.send(1, msg)
                mock_transceiver.send.assert_called_once_with(1, msg)
        run(_test())

    def test_send_sole_peer(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                msg = MockMessage(10)
                await t.send(msg)
                mock_transceiver.send.assert_called_once_with(msg, None)
        run(_test())

    def test_send_raw(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                await t.send_raw(1, 0x1234, b"\x00\x01")
                mock_transceiver.send_raw.assert_called_once_with(1, 0x1234, b"\x00\x01")
        run(_test())


class TestAsyncConfiguration:
    def test_set_queue_config(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                t.set_queue_config(capacity=2048)
                mock_transceiver.set_queue_config.assert_called_once_with(2048, 0, 0.0)
        run(_test())

    def test_set_worker_config(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                t.set_worker_config(thread_count=4)
                mock_transceiver.set_worker_config.assert_called_once_with(4, 0)
        run(_test())

    def test_add_peer(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                pid = t.add_peer("test", "session", MagicMock())
                assert pid == 1
                mock_transceiver.add_peer.assert_called_once()
        run(_test())


class TestAsyncQuery:
    def test_peer_count(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                assert t.peer_count() == 1
        run(_test())

    def test_sole_peer(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                assert t.sole_peer() == 1
        run(_test())

    def test_peer_by_name(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                assert t.peer_by_name("test") == 1
        run(_test())


class TestAsyncHandlerRegistration:
    def test_on_typed_handler(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                @t.on(MockMessage)
                async def handle(peer_id, msg):
                    pass
                # Should register with C ABI
                mock_transceiver._register_message_handler.assert_called_once()
        run(_test())

    def test_on_raw_handler(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                @t.on(type_id=0x9999)
                async def handle(peer_id, tid, name, data):
                    pass
                mock_transceiver._register_message_handler.assert_called_once()
        run(_test())

    def test_on_invalid_class_raises(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                with pytest.raises(TypeError):
                    @t.on("not_a_class")
                    async def handle(peer_id, msg):
                        pass
        run(_test())


class TestAsyncStateCallbacks:
    def test_on_state_change(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                async def handler(peer_id, state):
                    pass
                t.on_state_change(handler)
                mock_transceiver.on_state_change.assert_called_once()
        run(_test())

    def test_on_error(self, mock_transceiver):
        async def _test():
            async with AsyncTransceiver() as t:
                async def handler(pid, name, code, msg):
                    pass
                t.on_error(handler)
                mock_transceiver.on_error.assert_called_once()
        run(_test())


class TestMessageStream:
    def test_message_stream_iteration(self):
        async def _test():
            queue = asyncio.Queue()
            stream = MessageStream(queue)

            # Put some messages
            await queue.put((1, MockMessage(42)))
            await queue.put((2, MockMessage(99)))

            results = []
            for _ in range(2):
                peer_id, msg = await asyncio.wait_for(
                    stream.__anext__(), timeout=1.0)
                results.append((peer_id, msg.value))

            assert results[0] == (1, 42)
            assert results[1] == (2, 99)
        run(_test())


class TestDispatchAsync:
    def test_dispatch_sync_handler(self, mock_transceiver):
        async def _test():
            received = []

            async with AsyncTransceiver() as t:
                def sync_handler(peer_id, value):
                    received.append((peer_id, value))

                t._dispatch_async(sync_handler, 1, "hello")
                # Allow event loop to process
                await asyncio.sleep(0.01)

            assert received == [(1, "hello")]
        run(_test())

    def test_dispatch_async_handler(self, mock_transceiver):
        async def _test():
            received = []

            async with AsyncTransceiver() as t:
                async def async_handler(peer_id, value):
                    received.append((peer_id, value))

                t._dispatch_async(async_handler, 1, "hello")
                await asyncio.sleep(0.01)

            assert received == [(1, "hello")]
        run(_test())
