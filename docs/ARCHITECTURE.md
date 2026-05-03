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

## Per-chip TCP UART servers

Each chip exposes its UART as **two** TCP listening sockets on
localhost:

- `8600 + i` — **flasher port**.  Connecting kicks any prior client
  and triggers `chip_reset` (Arduino DTR-pulse emulation), and the
  engine suppresses its JSON `uart` event path while a flasher client
  is attached so STK500 binary traffic doesn't flood the UI.
  Disconnect triggers a soft reset (MCUSR=0) so Optiboot deterministically
  drops out of its sync-wait loop into the freshly programmed sketch.
- `8700 + i` — **bridge port**.  Passive subscriber: no chip reset on
  connect, no JSON suppression.  Used by the aneb-sim UI's `uart_bridge`
  to forward UART to virtual COM ports without disturbing the running
  firmware on every reconnect.

A new connection on either port preempts the previous client; the
single-client-slot model keeps STK500's half-duplex protocol intact.

This split exists because:

- avrdude over `-P net:...` needs reset-on-connect to catch Optiboot's
  startup window.
- The aneb-sim UI bridge auto-reconnects after every flash; if the
  bridge connected on the flasher port it would inadvertently kick
  avrdude during long flashes (>5 s on big sketches).

## Optiboot pre-loaded

`chip_init` embeds and pre-loads `firmware/bootloaders/optiboot_atmega328.hex`
into every ATmega328P chip on startup.  External tools (avrdude, the
Arduino IDE, the aneb-sim UI's "Flash via avrdude…" button) always
see a functioning bootloader without a manual "load Optiboot" step.
A halt stub (`rjmp .` at `0x0000`) keeps the chip from running off
the end of empty flash before a user sketch is present.

The hex is converted to a C string at CMake-configure time
(`optiboot_data.h.in`) using `file(STRINGS ... NO_HEX_CONVERSION)`,
so changing the bundled bootloader is a re-configure away.

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
- **M9** — packaging & polish — *in progress: themed Console/Plot/Flash panes via shared QML primitives in `qml/Theme.js`, `PaneHeader.qml`, etc.; live chip-info sidebar fed by the new `chipstat` event; com0com bundled installer for COM-port virtualization on Windows.*
