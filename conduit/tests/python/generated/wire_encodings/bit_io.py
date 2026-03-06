"""Bit-level I/O for binary protocols. Wire-compatible with conduit C++ BitReader/BitWriter."""
from __future__ import annotations
import struct
from typing import Optional


class ConduitError(Exception):
    """Base error for conduit codec operations."""
    pass


class DecodeError(ConduitError):
    """Raised when decoding fails."""
    pass


class EncodeError(ConduitError):
    """Raised when encoding fails."""
    pass


class ConstraintError(ConduitError):
    """Raised on constraint violation."""
    pass


# EBCDIC Code Page 037 <-> ASCII full 256-entry conversion tables
_EBCDIC_TO_ASCII = bytearray([
    0x00,0x01,0x02,0x03,0x1A,0x09,0x1A,0x7F, 0x1A,0x1A,0x1A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x1A,0x0A,0x08,0x1A, 0x18,0x19,0x1A,0x1A,0x1C,0x1D,0x1E,0x1F,
    0x1A,0x1A,0x1A,0x1A,0x1A,0x0A,0x17,0x1B, 0x1A,0x1A,0x1A,0x1A,0x1A,0x05,0x06,0x07,
    0x1A,0x1A,0x16,0x1A,0x1A,0x1A,0x1A,0x04, 0x1A,0x1A,0x1A,0x1A,0x14,0x15,0x1A,0x1A,
    0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5, 0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
    0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF, 0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0xAC,
    0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5, 0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
    0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF, 0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
    0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67, 0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
    0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70, 0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
    0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78, 0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
    0x5E,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC, 0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
    0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47, 0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
    0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50, 0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
    0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58, 0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, 0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x9F,
])
_ASCII_TO_EBCDIC = bytearray([
    0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F, 0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26, 0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
    0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D, 0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7, 0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
    0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7, 0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
    0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6, 0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0xB0,0x6D,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6, 0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
    0x20,0x21,0x22,0x23,0x24,0x15,0x06,0x17, 0x28,0x29,0x2A,0x2B,0x2C,0x09,0x0A,0x1B,
    0x30,0x31,0x1A,0x33,0x34,0x35,0x36,0x08, 0x38,0x39,0x3A,0x3B,0x04,0x14,0x3E,0xFF,
    0x41,0xAA,0x4A,0xB1,0x9F,0xB2,0x6A,0xB5, 0xBB,0xB4,0x9A,0x8A,0xB0,0xCA,0xAF,0xBC,
    0x90,0x8F,0xEA,0xFA,0xBE,0xA0,0xB6,0xB3, 0x9D,0xDA,0x9B,0x8B,0xB7,0xB8,0xB9,0xAB,
    0x64,0x65,0x62,0x66,0x63,0x67,0x9E,0x68, 0x74,0x71,0x72,0x73,0x78,0x75,0x76,0x77,
    0xAC,0x69,0xED,0xEE,0xEB,0xEF,0xEC,0xBF, 0x80,0xFD,0xFE,0xFB,0xFC,0xBA,0xAE,0x59,
    0x44,0x45,0x42,0x46,0x43,0x47,0x9C,0x48, 0x54,0x51,0x52,0x53,0x58,0x55,0x56,0x57,
    0x8C,0x49,0xCD,0xCE,0xCB,0xCF,0xCC,0xE1, 0x70,0xDD,0xDE,0xDB,0xDC,0x8D,0x8E,0xDF,
])


