# can_heartbeat — multi-node CAN demo

Four-ECU CAN bus exercise.  Each ECU broadcasts a heartbeat counter on
its own 11-bit CAN identifier and shows received frames on its LCD,
giving a clear visual of the bus carrying traffic between all four
nodes simultaneously.

| ECU  | CAN ID  | Hex file                  |
|------|---------|---------------------------|
| ECU1 | `0x101` | `can_heartbeat_ecu1.hex`  |
| ECU2 | `0x102` | `can_heartbeat_ecu2.hex`  |
| ECU3 | `0x103` | `can_heartbeat_ecu3.hex`  |
| ECU4 | `0x104` | `can_heartbeat_ecu4.hex`  |

## Build

```bash
arduino-cli core install arduino:avr
arduino-cli lib install "mcp_can"
arduino-cli lib install "LiquidCrystal I2C"

cd firmware/examples/can_heartbeat
./build_all.sh
```

The script produces `can_heartbeat_ecu1.hex` … `can_heartbeat_ecu4.hex`
in `firmware/examples/`.

## Run

In the simulator:

1. **Load ▾** → **Load ECU 1…** → pick `can_heartbeat_ecu1.hex`
2. Repeat for ECU 2, ECU 3, ECU 4 with the matching files.
3. Set **Speed** to `1.00` (or any non-zero value) so the millis-based
   timing runs at a sensible wall-clock rate; `Max` works too but the
   1-second TX cadence becomes "as fast as the simulator can run".

## What you should see

Each ECU's panel:

- **LCD line 1** — `0x101 TX:n` (own ID and own counter, increments
  every second)
- **LCD line 2** — `RX 0x10X:n` (most recent frame received: sender's
  ID and that sender's counter at TX time)
- **L LED** — flashes on every TX (~80 ms blip every second)
- **DOUT0 LED** — toggles on every RX (so it ripples whenever any
  other ECU broadcasts)

Also visible globally:

- **CAN monitor table** (right column of the simulator) lists every
  frame on the bus with timestamp, sender, ID, DLC and payload — you
  should see a steady stream of `0x101`, `0x102`, `0x103`, `0x104`
  cycling through.
- **CAN indicator** on each ECU's info sidebar stays green (`active`,
  TEC=0 REC=0) — the bus is healthy.

## Try this

- **Force a bus-off**: click the per-ECU `force_busoff` action (CLI
  command `force_busoff` or use the panel's CAN-state controls if
  exposed).  That ECU stops talking; the others keep going.
- **Inject a frame**: from the right column's CAN-inject panel, send a
  custom ID/payload — every ECU's LCD line 2 will show your frame on
  the next RX poll.

## Wiring assumed

Standard ANEB v1.1:

| Signal           | Pin              |
|------------------|------------------|
| MCP2515 CS       | D10 (PB2)        |
| MCP2515 INT      | D2  (PD2 / INT0) |
| MCP2515 SPI      | D11 / D12 / D13  |
| LCD I²C          | A4 SDA / A5 SCL @ 0x27 |
| L LED            | D13 (PB5)        |
| DOUT0 LED        | D3  (PD3)        |

The bus is configured for 500 kbps with the MCP2515 driven by a
16 MHz crystal — the simulator's MCP2515 model accepts these
parameters as register writes; bus timing isn't enforced, so the
choice doesn't affect the simulation but matches a real ANEB board so
the same hex would also flash to hardware.
