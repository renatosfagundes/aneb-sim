"""
qml_bridge — exposes SimState as Qt properties + slots for QML.

Wraps the existing SimState (observable store) and SimProxy (engine
subprocess + JSON wire) without modifying either: state.py keeps
emitting fine-grained Qt signals, sim_proxy.py keeps owning the
subprocess and command marshalling, and this bridge translates
between them and the QML side.

Also loads `qml_assets/arduino-coords.json` (produced by
`scripts/calibrate-nano.py`) at startup so the QML overlay knows
where each LED and pad sits on the Nano image.

QML reads:
  - bridge.engineRunning           bool
  - bridge.pinStates               dict[chip][pin] -> 0/1
  - bridge.pwmDuties               dict[chip][pin] -> 0..1
  - bridge.canFrames               list of can_tx event dicts (last 1000)
  - bridge.canStateOf(chip)        dict {tec, rec, state}

QML calls (slots):
  - bridge.setDin(chip, pin, val)
  - bridge.setAdc(chip, ch, val)
  - bridge.sendUart(chip, text)
  - bridge.injectCan(id_str, data_hex, ext, rtr, dlc)
  - bridge.forceBusoff(chip)
  - bridge.canRecover(chip)
  - bridge.pauseEngine() / resumeEngine()
  - bridge.openLoadDialog(chip)    pops a QFileDialog and issues `load`
  - bridge.canStateOf(chip)        helper (returns a snapshot dict)

Notify pattern: each property has a per-tick "seq" notifier
(`pinSeqChanged`, `pwmSeqChanged`, ...) that increments on every
underlying state change, and the QML uses `Connections { target:
bridge; function on...() {} }` or property bindings to react.
"""
from __future__ import annotations

import json
import logging
import re
from pathlib import Path

from PyQt6.QtCore    import QObject, pyqtProperty, pyqtSignal, pyqtSlot
from PyQt6.QtWidgets import QFileDialog

from .sim_proxy import SimProxy
from .state     import SimState


log = logging.getLogger(__name__)

# Where calibrate-nano.py drops the JSON.
NANO_COORDS_PATH = Path(__file__).resolve().parent / "qml_assets" / "arduino-coords.json"

# Lines starting with this prefix in any chip's UART are routed to the
# per-chip 16x2 LCD instead of (or in addition to) the serial console.
# Format: __LCD__<row>:<col>:<text>\n
#   row in {0, 1}
#   col in {0..15}
#   text up to 16 - col chars
#
# Example firmware code:
#   Serial.println("__LCD__0:0:Hello, World!");
#   Serial.println("__LCD__1:0:T = 23.4 C");
LCD_PATTERN = re.compile(r"^__LCD__(\d+):(\d+):(.*)$")
LCD_COLS    = 16
LCD_ROWS    = 2


def _load_nano_coords() -> dict:
    if not NANO_COORDS_PATH.exists():
        log.warning("nano-coords JSON not found at %s — overlays will be blank",
                    NANO_COORDS_PATH)
        return {}
    try:
        data = json.loads(NANO_COORDS_PATH.read_text(encoding="utf-8"))
        return data.get("coords") or {}
    except Exception as exc:
        log.error("failed to read nano-coords JSON: %s", exc)
        return {}