class BitReader:
    """Read individual bits and multi-byte values from a bytes buffer."""

    __slots__ = ('_data', '_bit_pos', '_bit_len')

    def __init__(self, data: bytes | bytearray | memoryview) -> None:
        self._data = bytes(data)
        self._bit_pos = 0
        self._bit_len = len(self._data) * 8

    def remaining_bits(self) -> int:
        return max(0, self._bit_len - self._bit_pos)

    def remaining_bytes(self) -> int:
        return self.remaining_bits() // 8

    def _check(self, n: int) -> None:
        if self._bit_pos + n > self._bit_len:
            raise DecodeError(f"underflow: need {n} bits, have {self.remaining_bits()}")

    def read_bits(self, n: int) -> int:
        self._check(n)
        val = 0
        remaining = n
        while remaining > 0:
            byte_idx = self._bit_pos // 8
            bit_off = self._bit_pos % 8
            avail = 8 - bit_off
            take = min(avail, remaining)
            mask = ((1 << take) - 1) << (avail - take)
            bits = (self._data[byte_idx] & mask) >> (avail - take)
            val = (val << take) | bits
            self._bit_pos += take
            remaining -= take
        return val

    def read_signed_bits(self, n: int) -> int:
        val = self.read_bits(n)
        if n > 0 and (val >> (n - 1)) & 1:
            val -= (1 << n)
        return val

    def read_u8(self) -> int:
        return self.read_bits(8)

    def read_u16(self, big_endian: bool = True) -> int:
        b0 = self.read_bits(8)
        b1 = self.read_bits(8)
        return struct.unpack('>H' if big_endian else '<H', bytes([b0, b1]))[0]

    def read_u32(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(4)])
        return struct.unpack('>I' if big_endian else '<I', b)[0]

    def read_u64(self, big_endian: bool = True) -> int:
        b = bytes([self.read_bits(8) for _ in range(8)])
        return struct.unpack('>Q' if big_endian else '<Q', b)[0]

    def read_f32(self, big_endian: bool = True) -> float:
        b = bytes([self.read_bits(8) for _ in range(4)])
        return struct.unpack('>f' if big_endian else '<f', b)[0]

    def read_f64(self, big_endian: bool = True) -> float:
        b = bytes([self.read_bits(8) for _ in range(8)])
        return struct.unpack('>d' if big_endian else '<d', b)[0]

    def read_string(self, length: int, encoding: int = 0) -> str:
        b = bytearray(self.read_bits(8) for _ in range(length))
        if encoding == 2:
            b = bytearray(_EBCDIC_TO_ASCII[c] for c in b)
        elif encoding == 1:
            b = bytearray(c & 0x7F for c in b)
        return bytes(b).decode('latin-1')

    def read_packed_chars(self, count: int, char_bits: int) -> str:
        chars = []
        for _ in range(count):
            c = self.read_bits(char_bits)
            if char_bits < 7:
                chars.append(chr(0 if c == 0 else (c + 0x40 if c < 32 else c)))
            else:
                chars.append(chr(c))
        return ''.join(chars)

    def read_terminated_string(self, terminator: int, max_len: int) -> str:
        chars = []
        for _ in range(max_len):
            c = self.read_bits(8)
            if c == terminator:
                break
            chars.append(chr(c))
        return ''.join(chars)

    def read_crlf_terminated_string(self, max_len: int) -> str:
        chars = []
        prev = 0
        for _ in range(max_len):
            c = self.read_bits(8)
            if prev == 0x0D and c == 0x0A:
                chars.pop()
                break
            chars.append(chr(c))
            prev = c
        return ''.join(chars)

    def read_bytes(self, length: int) -> bytes:
        return bytes(self.read_bits(8) for _ in range(length))

    def read_bcd(self, bits: int) -> int:
        raw = self.read_bits(bits)
        result = 0
        mult = 1
        for i in range(bits // 4):
            digit = (raw >> (i * 4)) & 0xF
            result += digit * mult
            mult *= 10
        return result

    def read_bcd_signed(self, bits: int) -> int:
        sign_bit = self.read_bits(1)
        val = self.read_bcd(bits - 1)
        return -val if sign_bit else val

    def read_sign_magnitude(self, bits: int) -> int:
        sign_bit = self.read_bits(1)
        magnitude = self.read_bits(bits - 1)
        return -magnitude if sign_bit else magnitude

    def skip_bits(self, n: int) -> None:
        self._check(n)
        self._bit_pos += n

    def sub_reader(self, byte_count: int) -> 'BitReader':
        return BitReader(self.read_bytes(byte_count))

    def align_to(self, boundary: int) -> None:
        if boundary <= 0:
            return
        rem = self._bit_pos % 8
        if rem != 0:
            self._bit_pos += (8 - rem)
        byte_pos = self._bit_pos // 8
        b_rem = byte_pos % boundary
        if b_rem != 0:
            self._bit_pos += (boundary - b_rem) * 8
        if self._bit_pos > self._bit_len:
            self._bit_pos = self._bit_len


class BitWriter:
    """Write individual bits and multi-byte values to a growable buffer."""

    __slots__ = ('_buf', '_bit_pos')

    def __init__(self) -> None:
        self._buf = bytearray()
        self._bit_pos = 0

    def _ensure(self, n: int) -> None:
        needed = (self._bit_pos + n + 7) // 8
        while len(self._buf) < needed:
            self._buf.append(0)

    def write_bits(self, value: int, n: int) -> None:
        self._ensure(n)
        remaining = n
        while remaining > 0:
            byte_idx = self._bit_pos // 8
            bit_off = self._bit_pos % 8
            avail = 8 - bit_off
            take = min(avail, remaining)
            shift = remaining - take
            bits = (value >> shift) & ((1 << take) - 1)
            self._buf[byte_idx] |= bits << (avail - take)
            self._bit_pos += take
            remaining -= take

    def write_signed_bits(self, value: int, n: int) -> None:
        if value < 0:
            value = value + (1 << n)
        self.write_bits(value & ((1 << n) - 1), n)

    def write_u8(self, value: int) -> None:
        self.write_bits(value & 0xFF, 8)

    def write_u16(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>H' if big_endian else '<H', value & 0xFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_u32(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>I' if big_endian else '<I', value & 0xFFFFFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_u64(self, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>Q' if big_endian else '<Q', value & 0xFFFFFFFFFFFFFFFF)
        for byte in b:
            self.write_bits(byte, 8)

    def write_f32(self, value: float, big_endian: bool = True) -> None:
        for byte in struct.pack('>f' if big_endian else '<f', value):
            self.write_bits(byte, 8)

    def write_f64(self, value: float, big_endian: bool = True) -> None:
        for byte in struct.pack('>d' if big_endian else '<d', value):
            self.write_bits(byte, 8)

    def write_string(self, s: str, length: int, pad: int = 0, encoding: int = 0) -> None:
        b = bytearray(s.encode('latin-1'))
        if encoding == 2:
            b = bytearray(_ASCII_TO_EBCDIC[c] for c in b)
        elif encoding == 1:
            b = bytearray(c & 0x7F for c in b)
        for i in range(length):
            self.write_u8(b[i] if i < len(b) else pad)

    def write_packed_chars(self, s: str, count: int, char_bits: int) -> None:
        for i in range(count):
            c = ord(s[i]) if i < len(s) else 0x20
            if char_bits < 7 and ord('a') <= c <= ord('z'):
                c -= 32
            self.write_bits(c - 0x40 if c >= 0x40 else c, char_bits)

    def write_terminated_string(self, s: str, terminator: int) -> None:
        for c in s:
            self.write_u8(ord(c))
        self.write_u8(terminator)

    def write_crlf_terminated_string(self, s: str) -> None:
        for c in s:
            self.write_u8(ord(c))
        self.write_u8(0x0D)
        self.write_u8(0x0A)

    def write_bytes(self, data: bytes | bytearray) -> None:
        for b in data:
            self.write_u8(b)

    def write_bcd(self, value: int, bits: int) -> None:
        max_val = 10 ** (bits // 4) - 1
        if abs(value) > max_val:
            raise EncodeError(f"BCD overflow: {value} exceeds {bits // 4}-digit max ({max_val})")
        raw = 0
        v = abs(value)
        for i in range(bits // 4):
            raw |= (v % 10) << (i * 4)
            v //= 10
        self.write_bits(raw, bits)

    def write_bcd_signed(self, value: int, bits: int) -> None:
        self.write_bits(1 if value < 0 else 0, 1)
        self.write_bcd(abs(value), bits - 1)

    def write_sign_magnitude(self, value: int, bits: int) -> None:
        self.write_bits(1 if value < 0 else 0, 1)
        self.write_bits(abs(value), bits - 1)

    def size_bytes(self) -> int:
        return (self._bit_pos + 7) // 8

    def to_bytes(self) -> bytes:
        return bytes(self._buf[:self.size_bytes()])

    def align_to(self, boundary: int) -> None:
        rem = self.size_bytes() % boundary
        if rem != 0:
            for _ in range(boundary - rem):
                self.write_u8(0)

    def patch_u8(self, offset: int, value: int) -> None:
        if offset < len(self._buf):
            self._buf[offset] = value & 0xFF

    def patch_u16(self, offset: int, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>H' if big_endian else '<H', value & 0xFFFF)
        for i, byte in enumerate(b):
            if offset + i < len(self._buf):
                self._buf[offset + i] = byte

    def patch_u32(self, offset: int, value: int, big_endian: bool = True) -> None:
        b = struct.pack('>I' if big_endian else '<I', value & 0xFFFFFFFF)
        for i, byte in enumerate(b):
            if offset + i < len(self._buf):
                self._buf[offset + i] = byte
