"""Conduit codec-only C ABI bindings via ctypes.

Wraps libconduit_codec_cabi for pure codec operations (no transport/threading).
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import sys
from dataclasses import dataclass
from typing import Optional


# ============================================================================
# C type definitions
# ============================================================================

class _DecodedMsg(ctypes.Structure):
    _fields_ = [
        ("type_id", ctypes.c_uint64),
        ("type_name", ctypes.c_char_p),
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("data_len", ctypes.c_size_t),
    ]

class _EncodeResult(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("data_len", ctypes.c_size_t),
    ]

class _Frame(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("data_len", ctypes.c_size_t),
    ]


# ============================================================================
# Library loading
# ============================================================================

def _load_codec_lib() -> ctypes.CDLL:
    """Load the conduit_codec_cabi shared library."""
    # Search in common locations
    search_paths = [
        os.environ.get("CONDUIT_CODEC_LIB", ""),
        os.path.join(os.path.dirname(__file__), "..", "..", "lib"),
        os.path.join(os.path.dirname(__file__), ".."),
    ]

    names = ["libconduit_codec_cabi.so", "libconduit_codec_cabi.dylib",
             "conduit_codec_cabi.dll"]

    for search_dir in search_paths:
        if not search_dir:
            continue
        for name in names:
            path = os.path.join(search_dir, name)
            if os.path.isfile(path):
                return ctypes.CDLL(path)

    # Try system library path
    lib_path = ctypes.util.find_library("conduit_codec_cabi")
    if lib_path:
        return ctypes.CDLL(lib_path)

    raise OSError(
        "Cannot find libconduit_codec_cabi. Set CONDUIT_CODEC_LIB environment "
        "variable to the path of the shared library."
    )


_lib: Optional[ctypes.CDLL] = None

def _get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        _lib = _load_codec_lib()
        _setup_signatures(_lib)
    return _lib


def _setup_signatures(lib: ctypes.CDLL) -> None:
    """Set up ctypes function signatures."""
    # Session lifecycle
    lib.conduit_session_create.argtypes = [ctypes.c_char_p]
    lib.conduit_session_create.restype = ctypes.c_void_p

    lib.conduit_session_destroy.argtypes = [ctypes.c_void_p]
    lib.conduit_session_destroy.restype = None

    lib.conduit_session_reset.argtypes = [ctypes.c_void_p]
    lib.conduit_session_reset.restype = None

    # Decode
    lib.conduit_decode_frame.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(_DecodedMsg)), ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.conduit_decode_frame.restype = ctypes.c_int32

    lib.conduit_free_decoded_msgs.argtypes = [ctypes.POINTER(_DecodedMsg), ctypes.c_size_t]
    lib.conduit_free_decoded_msgs.restype = None

    # Encode
    lib.conduit_encode_message.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
        ctypes.POINTER(_EncodeResult),
    ]
    lib.conduit_encode_message.restype = ctypes.c_int32

    lib.conduit_free_encode_result.argtypes = [ctypes.POINTER(_EncodeResult)]
    lib.conduit_free_encode_result.restype = None

    # Introspection
    lib.conduit_session_type_name.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.conduit_session_type_name.restype = ctypes.c_char_p

    lib.conduit_session_leaf_type_count.argtypes = [ctypes.c_void_p]
    lib.conduit_session_leaf_type_count.restype = ctypes.c_size_t

    lib.conduit_session_leaf_type_ids.argtypes = [ctypes.c_void_p]
    lib.conduit_session_leaf_type_ids.restype = ctypes.POINTER(ctypes.c_uint64)

    lib.conduit_session_is_receive_only.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.conduit_session_is_receive_only.restype = ctypes.c_int

    lib.conduit_session_protocol_name.argtypes = [ctypes.c_void_p]
    lib.conduit_session_protocol_name.restype = ctypes.c_char_p

    # Framing
    lib.conduit_framer_create.argtypes = [ctypes.c_void_p]
    lib.conduit_framer_create.restype = ctypes.c_void_p

    lib.conduit_framer_destroy.argtypes = [ctypes.c_void_p]
    lib.conduit_framer_destroy.restype = None

    lib.conduit_framer_feed.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
        ctypes.POINTER(ctypes.POINTER(_Frame)), ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.conduit_framer_feed.restype = ctypes.c_int32

    lib.conduit_free_frames.argtypes = [ctypes.POINTER(_Frame), ctypes.c_size_t]
    lib.conduit_free_frames.restype = None

    # Version
    lib.conduit_codec_version.argtypes = []
    lib.conduit_codec_version.restype = ctypes.c_char_p


# ============================================================================
# Python wrapper classes
# ============================================================================

@dataclass
class DecodedMessage:
    """A decoded message from the codec."""
    type_id: int
    type_name: str
    data: bytes


class ConduitCodecError(Exception):
    """Error from the codec C ABI."""
    def __init__(self, code: int, message: str = ""):
        self.code = code
        super().__init__(f"Conduit codec error {code}: {message}")


class CodecSession:
    """Wrapper around a conduit codec session (opaque handle).

    Usage:
        session = CodecSession("my_protocol")
        messages = session.decode_frame(raw_bytes)
        encoded = session.encode_message(type_id, payload_bytes)
    """

    def __init__(self, session_type: str):
        lib = _get_lib()
        self._handle = lib.conduit_session_create(session_type.encode("utf-8"))
        if not self._handle:
            raise ConduitCodecError(-4, f"Unknown session type: {session_type}")
        self._lib = lib

    def __del__(self):
        self.close()

    def close(self) -> None:
        if hasattr(self, "_handle") and self._handle:
            self._lib.conduit_session_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def reset(self) -> None:
        """Reset session state (sequence counters, etc.)."""
        if self._handle:
            self._lib.conduit_session_reset(self._handle)

    def decode_frame(self, data: bytes) -> list[DecodedMessage]:
        """Decode a frame into a list of messages."""
        buf = (ctypes.c_uint8 * len(data))(*data)
        msgs_ptr = ctypes.POINTER(_DecodedMsg)()
        count = ctypes.c_size_t(0)

        err = self._lib.conduit_decode_frame(
            self._handle, buf, len(data),
            ctypes.byref(msgs_ptr), ctypes.byref(count))

        if err != 0:
            raise ConduitCodecError(err, "decode_frame failed")

        results = []
        for i in range(count.value):
            msg = msgs_ptr[i]
            raw = bytes(msg.data[j] for j in range(msg.data_len)) if msg.data else b""
            name = msg.type_name.decode("utf-8") if msg.type_name else ""
            results.append(DecodedMessage(
                type_id=msg.type_id,
                type_name=name,
                data=raw,
            ))

        if msgs_ptr and count.value > 0:
            self._lib.conduit_free_decoded_msgs(msgs_ptr, count)

        return results

    def encode_message(self, type_id: int, payload: bytes) -> bytes:
        """Encode a message, returning wire bytes."""
        buf = (ctypes.c_uint8 * len(payload))(*payload)
        result = _EncodeResult()

        err = self._lib.conduit_encode_message(
            self._handle, type_id, buf, len(payload),
            ctypes.byref(result))

        if err != 0:
            raise ConduitCodecError(err, "encode_message failed")

        wire_bytes = bytes(result.data[i] for i in range(result.data_len)) if result.data else b""
        self._lib.conduit_free_encode_result(ctypes.byref(result))
        return wire_bytes

    def type_name(self, type_id: int) -> str:
        """Get the type name for a given type ID."""
        name = self._lib.conduit_session_type_name(self._handle, type_id)
        return name.decode("utf-8") if name else ""

    def leaf_type_ids(self) -> list[int]:
        """Get all leaf type IDs for this session."""
        count = self._lib.conduit_session_leaf_type_count(self._handle)
        if count == 0:
            return []
        ids_ptr = self._lib.conduit_session_leaf_type_ids(self._handle)
        return [ids_ptr[i] for i in range(count)]

    def is_receive_only(self, type_id: int) -> bool:
        """Check if a type is receive-only."""
        return bool(self._lib.conduit_session_is_receive_only(self._handle, type_id))

    def protocol_name(self) -> str:
        """Get the protocol name."""
        name = self._lib.conduit_session_protocol_name(self._handle)
        return name.decode("utf-8") if name else ""


class CodecFramer:
    """Stream framer wrapping the C ABI conduit_framer_t.

    Feed bytes and extract complete frames.
    """

    def __init__(self, session: CodecSession):
        lib = _get_lib()
        self._handle = lib.conduit_framer_create(session._handle)
        if not self._handle:
            raise ConduitCodecError(-99, "Failed to create framer")
        self._lib = lib

    def __del__(self):
        self.close()

    def close(self) -> None:
        if hasattr(self, "_handle") and self._handle:
            self._lib.conduit_framer_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def feed(self, data: bytes) -> list[bytes]:
        """Feed bytes and extract complete frames."""
        buf = (ctypes.c_uint8 * len(data))(*data)
        frames_ptr = ctypes.POINTER(_Frame)()
        count = ctypes.c_size_t(0)

        err = self._lib.conduit_framer_feed(
            self._handle, buf, len(data),
            ctypes.byref(frames_ptr), ctypes.byref(count))

        if err != 0:
            raise ConduitCodecError(err, "framer_feed failed")

        results = []
        for i in range(count.value):
            frame = frames_ptr[i]
            frame_bytes = bytes(frame.data[j] for j in range(frame.data_len)) if frame.data else b""
            results.append(frame_bytes)

        if frames_ptr and count.value > 0:
            self._lib.conduit_free_frames(frames_ptr, count)

        return results
