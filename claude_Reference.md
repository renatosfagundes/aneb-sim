# aneb-sim — Claude Session Reference

This document records the full context, decisions, and implementation details from the Claude Code sessions on this project. Use it to resume work without losing context.

---

## Project Overview

**aneb-sim** is an Automotive Network Evaluation Board (ANEB v1.1) simulator. It simulates up to 5 ATmega328P/328PB chips (ECU1–4 + MCU) connected over a CAN bus, with I2C LCD displays and SPI MCP2515 CAN controllers.

**Repo layout:**
```
c:\dev\aneb-sim\
  aneb-sim/          C engine (simavr-based AVR simulator)
    src/
      main.c          entry point, stdin reader thread
      sim_loop.c/h    per-chip thread scheduler + CAN routing
      chip.c/h        one simulated AVR chip
      cmd.c/h         command dispatch + queue
      proto.c/h       JSON-Lines wire protocol
      pin_names.c/h
      mcp2515/        pure-logic MCP2515 CAN controller model
      can_bus/        multi-node CAN bus router
      i2c_lcd/        PCF8574 + HD44780 1602 LCD model
    test/             unit tests (test_mcp2515, test_can_bus, etc.)
    CMakeLists.txt
  aneb-ui/           Python/QML frontend (PyQt6)
    aneb_ui/
      app_qml.py      application entry point
      qml_bridge.py   Qt properties + slots exposed to QML
      sim_proxy.py    engine subprocess manager (QProcess)
      state.py        observable simulation state
      plot_buffers.py rolling-window plotter data
      qml/
        Board.qml           main window
        EcuPanel.qml        per-ECU control panel
        ArduinoNano.qml     Nano board illustration + overlays
        Dashboard.qml       at-a-glance multi-chip dashboard
        DashboardWindow.qml dashboard top-level window
        PlotterWindow.qml   per-chip plotter window
        Plotter.qml         QtCharts time-series plotter
        CanInject.qml       CAN frame injection UI
        CanMonitor.qml      CAN frame log
        SerialConsoleWindow.qml
        Led.qml / LcdWidget.qml / BuzzerWidget.qml
        TrimPot.qml / PushButton.qml / CanIndicator.qml
      qml_assets/     arduino.png, arduino-coords.json, background.png
  external/simavr/   simavr library (built via scripts/bootstrap.sh)
  scripts/
    bootstrap.sh      build simavr
    build.sh          cmake + ninja build
    run.sh            launch engine
    run-ui.sh         launch full UI
```

**Build command:**
```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake --build c:/dev/aneb-sim/build --target aneb-sim
# or full rebuild:
bash scripts/build.sh
```

---

## Session 1 — CPU Usage Reduction

### Problem
- Python process was using ~12–21% CPU while simulating 1–4 chips with io_test firmware.
- Breakdown: C engine ~7–14%, QML UI ~6–14%.

### Root Causes Found

#### QML/Python side (fixed first, biggest win)
1. **Plotter always active** — 5 `Plotter` instances each subscribed to `bridge.plotSeqChanged` at 20 Hz. Even when their windows were closed, they ran `s.clear()` + 200 `s.append()` × 4 chart series every tick.
   - **Fix:** Added `property bool active: true` + `enabled: root.active` on the `Connections` in `Plotter.qml`. `PlotterWindow.qml` binds `active: root.visible`.

2. **PadOverlay calling Python getter 4× per pad per repaint** — `_level()` was a JS function called on each of 4 bindings per pad (20 pads = 80+ Python calls per `pinSeqChanged`).
   - **Fix:** Replaced with `property real _lev` evaluated once; all 4 bindings read the QML property.

3. **Dirty dict rebuild on every QML read** — `pinStates`, `pwmDuties`, `adcValues` each rebuilt a fresh dict on every QML property access.
   - **Fix:** Dirty-flag caching in `qml_bridge.py`. Signal handlers set the dirty flag; getters rebuild only once per emission.

#### C engine side
4. **`timeBeginPeriod(1)` inflated CPU** — Raising system timer resolution prevents CPU C-state transitions; Task Manager reports sleep time as CPU. Fully removed.

5. **Short `nanosleep` on Windows rounded to 15 ms** — `nanosleep(500µs)` snaps to ~15.6 ms on Windows without `timeBeginPeriod`, causing the sim to run at ~3% of real-time instead of pacing correctly.

6. **`g_speed = 0.0` default** — Flat-out is correct; pacing only activates when speed > 0.

