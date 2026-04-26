"""
qml_bridge — exposes SimState as Qt properties + slots for QML.

Wraps the existing SimState (observable store) and SimProxy (engine
subprocess + JSON wire) without modifying either: state.py keeps
emitting fine-grained Qt signals, sim_proxy.py keeps owning the
subprocess and command marshalling, and this bridge translates
between them and the QML side.

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

import logging
from pathlib import Path

from PyQt6.QtCore    import QObject, pyqtProperty, pyqtSignal, pyqtSlot
from PyQt6.QtWidgets import QFileDialog

from .sim_proxy import SimProxy
from .state     import SimState


log = logging.getLogger(__name__)


class QmlBridge(QObject):

    # Per-section change notifiers — QML observes these via Connections
    # or as binding dependencies on the corresponding properties.
    pinSeqChanged       = pyqtSignal()
    pwmSeqChanged       = pyqtSignal()
    canFramesSeqChanged = pyqtSignal()
    canStateSeqChanged  = pyqtSignal()
    engineRunningChanged = pyqtSignal()

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
