# Gemini Pro prompt — generate QML for the ANEB v1.1 simulator UI

> **Pass this entire document to Gemini Pro along with the attached
> photo of the physical Automotive Network Evaluation Board (ANEB v1.1).**
> Use the image-generation tool ("Nano Banana") freely for any board /
> chip / texture assets that improve the result.

---

## 1. What I want from you

I have a working C engine called `aneb-sim` that simulates the **ANEB
v1.1 hardware lab board** — five Atmel chips on one process: 4 ×
ATmega328P "ECUs" running student RTOS firmware + 1 × ATmega328PB
"MCU" board controller, all connected via MCP2515 controllers on a
single CAN bus. The engine emits live state as JSON-Lines events
on stdout and accepts JSON-Lines commands on stdin. See section 5
for the wire protocol.

I have a first-pass PyQt6 UI that drives this engine. It works but
**looks generic**. I want you to generate a complete set of **QML
files** (with the matching minimal Python ↔ QML bridge code) that
give the UI a much more authentic look — closer to the actual board
in the photo.

The goal: when a student looks at the simulator, the muscle-memory
transfer to the real jig should be near-perfect. They should
recognize the layout, the colors, the trim pots, the LEDs, the chip
placements.

**Output everything as a single `.zip`-equivalent of fenced code blocks**
(one per file) so I can drop them straight into the project.

## 2. Reference: existing QML in `remote_flasher`

