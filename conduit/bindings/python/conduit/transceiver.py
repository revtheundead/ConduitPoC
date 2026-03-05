"""Conduit Transceiver Python bindings via ctypes.

Wraps libconduit_cabi for full Transceiver operations.
Provides both typed message APIs (matching C++ UX) and raw byte-level access.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import inspect
import os
from collections import namedtuple
from typing import Callable, Optional

from conduit.types import (
    TransportConfig, TransportType,
    UdpConfig, TcpClientConfig, TcpServerConfig, SerialConfig,
    MessageLogMode, MessageLogOutput,
)


# ============================================================================
# C type definitions
# ============================================================================

class _CONDUIT_TRANSPORT_CONFIG(ctypes.Structure):
    """Mirrors the extended conduit_transport_config_t C struct.

    Fields use the "0 = use C++ default" convention.
    reconnect_enabled: >0 = on, 0 = use default (on), <0 = off.
    """
    _fields_ = [
        # Common
        ("type",                        ctypes.c_int),
        ("address",                     ctypes.c_char_p),
        ("baud_rate",                   ctypes.c_uint32),
        ("recv_buffer_size",            ctypes.c_size_t),
        # TCP client
        ("connect_timeout_ms",          ctypes.c_uint32),
        ("reconnect_enabled",           ctypes.c_int),
        ("reconnect_initial_delay_ms",  ctypes.c_uint32),
        ("reconnect_max_delay_ms",      ctypes.c_uint32),
        ("reconnect_backoff_multiplier",ctypes.c_double),
        ("reconnect_max_attempts",      ctypes.c_uint32),
        # UDP
        ("bind_address",                ctypes.c_char_p),
        ("bind_port",                   ctypes.c_uint16),
        ("remote_port",                 ctypes.c_uint16),
        ("max_datagram_size",           ctypes.c_size_t),
        ("max_peers",                   ctypes.c_size_t),
        ("peer_timeout_s",              ctypes.c_uint32),
        # TCP server
        ("max_clients",                 ctypes.c_size_t),
        # Serial
        ("data_bits",                   ctypes.c_uint8),
        ("parity",                      ctypes.c_int),
        ("stop_bits",                   ctypes.c_int),
        ("flow_control",                ctypes.c_int),
    ]

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

# Frame config for passthrough session registration
class _CONDUIT_FRAME_CONFIG(ctypes.Structure):
    _fields_ = [
        ("sync_pattern", ctypes.POINTER(ctypes.c_uint8)),
        ("sync_pattern_len", ctypes.c_size_t),
        ("min_header_size", ctypes.c_size_t),
        ("length_skip_bits", ctypes.c_size_t),
        ("length_field_bits", ctypes.c_size_t),
        ("length_big_endian", ctypes.c_int),
    ]

# Stats snapshot structure matching C ABI
class _CONDUIT_STATS(ctypes.Structure):
    _fields_ = [
        ("messages_received", ctypes.c_uint64),
        ("messages_dispatched", ctypes.c_uint64),
        ("messages_dropped", ctypes.c_uint64),
        ("decode_errors", ctypes.c_uint64),
        ("handler_errors", ctypes.c_uint64),
        ("handler_timeouts", ctypes.c_uint64),
        ("bytes_received", ctypes.c_uint64),
        ("bytes_sent", ctypes.c_uint64),
    ]


# Message log config structure matching C ABI
class _CONDUIT_MESSAGE_LOG_CONFIG(ctypes.Structure):
    _fields_ = [
        ("enabled", ctypes.c_int),
        ("mode", ctypes.c_int),
        ("output", ctypes.c_int),
        ("directory", ctypes.c_char_p),
        ("prefix", ctypes.c_char_p),
        ("filename", ctypes.c_char_p),
        ("sent_filename", ctypes.c_char_p),
        ("received_filename", ctypes.c_char_p),
        ("include_message_content", ctypes.c_int),
    ]


# Named tuple for Python-friendly stats access
Stats = namedtuple("Stats", [
    "messages_received", "messages_dispatched", "messages_dropped",
    "decode_errors", "handler_errors", "handler_timeouts",
    "bytes_received", "bytes_sent",
])


# ============================================================================
# Library loading
# ============================================================================

def _load_cabi_lib() -> ctypes.CDLL:
    """Load the conduit_cabi shared library."""
    # If CONDUIT_CABI_LIB points directly to a file, load it
    env_path = os.environ.get("CONDUIT_CABI_LIB", "")
    if env_path and os.path.isfile(env_path):
        return ctypes.CDLL(env_path)

    # Search relative to the conduit package location.
    # In a development (editable) install the layout is:
    #   conduit/bindings/python/conduit/transceiver.py  ->  ../../../../build/
    # In a flat build layout:
    #   conduit/build/  (standard build output)
    pkg_dir = os.path.dirname(__file__)
    search_paths = [
        env_path,
        os.path.join(pkg_dir, "..", "..", "..", "build"),    # editable: conduit/build/
        os.path.join(pkg_dir, "..", "..", "build"),           # alternate layout
        os.path.join(pkg_dir, "..", "..", "lib"),             # installed lib/ dir
        os.path.join(pkg_dir, ".."),                          # adjacent to package
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

    # Batch send
    lib.conduit_send_batch.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t,
    ]
    lib.conduit_send_batch.restype = ctypes.c_int32

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

    # Stats
    lib.conduit_stats.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(_CONDUIT_STATS),
    ]
    lib.conduit_stats.restype = ctypes.c_int32

    lib.conduit_stats_reset.argtypes = [ctypes.c_void_p]
    lib.conduit_stats_reset.restype = ctypes.c_int32

    # Pre-start configuration
    lib.conduit_set_queue_config.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_double,
    ]
    lib.conduit_set_queue_config.restype = ctypes.c_int32

    lib.conduit_set_worker_config.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64,
    ]
    lib.conduit_set_worker_config.restype = ctypes.c_int32

    lib.conduit_set_shutdown_timeout.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64,
    ]
    lib.conduit_set_shutdown_timeout.restype = ctypes.c_int32

    lib.conduit_set_message_log_config.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p,
    ]
    lib.conduit_set_message_log_config.restype = ctypes.c_int32

    # Passthrough session registration
    lib.conduit_register_passthrough_session.argtypes = [
        ctypes.c_char_p,                          # name
        ctypes.POINTER(_CONDUIT_FRAME_CONFIG),    # frame_config
        ctypes.POINTER(ctypes.c_uint64),          # type_ids
        ctypes.POINTER(ctypes.c_char_p),          # type_names
        ctypes.POINTER(ctypes.c_int),             # receive_only
        ctypes.c_size_t,                          # type_count
    ]
    lib.conduit_register_passthrough_session.restype = ctypes.c_int32

    # Logger configuration
    lib.conduit_set_log_level.argtypes = [ctypes.c_int]
    lib.conduit_set_log_level.restype = None

    lib.conduit_get_log_level.argtypes = []
    lib.conduit_get_log_level.restype = ctypes.c_int

    lib.conduit_log_add_console_sink.argtypes = [ctypes.c_int, ctypes.c_int]
    lib.conduit_log_add_console_sink.restype = None

    lib.conduit_log_add_file_sink.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.conduit_log_add_file_sink.restype = None

    lib.conduit_log_clear_sinks.argtypes = []
    lib.conduit_log_clear_sinks.restype = None

    # Version
    lib.conduit_version.argtypes = []
    lib.conduit_version.restype = ctypes.c_char_p


# ============================================================================
# Helper: check if an object is a bgen-generated message class
# ============================================================================

def _is_message_class(cls) -> bool:
    """Check if cls is a bgen-generated message class with TYPE_ID and codec methods."""
    return (
        isinstance(cls, type)
        and hasattr(cls, "TYPE_ID")
        and hasattr(cls, "encode_bytes")
        and hasattr(cls, "decode_bytes")
    )


def _is_message_instance(obj) -> bool:
    """Check if obj is an instance of a bgen-generated message class."""
    return (
        hasattr(obj, "TYPE_ID")
        and hasattr(obj, "encode_bytes")
        and callable(getattr(obj, "encode_bytes", None))
    )


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

    Provides a Pythonic API over libconduit_cabi that mirrors the C++ experience.
    Users work with typed message objects — never raw bytes or type IDs.

    Usage:
        with Transceiver() as t:
            t.add_peer("radar", "my_session", UdpConfig("0.0.0.0:5000"))

            @t.on(PingBody)
            def handle_ping(peer_id, msg):
                print(f"Received: {msg}")

            t.start()
            t.send(peer_id, ping_msg)
    """

    def __init__(self):
        lib = _get_lib()
        self._handle = lib.conduit_create()
        if not self._handle:
            raise ConduitError(-99, "Failed to create Transceiver")
        self._lib = lib
        # Keep references to prevent GC of callback closures
        self._callback_refs: list = []
        # Passthrough session support
        self._session = None
        self._session_handlers: dict = {}  # type_id -> list of callables

    # ========================================================================
    # Pre-start configuration (call before start())
    # ========================================================================

    def set_queue_config(self, capacity: int = 1024,
                         drop_policy: int = 0,
                         back_pressure_threshold: float = 0.0) -> None:
        """Configure the receive queue. Must be called before start().

        Args:
            capacity: Queue capacity (default 1024)
            drop_policy: 0=DropOldest, 1=DropNewest, 2=Block
            back_pressure_threshold: 0.0=disabled, 0.8=pause at 80%
        """
        err = self._lib.conduit_set_queue_config(
            self._handle, capacity, drop_policy, back_pressure_threshold)
        if err != 0:
            raise ConduitError(err, "Failed to set queue config")

    def set_worker_config(self, thread_count: int = 1,
                          handler_timeout_ms: int = 0) -> None:
        """Configure worker threads. Must be called before start().

        Args:
            thread_count: Number of dispatch threads (default 1)
            handler_timeout_ms: Log warning if handler exceeds this (0=off)
        """
        err = self._lib.conduit_set_worker_config(
            self._handle, thread_count, handler_timeout_ms)
        if err != 0:
            raise ConduitError(err, "Failed to set worker config")

    def set_shutdown_timeout(self, timeout_ms: int = 0) -> None:
        """Set the graceful shutdown timeout.

        Args:
            timeout_ms: Max time to wait for workers to drain (0=indefinite)
        """
        err = self._lib.conduit_set_shutdown_timeout(
            self._handle, timeout_ms)
        if err != 0:
            raise ConduitError(err, "Failed to set shutdown timeout")

    # ========================================================================
    # Logger configuration (global — controls internal Conduit logging)
    # ========================================================================

    @staticmethod
    def set_log_level(level: int) -> None:
        """Set the global Conduit log level.

        Controls internal diagnostic output (debug, warn, error, etc.).
        This is distinct from message_log_config which records message traffic.

        Args:
            level: 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Off
        """
        _get_lib().conduit_set_log_level(level)

    @staticmethod
    def get_log_level() -> int:
        """Get the current global Conduit log level.

        Returns:
            Current log level (0=Trace through 6=Off)
        """
        return _get_lib().conduit_get_log_level()

    @staticmethod
    def log_add_console_sink(use_stderr: bool = False,
                             colorize: bool = True) -> None:
        """Add a console log sink for Conduit internal logging.

        Args:
            use_stderr: If True, log to stderr; otherwise stdout
            colorize: If True, use ANSI color codes
        """
        _get_lib().conduit_log_add_console_sink(
            1 if use_stderr else 0, 1 if colorize else 0)

    @staticmethod
    def log_add_file_sink(path: str, append: bool = True) -> None:
        """Add a file log sink for Conduit internal logging.

        Args:
            path: File path for log output
            append: If True, append to existing file; otherwise overwrite
        """
        _get_lib().conduit_log_add_file_sink(
            path.encode("utf-8"), 1 if append else 0)

    @staticmethod
    def log_clear_sinks() -> None:
        """Remove all Conduit internal log sinks."""
        _get_lib().conduit_log_clear_sinks()

    def set_message_log_config(self, *,
                               enabled: bool = False,
                               mode=MessageLogMode.COMBINED,
                               output=MessageLogOutput.FILE,
                               directory: str = ".",
                               prefix: str = "conduit",
                               filename: str = "",
                               sent_filename: str = "",
                               received_filename: str = "",
                               include_message_content: bool = True) -> None:
        """Configure message logging. Must be called before start().

        Args:
            enabled:  Whether logging is enabled.
            mode:     :class:`MessageLogMode` (or int); controls file grouping.
            output:   :class:`MessageLogOutput` (or int); controls destination.
            directory: Log file directory.
            prefix:   Log file prefix.
            filename: Log filename pattern (supports ``{peer}``, ``{direction}``).
            sent_filename: Override for sent direction.
            received_filename: Override for received direction.
            include_message_content: Include ``to_string()`` output (has perf cost).
        """
        cfg = _CONDUIT_MESSAGE_LOG_CONFIG()
        cfg.enabled = 1 if enabled else 0
        cfg.mode = int(mode)
        cfg.output = int(output)
        cfg.directory = directory.encode("utf-8")
        cfg.prefix = prefix.encode("utf-8")
        cfg.filename = filename.encode("utf-8") if filename else None
        cfg.sent_filename = sent_filename.encode("utf-8") if sent_filename else None
        cfg.received_filename = received_filename.encode("utf-8") if received_filename else None
        cfg.include_message_content = 1 if include_message_content else 0

        err = self._lib.conduit_set_message_log_config(
            self._handle, ctypes.byref(cfg))
        if err != 0:
            raise ConduitError(err, "Failed to set message log config")

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

    # ========================================================================
    # Session registration (passthrough mode)
    # ========================================================================

    def register_session(self, name: str, session) -> None:
        """Register a passthrough session from a bgen-generated session object.

        Must be called before add_peer(). The native transceiver handles
        transport and framing; all encode/decode runs on the Python side.
        """
        # Extract framing config from the session
        sync = session.sync_pattern()
        min_header = session.min_frame_header_size()
        type_ids = session.leaf_type_ids()
        count = len(type_ids)

        # Probe frame length extraction parameters
        skip_bits = self._probe_skip_bits(session, min_header)
        field_bits = (min_header * 8) - skip_bits
        big_endian = self._probe_endianness(session, skip_bits, field_bits)

        # Build frame config struct
        fc = _CONDUIT_FRAME_CONFIG()
        if sync:
            sync_buf = (ctypes.c_uint8 * len(sync))(*sync)
            fc.sync_pattern = ctypes.cast(sync_buf, ctypes.POINTER(ctypes.c_uint8))
            fc.sync_pattern_len = len(sync)
            self._callback_refs.append(sync_buf)  # prevent GC
        else:
            fc.sync_pattern = None
            fc.sync_pattern_len = 0
        fc.min_header_size = min_header
        fc.length_skip_bits = skip_bits
        fc.length_field_bits = field_bits
        fc.length_big_endian = 1 if big_endian else 0

        # Build type arrays
        c_type_ids = (ctypes.c_uint64 * count)(*type_ids)
        c_names_raw = [session.type_name(tid).encode("utf-8") for tid in type_ids]
        c_names = (ctypes.c_char_p * count)(*c_names_raw)
        c_recv_only = (ctypes.c_int * count)(
            *(1 if session.is_receive_only(tid) else 0 for tid in type_ids))

        err = self._lib.conduit_register_passthrough_session(
            name.encode("utf-8"),
            ctypes.byref(fc),
            c_type_ids,
            c_names,
            c_recv_only,
            count,
        )
        if err != 0:
            raise ConduitError(err, f"Failed to register session '{name}'")

        self._session = session

        # Install a catch-all raw frame handler: PassthroughSession delivers
        # raw frames. We decode them here and dispatch to Python-side typed handlers.
        def _frame_dispatcher(peer_id, type_id, type_name, raw):
            try:
                messages = self._session.decode_frame(raw)
                for dm in messages:
                    tid = dm['type_id']
                    payload = dm['payload']
                    handlers = self._session_handlers.get(tid, [])
                    for h in handlers:
                        h(peer_id, payload)
            except Exception:
                pass  # frame decode failed

        self._register_message_handler(0, _frame_dispatcher, any_message=True)

    @staticmethod
    def _probe_skip_bits(session, header_size: int) -> int:
        for skip_bytes in range(header_size):
            header = bytearray(header_size)
            test_len = 42
            if header_size - skip_bytes >= 2:
                # Try big-endian
                header[skip_bytes] = (test_len >> 8) & 0xFF
                header[skip_bytes + 1] = test_len & 0xFF
                if session.extract_frame_length(bytes(header)) == test_len:
                    return skip_bytes * 8
                # Try little-endian
                header[skip_bytes] = test_len & 0xFF
                header[skip_bytes + 1] = (test_len >> 8) & 0xFF
                if session.extract_frame_length(bytes(header)) == test_len:
                    return skip_bytes * 8
        return (header_size - 2) * 8

    @staticmethod
    def _probe_endianness(session, skip_bits: int, field_bits: int) -> bool:
        skip_bytes = skip_bits // 8
        field_bytes = (field_bits + 7) // 8
        header_size = skip_bytes + field_bytes
        header = bytearray(header_size)
        test_len = 0x0102
        # Big-endian encoding
        header[skip_bytes] = 0x01
        if field_bytes > 1:
            header[skip_bytes + 1] = 0x02
        result = session.extract_frame_length(bytes(header))
        return result == test_len

    def add_peer(self, name: str, session_name: str,
                 transport) -> int:
        """Add a peer to the transceiver. Returns peer ID.

        Args:
            name:         Human-readable peer name.
            session_name: Registered session type name.
            transport:    Transport configuration — any of :class:`UdpConfig`,
                          :class:`TcpClientConfig`, :class:`TcpServerConfig`,
                          :class:`SerialConfig`, or the legacy
                          :class:`TransportConfig`.
        """
        cfg = _CONDUIT_TRANSPORT_CONFIG()
        cfg.type    = int(transport.type)
        _address = getattr(transport, "address", "") or ""
        if _address:
            cfg.address = _address.encode("utf-8")

        # Common optional fields
        cfg.recv_buffer_size = getattr(transport, "recv_buffer_size", 0) or 0

        if isinstance(transport, TcpClientConfig):
            cfg.connect_timeout_ms = transport.connect_timeout_ms
            rp = transport.reconnect
            if rp is None:
                cfg.reconnect_enabled = -1   # disabled
            elif not rp.enabled:
                cfg.reconnect_enabled = -1   # disabled
            else:
                cfg.reconnect_enabled          = 1
                cfg.reconnect_initial_delay_ms = rp.initial_delay_ms
                cfg.reconnect_max_delay_ms     = rp.max_delay_ms
                cfg.reconnect_backoff_multiplier = rp.backoff_multiplier
                cfg.reconnect_max_attempts     = rp.max_attempts

        elif isinstance(transport, UdpConfig):
            if transport.bind_address:
                cfg.bind_address = transport.bind_address.encode("utf-8")
            cfg.bind_port         = transport.bind_port
            cfg.remote_port       = transport.remote_port
            cfg.max_datagram_size = transport.max_datagram_size
            cfg.max_peers         = transport.max_peers
            cfg.peer_timeout_s    = transport.peer_timeout_s

        elif isinstance(transport, TcpServerConfig):
            cfg.max_clients = transport.max_clients

        elif isinstance(transport, SerialConfig):
            cfg.baud_rate   = transport.baud_rate
            cfg.data_bits   = transport.data_bits
            cfg.parity      = int(transport.parity)
            cfg.stop_bits   = int(transport.stop_bits)
            cfg.flow_control = int(transport.flow_control)

        elif isinstance(transport, TransportConfig):
            # Legacy plain TransportConfig
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

    # ========================================================================
    # Send — typed message API (matches C++ UX)
    # ========================================================================

    def send(self, peer_id_or_msg, msg=None) -> None:
        """Send a typed message to a peer.

        Usage:
            t.send(peer_id, msg)    # explicit peer
            t.send(msg)             # sole peer (convenience)

        The message must be a bgen-generated object with TYPE_ID and
        encode_bytes(). This mirrors the C++ transceiver.send<T>(msg) API.
        """
        if msg is None:
            # sole-peer convenience: send(msg)
            msg = peer_id_or_msg
            if not _is_message_instance(msg):
                raise TypeError(
                    f"Expected a message object with TYPE_ID and encode_bytes(), "
                    f"got {type(msg).__name__}"
                )
            peer_id = self.sole_peer()
        else:
            peer_id = peer_id_or_msg
            if not _is_message_instance(msg):
                raise TypeError(
                    f"Expected a message object with TYPE_ID and encode_bytes(), "
                    f"got {type(msg).__name__}"
                )

        type_id = msg.TYPE_ID

        if self._session is not None:
            # Passthrough mode: session wraps message into a framed data block
            result = self._session.encode_wrap(type_id, msg)
            if result is None:
                raise ConduitError(-1, f"Session encode_wrap failed for type_id=0x{type_id:x}")
            data = result['bytes']
        else:
            data = msg.encode_bytes()

        self.send_raw(peer_id, type_id, data)

    def send_raw(self, peer_id: int, type_id: int, data: bytes) -> None:
        """Send raw bytes to a peer (low-level API)."""
        buf = (ctypes.c_uint8 * len(data))(*data)
        err = self._lib.conduit_send(
            self._handle, peer_id, type_id, buf, len(data))
        if err != 0:
            raise ConduitError(err, "send failed")

    def send_batch(self, peer_id: int, type_id: int,
                   payloads: list[bytes]) -> None:
        """Send a batch of messages to a peer."""
        count = len(payloads)
        if count == 0:
            return

        ArrayOfPtr = ctypes.POINTER(ctypes.c_uint8) * count
        ArrayOfLen = ctypes.c_size_t * count

        buffers = []
        ptrs = ArrayOfPtr()
        lens = ArrayOfLen()

        for i, payload in enumerate(payloads):
            buf = (ctypes.c_uint8 * len(payload))(*payload)
            buffers.append(buf)  # prevent GC
            ptrs[i] = ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8))
            lens[i] = len(payload)

        err = self._lib.conduit_send_batch(
            self._handle, peer_id, type_id,
            ctypes.cast(ptrs, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8))),
            ctypes.cast(lens, ctypes.POINTER(ctypes.c_size_t)),
            count)
        if err != 0:
            raise ConduitError(err, "send_batch failed")

    # ========================================================================
    # Receive — typed handler registration (matches C++ UX)
    # ========================================================================

    def on(self, msg_class=None, *, type_id: Optional[int] = None):
        """Decorator for registering a message handler.

        Typed handler (auto-deserialization, matches C++ UX):
            @t.on(PingBody)
            def handle_ping(peer_id, msg):
                print(msg.timestamp)   # msg is already a PingBody instance

        Raw handler (explicit type_id, receives raw bytes):
            @t.on(type_id=0x1234)
            def handle_msg(peer_id, type_id, type_name, data):
                print(data.hex())
        """
        if msg_class is not None and type_id is None:
            # Typed handler: @t.on(PingBody)
            if not _is_message_class(msg_class):
                raise TypeError(
                    f"Expected a message class with TYPE_ID, encode_bytes, "
                    f"and decode_bytes, got {msg_class!r}"
                )
            resolved_type_id = msg_class.TYPE_ID

            def decorator(func):
                if self._session is not None:
                    # Passthrough mode: register in Python-side handler dict.
                    # The frame dispatcher will call these with decoded payloads.
                    self._session_handlers.setdefault(resolved_type_id, []).append(func)
                else:
                    # Direct mode: register at the C level with auto-decode
                    def _typed_handler(peer_id, tid, type_name, raw):
                        try:
                            decoded = msg_class.decode_bytes(raw)
                        except Exception:
                            return
                        func(peer_id, decoded)
                    self._register_message_handler(resolved_type_id, _typed_handler)
                return func

            return decorator

        # Raw handler: @t.on(type_id=0x1234)
        if type_id is not None:
            resolved_type_id = type_id
        elif msg_class is not None:
            resolved_type_id = getattr(msg_class, "TYPE_ID", 0)
        else:
            resolved_type_id = 0

        def decorator(func):
            self._register_message_handler(resolved_type_id, func)
            return func

        return decorator

    def on_any(self, func: Callable) -> int:
        """Register a catch-all message handler."""
        return self._register_message_handler(0, func, any_message=True)

    # ========================================================================
    # State & error callbacks
    # ========================================================================

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

    # ========================================================================
    # Query
    # ========================================================================

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

    # ========================================================================
    # Statistics
    # ========================================================================

    def stats(self) -> Stats:
        """Get a snapshot of transceiver statistics.

        Returns a Stats namedtuple with:
            messages_received, messages_dispatched, messages_dropped,
            decode_errors, handler_errors, handler_timeouts,
            bytes_received, bytes_sent
        """
        snap = _CONDUIT_STATS()
        err = self._lib.conduit_stats(self._handle, ctypes.byref(snap))
        if err != 0:
            raise ConduitError(err, "stats failed")
        return Stats(
            messages_received=snap.messages_received,
            messages_dispatched=snap.messages_dispatched,
            messages_dropped=snap.messages_dropped,
            decode_errors=snap.decode_errors,
            handler_errors=snap.handler_errors,
            handler_timeouts=snap.handler_timeouts,
            bytes_received=snap.bytes_received,
            bytes_sent=snap.bytes_sent,
        )

    def stats_reset(self) -> None:
        """Reset all statistics counters to zero."""
        err = self._lib.conduit_stats_reset(self._handle)
        if err != 0:
            raise ConduitError(err, "stats_reset failed")

    # ========================================================================
    # Handler/callback removal
    # ========================================================================

    def remove_handler(self, peer_id: int, type_id: int) -> bool:
        """Remove a message handler. Returns True if removed."""
        return bool(self._lib.conduit_remove_handler(
            self._handle, peer_id, type_id))

    def remove_state_change(self, callback_id: int) -> bool:
        """Remove a state change callback. Returns True if removed."""
        return bool(self._lib.conduit_remove_state_change(
            self._handle, callback_id))

    def remove_error_callback(self, callback_id: int) -> bool:
        """Remove an error callback. Returns True if removed."""
        return bool(self._lib.conduit_remove_error_callback(
            self._handle, callback_id))

    @staticmethod
    def version() -> str:
        """Get the library version string."""
        lib = _get_lib()
        v = lib.conduit_version()
        return v.decode("utf-8") if v else ""

    # ========================================================================
    # Internal
    # ========================================================================

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
