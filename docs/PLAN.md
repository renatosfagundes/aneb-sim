# Roadmap (v2 — May 2026)

This file replaces the original M0–M9 stubs in `ARCHITECTURE.md` with
a forward-looking plan that accounts for everything that's actually
shipped (some of which wasn't in the original plan) plus the new
acceptance bar of "examples for every peripheral + a polished
remote_flasher path."  See `DESIGN.md` for *why* the deferred items
are deferred.

## What's already done

- **M0–M5** — engine, multi-chip lockstep, JSON proto, MCP2515
  register-fidelity model, multi-ECU CAN bus, bus-off + recovery,
  GPIO/ADC/analog routing, LDR loop, buzzer, LOOP feedback.
- **M6** — full PyQt6/QML UI with per-chip Nano illustrations,
  16×2 LCD, domed 5 mm LEDs, angular-drag trim-pots, colored DIN
  buttons, per-chip serial console.
- **M7** — global Dashboard window + per-chip rolling-window
  Plotter (QtCharts, sampled at 20 Hz from `plot_buffers.py`).
- **M9 — partial** — themed Console / Plot / Flash panes via
  `qml/Theme.js` + shared QML primitives (`PaneHeader`, `PaneButton`,
  …); chip-info sidebar (Hex / Free RAM / COM) fed by the new
  `chipstat` event; com0com bundled installer; pt-br LaTeX manual.
- **Off-plan deliveries** that emerged during M9:
  - Embedded Optiboot + dual-port TCP UART (8600 flasher /
    8700 bridge) so external avrdude / Arduino IDE / Remote
    Firmware Flasher can program the simulator without manual
    bootstrap.
  - Local-mode integration in Remote Firmware Flasher v2.4.0
    (entry `Localhost (aneb-sim)`).
  - Live-app screenshot tool (`scripts/make_screenshots.py`) +
    offscreen QQuickView fallback (`scripts/screenshots/`).

## Phase A — close M8 (scenarios & teaching aids) — ~2 days

Original M8 said "error injection (done) + scenario player (not
built)".  Injection commands `force_busoff` / `can_errors` /
`can_recover` already shipped.  The replay tool is the missing half.

- **A1.** `aneb-sim --script demos/busoff_recovery.jsonl` flag.
  Each line: `{"at_ms": 1500, "c": "force_busoff", "chip": "ecu2"}`.
  Engine reads the file, schedules each command via a wallclock
  timer, executes through the existing `cmd.c` apply path.
- **A2.** UI menu button (Toolbar → Scenario ▾) that picks a `.jsonl`
  from `demos/` and POSTs it through the same code path as A1, via
  a `cmd_run_script` that the engine handles.
- **A3.** Three canned scenarios under `demos/`:
  - `busoff_recovery.jsonl` — drive ECU2 to bus-off, recover, repeat
  - `tec_climb.jsonl` — slow `can_errors` ramp showing TEC progression
  - `multi_node_arbitration.jsonl` — three ECUs TX simultaneously,
    illustrate frame ordering

**Done when:** instructor clicks Scenario → "Bus-off recovery", chips
drive themselves through the canned states with the Plotter showing
TEC climbing + dropping back to zero.

## Phase B — comprehensive example suite — ~3 days

One Arduino sketch per peripheral so students can poke each one
in isolation, plus a "kitchen sink" that wires everything.  Each
gets `.ino` source, pre-built `.hex` (with-bootloader variant for
external flashing), and a one-paragraph entry in the manual.

