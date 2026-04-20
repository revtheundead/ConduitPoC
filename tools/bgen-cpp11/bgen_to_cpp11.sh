#!/bin/sh
# Cross-platform wrapper for bgen_to_cpp11.py
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
    PY=python
fi
exec "$PY" "$SCRIPT_DIR/bgen_to_cpp11.py" "$@"
