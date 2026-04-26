"""
EcuPanel — composes one ECU's worth of widgets:

  - 2 LEDs (DOUT0 = pin 3 / PD3,  DOUT1 = pin 4 / PD4)
    plus a built-in LED indicator on PB5 (LED_BUILTIN)
  - Buzzer indicator on PD7 (ECU1 only; harmless on others)
  - 4 push-buttons (DIN1..DIN4 = A4 / A5 / D9 / D8)
  - 4 potentiometers (A0..A3)
  - Serial console for UART0
  - Per-ECU CAN state badge (TEC/REC/state)

The panel is purely a renderer of SimState. Inputs (button presses,
slider drags, typed UART text) call SimProxy.cmd_* directly.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui  import QColor
from PyQt6.QtWidgets import (
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..sim_proxy import SimProxy
from ..state import SimState
from .led_widget    import LedWidget
from .button_widget import PushButton
from .pot_widget    import PotWidget
from .serial_console import SerialConsole


# Per-ECU board pin map (matches Board.h — see ARCHITECTURE.md / DESIGN.md).
DIN_PINS  = ["A4", "A5", "D9", "D8"]
DOUT_PINS = ["D3", "D4"]              # DOUT0, DOUT1 (PD3 / PD4)
BUILTIN   = "PB5"
BUZZER    = "PD7"                     # ECU1 only — harmless to display


class EcuPanel(QGroupBox):
    def __init__(self, chip: str, proxy: SimProxy, state: SimState, parent=None) -> None:
        super().__init__(chip.upper(), parent)
        self._chip  = chip
        self._proxy = proxy
        self._state = state

        # --- LED row -----------------------------------------------------
        leds_box = QHBoxLayout()
        self._dout0 = LedWidget("DOUT0 (D3)")
        self._dout1 = LedWidget("DOUT1 (D4)")
        self._led_b = LedWidget("LED (D13)", color=QColor("#ffaa22"))
        self._buzz  = LedWidget("BUZZ (D7)", color=QColor("#ff4444"))
        for label, w in (("DOUT0", self._dout0), ("DOUT1", self._dout1),
                         ("LED",   self._led_b), ("BUZZ",  self._buzz)):
            col = QVBoxLayout()
            col.setContentsMargins(0, 0, 0, 0)
            col.addWidget(w, alignment=Qt.AlignmentFlag.AlignHCenter)
            col.addWidget(QLabel(label, alignment=Qt.AlignmentFlag.AlignHCenter))
            leds_box.addLayout(col)
        leds_box.addStretch(1)

        # --- Buttons -----------------------------------------------------
        btn_box = QHBoxLayout()
        self._btns = []
        for i, pin in enumerate(DIN_PINS, start=1):
            b = PushButton(f"DIN{i}\n{pin}", press_value=1, release_value=0)
            b.setMinimumHeight(40)
            b.value_changed.connect(
                lambda v, p=pin: self._proxy.cmd_din(self._chip, p, v)
            )
            btn_box.addWidget(b)
            self._btns.append(b)

        # --- Potentiometers ----------------------------------------------
        pots_form = QFormLayout()
        for ch in range(4):
            pot = PotWidget(f"AIN{ch} (A{ch})")
            pot.value_changed.connect(
                lambda v, c=ch: self._proxy.cmd_adc(self._chip, c, v)
            )
            pots_form.addRow(pot)

        # --- CAN state badge --------------------------------------------
        self._can_state_lbl = QLabel("CAN  TEC=0 REC=0 active")
        self._can_state_lbl.setStyleSheet(
            "QLabel { padding: 2px 6px; border-radius: 3px; "
            "background: #224422; color: #cdfac0; }"
        )
        can_recover = QPushButton("Recover")
        can_recover.clicked.connect(lambda: self._proxy.cmd_can_recover(self._chip))
        force_busoff = QPushButton("Force bus-off")
        force_busoff.clicked.connect(lambda: self._proxy.cmd_force_busoff(self._chip))

        can_row = QHBoxLayout()
        can_row.addWidget(self._can_state_lbl, 1)
        can_row.addWidget(force_busoff)
        can_row.addWidget(can_recover)

        # --- Serial console ----------------------------------------------
        self._serial = SerialConsole(chip)
        self._serial.text_to_send.connect(
            lambda s: self._proxy.send_command({"c": "uart", "chip": self._chip, "data": s})
        )

        # --- Assemble ----------------------------------------------------
        v = QVBoxLayout(self)
        v.addLayout(leds_box)
        v.addLayout(btn_box)
        v.addLayout(pots_form)
        v.addLayout(can_row)
        v.addWidget(self._serial, 1)

        # --- Wire to state -----------------------------------------------
        state.pin_changed.connect(self._on_pin_changed)
        state.pwm_changed.connect(self._on_pwm_changed)
        state.uart_appended.connect(self._on_uart_appended)
        state.can_state_changed.connect(self._on_can_state_changed)

    # -------- state slots ----------------------------------------------

    def _on_pin_changed(self, chip: str, pin: str, val: int) -> None:
        if chip != self._chip:
            return
        # Map port-form pin names to LED widgets where applicable.
        if pin == "PD3":     self._dout0.set_on(bool(val))
        elif pin == "PD4":   self._dout1.set_on(bool(val))
        elif pin == BUILTIN: self._led_b.set_on(bool(val))
        elif pin == BUZZER:  self._buzz.set_on(bool(val))

    def _on_pwm_changed(self, chip: str, pin: str, duty: float) -> None:
        if chip != self._chip:
            return
        # PWM on the LED-style outputs dims them.
        if pin == "PD3":     self._dout0.set_brightness(duty)
        elif pin == "PD6":   self._led_b.set_brightness(duty)  # LDR_LED on ECU1
        # Other PWM channels (PD5 LOOP, PB1) don't drive a panel LED.

    def _on_uart_appended(self, chip: str, data: str) -> None:
        if chip == self._chip:
            self._serial.append(data)

    def _on_can_state_changed(self, chip: str, tec: int, rec: int, state: str) -> None:
        if chip != self._chip:
            return
        bg = {"active": "#224422", "passive": "#665500", "bus-off": "#662222"}.get(state, "#222")
        fg = {"active": "#cdfac0", "passive": "#ffe089", "bus-off": "#ffb0b0"}.get(state, "#ddd")
        self._can_state_lbl.setText(f"CAN  TEC={tec} REC={rec} {state}")
        self._can_state_lbl.setStyleSheet(
            f"QLabel {{ padding: 2px 6px; border-radius: 3px; "
            f"background: {bg}; color: {fg}; }}"
        )
