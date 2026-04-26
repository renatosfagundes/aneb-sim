#!/usr/bin/env bash
#
# bootstrap.sh — one-shot setup for aneb-sim on a fresh MSYS2 install.
# Idempotent: safe to re-run.
#
set -euo pipefail

cd "$(dirname "$0")/.."

# ---- 1. Sanity check the shell environment --------------------------------
if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
    cat >&2 <<EOF
ERROR: this script must be run from the MSYS2 MINGW64 shell.
       Currently MSYSTEM='${MSYSTEM:-<unset>}'.

Open the "MSYS2 MINGW64" shell (blue icon) from the Start menu and rerun.
See BOOTSTRAP.md for details.
EOF
    exit 1
fi

# ---- 2. Install build dependencies ----------------------------------------
echo "==> Installing build dependencies via pacman"
pacman -S --needed --noconfirm \
    git \
    make \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-libelf \
    mingw-w64-x86_64-avr-gcc \
    mingw-w64-x86_64-avr-libc

# ---- 3. Initialize submodules ---------------------------------------------
echo "==> Updating submodules (simavr)"
git submodule update --init --recursive

# ---- 4. Build simavr ------------------------------------------------------
echo "==> Building simavr"
make -C external/simavr/simavr RELEASE=1

# ---- 5. Set up the UI virtual environment (Python 3.11.x) ----------------
# We deliberately target Python 3.11 (not the most-recent 3.13/3.14) for
# stability with PyQt6 wheels. The Windows Python launcher `py -3.11`
# discovers a python.org install; install one from
# https://www.python.org/downloads/release/python-3112/ if not present.
echo "==> Setting up Python 3.11 UI virtual environment"
PY311=""
if py -3.11 --version >/dev/null 2>&1; then
    PY311="py -3.11"
elif command -v python3.11 >/dev/null 2>&1; then
    PY311="python3.11"
fi

if [[ -z "$PY311" ]]; then
    cat >&2 <<'EOF'
WARN: Python 3.11.x not found.

Install it from https://www.python.org/downloads/release/python-3112/
(default install options are fine; the Windows Python launcher should
register it automatically). Then re-run scripts/bootstrap.sh.

Continuing without the UI — the engine itself will still build.
EOF
else
    echo "    Found: $($PY311 --version 2>&1)"
    $PY311 -m venv .venv
    # shellcheck disable=SC1091
    source .venv/Scripts/activate
    python -m pip install --upgrade pip wheel
    python -m pip install -e ./aneb-ui
    deactivate
    echo "    UI installed into .venv/. Launch with ./scripts/run-ui.sh"
fi

# ---- 6. Done --------------------------------------------------------------
echo
echo "Bootstrap complete."
echo "Next: ./scripts/build.sh   (then ./scripts/run-ui.sh)"
