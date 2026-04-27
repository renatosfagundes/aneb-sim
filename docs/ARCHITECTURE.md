# Architecture

> *Status: stub. Sections are filled in as their components land in their
> respective milestones — see the plan in the project root or PR descriptions.*

## Two-process design

The simulator is split between a C **engine** (`aneb-sim`) and a Python
**UI** (`aneb-ui`, landing in M6). They communicate over JSON-lines on stdio.

This separation:

- Isolates UI crashes from sim crashes
- Allows headless runs for CI and scripted lab demos
- Makes the eventual browser-UI port a UI-only project (same engine, same
  protocol)

## The five chips

| Role | MCU | Firmware | Notes |
|---|---|---|---|
| ECU1 | ATmega328P  | student code (Trampoline RTOS) | CAN1, LDR + LDR_LED, 2 LEDs, buzzer, 4 pots, 4 buttons |
| ECU2 | ATmega328P  | student code | CAN1, LOOP feedback (pin 5 → A7), 2 LEDs, 4 pots, 4 buttons |
| ECU3 | ATmega328P  | student code | CAN1, LIN, 2 LEDs, 4 pots, 4 buttons |
| ECU4 | ATmega328P  | student code | CAN1, LIN, 2 LEDs, 4 pots, 4 buttons |
| MCU  | ATmega328PB | `MCU_Firmware_v08.hex` | Mode selectors, state machine, board control |

All four ECUs share a single CAN bus (CAN1). CAN2 from the original board
is not modeled in v1.

## Time model

- Single-thread, deterministic lockstep
- All five cores advance N cycles → peripherals tick → bus ticks → loop
- N is tuned for ≥1× wallclock; speedup factor exposed via the `speed`
  command (see [PROTOCOL.md](PROTOCOL.md))

## Component breakdown

Filled in per milestone:

- **M0** — bootstrap, simavr build, GPIO trace smoke test *(done)*
- **M1** — JSON proto, multi-chip lockstep scheduler, UART, GPIO commands *(done)*
- **M2** — MCP2515 model + register-level tests (loopback only) *(done)*
- **M3** — CAN bus model + multi-ECU traffic *(done)*
- **M4** — bus-off and error-frame fidelity *(done)*
- **M5** — GPIO/ADC/analog routing + LDR loop + buzzer + LOOP feedback *(done)*
- **M6** — PyQt6 UI core *(done — QML-based per-chip panels with Nano illustration, I2C-driven LCD, domed 5mm LEDs, angular-drag trimpots, 2x2 colored buttons, togglable serial console window per chip)*
- **M7** — dashboard + plotter widgets *(done — per-chip rolling-window plotter window with QtCharts, sampled at 20 Hz from `plot_buffers.py`; global compact dashboard window toggled from the toolbar)*
- **M8** — teaching aids (error injection, scenario player)
- **M9** — packaging & polish