class QmlBridge(QObject):

    # Per-section change notifiers — QML observes these via Connections
    # or as binding dependencies on the corresponding properties.
    pinSeqChanged       = pyqtSignal()
    pwmSeqChanged       = pyqtSignal()
    canFramesSeqChanged = pyqtSignal()
    canStateSeqChanged  = pyqtSignal()
    engineRunningChanged = pyqtSignal()
    lcdLinesChanged     = pyqtSignal()

    # Per-event signals carrying the new payload, for QML widgets that
    # want streaming (serial console).
    uartAppended = pyqtSignal(str, str)   # chip, data — engine -> UI
    uartSent     = pyqtSignal(str)        # chip      — UI -> engine

    def __init__(self, state: SimState, proxy: SimProxy,
                 parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._state = state
        self._proxy = proxy
        self._engine_running = False
        self._nano_coords = _load_nano_coords()

        # Per-chip line buffer for LCD parsing — UART arrives in chunks
        # so we buffer until newlines appear.
        self._lcd_buf:   dict[str, str]            = {}
        # Per-chip 16x2 LCD state.
        self._lcd_lines: dict[str, list[str]]      = {}

        # Wire SimState's signals to our notifiers.
        state.pin_changed.connect      (self._on_pin_changed)
        state.pwm_changed.connect      (self._on_pwm_changed)
        state.uart_appended.connect    (self._on_uart_appended)
        state.can_tx_appended.connect  (self._on_can_tx_appended)
        state.can_state_changed.connect(self._on_can_state_changed)

        # Track engine running state.
        proxy.started.connect(self._on_engine_started)
        proxy.stopped.connect(self._on_engine_stopped)

    # ---- SimState slots ----------------------------------------------

    def _on_pin_changed(self, _chip, _pin, _val):
        self.pinSeqChanged.emit()

    def _on_pwm_changed(self, _chip, _pin, _duty):
        self.pwmSeqChanged.emit()

    def _on_uart_appended(self, chip: str, data: str):
        self.uartAppended.emit(chip, data)
        # Side-channel: parse for __LCD__ lines and update the per-chip
        # 16x2 LCD state. The original UART chunk still flows to the
        # serial console — we don't suppress it, so students can debug.
        self._parse_lcd_chunk(chip, data)

    def _parse_lcd_chunk(self, chip: str, data: str) -> None:
        buf = self._lcd_buf.get(chip, "") + data
        # Process each complete line.
        while "\n" in buf:
            line, _, buf = buf.partition("\n")
            line = line.rstrip("\r")
            m = LCD_PATTERN.match(line)
            if m:
                try:
                    row = int(m.group(1))
                    col = int(m.group(2))
                    text = m.group(3)
                except ValueError:
                    continue
                self._lcd_write(chip, row, col, text)
        self._lcd_buf[chip] = buf

    def _lcd_write(self, chip: str, row: int, col: int, text: str) -> None:
        if row < 0 or row >= LCD_ROWS:
            return
        if col < 0 or col >= LCD_COLS:
            return
        rows = self._lcd_lines.setdefault(chip,
                                          [" " * LCD_COLS for _ in range(LCD_ROWS)])
        # Pad existing row to 16, then overwrite at [col, col+len(text)].
        existing = rows[row].ljust(LCD_COLS)[:LCD_COLS]
        new_chars = text[: LCD_COLS - col]
        rows[row] = (existing[:col] + new_chars
                     + existing[col + len(new_chars):])[:LCD_COLS]
        self.lcdLinesChanged.emit()

    def _on_can_tx_appended(self, _evt):
        self.canFramesSeqChanged.emit()

    def _on_can_state_changed(self, _chip, _tec, _rec, _state):
        self.canStateSeqChanged.emit()

    def _on_engine_started(self):
        self._engine_running = True
        self.engineRunningChanged.emit()

    def _on_engine_stopped(self, _exit_code: int):
        self._engine_running = False
        self.engineRunningChanged.emit()

    # ---- Properties exposed to QML -----------------------------------

    @pyqtProperty(bool, notify=engineRunningChanged)
    def engineRunning(self) -> bool:
        return self._engine_running

    # Nano image overlay coordinates loaded from arduino-coords.json.
    # Static after startup — not notifiable. Layout:
    #   { "leds": { "tx": {x,y}, "rx": {x,y}, ... },
    #     "pads": { "top": { "d12": {x,y}, ... }, "bot": {...} } }
    # x and y are normalised to image dimensions (0..1).
    @pyqtProperty("QVariantMap", constant=True)
    def nanoCoords(self) -> dict:
        return self._nano_coords

    @pyqtProperty("QVariantMap", notify=pinSeqChanged)
    def pinStates(self) -> dict:
        # Return a shallow snapshot per chip — QML can't observe nested
        # mutations anyway, so a fresh dict is no worse.
        return {chip: dict(pins) for chip, pins in self._state._pins.items()}

    @pyqtProperty("QVariantMap", notify=pwmSeqChanged)
    def pwmDuties(self) -> dict:
        return {chip: dict(pwm) for chip, pwm in self._state._pwm.items()}

    @pyqtProperty("QVariantList", notify=canFramesSeqChanged)
    def canFrames(self) -> list:
        # Cap at last 1000 — keeps the QML list view responsive.
        return list(self._state.can_log()[-1000:])

    @pyqtSlot(str, result="QVariantMap")
    def canStateOf(self, chip: str) -> dict:
        return dict(self._state.can_state(chip))

    @pyqtProperty("QVariantMap", notify=lcdLinesChanged)
    def lcdLines(self) -> dict:
        # Return a fresh copy so QML re-evaluates dependent bindings.
        return {chip: list(rows) for chip, rows in self._lcd_lines.items()}

    # ---- Slots called from QML ---------------------------------------

    @pyqtSlot(str, str, int)
    def setDin(self, chip: str, pin: str, val: int) -> None:
        self._proxy.cmd_din(chip, pin, val)

    @pyqtSlot(str, int, int)
    def setAdc(self, chip: str, ch: int, val: int) -> None:
        self._proxy.cmd_adc(chip, ch, val)

    @pyqtSlot(str, str)
    def sendUart(self, chip: str, text: str) -> None:
        self._proxy.send_command({"c": "uart", "chip": chip, "data": text})
        # Notify the Nano illustration so its RX LED flashes briefly.
        self.uartSent.emit(chip)

    @pyqtSlot(str, str, bool, bool, int)
    def injectCan(self, id_str: str, data_hex: str,
                  ext: bool, rtr: bool, dlc: int) -> None:
        # Accept "0x123" or "291" — both parse via int(..., 0).
        try:
            frame_id = int(id_str.strip(), 0)
        except ValueError:
            log.warning("injectCan: bad id %r", id_str)
            return
        clean = data_hex.strip().replace(" ", "")
        if clean.lower().startswith("0x"):
            clean = clean[2:]
        self._proxy.cmd_can_inject(frame_id, clean.upper(),
                                   ext=ext, rtr=rtr, dlc=int(dlc))

    @pyqtSlot(str)
    def forceBusoff(self, chip: str) -> None:
        self._proxy.cmd_force_busoff(chip)

    @pyqtSlot(str)
    def canRecover(self, chip: str) -> None:
        self._proxy.cmd_can_recover(chip)

    @pyqtSlot()
    def pauseEngine(self) -> None:
        self._proxy.cmd_pause()

    @pyqtSlot()
    def resumeEngine(self) -> None:
        self._proxy.cmd_resume()

    @pyqtSlot(str)
    def openLoadDialog(self, chip: str) -> None:
        # QFileDialog lives in QtWidgets — fine to use from a bridge
        # that's owned by the QApplication.
        path, _ = QFileDialog.getOpenFileName(
            None, f"Load firmware for {chip}",
            "", "Intel hex (*.hex);;All files (*.*)"
        )
        if path:
            self._proxy.cmd_load(chip, path)
