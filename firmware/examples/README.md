# Example sketches

Each subdirectory is a stand-alone Arduino sketch demonstrating one
peripheral on the ANEB v1.1 board.  Build them all with
[`build_all.sh`](build_all.sh) (requires `arduino-cli` + the
`mcp_can` and `LiquidCrystal I2C` libraries) — the resulting `.hex`
files are dropped at the top of this directory so the simulator's
`Load ▾` menu picks them up automatically.

## Per-peripheral catalogue

| Sketch | What it demonstrates | Suggested chip |
|---|---|---|
| [`blink/`](blink) (hex only) | D13 / L LED blink | any |
| [`io_test/`](io_test) | Mixed: DIN1–4, AIN0–3, DOUT0/1, L LED, LDR_LED — broad smoke test | any |
| [`lcd_hello/`](lcd_hello) | I²C LCD 16×2 init + scrolling text | any |
| [`buzzer_tone/`](buzzer_tone) | `tone()` / `noTone()` on PD7 (BUZZ) | any |
| [`adc_to_pwm/`](adc_to_pwm) | Read AIN0 → drive DOUT0 PWM proportionally | any |
| [`button_counter/`](button_counter) | Debounced DIN1–4 → counter on the LCD | any |
| [`serial_echo/`](serial_echo) | UART RX line buffer → echo back with index | any |
| [`ldr_loop/`](ldr_loop) | ECU1's optical closed-loop: PD6 PWM → A0 LDR readback | ECU1 |
| [`loop_feedback/`](loop_feedback) | ECU2's electrical loop: PD5 PWM → A7 with proportional control | ECU2 |
| [`can_heartbeat/`](can_heartbeat) | 4-node CAN demo (each ECU broadcasts a counter) | ECU1–4 (build per-node) |
| [`can_busoff_demo/`](can_busoff_demo) | Reports MCP2515 EFLG/TEC/REC and recovers from bus-off | any ECU |
| [`mcu_mode_select/`](mcu_mode_select) | D8/D9 mode switches → 4-state FSM on the LCD | MCU |
| [`kitchen_sink/`](kitchen_sink) | Every peripheral in one sketch — broad demo | any ECU |

## Running an example

The fastest path:

1. **From the aneb-sim UI** — click `Load ▾` next to a chip's title
   and pick the `.hex`.  This uses the engine's internal `cmd_load`
   path — no avrdude, no flashing latency.
2. **From Remote Firmware Flasher** — select `Localhost (aneb-sim)`,
   pick a `Placa 01 (aneb-sim)` ECU, browse to the `.hex`, click
   *Upload + Reset + Flash*.  This exercises the real avrdude / STK500
   path through the dual-port TCP UART (8600 flasher / 8700 bridge),
   which is the same path students use against real lab hardware.

After flashing, open the chip's `Console`, `Plot`, or
`Flash` window from the UI to see UART output, sampled signals, or
re-trigger a flash session.

## Tips

- Many sketches print CSV-style telemetry to UART so the Plotter
  can graph the signal directly — open `Plot ▸` on the chip and
  enable the relevant `adc:N` / `pwm:Pxn` traces.
- The `kitchen_sink` and `can_heartbeat` sketches transmit on CAN.
  Open the global `Dashboard` from the toolbar to see the bus
  activity across all four ECUs at once.
- For sketches that exercise CAN error states (`can_busoff_demo`),
  drive errors from the UI's CAN-error injection menu or via the
  scenario player (`docs/PLAN.md` Phase A).

## Build environment

```bash
arduino-cli core install arduino:avr
arduino-cli lib  install "mcp_can"
arduino-cli lib  install "LiquidCrystal I2C"

cd firmware/examples
./build_all.sh
```

`build_all.sh` produces both `<name>.hex` (no bootloader) and
`<name>_with_bootloader.hex` (Optiboot-prefixed).  The simulator
pre-loads Optiboot at `chip_init`, so either variant works for both
the UI's `Load ▾` path and the external avrdude path.
