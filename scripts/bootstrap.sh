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

# ---- 5. Done --------------------------------------------------------------
echo
echo "Bootstrap complete."
echo "Next: ./scripts/build.sh"
