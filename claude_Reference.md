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

## Known Issues / Future Work

- **`CMD_STEP` not implemented in threaded mode** — emits a warning. Would need per-chip synchronization barriers.
- **Windows pacing is bursty** (~15 ms bursts at real-time speed) due to OS timer resolution. `timeBeginPeriod(1)` would help but inflates Task Manager CPU. Alternative: use `CreateWaitableTimerEx` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` flag (available Windows 10 1803+).
- **`can_bus_t.frames_delivered/broadcast` counters** — plain `uint64_t` incremented from multiple threads. Benign race (diagnostic counters only); could use `_Atomic uint64_t` if needed.
- **`g_speed` is `volatile double`** — technically a data race, benign in practice on x86-64. Could use `_Atomic double`.
- **Scaling to 16 chips** — `SIM_MAX_CHIPS` is 5 in `sim_loop.h`. The CAN RX queue array `g_can_rx[4]` and `g_can[4]` are hardcoded to 4 ECUs. To add more ECUs: increase `SIM_MAX_CHIPS`, expand `g_can` and `g_can_rx`, and update `wire_mcp2515` / `wire_lcd` loops and the `has_mcp = (idx < 4)` guard in `chip_thread_fn`.