| Sketch | Peripheral exercised | Status |
|---|---|---|
| `blink` | D13/L digital out | ✓ existing |
| `io_test` | DOUT0/1, DIN1–4, AIN0–3 mixed | ✓ existing (audit scope) |
| `lcd_hello` | I²C LCD 16×2 | ✓ existing |
| `can_heartbeat` | CAN TX/RX (4-node) | ✓ existing |
| `buzzer_tone` | `tone()` on PD4 (BUZZ) | **new** |
| `adc_to_pwm` | read AIN0 → DOUT0 PWM duty | **new** |
| `button_counter` | DIN1–4 → counter on LCD | **new** |
| `ldr_loop` | ECU1 closed loop: PD6 PWM lights LDR_LED, A0 reads LDR | **new** |
| `loop_feedback` | ECU2 wired loop: PD5 PWM → A7 | **new** |
| `serial_echo` | UART read+echo + line count | **new** |
| `can_busoff_demo` | repeated TX with no listener → bus-off → recover | **new** |
| `mcu_mode_select` | MCU D8/D9 mode switches → simple FSM | **new** (MCU-only) |
| `kitchen_sink` | every peripheral in one sketch | **new** |

**Tooling:** `firmware/examples/build_all.sh` driving `arduino-cli
compile` for each example, producing both `*.ino.hex` and
`*.ino.with_bootloader.hex` so the same artifact set works whether
flashed from the UI's "Flash" button (which loads via `cmd_load` —
includes the bootloader for safety) or via avrdude over TCP (which
keeps the engine's pre-loaded Optiboot intact).

**Done when:** every peripheral on the board has a focused example
a student can flash from remote_flasher in under 2 minutes.

## Phase C — close the remote_flasher loop — ~1 day

Local-mode works for one chip on one board today.

- **C1.** Multi-board: deferred unless we add multi-engine support
  (the engine models exactly one physical ANEB v1.1 board).  Document
  the limit; revisit if a second simulator instance is requested.
- **C2.** Serial-tab auto-reconnect-after-flash: today the user
  manually closes/reopens during the 5 s bridge-down window.  Have
  the LocalSerialWorker hold open and retry the COM port until the
  bridge reconnects, instead of dying on the first SerialException.
- **C3.** Hex-name push: have remote_flasher send the firmware path
  to the engine via the existing JSON-Lines protocol (a small
  `chipinfo` command) before launching avrdude, so the sidebar
  shows the actual filename instead of "(uploaded via avrdude)".

**Done when:** student does Upload+Flash in remote_flasher, switches
to the Serial tab, sees output appearing within ~7 s end-to-end with
no manual intervention.

## Phase D — close M9 (distribution & docs) — ~2 days

- **D1.** PyInstaller bundle: single `aneb-sim-setup.exe` containing
  the engine, the UI venv, `com0com_signed/`, and an Inno Setup
  wrapper that runs `setup_com.bat` on first launch.
- **D2.** Manual additions:
  - `Exemplos` chapter — one paragraph per Phase B sketch with
    optional screenshot.
  - `Cenários` chapter — Phase A scenarios + how to author new ones.
  - Updated troubleshooting deltas from C2/C3.
- **D3.** README + ARCHITECTURE — bring M8 + M9 to "done", link from
  the top to the manual PDF.
- **D4.** `docs/BROWSER_UI_PLAN.md` — separate file, write down the
  estimate (3–5 days) and the migration boundary; do not build.

**Done when:** a student with no MSYS2 / Python / com0com
pre-installed can double-click an installer, accept admin prompts,
and have a working aneb-sim ready to receive a flash from
remote_flasher.

## Deferred (per `DESIGN.md` §4.2)

Keep deferred unless explicit lab need surfaces:

- LIN bus on ECU3/ECU4
- CAN2 (second bus)
- Bit-time fidelity (CNF1/2/3 as runtime constraints)
- 128 × 11-recessive auto-recovery from bus-off
- AVR sleep modes
- MCP2515 one-shot mode
- CLKOUT / SOF / RXnBF / TXnRTS pins
- Wake-up from sleep on bus activity

## Order of work

1. **Phase B** first — biggest student-visible improvement and
   unblocks Phase D's "Exemplos" chapter.
2. **Phase C** in parallel with B (orthogonal; can pipeline).
3. **Phase A** — small, contained, finishes M8.
4. **Phase D** — distribution + final docs sweep.

**Total:** ~7–9 focused days of work to fully close out the original
plan with the deviations and new requirements.