I have another project, `remote_flasher`, that uses QML extensively
and that I want you to **match the conventions of**. It's at
`C:\Users\renat\OneDrive\Pos\RTOS\remote_flasher\src\qml\`. Key
patterns to mimic:

- `QtQuick 2.15` + `QtQuick.Controls 2.15` + `QtQuick.Layouts 1.15`
  imports.
- Widgets are `Item { id: root; property ... }` with the public API
  declared up top.
- A `_dim` style helper for size-relative scaling
  (`width * 0.05`, etc.) so the widgets remain crisp at any size.
- `Behavior on <prop> { NumberAnimation { duration: 200 } }` for soft
  state transitions (LED brightness, color fades).
- Glow halos via a slightly larger transparent `Rectangle` with
  `border.color: onColor; opacity: 0.4` underneath the LED.
- For richer drawing (gauges, dynamic curves), `Canvas` with
  `renderTarget: Canvas.Image` and `onPaint` doing 2D context calls.
- Tooltips via a `MouseArea { hoverEnabled: true; ToolTip.visible:
  containsMouse }`.

Concrete files to study before generating:

- `remote_flasher/src/qml/StatusLight.qml` — small green/grey LED
  with active-state glow. **The LED widget you produce should look
  like a more polished version of this.**
- `remote_flasher/src/qml/InfoCard.qml` — stylized panel with a
  dark navy background, accent top stripe, label + value layout.
  Use this style for the per-ECU stat cards (TEC/REC, current state).
- `remote_flasher/src/qml/hmi/main.qml` — adaptive scaling pattern
  (fixed canvas + uniform scale to fit). Reuse for the board view.
- `remote_flasher/src/qml/hmi/Gauge.qml` — Canvas-based artwork.
  Pattern for the rotary trim pots and the chip silkscreen.

## 3. Reference: the actual board (attached photo)

The attached photo shows the ANEB v1.1, top view. Read it carefully:

- **Background PCB**: pale blue / cyan, glossy.
- **Top center**: a daughterboard labeled "Automotive Network
  Evaluation Board" mounted on the main PCB. It has yellow LEDs and
  small components — this is the **MCU controller** (ATmega328PB,
  not a Nano).
- **2 × 2 Nano grid**: four Arduino Nanos plugged into header
  sockets, one per quadrant:
  - Top-left: **ECU 1** (CAN1 only, has the LDR + LDR_LED + buzzer)
  - Top-right: **ECU 2** (CAN1 + CAN2, has the LOOP A7)
  - Bottom-left: **ECU 4** (CAN1 or CAN2)
  - Bottom-right: **ECU 3** (CAN1 or CAN2, LIN bus)
- **Right side**: a vertical column of **4 blue trim potentiometers**
  (the AIN0..AIN3 inputs that all four ECUs share via mux) and **4
  pushbuttons** (DIN0..DIN3 inputs).
- **Bottom-left strip**: a row of **digital-output indicator LEDs**
  in two rows (DO#1..DO#4 yellow + ECU#1..ECU#4 red column status).
- **Bottom-center**: **CAN BUS#1** and **CAN BUS#2** screw-terminal
  blocks (with small green LEDs near each).
- **Bottom-right**: **buzzer** (round black cylinder), and the
  **LDR/LED** photoresistor pair.
- **Top-right**: USB connector for the board controller and a small
  panel of yellow LEDs (status row).
- The four Nanos each have their **own mini-USB** connectors at the
  outer edges of the 2 × 2 grid — these are how students flash each
  ECU.

Use Nano Banana to generate any of these as image assets if it makes
the result more authentic — a top-down Arduino Nano render, an
ATmega328P chip render, a blue trim pot, a pushbutton cap, the green
PCB texture. Resolution target: 2× the rendered size for retina-clean
display.

## 4. The five chips

| Role | MCU type    | Firmware           | Wiring on the bus |
|------|-------------|--------------------|-------------------|
| ECU1 | ATmega328P  | student RTOS       | CAN1; has LDR+LDR_LED+buzzer |
| ECU2 | ATmega328P  | student RTOS       | CAN1; has LOOP A7 |
| ECU3 | ATmega328P  | student RTOS       | CAN1; LIN bus     |
| ECU4 | ATmega328P  | student RTOS       | CAN1; LIN bus     |
| MCU  | ATmega328PB | `MCU_Firmware_v08` | board controller  |

All four ECUs share **CAN1** in the simulator (CAN2 is not modeled).

## 5. JSON wire protocol the QML must render

The engine speaks JSON Lines on stdio. A single `"v":1` envelope
field marks every message. Events (`"t":...`) flow engine → UI;
commands (`"c":...`) flow UI → engine.

### Engine → UI events

```jsonc
{"v":1,"t":"pin","chip":"ecu1","pin":"PB5","val":1,"ts":12345}
{"v":1,"t":"pwm","chip":"ecu1","pin":"PD6","duty":0.42,"ts":12345}
{"v":1,"t":"uart","chip":"ecu1","data":"hello\n","ts":12345}
{"v":1,"t":"can_tx","bus":"can1","src":"ecu1","id":"0x123","ext":false,"rtr":false,"dlc":2,"data":"CAFE","ts":12345}
{"v":1,"t":"can_state","chip":"ecu2","tec":40,"rec":0,"state":"active","ts":12345}
{"v":1,"t":"log","level":"info","msg":"..."}
```

`pin` carries port-form names ("PB5" = port B bit 5). The full
schema is in the project's `docs/PROTOCOL.md`.

### UI → engine commands

```jsonc
{"v":1,"c":"din","chip":"ecu1","pin":"PD2","val":1}
{"v":1,"c":"adc","chip":"ecu1","ch":0,"val":512}
{"v":1,"c":"uart","chip":"ecu1","data":"reset\n"}
{"v":1,"c":"load","chip":"ecu1","path":".../firmware.hex"}
{"v":1,"c":"reset","chip":"ecu1"}
{"v":1,"c":"can_inject","bus":"can1","id":"0x123","ext":false,"rtr":false,"dlc":2,"data":"CAFE"}
{"v":1,"c":"force_busoff","chip":"ecu2"}
{"v":1,"c":"can_recover","chip":"ecu2"}
{"v":1,"c":"can_errors","chip":"ecu1","tx":4,"rx":0}
{"v":1,"c":"pause"} {"v":1,"c":"resume"}
```

The QML widgets must **emit Qt signals** that the Python bridge
translates into these commands. Don't construct JSON in QML — the
bridge does that.

## 6. Pin map (per ECU, identical across ECU1..ECU4)

This comes straight from `Board.h` in the project:

| Function     | Arduino pin | AVR port  | Notes                   |
|--------------|-------------|-----------|-------------------------|
| DIN0 / DIN1  | A4 / A5     | PC4 / PC5 | pushbutton inputs       |
| DIN2 / DIN3  | D9 / D8     | PB1 / PB0 | pushbutton inputs       |
| AIN0..AIN3   | A0..A3      | PC0..PC3  | trim-pot inputs (10-bit)|
| DOUT0 / DOUT1| D3 / D4     | PD3 / PD4 | LED outputs             |
| BUZZER (ECU1)| D7          | PD7       | digital                 |
| LDR_LED PWM  | D6          | PD6       | OC0A (8-bit fast PWM)   |
| LOOP PWM     | D5          | PD5       | OC0B PWM                |
| LDR ADC      | A6          | -         | ADC ch 6 (no digital)   |
| LOOP ADC     | A7          | -         | ADC ch 7 (no digital)   |
| MCP2515 CS   | D10         | PB2       | SPI chip select         |
| MCP2515 INT  | D2          | PD2       | INT0                    |
| LED_BUILTIN  | D13         | PB5       | the on-board "L" LED    |

ECU3 / ECU4 also have LIN TX/RX on D5 / D6 (alternative use).

The MCU (ATmega328PB) has its own pin map for the two **Mode
Selector** buttons + status LEDs; render it as a smaller daughterboard
with two big buttons labeled "Mode 1" / "Mode 2" at the top.

## 7. Concrete deliverables

Generate these files, in this exact directory layout. The Python
side currently lives at `aneb-ui/aneb_ui/`. Replace the existing
widget directory with QML.

```
aneb-ui/aneb_ui/qml/
  Board.qml               ← the main board view (cyan PCB, 2×2 ECU
                            grid, MCU at top-center, peripherals
                            clustered at the right + bottom)
  ArduinoNano.qml         ← top-down Nano render with live pin states
  Mcu.qml                 ← ATmega328PB daughterboard render
  Led.qml                 ← stylized LED with glow + brightness 0..1
  TrimPot.qml             ← rotary blue trim pot (drag vertically)
  PushButton.qml          ← tactile-style cap (momentary or latching)
  CanIndicator.qml        ← small CAN_BUS#1/#2 screw-terminal block
                            with TEC/REC/state badge
  CanMonitor.qml          ← live table of CAN frames
  CanInject.qml           ← form for sending arbitrary frames
  SerialConsole.qml       ← per-ECU UART terminal
  EcuPanel.qml            ← composes one Nano + its peripherals
                            (LEDs, buttons, pots) at the right
                            cluster positions
  qmldir                  ← exposes all components

