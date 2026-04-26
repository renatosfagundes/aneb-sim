#!/usr/bin/env bash
#
# run.sh — launch the smoke test against a hex file.
# Default firmware: firmware/examples/blink.hex
#
set -euo pipefail

cd "$(dirname "$0")/.."

HEX="${1:-firmware/examples/blink.hex}"

if [[ ! -f "$HEX" ]]; then
    cat >&2 <<EOF
ERROR: firmware not found: $HEX

Copy a known-good Arduino blink hex into firmware/examples/blink.hex, e.g.:

  cp '/c/Users/$USER/OneDrive/Pos/RTOS/JigaAppCmd4-20260419T021937Z-3-001/JigaAppCmd4/firmware/mcu/nano/Blink1000.ino.eightanaloginputs.hex' firmware/examples/blink.hex
EOF
    exit 1
fi

if [[ ! -x build/aneb-sim/aneb-sim.exe ]]; then
    echo "ERROR: engine not built. Run scripts/build.sh first." >&2
    exit 1
fi

exec ./build/aneb-sim/aneb-sim.exe "$HEX"
