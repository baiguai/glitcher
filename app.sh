#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/build/glitcher"

if [ ! -f "$BINARY" ]; then
    "$SCRIPT_DIR/build.sh"
fi

"$BINARY"