### Result
- QML CPU: 13.8% → 6.3%
- C engine: settled at ~6–7% per chip at flat-out speed

---

## Session 2 — Per-Chip Threading

### Question: Is simavr the best approach for 16 chips?

**Answer: Yes, stay with simavr** — but the threading model needed changing.

Alternatives evaluated:
- **Renode** — better multi-chip support built-in, but would require rewriting all peripheral models (MCP2515 IRQ wiring, CAN bus, I2C LCD). Weeks of work to reach parity.
- **QEMU AVR** — maturing but less battle-tested for ATmega328P. Would need separate process per chip.
- **simavr with per-chip threads** — correct path. Each `avr_t` is independent; simavr was designed for this.

### Architecture: Before

Single-threaded main loop:
```
main thread:
    drain cmd queue
    sim_loop_tick()  ← runs all chips sequentially, 1000 instructions each
    sleep 1ms if no active chips
```

Problem at 16 chips: all chips on one core → chip 16 gets 1/16th of throughput.

### Architecture: After

Per-chip threads:
```
main thread:
    drain cmd queue
    sleep 1ms

chip_thread[i] (one per chip):
    drain_can_rx(i)        ← deliver incoming CAN frames under avr_lock
    avr_lock.lock()
    chip_step() × 1000    ← 1000 AVR instructions
    avr_lock.unlock()
    chip_pace_tick()       ← sleep if sim is ahead of real-time (when speed > 0)
```

### Locking Protocol (no deadlock by design)

Three mutex types, always taken in this order:

| Lock | Purpose | Holder |
|------|---------|--------|
| `chip_t.avr_lock` | Protects `avr_t` state (avr_run + avr_raise_irq) | chip thread during batch; cmd thread before IRQ raise |
| `chip_can_rx_t.lock` | Per-chip incoming CAN frame queue | Any thread enqueuing; chip thread dequeuing |
| `g_out_mutex` (proto.c) | Serializes stdout writes | Any thread calling proto_emit_* |

**Critical rule:** When `on_mcp_tx` fires (inside `avr_run()`, chip A holds `A.avr_lock`), it must NOT take any other chip's `avr_lock`. Instead it enqueues into `g_can_rx[peer].lock` — a lightweight separate lock. The peer chip thread later drains this queue under its own `avr_lock`.

This eliminates the AB/BA deadlock pattern.

### Files Changed

**chip.h** — added `pthread_mutex_t avr_lock`, pacing state (`pace_wall0`, `pace_sim0`, `pace_init`)

**chip.c** — `chip_init` calls `pthread_mutex_init`; `chip_free` calls `pthread_mutex_destroy`

**sim_loop.h** — removed `sim_loop_tick()`; added `sim_loop_start()`, `sim_loop_can_inject()`

**sim_loop.c** — complete rewrite of execution model:
- `g_can_rx[4]` — per-ECU deferred CAN frame queues
- `chip_avr_sleep()` — `avr->sleep` callback: just advances `avr->cycle`, no lock-while-sleeping
- `chip_pace_tick()` — per-chip real-time pacing, sleep capped at 50 ms for clean shutdown
- `chip_thread_fn()` — per-chip thread: drain CAN RX → check pause → run batch → pace
- `on_mcp_tx()` — enqueues to peers instead of calling `can_bus_broadcast` (which would race)
- `sim_loop_start()` — spawns chip threads
- `sim_loop_can_inject()` — thread-safe injection via CAN RX queues
- `sim_loop_pause_all()` — also resets `pace_init` on all chips under their locks
- `sim_loop_set_speed()` — same pace reset
- `sim_loop_shutdown()` — joins all threads before freeing chips

**main.c** — calls `sim_loop_start()` after pre-loading firmware; main loop no longer calls `sim_loop_tick()`; added `--speed=N` CLI flag

**cmd.c** — every `avr_raise_irq` call now takes `c->avr_lock` first:
- `apply_din`, `apply_adc`, `apply_uart` — lock before raise, unlock after
- `apply_load`, `apply_reset` — lock around entire operation, reset `pace_init`
- `apply_force_busoff`, `apply_can_errors`, `apply_can_recover` — lock around MCP2515 calls that may trigger INT pin
- `apply_can_inject` — uses `sim_loop_can_inject()` instead of `can_bus_inject()`
- `CMD_STEP` — emits a warning (not supported in threaded mode)
- `CMD_UNLOAD` (new) — stops chip without clearing flash; chip resumes when new firmware is loaded

