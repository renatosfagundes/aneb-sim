"""
McuPanel — small panel for the ATmega328PB board controller.

Shows the two mode-selector pushbuttons, the LED_BUILTIN, and a serial
console. The MCU has no MCP2515 / no CAN involvement; we deliberately
omit CAN-state widgets here.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui  import QColor
from PyQt6.QtWidgets import (
    QGroupBox, QHBoxLayout, QLabel, QVBoxLayout, QWidget,
)

from ..sim_proxy import SimProxy
from ..state import SimState
from .led_widget    import LedWidget
from .button_widget import PushButton
from .serial_console import SerialConsole


# The MCU firmware reacts to two pushbuttons that route through specific
# pins (per StateMachine.ino + Definitions.h on the original 328PB board).
# We treat them as two latching panel buttons. A future M6b can refine
# the exact pin mapping once we cross-reference Definitions.h.
MODE_BUTTONS = [
    ("Mode 1",  "D8"),
    ("Mode 2",  "D9"),
]


class McuPanel(QGroupBox):
    def __init__(self, proxy: SimProxy, state: SimState, parent=None) -> None:
        super().__init__("MCU (ATmega328PB)", parent)
        self._chip  = "mcu"
        self._proxy = proxy
        self._state = state

        # Mode-selector buttons (latching).
        btn_row = QHBoxLayout()
        for label, pin in MODE_BUTTONS:
            b = PushButton(f"{label}\n{pin}", press_value=1, release_value=0,
                           latching=True)
            b.setMinimumHeight(48)
            b.value_changed.connect(
                lambda v, p=pin: self._proxy.cmd_din(self._chip, p, v)
            )
            btn_row.addWidget(b)
        btn_row.addStretch(1)

        # LED_BUILTIN.
        self._led = LedWidget("LED_BUILTIN", color=QColor("#ffaa22"))
        led_row = QHBoxLayout()
        led_row.addWidget(QLabel("LED:"))
        led_row.addWidget(self._led)
        led_row.addStretch(1)

        # Serial console.
        self._serial = SerialConsole("mcu")
        self._serial.text_to_send.connect(
            lambda s: self._proxy.send_command({"c": "uart", "chip": "mcu", "data": s})
        )

        v = QVBoxLayout(self)
        v.addLayout(btn_row)
        v.addLayout(led_row)
        v.addWidget(self._serial, 1)

        state.pin_changed.connect(self._on_pin_changed)
        state.uart_appended.connect(self._on_uart_appended)

    def _on_pin_changed(self, chip: str, pin: str, val: int) -> None:
        if chip == self._chip and pin == "PB5":
            self._led.set_on(bool(val))

    def _on_uart_appended(self, chip: str, data: str) -> None:
        if chip == self._chip:
            self._serial.append(data)
