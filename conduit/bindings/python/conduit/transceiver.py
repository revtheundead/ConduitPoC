"""Conduit Transceiver Python bindings via ctypes.

Wraps libconduit_cabi for full Transceiver operations.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from typing import Callable, Optional

from conduit.types import TransportConfig, TransportType


# ============================================================================
# C type definitions
# ============================================================================

_CONDUIT_TRANSPORT_CONFIG = type("_TransportConfig", (ctypes.Structure,), {
    "_fields_": [
        ("type", ctypes.c_int),
        ("address", ctypes.c_char_p),
        ("baud_rate", ctypes.c_uint32),
    ]
})

# Callback types
_MSG_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.c_uint32,       # peer_id
    ctypes.c_uint64,       # type_id
    ctypes.c_char_p,       # type_name
    ctypes.POINTER(ctypes.c_uint8),  # data
    ctypes.c_size_t,       # len
    ctypes.c_void_p,       # user_data
)

_STATE_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.c_uint32,       # peer_id
    ctypes.c_int32,        # new_state
    ctypes.c_void_p,       # user_data
)

_ERROR_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.c_uint32,       # peer_id
    ctypes.c_char_p,       # peer_name
    ctypes.c_int32,        # error_code
    ctypes.c_char_p,       # error_message
    ctypes.c_void_p,       # user_data
)


# ============================================================================
# Library loading
# ============================================================================

def _load_cabi_lib() -> ctypes.CDLL:
    """Load the conduit_cabi shared library."""
    # If CONDUIT_CABI_LIB points directly to a file, load it
    env_path = os.environ.get("CONDUIT_CABI_LIB", "")
    if env_path and os.path.isfile(env_path):
        return ctypes.CDLL(env_path)

    search_paths = [
        env_path,
        os.path.join(os.path.dirname(__file__), "..", "..", "lib"),
        os.path.join(os.path.dirname(__file__), ".."),
    ]

    names = ["libconduit_cabi.so", "libconduit_cabi.dylib", "conduit_cabi.dll"]

    for search_dir in search_paths:
        if not search_dir:
            continue
        for name in names:
            path = os.path.join(search_dir, name)
            if os.path.isfile(path):
                return ctypes.CDLL(path)

    lib_path = ctypes.util.find_library("conduit_cabi")
    if lib_path:
        return ctypes.CDLL(lib_path)

    raise OSError(
        "Cannot find libconduit_cabi. Set CONDUIT_CABI_LIB environment "
        "variable to the path of the shared library."
    )


_lib: Optional[ctypes.CDLL] = None

def _get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        _lib = _load_cabi_lib()
        _setup_signatures(_lib)
    return _lib


def _setup_signatures(lib: ctypes.CDLL) -> None:
    """Set up ctypes function signatures."""
    # Lifecycle
    lib.conduit_create.argtypes = []
    lib.conduit_create.restype = ctypes.c_void_p

    lib.conduit_destroy.argtypes = [ctypes.c_void_p]
    lib.conduit_destroy.restype = None

    lib.conduit_start.argtypes = [ctypes.c_void_p]
    lib.conduit_start.restype = ctypes.c_int32

    lib.conduit_stop.argtypes = [ctypes.c_void_p]
    lib.conduit_stop.restype = None

    lib.conduit_is_running.argtypes = [ctypes.c_void_p]
    lib.conduit_is_running.restype = ctypes.c_int

    # Peer management
    lib.conduit_add_peer.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.POINTER(_CONDUIT_TRANSPORT_CONFIG),
        ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.conduit_add_peer.restype = ctypes.c_int32

    lib.conduit_peer_by_name.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.conduit_peer_by_name.restype = ctypes.c_int32

    lib.conduit_sole_peer.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32),
    ]
    lib.conduit_sole_peer.restype = ctypes.c_int32

    # Messaging
    lib.conduit_send.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
    ]
    lib.conduit_send.restype = ctypes.c_int32

    # Handler registration
    lib.conduit_on_message.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64, _MSG_CALLBACK, ctypes.c_void_p,
    ]
    lib.conduit_on_message.restype = ctypes.c_uint32

    lib.conduit_on_any_message.argtypes = [
        ctypes.c_void_p, _MSG_CALLBACK, ctypes.c_void_p,
    ]
    lib.conduit_on_any_message.restype = ctypes.c_uint32

    lib.conduit_remove_handler.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
    ]
    lib.conduit_remove_handler.restype = ctypes.c_int

    # State/error callbacks
    lib.conduit_on_state_change.argtypes = [
        ctypes.c_void_p, _STATE_CALLBACK, ctypes.c_void_p,
    ]
    lib.conduit_on_state_change.restype = ctypes.c_uint32

    lib.conduit_remove_state_change.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.conduit_remove_state_change.restype = ctypes.c_int

    lib.conduit_on_error.argtypes = [
        ctypes.c_void_p, _ERROR_CALLBACK, ctypes.c_void_p,
    ]
    lib.conduit_on_error.restype = ctypes.c_uint32

    lib.conduit_remove_error_callback.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.conduit_remove_error_callback.restype = ctypes.c_int

    # Query
    lib.conduit_peer_count.argtypes = [ctypes.c_void_p]
    lib.conduit_peer_count.restype = ctypes.c_size_t

    lib.conduit_peer_state.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.conduit_peer_state.restype = ctypes.c_int32

    # Version
    lib.conduit_version.argtypes = []
    lib.conduit_version.restype = ctypes.c_char_p


# ============================================================================
# Python Transceiver class
# ============================================================================

class ConduitError(Exception):
    """Error from the Conduit Transceiver C ABI."""
    def __init__(self, code: int, message: str = ""):
        self.code = code
        super().__init__(f"Conduit error {code}: {message}")


class Transceiver:
    """Python wrapper for the Conduit Transceiver.

    Provides a Pythonic API over libconduit_cabi, with context manager support,
    decorator-based handler registration, and automatic lifecycle management.

    Usage:
        with Transceiver() as t:
            t.add_peer("radar", "my_session", UdpConfig("0.0.0.0:5000"))

            @t.on(type_id=0x1234)
            def handle_msg(peer_id, type_id, type_name, data):
                print(f"Got {type_name}: {data.hex()}")

            t.start()
            # ... do work ...
    """

    def __init__(self):
        lib = _get_lib()
        self._handle = lib.conduit_create()
        if not self._handle:
            raise ConduitError(-99, "Failed to create Transceiver")
        self._lib = lib
        # Keep references to prevent GC of callback closures
        self._callback_refs: list = []

    def __del__(self):
        self.close()

    def close(self) -> None:
        """Stop (if running) and destroy the transceiver."""
        if hasattr(self, "_handle") and self._handle:
            try:
                if self._lib.conduit_is_running(self._handle):
                    self._lib.conduit_stop(self._handle)
            except Exception:
                pass  # best-effort stop
            self._lib.conduit_destroy(self._handle)
            self._handle = None
            self._callback_refs.clear()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def add_peer(self, name: str, session_name: str,
                 transport: TransportConfig) -> int:
        """Add a peer to the transceiver. Returns peer ID."""
        cfg = _CONDUIT_TRANSPORT_CONFIG()
        cfg.type = transport.type.value
        cfg.address = transport.address.encode("utf-8")
        cfg.baud_rate = transport.baud_rate

        peer_id = ctypes.c_uint32(0)
        err = self._lib.conduit_add_peer(
            self._handle,
            name.encode("utf-8"),
            session_name.encode("utf-8"),
            ctypes.byref(cfg),
            ctypes.byref(peer_id),
        )
        if err != 0:
            raise ConduitError(err, f"Failed to add peer '{name}'")
        return peer_id.value

    def start(self) -> None:
        """Start the transceiver."""
        err = self._lib.conduit_start(self._handle)
        if err != 0:
            raise ConduitError(err, "Failed to start transceiver")

    def stop(self) -> None:
        """Stop the transceiver."""
        self._lib.conduit_stop(self._handle)

    def is_running(self) -> bool:
        """Check if the transceiver is running."""
        return bool(self._lib.conduit_is_running(self._handle))

    def send(self, peer_id: int, type_id: int, data: bytes) -> None:
        """Send raw bytes to a peer."""
        buf = (ctypes.c_uint8 * len(data))(*data)
        err = self._lib.conduit_send(
            self._handle, peer_id, type_id, buf, len(data))
        if err != 0:
            raise ConduitError(err, "send failed")

    def on(self, msg_class=None, *, type_id: Optional[int] = None):
        """Decorator for registering a message handler.

        Can be used with a message class that has TYPE_ID:
            @t.on(Heartbeat)
            def handle_hb(peer_id, type_id, type_name, data): ...

        Or with an explicit type_id:
            @t.on(type_id=0x1234)
            def handle_msg(peer_id, type_id, type_name, data): ...
        """
        if msg_class is not None and type_id is None:
            # msg_class should have TYPE_ID attribute
            type_id = getattr(msg_class, "TYPE_ID", 0)

        def decorator(func):
            self._register_message_handler(type_id or 0, func)
            return func

        return decorator

    def on_any(self, func: Callable) -> int:
        """Register a catch-all message handler."""
        return self._register_message_handler(0, func, any_message=True)

    def on_state_change(self, func: Callable[[int, int], None]) -> int:
        """Register a connection state change callback. Returns callback ID."""
        @_STATE_CALLBACK
        def _cb(peer_id, new_state, _user_data):
            func(peer_id, new_state)

        self._callback_refs.append(_cb)
        return self._lib.conduit_on_state_change(self._handle, _cb, None)

    def on_error(self, func: Callable[[int, str, int, str], None]) -> int:
        """Register an error callback. Returns callback ID."""
        @_ERROR_CALLBACK
        def _cb(peer_id, peer_name, error_code, error_msg, _user_data):
            func(
                peer_id,
                peer_name.decode("utf-8") if peer_name else "",
                error_code,
                error_msg.decode("utf-8") if error_msg else "",
            )

        self._callback_refs.append(_cb)
        return self._lib.conduit_on_error(self._handle, _cb, None)

    def peer_count(self) -> int:
        """Get the number of peers."""
        return self._lib.conduit_peer_count(self._handle)

    def peer_state(self, peer_id: int) -> int:
        """Get the connection state of a peer."""
        return self._lib.conduit_peer_state(self._handle, peer_id)

    def sole_peer(self) -> int:
        """Get the sole peer ID (when only one peer exists)."""
        pid = ctypes.c_uint32(0)
        err = self._lib.conduit_sole_peer(self._handle, ctypes.byref(pid))
        if err != 0:
            raise ConduitError(err, "sole_peer failed")
        return pid.value

    def peer_by_name(self, name: str) -> int:
        """Look up a peer by name."""
        pid = ctypes.c_uint32(0)
        err = self._lib.conduit_peer_by_name(
            self._handle, name.encode("utf-8"), ctypes.byref(pid))
        if err != 0:
            raise ConduitError(err, f"peer_by_name('{name}') failed")
        return pid.value

    def _register_message_handler(
        self, type_id: int, func: Callable,
        any_message: bool = False
    ) -> int:
        """Internal: register a C message callback that calls func."""
        @_MSG_CALLBACK
        def _cb(peer_id, tid, type_name, data, data_len, _user_data):
            raw = bytes(data[i] for i in range(data_len)) if data and data_len > 0 else b""
            name = type_name.decode("utf-8") if type_name else ""
            func(peer_id, tid, name, raw)

        self._callback_refs.append(_cb)

        if any_message:
            return self._lib.conduit_on_any_message(self._handle, _cb, None)
        else:
            return self._lib.conduit_on_message(
                self._handle, type_id, _cb, None)
