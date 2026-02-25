"""Conduit transport configuration types."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class TransportType(IntEnum):
    """Transport type enumeration matching conduit_transport_type_t."""
    UDP = 0
    TCP_CLIENT = 1
    TCP_SERVER = 2
    SERIAL = 3


@dataclass
class TransportConfig:
    """Transport configuration matching conduit_transport_config_t."""
    type: TransportType
    address: str
    baud_rate: int = 0


def UdpConfig(bind: str) -> TransportConfig:
    """Create a UDP transport config. bind format: 'host:port'."""
    return TransportConfig(type=TransportType.UDP, address=bind)


def TcpClientConfig(connect: str) -> TransportConfig:
    """Create a TCP client transport config. connect format: 'host:port'."""
    return TransportConfig(type=TransportType.TCP_CLIENT, address=connect)


def TcpServerConfig(listen: str) -> TransportConfig:
    """Create a TCP server transport config. listen format: ':port' or '0.0.0.0:port'."""
    return TransportConfig(type=TransportType.TCP_SERVER, address=listen)
