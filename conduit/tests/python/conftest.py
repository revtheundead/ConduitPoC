"""Pytest configuration and shared fixtures for Conduit generated Python codec tests."""
import sys
import os
import pytest

# Add the generated code directory to sys.path so packages are importable
_generated_dir = os.path.join(os.path.dirname(__file__), "generated")
if _generated_dir not in sys.path:
    sys.path.insert(0, _generated_dir)

# Patch arrays_choices module-level constants that are referenced as bare names
# in the generated code but only defined inside the Constants class.
import arrays_choices.messages as _acm
import arrays_choices.structs as _acs
from arrays_choices.constants import Constants as _AC_Constants

_acm.TYPE_A = _AC_Constants.TYPE_A
_acm.TYPE_B = _AC_Constants.TYPE_B
_acs.SUB_X = _AC_Constants.SUB_X
_acs.SUB_Y = _AC_Constants.SUB_Y


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def bit_reader_cls():
    """Return the BitReader class from all_types."""
    from all_types.bit_io import BitReader
    return BitReader


@pytest.fixture
def bit_writer_cls():
    """Return the BitWriter class from all_types."""
    from all_types.bit_io import BitWriter
    return BitWriter


@pytest.fixture
def all_types_msg():
    """Return a fully-populated AllTypesMessage with non-default values."""
    from all_types import AllTypesMessage, AsciiStr, Utf8Str, ScaledTemp, ColorEnum, StatusFlags

    msg = AllTypesMessage()
    msg.u8 = 0xAB
    msg.u16 = 0x1234
    msg.u32 = 0xDEADBEEF
    msg.u64 = 0x0102030405060708
    msg.i8 = -42
    msg.i16 = -1000
    msg.i32 = -100000
    msg.f32 = 3.140000104904175  # closest float32 to 3.14
    msg.f64 = -1.5
    msg.flag = True
    msg.ascii = AsciiStr("Hello")
    msg.utf8 = Utf8Str("World")
    msg.raw = 0xCAFEBABE00000000
    msg.le16 = 0x5678
    msg.le32 = 0xABCD1234
    msg.temp = ScaledTemp(4000)  # value = 0.0
    msg.hex = 0xFF00FF00
    msg.color = ColorEnum.GREEN
    msg.status = StatusFlags(0b00000101)  # active + ready
    return msg
