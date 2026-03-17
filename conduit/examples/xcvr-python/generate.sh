#!/usr/bin/env bash
# ============================================================================
# xcvr-python — Generate Python code from BMDL definitions
#
# Usage:
#   ./generate.sh                  # Use bgen from ../../build/bgen/bgen
#   BGEN=/path/to/bgen ./generate.sh   # Use a specific bgen binary
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BGEN="${BGEN:-${SCRIPT_DIR}/../../build/bgen/bgen}"

if [ ! -x "$BGEN" ]; then
    echo "bgen not found at $BGEN — set BGEN env var or build first" >&2
    exit 1
fi

for dir in asterix asterix-alt; do
    input="$SCRIPT_DIR/$dir/$dir.bmdl.xml"
    output="$SCRIPT_DIR/$dir/generated"
    if [ ! -f "$input" ]; then
        echo "Skipping $dir: $input not found" >&2
        continue
    fi
    mkdir -p "$output"
    echo "Generating Python code: $dir -> $output"
    "$BGEN" --input "$input" --output "$output" --language python
done

echo "Done."
