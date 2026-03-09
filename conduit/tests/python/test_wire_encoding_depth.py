"""Deep wire encoding tests matching C++ test_wire_encoding_roundtrip.cpp depth.
Covers: BCD_S overflow, all-negative stress, extreme value stress,
non-zero tight packing, multiple negative/positive BCD heading values,
double-encode idempotency, sign-magnitude and two's complement boundaries.
"""
import pytest

from wire_encodings import WireEncodingMsg


# ---------------------------------------------------------------------------
# BCD_S overflow on encode (C++ "BCD_S overflow on encode" test)
# ---------------------------------------------------------------------------

class TestBcdSOverflow:

    def test_bcd_s_overflow_positive(self):
        """BCD_S heading overflow: value 1000 exceeds 13-bit BCD_S max (999)."""
        msg = WireEncodingMsg()
        msg.bcd_hdg = 1000
        with pytest.raises(Exception):
            msg.encode_bytes()

    def test_bcd_s_overflow_negative(self):
        """BCD_S heading overflow: value -1000 exceeds 13-bit BCD_S max."""
        msg = WireEncodingMsg()
        msg.bcd_hdg = -1000
        with pytest.raises(Exception):
            msg.encode_bytes()


# ---------------------------------------------------------------------------
# All-negative stress test (C++ "all-negative stress roundtrip")
# ---------------------------------------------------------------------------

class TestAllNegativeStress:

    def test_all_signed_fields_at_max_negative(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 0
        msg.bcd_hdg = -999         # max negative 13-bit BCD_S
        msg.sm_offset = -32767     # max negative 16-bit BNR_S
        msg.cb2_val = -32768       # min int16 CB2 (two's complement)
        msg.bnr_val = 0
        msg.inline_bcd = 0
        msg.inline_bnrs = -32767   # max negative 16-bit BNR_S

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 0
        assert decoded.bcd_hdg == -999
        assert decoded.sm_offset == -32767
        assert decoded.cb2_val == -32768
        assert decoded.bnr_val == 0
        assert decoded.inline_bcd == 0
        assert decoded.inline_bnrs == -32767


# ---------------------------------------------------------------------------
# Extreme value stress test (C++ "extreme value stress roundtrip")
# ---------------------------------------------------------------------------

class TestExtremeValueStress:

    def test_all_fields_at_max_simultaneously(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 9999         # max 16-bit BCD
        msg.bcd_hdg = 999          # max positive 13-bit BCD_S
        msg.sm_offset = 32767      # max positive 16-bit BNR_S
        msg.cb2_val = 32767        # max positive int16 CB2
        msg.bnr_val = 65535        # max uint16 BNR
        msg.inline_bcd = 999       # max 12-bit BCD
        msg.inline_bnrs = 32767    # max positive 16-bit BNR_S

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 9999
        assert decoded.bcd_hdg == 999
        assert decoded.sm_offset == 32767
        assert decoded.cb2_val == 32767
        assert decoded.bnr_val == 65535
        assert decoded.inline_bcd == 999
        assert decoded.inline_bnrs == 32767


# ---------------------------------------------------------------------------
# Non-zero tight packing stress (C++ "non-zero tight packing roundtrip")
# Tests bit-boundary correctness after 13-bit BCD_S shifts alignment
# ---------------------------------------------------------------------------

class TestNonZeroTightPacking:

    def test_stress_bit_boundary_correctness(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 9876         # 16-bit BCD
        msg.bcd_hdg = -999         # 13-bit BCD_S — shifts alignment by 5 bits
        msg.sm_offset = -32767     # 16-bit BNR_S, now at bit 29
        msg.cb2_val = -1           # 16-bit CB2, now at bit 45
        msg.bnr_val = 65535        # 16-bit BNR, now at bit 61
        msg.inline_bcd = 888       # 12-bit BCD, now at bit 77
        msg.inline_bnrs = -1000    # 16-bit BNR_S, now at bit 89

        encoded = msg.encode_bytes()
        assert len(encoded) == 14, "Tight-packed wire size should be 14 bytes"

        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_alt == 9876
        assert decoded.bcd_hdg == -999
        assert decoded.sm_offset == -32767
        assert decoded.cb2_val == -1
        assert decoded.bnr_val == 65535
        assert decoded.inline_bcd == 888
        assert decoded.inline_bnrs == -1000


# ---------------------------------------------------------------------------
# Multiple negative BCD heading values
# (C++ "negative BCD heading roundtrip" tests several values)
# ---------------------------------------------------------------------------

class TestBcdHeadingNegative:

    @pytest.mark.parametrize("value", [-1, -99, -456, -999])
    def test_negative_bcd_heading(self, value):
        msg = WireEncodingMsg()
        msg.bcd_hdg = value
        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_hdg == value


# ---------------------------------------------------------------------------
# Multiple positive BCD heading values
# (C++ "positive BCD heading roundtrip" tests several values)
# ---------------------------------------------------------------------------

class TestBcdHeadingPositive:

    @pytest.mark.parametrize("value", [0, 1, 42, 123, 999])
    def test_positive_bcd_heading(self, value):
        msg = WireEncodingMsg()
        msg.bcd_hdg = value
        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.bcd_hdg == value


# ---------------------------------------------------------------------------
# Double-encode idempotency for wire encodings
# ---------------------------------------------------------------------------

class TestDoubleEncodeIdempotency:

    def test_wire_encoding_double_encode(self):
        msg = WireEncodingMsg()
        msg.bcd_alt = 4567
        msg.bcd_hdg = -321
        msg.sm_offset = 1234
        msg.cb2_val = -5678
        msg.bnr_val = 9999
        msg.inline_bcd = 789
        msg.inline_bnrs = -500

        first = msg.encode_bytes()
        second = msg.encode_bytes()
        assert first == second, \
            "Encoding the same message twice should produce identical bytes"


# ---------------------------------------------------------------------------
# Sign-magnitude boundary tests
# ---------------------------------------------------------------------------

class TestSignMagnitudeBoundary:

    def test_max_negative_16bit(self):
        msg = WireEncodingMsg()
        msg.sm_offset = -32767
        msg.inline_bnrs = -32767

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.sm_offset == -32767
        assert decoded.inline_bnrs == -32767


# ---------------------------------------------------------------------------
# Two's complement boundary tests
# ---------------------------------------------------------------------------

class TestTwosComplementBoundary:

    def test_min_int16(self):
        msg = WireEncodingMsg()
        msg.cb2_val = -32768

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.cb2_val == -32768

    def test_neg1(self):
        msg = WireEncodingMsg()
        msg.cb2_val = -1

        encoded = msg.encode_bytes()
        decoded = WireEncodingMsg.decode_bytes(encoded)
        assert decoded.cb2_val == -1
