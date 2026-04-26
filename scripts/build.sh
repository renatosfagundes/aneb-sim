#!/usr/bin/env bash
#
# build.sh — configure and build the aneb-sim engine.
# Assumes scripts/bootstrap.sh has already been run.
#
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -f external/simavr/simavr/libsimavr.a ]]; then
    echo "ERROR: simavr is not built. Run scripts/bootstrap.sh first." >&2
    exit 1
fi

mkdir -p build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo
echo "Built: build/aneb-sim/aneb-sim.exe"