aneb-ui/aneb_ui/qml_bridge.py
                          ← QObject subclass exposing every state
                            field as a notifying Qt property and
                            every UI input as a Slot that calls
                            SimProxy.send_command()

aneb-ui/aneb_ui/qml_assets/   ← any PNG/SVG that Nano Banana
                                generates for you (board background,
                                Nano top-down, chip render, etc.)

aneb-ui/aneb_ui/app_qml.py    ← MainWindow that loads Board.qml in a
                                QQuickWidget and wires the bridge
                                to the existing SimProxy + SimState
```

The existing `sim_proxy.py` and `state.py` are good and should not
need changes — your bridge sits between `state.py` (already emits
fine-grained Qt signals on every event) and the QML.

## 8. Python ↔ QML bridge — pattern to follow

`remote_flasher` uses this pattern (see
`src/dashboard_backend.py` and `src/tabs/hmi_tab.py`):

```python
from PySide6.QtCore  import QObject, Signal, Slot, Property
from PySide6.QtQml   import qmlRegisterType
from PySide6.QtQuickWidgets import QQuickWidget

class _Bridge(QObject):
    speedChanged = Signal()
    @Property(float, notify=speedChanged)
    def speed(self): return self._speed
    # ...

    @Slot(int)
    def setMode(self, mode): ...    # called from QML
```

**Use PyQt6** in our project, NOT PySide6 — but the API is
identical: `from PyQt6.QtCore import pyqtProperty as Property,
pyqtSignal as Signal, pyqtSlot as Slot`. Keep the `_Bridge` helper
pattern.

The bridge needs these notifying properties (one per
state-section), each backed by `state.py`'s observable store:

| Property        | Type                     | Emitted by                         |
|-----------------|--------------------------|------------------------------------|
| `pinStates`     | `dict` (chip → pin → 0/1)| `state.pin_changed`                |
| `pwmDuties`     | `dict` (chip → pin → 0..1)| `state.pwm_changed`               |
| `uartLines`     | `dict` (chip → str)      | `state.uart_appended`              |
| `canFrames`     | `list` (last 1000)       | `state.can_tx_appended`            |
| `canStates`     | `dict` (chip → {tec,rec,state}) | `state.can_state_changed`   |

And these slots, called from QML on user input:

| Slot                                          | Maps to engine command |
|-----------------------------------------------|------------------------|
| `setDin(chip:str, pin:str, val:int)`          | `din`                  |
| `setAdc(chip:str, ch:int, val:int)`           | `adc`                  |
| `sendUart(chip:str, text:str)`                | `uart`                 |
| `loadFirmware(chip:str, path:str)`            | `load`                 |
| `injectCan(id:int, data:str, ext:bool, ...)`  | `can_inject`           |
| `forceBusoff(chip:str)`                       | `force_busoff`         |
| `recoverChip(chip:str)`                       | `can_recover`          |
| `pauseEngine()` / `resumeEngine()`            | `pause` / `resume`     |

## 9. Visual specification

### Color palette

- Main PCB background (`Board.qml`): **#9fbfd0** (pale cyan), with
  a subtle solder-mask texture / very gentle scratches generated by
  Nano Banana. Slight gradient from top (lighter) to bottom (darker).
- Nano PCB: **#1a6b6b** (teal-green), with the gold pad rings on
  every header pin.
- LED on (DOUT, DIN-button-feedback): **#ffd24a** (amber).
- LED on (built-in D13): **#ffaa22** (warmer amber).
- LED on (BUZZ): **#ff4444** (red).
- LED on (CAN status, healthy): **#22cc44** (green).
- LED off: very dark grey (`#1a1a1a`) with no glow.
- Trim pot body: **#1860a0** (medium blue) with white tick mark.
- Pushbutton cap: black plastic with brushed-metal collar.
- Text labels (silkscreen): **#f0f5ed** off-white, monospace, small.

