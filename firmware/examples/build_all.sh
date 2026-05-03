#!/bin/bash
# build_all.sh — compile every Phase B example sketch via arduino-cli.
#
# One-shot script the user runs after pulling new firmware sources to
# regenerate every example .hex.  Each sketch lives in its own
# subdirectory; the resulting *.ino.hex (no bootloader) and
# *.ino.with_bootloader.hex (with Optiboot baked in) are dropped at
# the top of firmware/examples/ so the UI's Load menu can find them
# and remote_flasher can flash them as-is.
#
# Requires (one-time):
#   arduino-cli core install arduino:avr
#   arduino-cli lib install "mcp_can"
#   arduino-cli lib install "LiquidCrystal I2C"
#
# Notes:
#   * The "with_bootloader" variant is what remote_flasher / external
#     avrdude expect — the simulator pre-loads Optiboot so either form
#     works, but having both keeps the artifact set drop-in-replacement
#     compatible with real hardware too.
#   * can_heartbeat has its own build script (4 IDs, 4 ECUs) and isn't
#     re-run from here; invoke can_heartbeat/build_all.sh separately.

set -euo pipefail

cd "$(dirname "$0")"

# Sketches to build.  Order is alphabetical for predictable logs.
SKETCHES=(
    adc_to_pwm
    blink                # already shipped as a hex; rebuilt for parity
    buzzer_tone
    button_counter
    can_busoff_demo
    io_test
    kitchen_sink
    lcd_hello
    ldr_loop
    loop_feedback
    mcu_mode_select
    serial_echo
)

FQBN_ECU="arduino:avr:nano"
# The MCU is an ATmega328PB.  The stock arduino:avr:nano FQBN is for
# 328P and works for our purposes since the simulator emulates the
# 328PB-specific peripherals separately.  Override per-sketch below
# if the FQBN ever needs to differ.

failed=0
for s in "${SKETCHES[@]}"; do
    if [[ ! -d "$s" ]]; then
        echo "  SKIP $s (no source directory)"
        continue
    fi
    if [[ ! -f "$s/$s.ino" ]]; then
        echo "  SKIP $s (no $s/$s.ino — possibly a hex-only legacy example)"
        continue
    fi

    # mcu_mode_select is intended for the ATmega328PB, but builds
    # cleanly with the same nano FQBN — the simulator side handles
    # the chip class.
    fqbn="$FQBN_ECU"

    echo
    echo "=== $s ==="
    out="$s/build"
    rm -rf "$out"
    if ! arduino-cli compile \
            --fqbn "$fqbn" \
            --output-dir "$out" \
            "$s" ; then
        echo "  FAILED $s"
        failed=$((failed + 1))
        continue
    fi
    # Drop the no-bootloader hex at the top of firmware/examples/ so it
    # appears in the UI's "Load ▾" menu and is the file Remote Flasher
    # picks up.  Also keep the with_bootloader variant alongside for
    # external flashers that prefer it.
    cp "$out/$s.ino.hex"                "$s.hex"
    if [[ -f "$out/$s.ino.with_bootloader.hex" ]]; then
        cp "$out/$s.ino.with_bootloader.hex" "${s}_with_bootloader.hex"
    fi
    echo "  -> $s.hex"
done

echo
if (( failed == 0 )); then
    echo "All $(printf '%s\n' "${SKETCHES[@]}" | wc -l) sketches built successfully."
else
    echo "WARNING: $failed sketch(es) failed to build — see logs above."
    exit 1
fi
