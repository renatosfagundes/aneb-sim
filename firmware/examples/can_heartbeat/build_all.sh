#!/bin/bash
# build_all.sh — compile can_heartbeat for ECU1..ECU4 with unique CAN IDs.
#
# Produces ../can_heartbeat_ecu1.hex through ../can_heartbeat_ecu4.hex,
# ready to drop into the simulator's "Load ▾" menu.  Each ECU gets a
# distinct 11-bit identifier (0x101 / 0x102 / 0x103 / 0x104) so the
# four nodes are individually identifiable on the bus.
#
# Requires:
#   arduino-cli core install arduino:avr
#   arduino-cli lib install "mcp_can"
#   arduino-cli lib install "LiquidCrystal I2C"

set -euo pipefail

cd "$(dirname "$0")"

for ECU in 1 2 3 4; do
    ID=$(printf '0x10%d' "$ECU")
    OUTDIR="build_ecu${ECU}"
    HEX="../can_heartbeat_ecu${ECU}.hex"

    echo
    echo "=== Building ECU${ECU} (CAN ID ${ID}) ==="
    arduino-cli compile \
        --fqbn arduino:avr:nano \
        --build-property "build.extra_flags=-DMY_ID=${ID}" \
        --output-dir "${OUTDIR}" \
        .

    cp "${OUTDIR}/can_heartbeat.ino.hex" "${HEX}"
    echo "  -> ${HEX}"
done

echo
echo "All four ECU hex files built. Flash them in the simulator:"
echo "  Load ▾ -> Load ECU 1...  pick can_heartbeat_ecu1.hex"
echo "  Load ▾ -> Load ECU 2...  pick can_heartbeat_ecu2.hex"
echo "  Load ▾ -> Load ECU 3...  pick can_heartbeat_ecu3.hex"
echo "  Load ▾ -> Load ECU 4...  pick can_heartbeat_ecu4.hex"
