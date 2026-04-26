"""
state.py — central observable simulation state.

Single source of truth. SimProxy events feed into update_*() methods,
which mutate internal data structures and emit fine-grained signals
that widgets connect to. Widgets are pure renderers — they never call
update_* themselves; user input flows through SimProxy.send_command().

Keeping this Python-side store simple (plain dicts, no ORM) makes the
eventual browser-UI port mostly mechanical: ship the dict over the
wire, render in the browser.
"""
from __future__ import annotations

from typing import Any

from PyQt6.QtCore import QObject, pyqtSignal


CHIPS = ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")


class SimState(QObject):
    # Per-chip per-pin signals carry (chip_id, pin_name, value).
    pin_changed       = pyqtSignal(str, str, int)
    pwm_changed       = pyqtSignal(str, str, float)
    uart_appended     = pyqtSignal(str, str)            # chip, decoded chunk
    can_tx_appended   = pyqtSignal(dict)                # full frame record
    can_state_changed = pyqtSignal(str, int, int, str)  # chip, tec, rec, state
    lcd_changed       = pyqtSignal(str, str, str)        # chip, line0, line1
    log_appended      = pyqtSignal(dict)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._pins:      dict[str, dict[str, int]]    = {c: {} for c in CHIPS}
        self._pwm:       dict[str, dict[str, float]]  = {c: {} for c in CHIPS}
        self._uart_buf:  dict[str, str]               = {c: "" for c in CHIPS}
        self._can_log:   list[dict[str, Any]]         = []
        self._can_state: dict[str, dict[str, Any]]    = {
            c: {"tec": 0, "rec": 0, "state": "active"} for c in CHIPS
        }
        # 16x2 LCD content per chip. Engine sends a full snapshot of
        # both lines on every change, so no merging on this side.
        self._lcd:       dict[str, list[str]]         = {}
        self._log:       list[dict[str, Any]]         = []

    # ---------- accessors --------------------------------------------

    def pin(self, chip: str, pin: str) -> int:
        return self._pins.get(chip, {}).get(pin, 0)

    def pwm(self, chip: str, pin: str) -> float:
        return self._pwm.get(chip, {}).get(pin, 0.0)

    def can_log(self) -> list[dict[str, Any]]:
        return list(self._can_log)

    def can_state(self, chip: str) -> dict[str, Any]:
        return dict(self._can_state.get(chip, {}))

    # ---------- mutators (slots that SimProxy connects to) -----------

    def update_pin(self, evt: dict) -> None:
        chip = evt.get("chip"); pin = evt.get("pin"); val = evt.get("val")
        if chip is None or pin is None or val is None:
            return
        self._pins.setdefault(chip, {})[pin] = int(val)
        self.pin_changed.emit(chip, pin, int(val))

    def update_pwm(self, evt: dict) -> None:
        chip = evt.get("chip"); pin = evt.get("pin"); duty = evt.get("duty")
        if chip is None or pin is None or duty is None:
            return
        self._pwm.setdefault(chip, {})[pin] = float(duty)
        self.pwm_changed.emit(chip, pin, float(duty))

    def update_uart(self, evt: dict) -> None:
        chip = evt.get("chip"); data = evt.get("data", "")
        if chip is None:
            return
        self._uart_buf[chip] = self._uart_buf.get(chip, "") + data
        self.uart_appended.emit(chip, data)

    def update_can_tx(self, evt: dict) -> None:
        self._can_log.append(evt)
        # Bound the log so a runaway sim doesn't bloat memory.
        if len(self._can_log) > 5000:
            self._can_log = self._can_log[-4000:]
        self.can_tx_appended.emit(evt)

    def update_can_state(self, evt: dict) -> None:
        chip = evt.get("chip")
        if not chip:
            return
        self._can_state[chip] = {
            "tec":   evt.get("tec",   0),
            "rec":   evt.get("rec",   0),
            "state": evt.get("state", "active"),
        }
        self.can_state_changed.emit(
            chip, int(evt.get("tec", 0)), int(evt.get("rec", 0)),
            str(evt.get("state", "active")),
        )

    def update_lcd(self, evt: dict) -> None:
        chip  = evt.get("chip")
        line0 = evt.get("line0", "")
        line1 = evt.get("line1", "")
        if not chip:
            return
        self._lcd[chip] = [str(line0), str(line1)]
        self.lcd_changed.emit(chip, str(line0), str(line1))

    def lcd_lines(self, chip: str) -> list[str]:
        return list(self._lcd.get(chip, ["", ""]))

    def update_log(self, evt: dict) -> None:
        self._log.append(evt)
        if len(self._log) > 5000:
            self._log = self._log[-4000:]
        self.log_appended.emit(evt)
