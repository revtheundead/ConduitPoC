#!/usr/bin/env python3
"""Dummy ASTERIX Peer -- Encode/Decode Roundtrip Test (server perspective)

Tests all record types from the server-perspective generated package (asterix-alt).
From the server perspective, Cat007DownlinkRecord is send-only (so we generate and
test those), along with Cat021, Cat048, and Cat253 which are bidirectional.

Usage: python -m src.dummy_peer [--num-tests N] [--seed S]
"""

import sys
import os
import random
import argparse

# Ensure the asterix-alt generated package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'asterix-alt'))

from . import random_asterix_alt


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
        description="Dummy ASTERIX Encode/Decode Roundtrip Test (server perspective)")
    parser.add_argument("--num-tests", type=int, default=100,
                        help="Number of test iterations (default: 100)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed (default: 42)")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    num_tests = args.num_tests
    passed = 0
    failed = 0

    print(f"[dummy_peer] Running {num_tests} roundtrip iterations "
          f"(seed={args.seed}, server perspective)")

    for i in range(num_tests):
        for name, gen_fn in [
            ("Cat007Downlink", random_asterix_alt.random_cat007_downlink),
            ("Cat021", random_asterix_alt.random_cat021),
            ("Cat048", random_asterix_alt.random_cat048),
            ("Cat253", random_asterix_alt.random_cat253),
        ]:
            rec = gen_fn(rng)
            if test_roundtrip(name, rec):
                passed += 1
            else:
                failed += 1

    total = passed + failed
    print(f"[dummy_peer] Results: {passed} passed, {failed} failed out of {total}")
    if failed > 0:
        print("[dummy_peer] SOME TESTS FAILED", file=sys.stderr)
        sys.exit(1)
    else:
        print("[dummy_peer] All tests passed.")


if __name__ == "__main__":
    main()
