"""
McuPanel — small panel for the ATmega328PB board controller.

Visually styled like an EcuPanel but with a smaller Nano illustration
and only the two latching mode-selector buttons (no DIN array, no pots,
no CAN — the MCU has no MCP2515).
"""
from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui  import QColor
from PyQt6.QtWidgets import (
    QGroupBox, QHBoxLayout, QLabel, QVBoxLayout, QWidget,
)

from ..sim_proxy import SimProxy
from ..state import SimState
from .led_widget    import LedWidget
from .button_widget import PushButton
from .serial_console import SerialConsole
from .nano_widget   import NanoWidget


MODE_BUTTONS = [
    ("Mode 1",  "D8"),
    ("Mode 2",  "D9"),
]


def _split_port(pin_name: str):
    if (len(pin_name) == 3 and pin_name[0] == 'P'
            and pin_name[1] in 'BCD' and pin_name[2].isdigit()):
        return ("P" + pin_name[1], int(pin_name[2]))
    return None


class McuPanel(QGroupBox):
    def __init__(self, proxy: SimProxy, state: SimState, parent=None) -> None:
        super().__init__("MCU (ATmega328PB)", parent)
        self._chip  = "mcu"
        self._proxy = proxy
        self._state = state

        # ---- LED row + label ----
        self._led = LedWidget("LED_BUILTIN", color=QColor("#ffaa22"))
        led_row = QHBoxLayout()
        led_row.addStretch(1)
        led_row.addWidget(QLabel("LED:"))
        led_row.addWidget(self._led)
        led_row.addStretch(1)

        # ---- Nano illustration -----------------------------------------
        self._nano = NanoWidget("MCU")
        self._nano.setMinimumSize(280, 120)
        self._uart_decay = QTimer(self)
        self._uart_decay.setInterval(80)
        self._uart_decay.timeout.connect(self._nano.decay_uart)
        self._uart_decay.start()

        # ---- Mode-selector latching buttons ----------------------------
        btn_row = QHBoxLayout()
        btn_row.setSpacing(12)
        btn_row.addStretch(1)
        for label, pin in MODE_BUTTONS:
            b = PushButton(f"{label}\n{pin}", press_value=1, release_value=0,
                           latching=True)
            b.value_changed.connect(
                lambda v, p=pin: self._proxy.cmd_din(self._chip, p, v)
            )
            btn_row.addWidget(b)
        btn_row.addStretch(1)

        # ---- Serial console --------------------------------------------
        self._serial = SerialConsole("mcu")
        self._serial.text_to_send.connect(
            lambda s: self._proxy.send_command({"c": "uart", "chip": "mcu", "data": s})
        )

        # ---- Assemble --------------------------------------------------
        v = QVBoxLayout(self)
        v.setContentsMargins(8, 14, 8, 8)
        v.setSpacing(8)
        v.addLayout(led_row)
        v.addWidget(self._nano)
        v.addLayout(btn_row)
        v.addWidget(self._serial, 1)

        # ---- State wiring ----------------------------------------------
        state.pin_changed.connect(self._on_pin_changed)
        state.uart_appended.connect(self._on_uart_appended)

    def _on_pin_changed(self, chip: str, pin: str, val: int) -> None:
        if chip != self._chip:
            return
        if pin == "PB5":
            self._led.set_on(bool(val))
        mapped = _split_port(pin)
        if mapped:
            self._nano.set_pin_state(mapped[0], mapped[1], val)

    def _on_uart_appended(self, chip: str, data: str) -> None:
        if chip == self._chip:
            self._serial.append(data)
            self._nano.pulse_tx()
