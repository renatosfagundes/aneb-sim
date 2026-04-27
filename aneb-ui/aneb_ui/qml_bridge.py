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
from pathlib import Path

from PyQt6.QtCore    import (QObject, QPointF, QTimer, pyqtProperty,
                              pyqtSignal, pyqtSlot)
from PyQt6.QtWidgets import QFileDialog

from .plot_buffers import PlotBuffers, Signal as PlotSignal
from .sim_proxy    import SimProxy
from .state        import SimState


log = logging.getLogger(__name__)

# Where calibrate-nano.py drops the JSON.
NANO_COORDS_PATH = Path(__file__).resolve().parent / "qml_assets" / "arduino-coords.json"

LCD_COLS = 16
LCD_ROWS = 2

# All signals the plotter knows how to sample. The QML chooses which
# ones to render; the buffer always has data ready for any of them.
PLOT_CHIPS   = ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")
PLOT_SIGNALS = (
    PlotSignal("adc", "0"),
    PlotSignal("adc", "1"),
    PlotSignal("adc", "2"),
    PlotSignal("adc", "3"),
    PlotSignal("pwm", "PD3"),    # DOUT0 dimmable
    PlotSignal("pwm", "PD5"),    # LOOP
    PlotSignal("pwm", "PD6"),    # LDR_LED
    PlotSignal("pwm", "PB1"),    # free
    PlotSignal("pin", "PB5"),    # L LED
    PlotSignal("pin", "PD4"),    # DOUT1
    PlotSignal("pin", "PC4"),    # DIN1 / A4
    PlotSignal("pin", "PC5"),    # DIN2 / A5
    PlotSignal("pin", "PB1"),    # DIN3 / D9 (also pwm-capable)
    PlotSignal("pin", "PB0"),    # DIN4 / D8
)


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
    plotSeqChanged      = pyqtSignal()

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

        # Per-chip 16x2 LCD state — sourced from `lcd` events emitted
        # by the engine's I2C peripheral decoder. The engine sends a
        # full snapshot of both lines on every change, so this dict
        # just mirrors the latest snapshot per chip.
        self._lcd_lines: dict[str, list[str]] = {}

        # Plotter rolling-window buffers. Sampled on a QTimer rather
        # than driven by events, so the plotter's UI work is decoupled
        # from the engine's event rate (the M6 lesson). 20 Hz × 10 s
        # window = 200 points per (chip, signal) trace.
        self._plot         = PlotBuffers()
        self._plot_targets = [
            (chip, sig) for chip in PLOT_CHIPS for sig in PLOT_SIGNALS
        ]
        self._plot_timer = QTimer(self)
        self._plot_timer.setInterval(50)   # 20 Hz
        self._plot_timer.timeout.connect(self._tick_plot)
        self._plot_timer.start()

        # Wire SimState's signals to our notifiers.
        state.pin_changed.connect      (self._on_pin_changed)
        state.pwm_changed.connect      (self._on_pwm_changed)
        state.uart_appended.connect    (self._on_uart_appended)
        state.can_tx_appended.connect  (self._on_can_tx_appended)
        state.can_state_changed.connect(self._on_can_state_changed)
        state.lcd_changed.connect      (self._on_lcd_changed)

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

    def _on_lcd_changed(self, chip: str, line0: str, line1: str) -> None:
        # Engine sends a full snapshot of both rows on every visible
        # change; pad/truncate to 16 chars so QML's column count is
        # always consistent.
        l0 = (line0 or "").ljust(LCD_COLS)[:LCD_COLS]
        l1 = (line1 or "").ljust(LCD_COLS)[:LCD_COLS]
        self._lcd_lines[chip] = [l0, l1]
        self.lcdLinesChanged.emit()

    def _on_can_tx_appended(self, _evt):
        self.canFramesSeqChanged.emit()

    def _tick_plot(self) -> None:
        """Sample one snapshot of every (chip, signal) the plotter
        cares about and notify QML. Cheap because the buffer just
        appends to a deque per slot — no allocation in the hot path
        beyond the deque slot itself."""
        self._plot.tick(self._state, self._plot_targets)
        self.plotSeqChanged.emit()

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

    @pyqtSlot(str, str, result="QVariantList")
    def plotSeries(self, chip: str, signal_key: str) -> list:
        """Return the rolling-window samples for one (chip, signal).

        `signal_key` is "kind:name" — e.g. "adc:0", "pwm:PD6",
        "pin:PB5". The QML plotter passes the same keys it gets from
        plotSignals(), so they round-trip cleanly.

        Returns a list of QPointF so QtCharts.LineSeries.append() can
        consume each entry directly.
        """
        try:
            kind, name = signal_key.split(":", 1)
        except ValueError:
            return []
        return [QPointF(t, v)
                for t, v in self._plot.series(chip, PlotSignal(kind, name))]

    @pyqtSlot(result="QVariantList")
    def plotSignals(self) -> list:
        """List of {key, kind, name, label, range} dicts the plotter
        UI can render checkboxes for. Static — same for every chip."""
        return [
            {"key": "adc:0", "label": "AIN0",  "axis": "adc"},
            {"key": "adc:1", "label": "AIN1",  "axis": "adc"},
            {"key": "adc:2", "label": "AIN2",  "axis": "adc"},
            {"key": "adc:3", "label": "AIN3",  "axis": "adc"},
            {"key": "pwm:PD3", "label": "DOUT0 PWM",  "axis": "pwm"},
            {"key": "pwm:PD6", "label": "LDR PWM",    "axis": "pwm"},
            {"key": "pwm:PD5", "label": "LOOP PWM",   "axis": "pwm"},
            {"key": "pwm:PB1", "label": "D9 PWM",     "axis": "pwm"},
            {"key": "pin:PB5", "label": "L LED",      "axis": "digital"},
            {"key": "pin:PD4", "label": "DOUT1",      "axis": "digital"},
            {"key": "pin:PC4", "label": "DIN1 (A4)",  "axis": "digital"},
            {"key": "pin:PC5", "label": "DIN2 (A5)",  "axis": "digital"},
            {"key": "pin:PB1", "label": "DIN3 (D9)",  "axis": "digital"},
            {"key": "pin:PB0", "label": "DIN4 (D8)",  "axis": "digital"},
        ]

    # ---- Slots called from QML ---------------------------------------

    @pyqtSlot(str, str, int)
    def setDin(self, chip: str, pin: str, val: int) -> None:
        self._proxy.cmd_din(chip, pin, val)

    @pyqtSlot(str, int, int)
    def setAdc(self, chip: str, ch: int, val: int) -> None:
        # Cache the value in SimState first so the plotter sees it
        # at its next sample tick, then forward to the engine.
        self._state.update_adc(chip, ch, val)
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
