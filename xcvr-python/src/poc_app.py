#!/usr/bin/env python3
"""PoC ASTERIX Transceiver -- Encode/Decode Roundtrip Test (client perspective)

Tests all record types from the client-perspective generated package (asterix).
For each record type, generates random instances, encodes to bytes, decodes back,
re-encodes, and verifies the byte sequences match.

Usage: python -m src.poc_app [--num-tests N] [--seed S]
"""

import sys
import os
import random
import argparse

# Ensure the asterix generated package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'asterix'))

from . import random_asterix


def test_roundtrip(name, record):
    """Encode, decode, re-encode and verify bytes match."""
    encoded = record.encode_bytes()
    decoded = type(record).decode_bytes(encoded)
    re_encoded = decoded.encode_bytes()
    if encoded == re_encoded:
        return True
    print(f"FAIL: {name} roundtrip mismatch (orig={len(encoded)} re={len(re_encoded)})")
    return False


def main():
    parser = argparse.ArgumentParser(
        description="PoC ASTERIX Encode/Decode Roundtrip Test (client perspective)")
    parser.add_argument("--num-tests", type=int, default=100,
                        help="Number of test iterations (default: 100)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed (default: 42)")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    num_tests = args.num_tests
    passed = 0
    failed = 0

    print(f"[poc_app] Running {num_tests} roundtrip iterations "
          f"(seed={args.seed}, client perspective)")

    for i in range(num_tests):
        for name, gen_fn in [
            ("Cat007Uplink", random_asterix.random_cat007_uplink),
            ("Cat007Downlink", random_asterix.random_cat007_downlink),
            ("Cat021", random_asterix.random_cat021),
            ("Cat048", random_asterix.random_cat048),
            ("Cat253", random_asterix.random_cat253),
        ]:
            rec = gen_fn(rng)
            if test_roundtrip(name, rec):
                passed += 1
            else:
                failed += 1

    total = passed + failed
    print(f"[poc_app] Results: {passed} passed, {failed} failed out of {total}")
    if failed > 0:
        print("[poc_app] SOME TESTS FAILED", file=sys.stderr)
        sys.exit(1)
    else:
        print("[poc_app] All tests passed.")


if __name__ == "__main__":
    main()
