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
import os
import shutil
import subprocess
import threading
from pathlib import Path

from PyQt6.QtCore    import (QObject, QPointF, QTimer, pyqtProperty,
                              pyqtSignal, pyqtSlot)
from PyQt6.QtWidgets import QFileDialog

from .plot_buffers import PlotBuffers, Signal as PlotSignal
from .sim_proxy    import SimProxy
from .state        import SimState
from .uart_bridge  import UartBridgeManager


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
    adcSeqChanged       = pyqtSignal()
    canFramesSeqChanged = pyqtSignal()
    canStateSeqChanged  = pyqtSignal()
    engineRunningChanged = pyqtSignal()
    lcdLinesChanged     = pyqtSignal()
    plotSeqChanged      = pyqtSignal()

    speedChanged = pyqtSignal()

    # Per-event signals carrying the new payload, for QML widgets that
    # want streaming (serial console).
    uartAppended = pyqtSignal(str, str)   # chip, data — engine -> UI
    uartSent     = pyqtSignal(str)        # chip      — UI -> engine

    # avrdude flashing progress (emitted from background thread — Qt queues
    # across thread boundary automatically with AutoConnection).
    avrdudeOutput       = pyqtSignal(str, str)   # chip, line
    avrdudeStateChanged = pyqtSignal(str, bool)  # chip, is_running

    # Fires whenever the set of active virtual-COM bridges changes.
    uartBridgeChanged   = pyqtSignal()

    # Fires whenever the engine emits a chipstat update (≈1 Hz per chip).
    # Carries the chip id; QML re-reads via chipStat() to get the new
    # values.  We don't push the dict through the signal directly because
    # QML's autoconvert wraps it in a QVariantMap each fire and that
    # adds GC churn for a 5-chip × 1 Hz event stream.
    chipStatChanged     = pyqtSignal(str)

    def __init__(self, state: SimState, proxy: SimProxy,
                 parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._state = state
        self._proxy = proxy
        self._engine_running = False
        self._speed = 1.0
        self._nano_coords = _load_nano_coords()
        self._avrdude_path: str | None = None
        self._avrdude_threads: dict[str, threading.Thread] = {}

        # Virtual-COM bridges: each chip's TCP UART socket forwards bytes
        # in both directions to a com0com pair so external tools (Arduino
        # Serial Monitor / PuTTY) can see the chip as a normal COM port.
        # The bridge owns COM10/12/14/16/18; the user's tool opens
        # COM11/13/15/17/19. Bridges silently fail to start if com0com
        # isn't installed — TCP path keeps working regardless.
        # Run scripts/setup_com.bat once to create the pairs.
        #
        # The actual `_start_uart_bridges()` call is deferred until the
        # engine subprocess has reported it's running (see
        # _on_engine_started below) — otherwise the bridge tries to
        # connect to localhost:8600..8604 before the engine has bound
        # those ports and fails with "timed out" for every chip.
        self._uart_bridges = UartBridgeManager()

        # Per-chip 16x2 LCD state — sourced from `lcd` events emitted
        # by the engine's I2C peripheral decoder. The engine sends a
        # full snapshot of both lines on every change, so this dict
        # just mirrors the latest snapshot per chip.
        self._lcd_lines: dict[str, list[str]] = {}

        # Cached snapshots for hot-path properties.  Rebuilt at most
        # once per notify-signal emission so repeated QML reads within
        # a single signal propagation don't each re-allocate the dict.
        self._pin_states_dirty = True
        self._pin_states_cache: dict = {}
        self._pwm_duties_dirty = True
        self._pwm_duties_cache: dict = {}
        self._adc_values_dirty = True
        self._adc_values_cache: dict = {}

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
        state.adc_changed.connect      (self._on_adc_changed)
        state.uart_appended.connect    (self._on_uart_appended)
        state.can_tx_appended.connect  (self._on_can_tx_appended)
        state.can_state_changed.connect(self._on_can_state_changed)
        state.lcd_changed.connect      (self._on_lcd_changed)
        state.chip_stat_changed.connect(self.chipStatChanged.emit)

        # Track engine running state.
        proxy.started.connect(self._on_engine_started)
        proxy.stopped.connect(self._on_engine_stopped)

    # ---- SimState slots ----------------------------------------------

    def _on_pin_changed(self, _chip, _pin, _val):
        self._pin_states_dirty = True
        self.pinSeqChanged.emit()

    def _on_pwm_changed(self, _chip, _pin, _duty):
        self._pwm_duties_dirty = True
        self.pwmSeqChanged.emit()

    def _on_adc_changed(self, _chip, _ch, _val):
        self._adc_values_dirty = True
        self.adcSeqChanged.emit()

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
        # The QProcess `started` signal fires the instant the child
        # process exists — its TCP listeners (8600..8604) won't be
        # bound for another few ms.  Defer bridge startup briefly so
        # the bridge's connect doesn't race with bind() and fail with
        # "timed out".
        QTimer.singleShot(250, self._start_uart_bridges)

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
        if self._pin_states_dirty:
            self._pin_states_cache = {
                chip: dict(pins) for chip, pins in self._state._pins.items()
            }
            self._pin_states_dirty = False
        return self._pin_states_cache

    @pyqtProperty("QVariantMap", notify=pwmSeqChanged)
    def pwmDuties(self) -> dict:
        if self._pwm_duties_dirty:
            self._pwm_duties_cache = {
                chip: dict(pwm) for chip, pwm in self._state._pwm.items()
            }
            self._pwm_duties_dirty = False
        return self._pwm_duties_cache

    @pyqtProperty("QVariantMap", notify=adcSeqChanged)
    def adcValues(self) -> dict:
        if self._adc_values_dirty:
            self._adc_values_cache = {
                chip: {str(ch): v for ch, v in chans.items()}
                for chip, chans in self._state._adc.items()
            }
            self._adc_values_dirty = False
        return self._adc_values_cache

    @pyqtProperty("QVariantList", notify=canFramesSeqChanged)
    def canFrames(self) -> list:
        # Cap at last 1000 — keeps the QML list view responsive.
        return list(self._state.can_log()[-1000:])

    @pyqtSlot(str, result="QVariantMap")
    def canStateOf(self, chip: str) -> dict:
        return dict(self._state.can_state(chip))

    @pyqtSlot(str, result="QVariantMap")
    def chipStat(self, chip: str) -> dict:
        """Per-chip metadata snapshot consumed by the chip-info sidebar:
        {hex_name, hex_path, free_ram, ram_size, sp}.  Updated at ~1 Hz
        from the engine's chipstat events; QML re-reads on chipStatChanged."""
        return dict(self._state.chip_stat(chip))

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
        path, _ = QFileDialog.getOpenFileName(
            None, f"Load firmware for {chip}",
            "", "Intel hex (*.hex);;All files (*.*)"
        )
        if path:
            self._proxy.cmd_load(chip, path)

    @pyqtSlot()
    def openLoadAllDialog(self) -> None:
        """Flash ECU 1-4 with the same hex file."""
        path, _ = QFileDialog.getOpenFileName(
            None, "Flash all ECUs with firmware",
            "", "Intel hex (*.hex);;All files (*.*)"
        )
        if path:
            for chip in ("ecu1", "ecu2", "ecu3", "ecu4"):
                self._proxy.cmd_load(chip, path)

    @pyqtSlot(str)
    def unloadChip(self, chip: str) -> None:
        """Stop a chip and clear its firmware."""
        self._proxy.cmd_unload(chip)

    @pyqtSlot(float)
    def setSpeed(self, factor: float) -> None:
        """Set real-time multiplier: 0.0 = flat-out, 1.0 = real-time."""
        self._speed = max(0.0, float(factor))
        self._proxy.cmd_speed(self._speed)
        self.speedChanged.emit()

    @pyqtProperty(float, notify=speedChanged)
    def speed(self) -> float:
        return self._speed

    # ---- avrdude flashing -------------------------------------------

    _CHIP_MCU = {
        "ecu1": "atmega328p", "ecu2": "atmega328p",
        "ecu3": "atmega328p", "ecu4": "atmega328p",
        "mcu":  "atmega328pb",
    }
    _CHIP_TCP_PORT = {
        "ecu1": 8600, "ecu2": 8601, "ecu3": 8602, "ecu4": 8603, "mcu": 8604,
    }
    # Pairs created by scripts/setup_com.bat.  Bridge holds the first port,
    # user's tool opens the second.  Edit both this dict and setup_com.bat
    # if you want a different pair layout.
    _CHIP_COM_BRIDGE = {
        "ecu1": "COM10", "ecu2": "COM12", "ecu3": "COM14",
        "ecu4": "COM16", "mcu":  "COM18",
    }
    _CHIP_COM_USER = {
        "ecu1": "COM11", "ecu2": "COM13", "ecu3": "COM15",
        "ecu4": "COM17", "mcu":  "COM19",
    }
    _AVRDUDE_SEARCH = [
        r"C:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe",
        r"C:\Program Files\Arduino\hardware\tools\avr\bin\avrdude.exe",
        r"C:\msys64\mingw64\bin\avrdude.exe",
        r"C:\msys64\usr\bin\avrdude.exe",
    ]
    # Optiboot for ATmega328P — bundled in Arduino IDE.
    # avrdude -c arduino requires Optiboot in the chip's flash (bootloader section).
    _OPTIBOOT_SEARCH = [
        r"C:\Program Files (x86)\Arduino\hardware\arduino\avr\bootloaders\optiboot\optiboot_atmega328.hex",
        r"C:\Program Files\Arduino\hardware\arduino\avr\bootloaders\optiboot\optiboot_atmega328.hex",
    ]

    def _find_avrdude(self) -> str | None:
        if self._avrdude_path and Path(self._avrdude_path).exists():
            return self._avrdude_path
        found = shutil.which("avrdude") or shutil.which("avrdude.exe")
        if found:
            self._avrdude_path = found
            return found
        for p in self._AVRDUDE_SEARCH:
            if Path(p).exists():
                self._avrdude_path = p
                return p
        return None

    def _find_optiboot(self) -> str | None:
        for p in self._OPTIBOOT_SEARCH:
            if Path(p).exists():
                return p
        return None

    @staticmethod
    def _find_avrdude_conf(avrdude_exe: str) -> str | None:
        """Return the avrdude.conf path bundled next to the executable, or None."""
        exe = Path(avrdude_exe)
        candidates = [
            exe.parent.parent / "etc" / "avrdude.conf",   # Arduino layout: bin/../etc/
            exe.parent / "avrdude.conf",                   # flat layout
        ]
        for c in candidates:
            if c.exists():
                return str(c)
        return None

    @pyqtSlot(str)
    def flashChipAvrdude(self, chip: str) -> None:
        """Open a file dialog and flash the chip via avrdude over TCP."""
        t = self._avrdude_threads.get(chip)
        if t and t.is_alive():
            self.avrdudeOutput.emit(chip, "⚠ avrdude already running for this chip")
            return

        avrdude = self._find_avrdude()
        if not avrdude:
            self.avrdudeOutput.emit(chip,
                "ERROR: avrdude not found.\n"
                "Install Arduino IDE, or add avrdude to PATH, or place it in\n"
                "  C:\\Program Files (x86)\\Arduino\\hardware\\tools\\avr\\bin\\")
            self.avrdudeStateChanged.emit(chip, False)
            return

        hex_path, _ = QFileDialog.getOpenFileName(
            None, f"Flash {chip.upper()} via avrdude",
            "", "Intel hex (*.hex);;All files (*.*)"
        )
        if not hex_path:
            return

        # avrdude -c arduino speaks STK500 to Optiboot — Optiboot must be
        # in the chip's flash.  Load it now so the chip is running a
        # bootloader when avrdude's TCP connect triggers the reset.
        optiboot = self._find_optiboot()
        if not optiboot:
            self.avrdudeOutput.emit(chip,
                "ERROR: optiboot_atmega328.hex not found.\n"
                "Expected in Arduino IDE at:\n"
                "  hardware\\arduino\\avr\\bootloaders\\optiboot\\")
            self.avrdudeStateChanged.emit(chip, False)
            return

        self.avrdudeOutput.emit(chip, f"INFO: loading Optiboot → {optiboot}")
        self._proxy.cmd_load(chip, optiboot)

        # Bootloader timing requires real-time — force it silently.
        if self._speed != 1.0:
            self.setSpeed(1.0)
            self.avrdudeOutput.emit(chip, "INFO: speed set to 1.0× (required for bootloader)")

        mcu      = self._CHIP_MCU.get(chip, "atmega328p")
        tcp_port = self._CHIP_TCP_PORT.get(chip, 8600)

        conf = self._find_avrdude_conf(avrdude)
        cmd = [avrdude]
        if conf:
            cmd += ["-C", conf]
        cmd += [
            "-c", "arduino",
            "-p", mcu,
            "-b", "115200",
            "-P", f"net:localhost:{tcp_port}",
            "-U", f"flash:w:{hex_path}:i",
        ]
        self.avrdudeOutput.emit(chip, "$ " + " ".join(cmd))
        self.avrdudeStateChanged.emit(chip, True)

        # Free the TCP UART socket so avrdude can grab it.  The bridge
        # holds a persistent connection from app launch, otherwise
        # avrdude's connect would be refused. Resume after avrdude exits.
        bridge_was_active = self._pause_bridge(chip)
        if bridge_was_active:
            self.avrdudeOutput.emit(chip,
                f"INFO: paused {self._CHIP_COM_USER[chip]} bridge")

        t = threading.Thread(
            target=self._run_avrdude, args=(chip, cmd, bridge_was_active),
            daemon=True, name=f"avrdude-{chip}"
        )
        self._avrdude_threads[chip] = t
        t.start()

    def _run_avrdude(self, chip: str, cmd: list, restore_bridge: bool) -> None:
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
            )
            for line in proc.stdout:
                self.avrdudeOutput.emit(chip, line.rstrip())
            proc.wait()
            ok = proc.returncode == 0
            self.avrdudeOutput.emit(
                chip,
                ("✓ Flash successful" if ok
                 else f"✗ avrdude failed (exit {proc.returncode})")
            )
        except Exception as exc:
            self.avrdudeOutput.emit(chip, f"ERROR: {exc}")
        finally:
            self.avrdudeStateChanged.emit(chip, False)
            if restore_bridge:
                self._resume_bridge(chip)
                self.avrdudeOutput.emit(chip,
                    f"INFO: resumed {self._CHIP_COM_USER[chip]} bridge")

    @pyqtProperty("QVariantMap", constant=True)
    def uartPorts(self) -> dict:
        """TCP port numbers for each chip's raw UART socket.
        Connect with: avrdude -c arduino -P net:localhost:<port>
        or open with any TCP terminal (e.g. PuTTY raw mode).
        The chip resets automatically when a client connects (Optiboot DTR emulation).
        For virtual COM ports install com0com and run uart_bridge.py."""
        return {
            "ecu1": 8600,
            "ecu2": 8601,
            "ecu3": 8602,
            "ecu4": 8603,
            "mcu":  8604,
        }

    @pyqtProperty("QVariantMap", notify=uartBridgeChanged)
    def userComPorts(self) -> dict:
        """User-facing COM port for each chip (the side the user's tool opens).
        Empty for any chip whose bridge failed to start (com0com not installed,
        port already taken, etc.). 115200 baud."""
        active = self._uart_bridges.active   # {chip: bridge_com}
        return {
            chip: self._CHIP_COM_USER[chip]
            for chip in self._CHIP_COM_USER
            if chip in active
        }

    # ---- UART bridge management -------------------------------------

    def _start_uart_bridges(self) -> None:
        """Try to start all chip→COM bridges.  Fails silently per chip
        if com0com isn't installed or the port is taken."""
        results = self._uart_bridges.start(dict(self._CHIP_COM_BRIDGE))
        active = [c for c, ok in results.items() if ok]
        failed = [c for c, ok in results.items() if not ok]
        if active:
            log.info("UART bridges active: %s", ", ".join(
                f"{c}→{self._CHIP_COM_USER[c]}" for c in active))
        if failed:
            log.info("UART bridges unavailable for: %s "
                     "(install com0com and run scripts/setup_com.bat)",
                     ", ".join(failed))
        self.uartBridgeChanged.emit()

    def stop_uart_bridges(self) -> None:
        """Tear down all bridges.  Called from app_qml.closeEvent."""
        self._uart_bridges.stop()

    # ---- COM port setup -----------------------------------------------

    # True while scripts/setup_com.ps1 is running in a worker thread;
    # gates the slot so back-to-back clicks can't spawn two installers.
    _com_setup_running = False

    @pyqtSlot()
    def installComPorts(self) -> None:
        """Run scripts/setup_com.ps1 to install/repair the com0com pairs.
        Triggers a UAC prompt; the script self-elevates and creates the
        five ECU pairs if they don't exist, then sets a friendly name on
        each (ECU1 (aneb-sim), etc.).  Bridges are restarted automatically
        once the script exits successfully.

        Runs in a background thread so the QML UI thread stays
        responsive while the user clicks through the UAC prompt and the
        elevated PowerShell window does its work.
        """
        import sys
        if sys.platform != "win32":
            log.warning("COM port setup is Windows-only")
            return
        if self._com_setup_running:
            log.info("COM port setup already in progress")
            return

        ps1 = Path(__file__).resolve().parents[2] / "scripts" / "setup_com.ps1"
        if not ps1.exists():
            log.error("setup_com.ps1 not found at %s", ps1)
            return

        self._com_setup_running = True
        threading.Thread(
            target=self._run_com_setup, args=(ps1,),
            daemon=True, name="com-port-setup"
        ).start()

    @pyqtSlot()
    def removeComPorts(self) -> None:
        """Run scripts/setup_com.ps1 -Remove to delete the aneb-sim
        com0com pairs (COM10..COM19).  Active bridges are stopped first
        so pyserial isn't holding the COM ports open when setupc tries
        to remove them.  Triggers a UAC prompt; the script self-elevates."""
        import sys
        if sys.platform != "win32":
            log.warning("COM port removal is Windows-only")
            return
        if self._com_setup_running:
            log.info("COM port operation already in progress")
            return

        ps1 = Path(__file__).resolve().parents[2] / "scripts" / "setup_com.ps1"
        if not ps1.exists():
            log.error("setup_com.ps1 not found at %s", ps1)
            return

        # Release the COM ports BEFORE running the remove script —
        # setupc can't delete a port whose handle is still open.
        log.info("stopping bridges before removing pairs")
        self._uart_bridges.stop()
        self.uartBridgeChanged.emit()

        self._com_setup_running = True
        threading.Thread(
            target=self._run_com_setup, args=(ps1, ["-Remove"]),
            daemon=True, name="com-port-remove"
        ).start()

    def _run_com_setup(self, ps1: Path, script_args: list[str] | None = None) -> None:
        """Worker thread: spawn PowerShell, wait, restart bridges.

        `script_args` is forwarded to the .ps1 — pass ['-Remove'] to
        invoke the uninstall path instead of the default install path."""
        script_args = script_args or []
        try:
            log.info("launching %s %s (UAC prompt will appear)",
                     ps1, ' '.join(script_args))
            # Pass ANEB_SIM_NONINTERACTIVE so the script skips the
            # final Read-Host pause that's only useful for the
            # double-click-from-Explorer path.
            env = os.environ.copy()
            env["ANEB_SIM_NONINTERACTIVE"] = "1"
            result = subprocess.run(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", str(ps1), *script_args],
                env=env, check=False,
            )
            log.info("setup_com.ps1 exited with code %d", result.returncode)
        except Exception as exc:
            log.error("COM port setup failed: %s", exc)
        finally:
            self._com_setup_running = False
            # Cross-thread: schedule the bridge restart on the Qt event
            # loop so we don't poke pyserial / Qt signals from the wrong
            # thread.
            QTimer.singleShot(0, self._restart_uart_bridges)

    def _restart_uart_bridges(self) -> None:
        """Stop existing bridges and try to start them again.  Called
        after setup_com.ps1 finishes (or if the user explicitly hits
        a refresh button)."""
        self._uart_bridges.stop()
        self._start_uart_bridges()

    def _pause_bridge(self, chip: str) -> bool:
        """Disconnect this chip's bridge so avrdude can grab the TCP port.
        Returns True if there was an active bridge to pause."""
        was_active = chip in self._uart_bridges.active
        if was_active:
            self._uart_bridges.stop_chip(chip)
            self.uartBridgeChanged.emit()
        return was_active

    def _resume_bridge(self, chip: str) -> None:
        """Reattach this chip's bridge after avrdude is done."""
        com = self._CHIP_COM_BRIDGE.get(chip)
        if not com:
            return
        ok = self._uart_bridges.start_chip(chip, com)
        if ok:
            self.uartBridgeChanged.emit()
