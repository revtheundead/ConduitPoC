"""CLI wrapper for invoking bgen to generate Python/Java/C++ protocol code.

Usage:
    conduit-codegen --input protocol.bmdl.xml --output ./generated/my_protocol
    conduit-codegen --input protocol.bmdl.xml --output ./generated --language java
"""

import argparse
import os
import shutil
import subprocess
import sys


def _find_bgen() -> str:
    """Locate the bgen executable."""
    # 1. Explicit env var
    env_path = os.environ.get("CONDUIT_BGEN", "")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. On PATH
    found = shutil.which("bgen")
    if found:
        return found

    raise FileNotFoundError(
        "Cannot find 'bgen' executable. Set CONDUIT_BGEN environment variable "
        "or ensure bgen is on your PATH (built via CMake with CONDUIT_BUILD_BGEN=ON)."
    )


def main():
    parser = argparse.ArgumentParser(
        prog="conduit-codegen",
        description="Generate protocol code from a .bmdl.xml file using bgen",
    )
    parser.add_argument("--input", required=True, help="Path to .bmdl.xml file")
    parser.add_argument("--output", required=True, help="Output directory for generated code")
    parser.add_argument("--namespace", default=None, help="Namespace for generated code")
    parser.add_argument(
        "--language",
        default="python",
        choices=["python", "java", "cpp"],
        help="Target language (default: python)",
    )
    args = parser.parse_args()

    bgen = _find_bgen()
    cmd = [bgen, "--input", args.input, "--output", args.output, "--language", args.language]
    if args.namespace:
        cmd.extend(["--namespace", args.namespace])

    result = subprocess.run(cmd)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