### Sizes / proportions

- Default canvas: 1600 × 1000.
- ECU panel "tile" (one Nano + its peripherals): roughly
  600 × 400 px each in the 2×2 grid.
- Per Nano: ~420 × 160 px (matches the real Nano aspect ratio).
- Pots and pushbuttons clustered to the **right** of the ECU grid
  (not inside each ECU panel) — matches the physical board where
  the pots are a shared column on the right edge.
- Bottom strip: CAN BUS terminal blocks + the indicator LED row.

### Live-state behavior

- **Pin events**: when `pinStates[chip][pin]` flips to 1, the
  corresponding header-pin dot on the Nano lights up in amber with a
  glow halo, animated with `Behavior on opacity { NumberAnimation {
  duration: 80 } }`.
- **PWM events**: same but with brightness scaled to `duty`.
- **UART events**: flash the on-board TX or RX LED for ~150 ms,
  decay smoothly. Append the text to the chip's serial console.
- **CAN frames**: append a row to the CAN monitor table; pulse the
  CAN BUS#1 indicator LED briefly.
- **Bus-off / passive**: change the CAN status badge color
  (active=green, passive=orange, bus-off=red).

## 10. Acceptance criteria

A reviewer (the project owner) should be able to:

1. Look at the simulator UI alongside the board photo and recognize
   the same layout — Nanos in the same 2×2 positions, pots in the
   same column, buttons in the same row, CAN screw terminals in the
   same place.
2. Click a pushbutton on the right column and see the corresponding
   ECU's PD0/PD1/PC4/PC5 pin dot light up on the Nano illustration.
3. Drag a trim pot and see ECU firmware's `analogRead(A0)` reflect
   the value (visible because the firmware will print it on the
   serial console).
4. Watch a "blink" firmware run on ECU1 and see both:
   - The on-board "L" LED on the Nano illustration pulse.
   - The DOUT0 LED on the right-side breakout strip pulse (if the
     firmware drives D3).
5. Inject a CAN frame from the form and see it appear in the live
   monitor table; also see the can1 indicator LED flash.
6. Force ECU2 into bus-off via the button and see its status badge
   turn red; click recover and watch it return to green.

## 11. What you should NOT do

- **Don't** re-implement the C engine or the JSON proto — they're
  fixed and tested. Just consume the events / produce the commands.
- **Don't** invent new commands or events — stick to the v1 schema
  in section 5.
- **Don't** depend on PySide6 — use PyQt6 (or note clearly if you
  must use PySide6 and explain why).
- **Don't** ship binary asset blobs in the QML — keep them in
  `aneb-ui/aneb_ui/qml_assets/` and reference by relative path.
- **Don't** touch `aneb-sim/` (the C engine) or `external/`.
- **Don't** add network or filesystem features — the engine is the
  only IO boundary.

## 12. Constraints to honor

- 5-chip total simulation, **≤ 1 GB RAM** budget for engine + UI +
  child processes combined. Avoid heavy effects (full-window blur,
  16K textures) that would blow the memory budget.
- ≥ 30 fps on a commodity laptop with integrated graphics. Prefer
  static SVG / pre-rendered PNG over per-frame Canvas paints when
  possible.
- All text must remain readable at 1280 × 720 (the smallest screen
  a student is likely to use).
- The UI must work without an internet connection — embed all
  fonts and assets locally.

## 13. Now, please generate

Output, in order:

1. The 12 QML files listed in section 7, each in its own fenced
   block prefixed with `// === aneb-ui/aneb_ui/qml/<File>.qml ===`.
2. The `qml_bridge.py` file.
3. The `app_qml.py` file.
4. A `qmldir` file.
5. A short list of any image assets you generated (Nano Banana
   prompts + the resulting filenames in `qml_assets/`).
6. A 5-line "what to do next" instruction telling me how to run it
   (`pip install ...`, command line, etc.).

If you need to make a judgment call on aesthetics that this prompt
doesn't cover, prefer choices that match the board photo's
appearance over generic-modern.

Don't ask clarifying questions — make the call and explain it
inline as a `// NOTE:` comment in the relevant file. I'd rather
review your judgment than ping-pong on minor decisions.
