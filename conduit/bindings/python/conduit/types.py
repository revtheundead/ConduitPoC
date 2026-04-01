"""Conduit transport and logging configuration types."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional


# ============================================================================
# Transport type enumeration
# ============================================================================

class TransportType(IntEnum):
    """Transport type enumeration matching conduit_transport_type_t."""
    UDP = 0
    TCP_CLIENT = 1
    TCP_SERVER = 2
    SERIAL = 3


# ============================================================================
# Message-log enums (mirror C++ MessageLogMode / MessageLogOutput)
# ============================================================================

class MessageLogMode(IntEnum):
    """Message-log grouping mode (mirrors C++ ``MessageLogMode``)."""
    COMBINED         = 0  #: All messages in a single file.
    SEPARATE_DIRECTION = 1  #: Separate files for sent and received.
    PER_PEER         = 2  #: One file per peer (both directions).
    PER_PEER_DIRECTION = 3  #: Separate sent/received files per peer.


class MessageLogOutput(IntEnum):
    """Message-log output destination (mirrors C++ ``MessageLogOutput``)."""
    FILE   = 0  #: Write to log files.
    STDOUT = 1  #: Write to stdout.
    BOTH   = 2  #: Write to both files and stdout.


# ============================================================================
# Serial configuration enums
# ============================================================================

class SerialParity(IntEnum):
    """Serial parity (mirrors C++ ``conduit::transceiver::transport::Parity``)."""
    NONE = 0
    ODD  = 1
    EVEN = 2


class SerialStopBits(IntEnum):
    """Serial stop-bits (mirrors C++ ``StopBits``)."""
    ONE = 0
    TWO = 1


class SerialFlowControl(IntEnum):
    """Serial flow-control (mirrors C++ ``FlowControl``)."""
    NONE     = 0
    HARDWARE = 1
    SOFTWARE = 2


# ============================================================================
# Reconnect policy (mirrors C++ ReconnectPolicy)
# ============================================================================

@dataclass
class ReconnectPolicy:
    """Reconnect policy for TCP client (mirrors C++ ``ReconnectPolicy``).

    All fields default to the C++ library defaults; set only what you need.

    Args:
        enabled:            Whether reconnect is enabled (default True).
        initial_delay_ms:   Initial backoff delay in ms (0 = C++ default 1000 ms).
        max_delay_ms:       Maximum backoff delay in ms (0 = C++ default 30000 ms).
        backoff_multiplier: Backoff multiplier (0.0 = C++ default 2.0).
        max_attempts:       Maximum reconnect attempts (0 = unlimited, C++ default).
    """
    enabled:            bool  = True
    initial_delay_ms:   int   = 0
    max_delay_ms:       int   = 0
    backoff_multiplier: float = 0.0
    max_attempts:       int   = 0


# ============================================================================
# Per-transport configuration dataclasses
# ============================================================================

@dataclass
class UdpConfig:
    """UDP transport configuration (mirrors C++ ``UdpConfig``).

    Args:
        address:          Remote ``"host:port"`` string (may also be set via
                          ``remote_address`` + ``remote_port``).
        bind_address:     Local bind address; ``None`` = ``"0.0.0.0"``.
        bind_port:        Local bind port; 0 = ephemeral.
        remote_address:   Explicit remote host (overrides *address* host part).
        remote_port:      Explicit remote port; 0 = use port from *address*.
        recv_buffer_size: Socket receive buffer; 0 = use default (65536).
        max_datagram_size: Maximum datagram size; 0 = use default (65507).
        max_peers:        Maximum tracked peers; 0 = use default (1024).
        peer_timeout_s:   Inactivity timeout in seconds; 0 = no timeout.
    """
    address:          str           = ""
    bind_address:     Optional[str] = None
    bind_port:        int           = 0
    remote_address:   str           = ""
    remote_port:      int           = 0
    recv_buffer_size: int           = 0
    send_buffer_size: int           = 0
    max_datagram_size: int          = 0
    max_peers:        int           = 0
    peer_timeout_s:   int           = 0
    multicast_group:     str           = ""    # Multicast group IP (empty = unicast)
    multicast_interface: str           = ""    # NIC to join/send on (empty = OS default)
    multicast_ttl:       int           = 0     # 0 = use default (1)
    multicast_loop:      Optional[bool] = None # None = use default (True)

    @property
    def type(self) -> TransportType:
        return TransportType.UDP


@dataclass
class TcpClientConfig:
    """TCP client transport configuration (mirrors C++ ``TcpClientConfig``).

    Args:
        address:           Remote ``"host:port"`` string.
        recv_buffer_size:  Receive buffer size; 0 = use default (65536).
        connect_timeout_ms: Connect timeout in ms; 0 = use default (10000 ms).
        reconnect:         Reconnect policy; ``None`` = use C++ defaults (enabled).
    """
    address:            str                      = ""
    recv_buffer_size:   int                      = 0
    connect_timeout_ms: int                      = 0
    reconnect:          Optional[ReconnectPolicy] = field(default_factory=ReconnectPolicy)

    @property
    def type(self) -> TransportType:
        return TransportType.TCP_CLIENT


@dataclass
class TcpServerConfig:
    """TCP server transport configuration (mirrors C++ ``TcpServerConfig``).

    Args:
        address:          Bind ``":port"`` or ``"host:port"`` string.
        max_clients:      Maximum simultaneous clients; 0 = use default (64).
        recv_buffer_size: Per-client receive buffer; 0 = use default (65536).
    """
    address:          str = ""
    max_clients:      int = 0
    recv_buffer_size: int = 0

    @property
    def type(self) -> TransportType:
        return TransportType.TCP_SERVER


@dataclass
class SerialConfig:
    """Serial transport configuration (mirrors C++ ``SerialConfig``).

    Args:
        port:             Device path (``"/dev/ttyUSB0"`` or ``"COM3"``).
        baud_rate:        Baud rate (0 = use default 9600).
        data_bits:        Data bits per frame; 0 = use default (8).
        parity:           Parity mode; default ``SerialParity.NONE``.
        stop_bits:        Stop bits; default ``SerialStopBits.ONE``.
        flow_control:     Flow control; default ``SerialFlowControl.NONE``.
        recv_buffer_size: Receive buffer; 0 = use default (4096).
    """
    port:             str              = ""
    baud_rate:        int              = 0
    data_bits:        int              = 0
    parity:           SerialParity     = SerialParity.NONE
    stop_bits:        SerialStopBits   = SerialStopBits.ONE
    flow_control:     SerialFlowControl = SerialFlowControl.NONE
    recv_buffer_size: int              = 0

    @property
    def type(self) -> TransportType:
        return TransportType.SERIAL


# ============================================================================
# Convenience aliases — plain "host:port" form matching old API
# ============================================================================

# Keep backward-compatible: a plain TransportConfig can still be used anywhere.
@dataclass
class TransportConfig:
    """Generic transport configuration (simple form).

    Prefer the concrete sub-types (:class:`UdpConfig`, :class:`TcpClientConfig`,
    :class:`TcpServerConfig`, :class:`SerialConfig`) for full option coverage.
    """
    type: TransportType
    address: str
    baud_rate: int = 0
