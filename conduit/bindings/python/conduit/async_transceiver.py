"""Conduit AsyncTransceiver - asyncio wrapper for the Conduit Transceiver.

Provides an async/await interface over the synchronous Transceiver, bridging
C++ I/O thread callbacks into the Python asyncio event loop.

Usage:
    async with AsyncTransceiver() as t:
        t.add_peer("radar", "my_session", UdpConfig("0.0.0.0:5000"))

        @t.on(PingBody)
        async def handle_ping(peer_id, msg):
            print(f"Received: {msg}")

        await t.start()
        await t.send(peer_id, ping_msg)
"""

from __future__ import annotations

import asyncio
import functools
from typing import Any, Callable, Optional

from conduit.transceiver import Transceiver, ConduitError, _is_message_class, _is_message_instance
from conduit.types import (
    TransportConfig, UdpConfig, TcpClientConfig, TcpServerConfig, SerialConfig,
    MessageLogMode, MessageLogOutput,
)


class AsyncTransceiver:
    """Async wrapper around the synchronous Conduit Transceiver.

    Bridges C++ I/O thread callbacks into the asyncio event loop using
    ``loop.call_soon_threadsafe`` and ``asyncio.Queue`` for message delivery.

    All blocking operations (start, stop, send) are offloaded to an executor
    so they never block the event loop.
    """

    def __init__(self):
        self._transceiver = Transceiver()
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._message_queues: dict[int, asyncio.Queue] = {}
        self._handlers: dict[int, list[Callable]] = {}
        self._any_handlers: list[Callable] = []
        self._state_handlers: list[Callable] = []
        self._error_handlers: list[Callable] = []
        self._started = False

    async def __aenter__(self):
        self._loop = asyncio.get_running_loop()
        return self

    async def __aexit__(self, *args):
        if self._started:
            await self.stop()
        self._transceiver.close()

    # ========================================================================
    # Pre-start configuration (delegates to sync transceiver)
    # ========================================================================

    def set_queue_config(self, capacity: int = 1024,
                         drop_policy: int = 0,
                         back_pressure_threshold: float = 0.0) -> None:
        """Configure the receive queue. Must be called before start()."""
        self._transceiver.set_queue_config(capacity, drop_policy,
                                           back_pressure_threshold)

    def set_worker_config(self, thread_count: int = 1,
                          handler_timeout_ms: int = 0) -> None:
        """Configure worker threads. Must be called before start()."""
        self._transceiver.set_worker_config(thread_count, handler_timeout_ms)

    def set_shutdown_timeout(self, timeout_ms: int = 0) -> None:
        """Set the graceful shutdown timeout."""
        self._transceiver.set_shutdown_timeout(timeout_ms)

    def set_message_log_config(self, **kwargs) -> None:
        """Configure message logging. Must be called before start()."""
        self._transceiver.set_message_log_config(**kwargs)

    def register_session(self, name: str, session) -> None:
        """Register a passthrough session. Must be called before add_peer()."""
        self._transceiver.register_session(name, session)

    def add_peer(self, name: str, session_name: str, transport) -> int:
        """Add a peer. Returns peer ID."""
        return self._transceiver.add_peer(name, session_name, transport)

    # ========================================================================
    # Async lifecycle
    # ========================================================================

    async def start(self) -> None:
        """Start the transceiver (offloaded to executor)."""
        loop = self._loop or asyncio.get_running_loop()
        self._loop = loop
        await loop.run_in_executor(None, self._transceiver.start)
        self._started = True

    async def stop(self) -> None:
        """Stop the transceiver (offloaded to executor)."""
        loop = self._loop or asyncio.get_running_loop()
        await loop.run_in_executor(None, self._transceiver.stop)
        self._started = False

    async def close(self) -> None:
        """Stop (if running) and destroy the transceiver."""
        if self._started:
            await self.stop()
        self._transceiver.close()

    def is_running(self) -> bool:
        """Check if the transceiver is running."""
        return self._transceiver.is_running()

    # ========================================================================
    # Async send
    # ========================================================================

    async def send(self, peer_id_or_msg, msg=None) -> None:
        """Send a typed message to a peer (offloaded to executor).

        Usage:
            await t.send(peer_id, msg)    # explicit peer
            await t.send(msg)             # sole peer (convenience)
        """
        loop = self._loop or asyncio.get_running_loop()
        await loop.run_in_executor(
            None, self._transceiver.send, peer_id_or_msg, msg)

    async def send_raw(self, peer_id: int, type_id: int,
                       data: bytes) -> None:
        """Send raw bytes to a peer (offloaded to executor)."""
        loop = self._loop or asyncio.get_running_loop()
        await loop.run_in_executor(
            None, self._transceiver.send_raw, peer_id, type_id, data)

    # ========================================================================
    # Async receive — typed handler registration
    # ========================================================================

    def on(self, msg_class=None, *, type_id: Optional[int] = None):
        """Decorator for registering an async message handler.

        Typed handler (auto-deserialization):
            @t.on(PingBody)
            async def handle_ping(peer_id, msg):
                print(msg.timestamp)

        Raw handler (explicit type_id):
            @t.on(type_id=0x1234)
            async def handle_msg(peer_id, type_id, type_name, data):
                print(data.hex())

        Synchronous handlers are also supported — they will be called
        directly from the event loop thread.
        """
        if msg_class is not None and type_id is None:
            if not _is_message_class(msg_class):
                raise TypeError(
                    f"Expected a message class with TYPE_ID, encode_bytes, "
                    f"and decode_bytes, got {msg_class!r}"
                )
            resolved_type_id = msg_class.TYPE_ID

            def decorator(func):
                if self._transceiver._session is not None:
                    # Passthrough mode: register in Python-side handler dict
                    self._transceiver._session_handlers.setdefault(
                        resolved_type_id, []).append(
                        self._make_async_bridge(func))
                else:
                    # Direct mode: register typed handler with C ABI
                    def _typed_handler(peer_id, tid, type_name, raw):
                        try:
                            decoded = msg_class.decode_bytes(raw)
                        except Exception:
                            return
                        self._dispatch_async(func, peer_id, decoded)

                    self._transceiver._register_message_handler(
                        resolved_type_id, _typed_handler)
                return func
            return decorator

        # Raw handler
        if type_id is not None:
            resolved_type_id = type_id
        elif msg_class is not None:
            resolved_type_id = getattr(msg_class, "TYPE_ID", 0)
        else:
            resolved_type_id = 0

        def decorator(func):
            def _raw_handler(peer_id, tid, type_name, raw):
                self._dispatch_async(func, peer_id, tid, type_name, raw)
            self._transceiver._register_message_handler(
                resolved_type_id, _raw_handler)
            return func
        return decorator

    def on_any(self, func: Callable) -> None:
        """Register a catch-all async message handler."""
        def _raw_handler(peer_id, tid, type_name, raw):
            self._dispatch_async(func, peer_id, tid, type_name, raw)
        self._transceiver.on_any(_raw_handler)

    # ========================================================================
    # Async state & error callbacks
    # ========================================================================

    def on_state_change(self, func: Callable) -> int:
        """Register an async state change callback."""
        def _bridge(peer_id, new_state):
            self._dispatch_async(func, peer_id, new_state)
        return self._transceiver.on_state_change(_bridge)

    def on_error(self, func: Callable) -> int:
        """Register an async error callback."""
        def _bridge(peer_id, peer_name, error_code, error_msg):
            self._dispatch_async(func, peer_id, peer_name,
                                 error_code, error_msg)
        return self._transceiver.on_error(_bridge)

    # ========================================================================
    # Message stream (async iterator)
    # ========================================================================

    def messages(self, msg_class=None, *,
                 type_id: Optional[int] = None,
                 maxsize: int = 0) -> "MessageStream":
        """Return an async iterator for messages of a given type.

        Usage:
            async for peer_id, msg in t.messages(PingBody):
                print(msg)
        """
        if msg_class is not None:
            if not _is_message_class(msg_class):
                raise TypeError(f"Expected a message class, got {msg_class!r}")
            resolved_type_id = msg_class.TYPE_ID
        elif type_id is not None:
            resolved_type_id = type_id
        else:
            raise ValueError("Must specify msg_class or type_id")

        queue: asyncio.Queue = asyncio.Queue(maxsize=maxsize)
        self._message_queues.setdefault(resolved_type_id, queue)

        # Register handler to feed the queue
        if msg_class is not None:
            @self.on(msg_class)
            async def _feed(peer_id, msg):
                await queue.put((peer_id, msg))
        else:
            @self.on(type_id=resolved_type_id)
            async def _feed(peer_id, tid, type_name, raw):
                await queue.put((peer_id, raw))

        return MessageStream(queue)

    # ========================================================================
    # Query (sync, non-blocking)
    # ========================================================================

    def peer_count(self) -> int:
        return self._transceiver.peer_count()

    def peer_state(self, peer_id: int) -> int:
        return self._transceiver.peer_state(peer_id)

    def sole_peer(self) -> int:
        return self._transceiver.sole_peer()

    def peer_by_name(self, name: str) -> int:
        return self._transceiver.peer_by_name(name)

    def stats(self):
        return self._transceiver.stats()

    def stats_reset(self) -> None:
        self._transceiver.stats_reset()

    @staticmethod
    def version() -> str:
        return Transceiver.version()

    # ========================================================================
    # Internal: bridge C++ thread callbacks to asyncio
    # ========================================================================

    def _dispatch_async(self, func: Callable, *args) -> None:
        """Schedule an async or sync handler from a C++ thread."""
        loop = self._loop
        if loop is None or loop.is_closed():
            return

        if asyncio.iscoroutinefunction(func):
            loop.call_soon_threadsafe(
                lambda: asyncio.ensure_future(func(*args)))
        else:
            loop.call_soon_threadsafe(func, *args)

    def _make_async_bridge(self, func: Callable) -> Callable:
        """Create a sync callback that bridges to an async handler."""
        if asyncio.iscoroutinefunction(func):
            def bridge(peer_id, payload):
                self._dispatch_async(func, peer_id, payload)
            return bridge
        return func


class MessageStream:
    """Async iterator over messages from an asyncio.Queue."""

    def __init__(self, queue: asyncio.Queue):
        self._queue = queue

    def __aiter__(self):
        return self

    async def __anext__(self):
        return await self._queue.get()
