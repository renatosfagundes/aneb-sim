#!/usr/bin/env bash
#
# run-ui.sh — launch the PyQt6 UI against the locally-built engine.
#
# Activates the Python 3.11 virtual environment created by bootstrap.sh
# and starts aneb-ui. Override the engine path with --engine, otherwise
# the default ./build/aneb-sim/aneb-sim.exe is used.
#
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -x build/aneb-sim/aneb-sim.exe ]]; then
    echo "ERROR: engine not built. Run scripts/build.sh first." >&2
    exit 1
fi

if [[ ! -f .venv/Scripts/python.exe ]]; then
    cat >&2 <<'EOF'
ERROR: UI virtualenv not found at .venv/.

Re-run scripts/bootstrap.sh after installing Python 3.11.x from
https://www.python.org/downloads/release/python-3112/
EOF
    exit 1
fi

exec .venv/Scripts/python.exe -m aneb_ui "$@"