### Real-Time Pacing

`g_speed` (volatile double, default 0.0):
- `0.0` — flat-out, no sleeping, maximum throughput
- `1.0` — real-time (each chip's simulated time tracks wall clock)
- `0.5` — half real-time

**Windows caveat:** Without `timeBeginPeriod(1)`, `nanosleep` has ~15.6 ms granularity. At real-time speed the chip will burst 15 ms of sim time then sleep 15 ms — average is correct, but bursty visually. This is acceptable.

**Why `timeBeginPeriod(1)` was removed:** It prevents CPU C-state transitions, inflating Task Manager CPU readings even during sleep (7.8% → 13.7%).

**`avr->sleep` callback** (`chip_avr_sleep`): fires when firmware executes AVR `SLEEP` instruction. Just advances `avr->cycle`; does not sleep (to avoid holding `avr_lock` during sleep). Batch pacing (`chip_pace_tick`) handles the actual wall-clock sleep after the lock is released.

**Arduino `delay()` note:** `delay()` compiles to a busy-wait loop — it does NOT use the AVR SLEEP instruction. There is no way to skip these cycles in instruction-accurate simulation. The ~6-7% per-chip CPU is the fundamental cost of simulating `delay()`-heavy firmware.

---

## Session 3 — UI Features

### Feature 1: Simulation Speed Field

**Where:** Second row of the Board.qml toolbar.

**UI:** `TextField` (type a float) + "1× RT" button (sets 1.0) + "Max" button (sets 0.0).

**Wire-up:**
- `qml_bridge.py`: `speedChanged` signal, `speed: float` property, `setSpeed(float)` slot
- `sim_proxy.py`: `cmd_speed(factor: float)` → `{"c":"speed","speed":N}`
- Engine: `CMD_SPEED` was already handled; `sim_loop_set_speed()` resets per-chip pacing

### Feature 2: Flash All ECUs

**Where:** Second toolbar row, leftmost button.

**Behavior:** Opens one file dialog; loads the selected `.hex` into ECU1, ECU2, ECU3, ECU4. MCU excluded (it's atmega328pb, different firmware).

**Wire-up:**
- `qml_bridge.py`: `openLoadAllDialog()` slot — calls `proxy.cmd_load(chip, path)` for each ECU
- No new engine command needed; reuses existing `CMD_LOAD`

### Feature 3: Eject Firmware Per ECU

**Where:** "Eject" button in each `EcuPanel.qml` title row (next to Plot button), red text.

**Behavior:** Stops the chip (`c->running = false`). Flash is NOT cleared — if you want to restart the same firmware, send `reset`.

**Wire-up:**
- `qml_bridge.py`: `unloadChip(chip: str)` slot → `proxy.cmd_unload(chip)`
- `sim_proxy.py`: `cmd_unload(chip)` → `{"c":"unload","chip":chip}`
- `proto.h`: `CMD_UNLOAD` added to `cmd_type_t`
- `proto.c`: `"unload"` added to `parse_cmd_type()`
- `cmd.c`: `apply_unload()` — locks `avr_lock`, sets `c->running = false`, resets `pace_init`

---

## Key Invariants to Preserve

1. **Never call `avr_raise_irq` on chip B's `avr_t` from chip A's thread.** Always enqueue into `g_can_rx[B]` instead.

2. **Lock order:** `avr_lock` → `can_rx.lock` → `g_out_mutex` (innermost). Never take `avr_lock` while holding `can_rx.lock`.

3. **`on_mcp_tx` fires inside `avr_run()` under chip A's `avr_lock`.** It may take `can_rx[peer].lock` (briefly). It must never take `can_rx[A].lock` or `chips[B].avr_lock`.

4. **`proto_emit_*` is always safe to call from any thread** — it takes `g_out_mutex` internally.

5. **`chip_pace_tick()` is called outside `avr_lock`** to avoid holding the lock during sleep.

6. **`g_speed = 0.0` is the default.** Set via UI speed field or `--speed=N` CLI flag.

---

## Protocol Reference (JSON-Lines)

### Engine → UI events

| `"t"` | Fields | Description |
|-------|--------|-------------|
| `"pin"` | chip, pin, val (0/1), ts | GPIO state change |
| `"pwm"` | chip, pin, duty (0..1), ts | PWM duty change |
| `"uart"` | chip, data (string), ts | UART TX byte(s) |
| `"can_tx"` | bus, src, id, ext, rtr, dlc, data (hex), ts | CAN frame transmitted |
| `"can_state"` | chip, tec, rec, state, ts | CAN error state change |
| `"lcd"` | chip, line0, line1, ts | LCD content change |
| `"log"` | level, msg | Diagnostic log |

### UI → Engine commands

| `"c"` | Fields | Description |
|-------|--------|-------------|
| `"load"` | chip, path | Load .hex firmware |
| `"unload"` | chip | Stop chip, clear running flag |
| `"reset"` | chip | Hard reset (firmware preserved) |
| `"din"` | chip, pin, val | Set digital input |
| `"adc"` | chip, ch, val | Set ADC channel value (0–1023) |
| `"uart"` | chip, data | Push bytes to chip UART RX |
| `"speed"` | speed (float) | Set real-time multiplier |
| `"pause"` | — | Pause all chips |
| `"resume"` | — | Resume all chips |
| `"can_inject"` | id, ext, rtr, dlc, data, bus | Inject CAN frame |
| `"force_busoff"` | chip | Force chip's MCP2515 into bus-off |
| `"can_recover"` | chip | Clear bus-off |
| `"can_errors"` | chip, tx, rx | Inject error counter increments |

---

## Peripheral Wiring (ANEB v1.1 Board Definition)

Each ECU (atmega328p, 16 MHz):

| Peripheral | AVR Pins |
|-----------|---------|
| MCP2515 CS | PB2 (Arduino D10) |
| MCP2515 INT | PD2 (INT0) |
| MCP2515 SPI | PB3 (MOSI), PB4 (MISO), PB5 (SCK) |
| I2C LCD PCF8574 | PC4 (SDA), PC5 (SCL) — addr 0x27 |
| DOUT0 (dimmable) | PD3 (OC2B) |
| DOUT1 | PD4 |
| LDR_LED | PD6 (OC0A) |
| LOOP PWM | PD5 (OC0B) |
| D9 PWM | PB1 (OC1A) |
| L LED | PB5 |
| DIN1–DIN4 | PC4/PC5/PB1/PB0 (A4/A5/D9/D8) |
| ADC channels | PC0–PC3 (AIN0–AIN3) |

MCU (atmega328pb) has no MCP2515 and no LCD.

---

## Session 4 — TCP UART Server (Virtual COM Ports)

### Goal

Expose each chip's simulated UART as a real TCP socket on localhost so external tools (avrdude, serial terminals, remote_flasher) can communicate with simulated chips without going through the JSON-Lines protocol.

### Architecture

```
simulated chip UART
    │ (avr_raise_irq UART_IRQ_OUTPUT → on_uart_byte)
    ▼
uart_server TX ring buffer  ──flush_tx()──►  TCP socket (localhost:860x)
                                                    │
                                           ◄── recv() in server thread
uart_server RX ring buffer  ◄──pop_rx()──    RX ring buffer
    │ (avr_raise_irq UART_IRQ_INPUT in chip thread)
    ▼
simulated chip UART RX
```

### TCP Ports

| Chip  | Port |
|-------|------|
| ecu1  | 8600 |
| ecu2  | 8601 |
| ecu3  | 8602 |
| ecu4  | 8603 |
| mcu   | 8604 |

### Reset-on-Connect

When a TCP client connects, `uart_server_pop_connect()` returns true and the chip thread calls `chip_reset()` under `avr_lock`. This emulates the DTR toggle that real Arduinos receive when a COM port is opened. Optiboot starts its bootloader window at that moment.

**Critical**: Set simulation speed to **1.0× (real-time)** before flashing. At flat-out speed the bootloader watchdog fires in microseconds of wall time — avrdude cannot respond in time.

### avrdude Usage (no COM ports needed)

```bash
# Set sim speed to 1.0 first in the UI, then:
avrdude -c arduino -p atmega328p -b 115200 -P net:localhost:8600 -U flash:w:firmware.hex
```

avrdude's `-c arduino` programmer supports `net:host:port` syntax natively — no virtual COM port driver required.

### Virtual COM Port Bridge (optional, requires com0com)

For tools that only accept COM port names (not `net:` syntax):

1. Install [com0com](https://sourceforge.net/projects/com0com/) — creates null-modem pairs
2. Create pairs: COM10↔COM11 (ecu1), COM12↔COM13 (ecu2), COM14↔COM15 (ecu3), COM16↔COM17 (ecu4)
3. Run the bridge (bridge connects to COM10/12/14/16; user tools use COM11/13/15/17):
   ```
   python -m aneb_ui.uart_bridge COM10 COM12 COM14 COM16
   ```
4. avrdude: `avrdude -c arduino -p atmega328p -b 115200 -P COM11 -U flash:w:firmware.hex`

### Files Changed

**`aneb-sim/src/uart_server.h`** (new) — API: `uart_server_init`, `uart_server_start`, `push_tx`, `flush_tx`, `pop_rx`, `pop_connect`, `port`, `shutdown`

**`aneb-sim/src/uart_server.c`** (new) — Winsock2 TCP server; one accept/recv pthread per chip; TX and RX ring buffers (4096 bytes each) with separate mutexes; `new_connect` atomic flag for reset-on-connect; graceful shutdown via `g_stop` + closing listen sockets

**`aneb-sim/src/sim_loop.c`** — added `#include "uart_server.h"`; `g_uart_rx_irq[SIM_MAX_CHIPS]` cached in `wire_chip_irqs`; `on_uart_byte` calls `uart_server_push_tx`; `chip_thread_fn` checks `pop_connect` → `chip_reset`, drains TCP RX under `avr_lock`, flushes TCP TX after unlock; `sim_loop_init` calls `uart_server_init`; `sim_loop_start` calls `uart_server_start`; `sim_loop_shutdown` calls `uart_server_shutdown`

**`aneb-sim/aneb-sim/CMakeLists.txt`** — added `src/uart_server.c` to `aneb-sim` sources

**`aneb-ui/aneb_ui/uart_bridge.py`** (new) — `ChipBridge` (TCP↔pyserial, two daemon threads), `UartBridgeManager` (starts/stops all bridges), CLI entry point

**`aneb-ui/aneb_ui/qml_bridge.py`** — added `uartPorts` constant property (`{"ecu1":8600,...}`)

**`aneb-ui/aneb_ui/qml/Board.qml`** — added UART TCP port badges in second toolbar row (chip:port labels with avrdude tooltip); requires `Speed=1.0` reminder in tooltip

### Locking Notes

- `push_tx()` is called inside `avr_lock` (from `on_uart_byte`); takes only `tx.lock` (a different lock) — no deadlock
- `flush_tx()` called outside `avr_lock`; takes `tx.lock` then `client_lock` — always in this order
- Server recv thread: takes `client_lock` (on accept), then `rx.lock` (on recv) — never takes `avr_lock`
- `pop_rx()` called inside `avr_lock`; takes only `rx.lock` — server thread never holds `avr_lock`

### Next Step: Modify remote_flasher for Local Use

`C:\Users\renat\OneDrive\Pos\RTOS\remote_flasher` currently connects to lab PCs via SSH/paramiko, then runs `avrdude -c arduino -p atmega328p -b 115200 -P COMx` remotely. To use with the simulator:
- Replace SSH transport with localhost connection
- Change avrdude port to `net:localhost:8600` (or use COM port bridge)
- Replace `serialterm.py`'s SSH serial forwarding with direct TCP socket to the simulator UART ports

---

## Session 5 — avrdude Flashing from the UI

### Goal

Add a "Flash ▸" button to each ECU panel that drives the full avrdude workflow — file dialog, speed enforcement, Optiboot bootstrap, TCP connect, live output — without the user touching a terminal.

### How It Works

```
User clicks "Flash ▸"
    │
    ├─ Python opens file dialog → user picks application .hex
    ├─ _find_optiboot() → loads Optiboot into the chip via cmd_load
    │    (chip immediately starts running the bootloader)
    ├─ setSpeed(1.0) if not already (bootloader timing requirement)
    ├─ starts _run_avrdude() in a daemon thread
    │
avrdude process
    │
    ├─ opens TCP net:localhost:860x
    │    │
    │    └─ chip_thread_fn sees pop_connect → chip_reset()
    │         (Optiboot restarts, stale TX buffer cleared)
    │
    ├─ sends STK_GET_SYNC (0x30)
    ├─ chip runs Optiboot, responds 0x14 0x10
    ├─ STK500 handshake completes → flashes application .hex
    └─ chip resets → application runs
```

### Files Changed

**`aneb-ui/aneb_ui/qml_bridge.py`**
- `avrdudeOutput = pyqtSignal(str, str)` — (chip, line) streamed from background thread
- `avrdudeStateChanged = pyqtSignal(str, bool)` — (chip, is_running) for button state
- `_CHIP_MCU`, `_CHIP_TCP_PORT` — chip-to-MCU and chip-to-port dicts
- `_AVRDUDE_SEARCH` — fallback paths (Arduino IDE, msys64)
- `_OPTIBOOT_SEARCH` — Arduino IDE bundled Optiboot paths
- `_find_avrdude()` — searches PATH then fallback list
- `_find_avrdude_conf(exe)` — finds `etc/avrdude.conf` relative to binary (needed on Windows)
- `_find_optiboot()` — searches Arduino IDE bootloader directory
- `flashChipAvrdude(chip)` slot — full workflow: find tools, dialog, load Optiboot, set speed, spawn thread
- `_run_avrdude(chip, cmd)` — runs subprocess, streams each output line via signal

**`aneb-ui/aneb_ui/qml/AvrdudeWindow.qml`** (new)
- Floating `Window` per chip, auto-opens when flash starts
- `TextArea` for live avrdude output, `ScrollView` auto-scrolls
- Animated yellow blink indicator while running, green when idle
- "Flash Again" button (re-triggers `flashChipAvrdude`), "Clear" button
- Connects to `bridge.avrdudeOutput` and `bridge.avrdudeStateChanged`

**`aneb-ui/aneb_ui/qml/EcuPanel.qml`**
- "Flash ▸" / "Flashing…" button added to title row (between Plot and Eject)
- Button text and color reflect busy state (`_busy` bool, yellow while running, cyan when idle)
- `AvrdudeWindow { id: avrdudeWindow }` instance added alongside console/plotter windows

**`aneb-sim/src/uart_server.c`**
- TX and RX ring buffers are cleared on new TCP client connect, before setting `client_sock`
- Prevents stale firmware UART output from reaching avrdude's STK500 receiver

### Bugs Fixed

#### 1. avrdude can't find config file

**Symptom:** `avrdude.exe: can't open config file "": Invalid argument`

**Cause:** Arduino IDE's avrdude.exe expects `avrdude.conf` relative to itself. When launched from Python subprocess, it can't find the config file by the default empty path.

**Fix:** `_find_avrdude_conf()` looks for `bin/../etc/avrdude.conf` relative to the avrdude binary and passes `-C <path>` to avrdude.

#### 2. `resp=0xac` — not in sync

**Symptom:** `stk500_getsync() attempt 1 of 10: not in sync: resp=0xac`

**Root causes (both must be fixed):**

a. **Stale TX data** — the chip was running application firmware that sent UART bytes. Those bytes sat in the TX ring buffer and were delivered to avrdude when it connected, before any Optiboot response arrived. avrdude received `0xac` (garbage from application) instead of `0x14` (STK_INSYNC from Optiboot).
   - Fix: clear TX (and RX) ring buffer in `uart_server_thread` immediately on accept, before setting `client_sock`.

b. **No Optiboot in flash** — the chip had `io_test.hex` (application firmware, no bootloader). avrdude `-c arduino` speaks STK500 to Optiboot; without Optiboot the chip has no protocol handler.
   - Fix: `flashChipAvrdude` calls `cmd_load(chip, optiboot_path)` first, loading `optiboot_atmega328.hex` from the Arduino IDE installation. The chip_reset on TCP connect then starts Optiboot (not the application).

#### 3. `ser_drain(): read error: Parâmetro incorreto` (harmless)

**Cause:** avrdude calls `tcflush()` (serial-port-specific API) on a Windows TCP socket — fails with WSAEINVAL. This is a known avrdude limitation on Windows with `net:` addresses.

**Impact:** None — avrdude prints the warning and continues. The three repeated errors appear during avrdude's startup phase before it attempts STK500 sync.

#### 4. `resp=0x3f` — chip crashes after Optiboot load

**Symptom:** Engine logs `read_ihex_chunks: ...optiboot..., unsupported check type 03`, then `Additional data blocks were ignored.`, then `avr_sadly_crashed`. avrdude sees `resp=0x3f` (or any random byte).

**Root cause:** simavr's `read_ihex_file()` returns only the **first contiguous chunk** of an Intel-HEX file and silently drops the rest. Optiboot's `.hex` contains a record-type `03` (Start Segment Address — CS:IP for DOS, irrelevant on AVR) which isn't a real problem in itself, but it triggers simavr's "unsupported" warning *and* the file's data layout produces multiple chunks, of which only the first survives. Result: Optiboot is partially loaded, the chip resets to PC=0 on TCP connect (where flash is 0xFF), and the AVR walks into garbage and crashes.

**Fix:** Replaced `read_ihex_file()` with a custom Intel-HEX parser in [chip.c](aneb-sim/src/chip.c) (`load_ihex_into_flash`). It handles record types 00/01/02/03/04/05 and writes every data record at its absolute flash address — no chunking. Also sets `avr->reset_pc = load_base` so reset-on-connect lands on Optiboot's bootloader entry (0x7E00) instead of 0x0000, emulating the BOOTRST fuse. Application firmware loaded at base 0 is unaffected.

#### 5. Optiboot loaded but chip still crashes ~1s after avrdude connects

**Symptom:** With the parser fix in #4, the engine still logs `avr_sadly_crashed` about a second after `[uart:0] client connected`. avrdude prints `resp=0x3f`.

**Root cause:** Modern Optiboot (≥ v6, including Arduino IDE's bundled v8) reads `MCUSR` on entry: if the EXTRF (external-reset) bit isn't set it treats the boot as a warm reset and `JMP 0x0000` to the application. simavr's `avr_reset()` zeroes all I/O regs (including MCUSR), so Optiboot sees `MCUSR=0`, jumps to 0x0000 where flash is empty (0xFF), walks `SBRS r31,7` instructions until PC ≥ flashend, and simavr crashes the chip.

**Fix:** After `avr_reset` in `chip_reset()`, set `c->avr->data[0x54] = 0x02` (MCUSR.EXTRF on atmega328p — I/O reg 0x34 → data 0x54, bit 1). Same write in `chip_load_hex()` for the initial run between load and the first TCP connect. This emulates avrdude's DTR pulse acting as an external-reset, so Optiboot enters its STK500 sync-wait loop instead of jumping to empty application flash.

#### 6. STK500 framing slides by 4 bytes after sync — `(a) protocol error`

**Symptom:** Sync handshake works, but `stk500_getparm` and `stk500_initialize` print `(a) protocol error, expect=0x14, resp=0x14` / `resp=0x10` / `resp=0x04`. avrdude exits with rc=-1.

**Root cause:** avrdude's `serial_drain` on Windows TCP calls `tcflush`/`PurgeComm` which both fail on a socket fd (the `Parâmetro incorreto` warnings at the top of every flash log are exactly this). With drain broken, avrdude's sync routine sends `30 20` three times in <1 ms (rapid retries while waiting for the bootloader's first reply). Optiboot dutifully replies `14 10` to each → 6 bytes queue up in avrdude's TCP recv buffer. avrdude consumes 2 (`14 10`) for sync success and leaves 4 leftover. Each subsequent `getparm`/`set_device` call then reads the leftover instead of the new reply, sliding the framing by 4 every command.

**Fix:** Coalesce duplicate STK_GET_SYNC at the engine's `uart_server` recv path. When a recv batch is exactly 2 bytes equal to `30 20` AND the previous matching batch was less than 100 ms ago, drop it before pushing into the chip's RX ring. The bootloader replies exactly once per sync burst; no leftover bytes accumulate in the TCP buffer; subsequent commands frame correctly. Match is restricted to exact 2-byte recv batches so flash-programming payloads (which can legitimately contain `0x30,0x20` byte sequences) are never affected.

See [aneb-sim/src/uart_server.c](aneb-sim/src/uart_server.c) — the `last_sync_ns` field on `chip_uart_t` and the filter in the recv loop.

#### 7. UART RX overrun on STK_PROG_PAGE — `resp=0x3f` mid-flash

**Symptom:** Init/erase succeed, then while writing flash: dozens of `UART0: avr_uart_irq_input: RX buffer overrun, lost char=...` messages from simavr, followed by `stk500_paged_write(): (a) protocol error, expect=0x14, resp=0x3f`.

**Root cause:** simavr's UART input FIFO is hard-coded to 64 bytes (`DECLARE_FIFO(uint16_t, uart_fifo, 64)` in `avr_uart.h`). Avrdude's `STK_PROG_PAGE` for atmega328p sends `0x64 size_hi size_lo memtype <128 data bytes> 0x20` = ~133 bytes in a single TCP write. The chip thread's old RX-drain loop popped every byte from our ring and called `avr_raise_irq` 133 times in one batch with no pacing. simavr appended the first 64 to its FIFO and dropped the remaining 69 with `DOR=1`. Optiboot then read a truncated page header (size and data fields shifted), wrote junk to flash, replied with whatever was in `r24` (`0x3F` here — `?` from a partially-loaded register), and avrdude saw protocol garbage.

**Fix:** Pace the IRQ rate to match the UART byte rate (16 MHz / (115200 baud / 10 bits-per-byte) ≈ 1389 cycles per byte). The chip thread now tracks `uart_rx_due_cycle` per chip; only pops the next byte from the TCP ring once the AVR's `cycle` counter has advanced that many cycles past the previous push. simavr's FIFO drains at the same rate it fills, so it stays well under 64 even for the full 133-byte page burst.

See [aneb-sim/src/chip.h](aneb-sim/src/chip.h) (`uart_rx_due_cycle`) and [aneb-sim/src/sim_loop.c](aneb-sim/src/sim_loop.c) (the paced drain loop in `chip_thread_fn`).

#### 8. UI freezes 10–20 s during avrdude verify

**Symptom:** Flash succeeds end-to-end, but the QML window stops repainting for ~20 s during `avrdude.exe: verifying ...`. avrdude itself reports the verify took only ~0.5 s.

**Root cause:** `on_uart_byte` in `sim_loop.c` emits a JSON-Lines `uart` event for *every* TX byte. During a flash+verify the chip emits ~10K bytes (Optiboot replying to STK_PROG_PAGE / STK_READ_PAGE), so the QML side gets ~10K signals that each append to the per-chip serial console's `TextArea`. QML's text-edit append is roughly O(n) in current text length, so the back-pressure peaks well after avrdude has finished and the GUI thread is still draining the queue.

**Fix:** When a TCP client is attached to that chip's UART (i.e. avrdude is using it), suppress the JSON emit. The bytes still go to the TCP socket — that's where avrdude needs them. The serial console wouldn't show anything human-readable during a flash anyway (binary STK500 frames). Implemented via an atomic flag `client_attached` on `chip_uart_t`, exposed as `uart_server_has_client(idx)`, checked once per byte in `on_uart_byte`.

See [aneb-sim/src/uart_server.h](aneb-sim/src/uart_server.h) and the early-return in `on_uart_byte` at [aneb-sim/src/sim_loop.c](aneb-sim/src/sim_loop.c).

### Optiboot Requirement

avrdude `-c arduino` is the Arduino bootloader programmer; it speaks STK500v1 to **Optiboot** running on the chip. The workflow requires:
- Optiboot is auto-loaded by `flashChipAvrdude` from `hardware\arduino\avr\bootloaders\optiboot\optiboot_atmega328.hex`
- Chip must be **running** when avrdude connects (reset-on-connect needs a live chip thread)
- Simulation speed must be **1.0×** so Optiboot's 1-second watchdog window is in wall-clock time

After avrdude finishes, Optiboot remains in the upper bootloader section of flash; the new application occupies `0x0000` onwards. On the next reset, Optiboot runs briefly, times out (no avrdude), and jumps to the application.

### Known Limitation

Optiboot path is hard-coded to the Arduino IDE installation locations. If Optiboot is not found at those paths (e.g. Arduino 2.x which uses a different directory layout), the flash window shows a clear error with the expected path.

---

## Known Issues / Future Work

- **`CMD_STEP` not implemented in threaded mode** — emits a warning. Would need per-chip synchronization barriers.
- **Windows pacing is bursty** (~15 ms bursts at real-time speed) due to OS timer resolution. `timeBeginPeriod(1)` would help but inflates Task Manager CPU. Alternative: use `CreateWaitableTimerEx` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` flag (available Windows 10 1803+).
- **`can_bus_t.frames_delivered/broadcast` counters** — plain `uint64_t` incremented from multiple threads. Benign race (diagnostic counters only); could use `_Atomic uint64_t` if needed.
- **`g_speed` is `volatile double`** — technically a data race, benign in practice on x86-64. Could use `_Atomic double`.
- **Scaling to 16 chips** — `SIM_MAX_CHIPS` is 5 in `sim_loop.h`. The CAN RX queue array `g_can_rx[4]` and `g_can[4]` are hardcoded to 4 ECUs. To add more ECUs: increase `SIM_MAX_CHIPS`, expand `g_can` and `g_can_rx`, and update `wire_mcp2515` / `wire_lcd` loops and the `has_mcp = (idx < 4)` guard in `chip_thread_fn`.
