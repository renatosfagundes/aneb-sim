"""
EcuPanel — visually-styled per-ECU panel.

Layout, top to bottom:
    LEDs row  (DOUT0 / DOUT1 / built-in LED / BUZZ)
    NanoWidget — stylized Arduino Nano with live pin states
    DIN buttons (4)
    AIN pots (4)
    CAN state badge + force-busoff / recover
    Serial console (compact, expandable)

The panel renders SimState — it never mutates state. User input flows
through SimProxy.cmd_*.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui  import QColor
from PyQt6.QtWidgets import (
    QGroupBox, QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout, QWidget,
)

from ..sim_proxy import SimProxy
from ..state import SimState
from .led_widget    import LedWidget
from .button_widget import PushButton
from .pot_widget    import PotWidget
from .serial_console import SerialConsole
from .nano_widget   import NanoWidget


DIN_PINS  = ["A4", "A5", "D9", "D8"]
BUILTIN   = "PB5"
BUZZER    = "PD7"


def _labeled(widget: QWidget, label: str) -> QWidget:
    holder = QWidget()
    v = QVBoxLayout(holder)
    v.setContentsMargins(0, 0, 0, 0)
    v.setSpacing(1)
    v.addWidget(widget, alignment=Qt.AlignmentFlag.AlignHCenter)
    cap = QLabel(label)
    cap.setAlignment(Qt.AlignmentFlag.AlignHCenter)
    cap.setStyleSheet("QLabel { color: #b8e0a8; font-size: 8pt; }")
    v.addWidget(cap)
    return holder


# All AVR ports we care about for the Nano widget's live header dots.
def _split_port(pin_name: str) -> tuple[str, int] | None:
    """Convert 'PB5' -> ('PB', 5). Return None if it's not a port-form name."""
    if (len(pin_name) == 3 and pin_name[0] == 'P'
            and pin_name[1] in 'BCD' and pin_name[2].isdigit()):
        return ("P" + pin_name[1], int(pin_name[2]))
    return None


class EcuPanel(QGroupBox):
    def __init__(self, chip: str, proxy: SimProxy, state: SimState, parent=None) -> None:
        super().__init__(chip.upper(), parent)
        self._chip  = chip
        self._proxy = proxy
        self._state = state

        # ---- LEDs row --------------------------------------------------
        self._dout0 = LedWidget("DOUT0", color=QColor("#22cc44"))   # green
        self._dout1 = LedWidget("DOUT1", color=QColor("#22cc44"))
        self._led_b = LedWidget("LED",   color=QColor("#ffaa22"))   # yellow
        self._buzz  = LedWidget("BUZZ",  color=QColor("#ff4444"))   # red

        leds_row = QHBoxLayout()
        leds_row.setSpacing(12)
        leds_row.addStretch(1)
        for w, lbl in ((self._dout0, "DOUT0\nD3"),
                       (self._dout1, "DOUT1\nD4"),
                       (self._led_b, "LED\nD13"),
                       (self._buzz,  "BUZZ\nD7")):
            leds_row.addWidget(_labeled(w, lbl))
        leds_row.addStretch(1)

        # ---- The Nano! -----------------------------------------------
        self._nano = NanoWidget(chip)

        # Periodic decay so TX/RX activity LEDs fade between bytes.
        self._uart_decay = QTimer(self)
        self._uart_decay.setInterval(80)
        self._uart_decay.timeout.connect(self._nano.decay_uart)
        self._uart_decay.start()

        # ---- Buttons row -----------------------------------------------
        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)
        btn_row.addStretch(1)
        for i, pin in enumerate(DIN_PINS, start=1):
            b = PushButton(f"DIN{i}\n{pin}", press_value=1, release_value=0)
            b.value_changed.connect(
                lambda v, p=pin: self._proxy.cmd_din(self._chip, p, v)
            )
            btn_row.addWidget(b)
        btn_row.addStretch(1)

        # ---- Pots row ---------------------------------------------------
        pots_row = QHBoxLayout()
        pots_row.setSpacing(8)
        pots_row.addStretch(1)
        for ch in range(4):
            pot = PotWidget(f"AIN{ch} (A{ch})")
            pot.value_changed.connect(
                lambda v, c=ch: self._proxy.cmd_adc(self._chip, c, v)
            )
            pots_row.addWidget(pot)
        pots_row.addStretch(1)

        # ---- CAN state badge -------------------------------------------
        self._can_state_lbl = QLabel("CAN  TEC=0 REC=0  active")
        self._can_state_lbl.setStyleSheet(
            "QLabel { padding: 3px 8px; border-radius: 4px; "
            "background: #224422; color: #cdfac0; "
            "font-family: Consolas, monospace; font-size: 9pt; }"
        )
        recover = QPushButton("Recover");      recover.setStyleSheet(self._small_btn_style())
        recover.clicked.connect(lambda: self._proxy.cmd_can_recover(self._chip))
        force   = QPushButton("Force bus-off"); force.setStyleSheet(self._small_btn_style())
        force.clicked.connect(lambda: self._proxy.cmd_force_busoff(self._chip))

        can_row = QHBoxLayout()
        can_row.addWidget(self._can_state_lbl, 1)
        can_row.addWidget(force)
        can_row.addWidget(recover)

        # ---- Serial console -------------------------------------------
        self._serial = SerialConsole(chip)
        self._serial.text_to_send.connect(
            lambda s: self._proxy.send_command({"c": "uart", "chip": self._chip, "data": s})
        )

        # ---- Assemble --------------------------------------------------
        v = QVBoxLayout(self)
        v.setContentsMargins(8, 14, 8, 8)
        v.setSpacing(8)
        v.addLayout(leds_row)
        v.addWidget(self._nano)
        v.addLayout(btn_row)
        v.addLayout(pots_row)
        v.addLayout(can_row)
        v.addWidget(self._serial, 1)

        # ---- State wiring ----------------------------------------------
        state.pin_changed.connect(self._on_pin_changed)
        state.pwm_changed.connect(self._on_pwm_changed)
        state.uart_appended.connect(self._on_uart_appended)
        state.can_state_changed.connect(self._on_can_state_changed)

    # -------- visual helpers ------------------------------------------

    @staticmethod
    def _small_btn_style() -> str:
        return (
            "QPushButton { background: #1c2a20; color: #cdfac0; "
            "border: 1px solid #4a8a5d; padding: 3px 8px; border-radius: 3px; "
            "font-size: 9pt; }"
            "QPushButton:hover { background: #284033; }"
            "QPushButton:pressed { background: #122018; }"
        )

    # -------- state slots ---------------------------------------------

    def _on_pin_changed(self, chip: str, pin: str, val: int) -> None:
        if chip != self._chip:
            return
        # Drive the breakout LEDs.
        if pin == "PD3":     self._dout0.set_on(bool(val))
        elif pin == "PD4":   self._dout1.set_on(bool(val))
        elif pin == BUILTIN: self._led_b.set_on(bool(val))
        elif pin == BUZZER:  self._buzz.set_on(bool(val))
        # Drive the per-pin live indicator on the Nano.
        mapped = _split_port(pin)
        if mapped:
            self._nano.set_pin_state(mapped[0], mapped[1], val)

    def _on_pwm_changed(self, chip: str, pin: str, duty: float) -> None:
        if chip != self._chip:
            return
        if pin == "PD3":   self._dout0.set_brightness(duty)
        elif pin == "PD6": self._led_b.set_brightness(duty)   # LDR_LED on ECU1
        mapped = _split_port(pin)
        if mapped:
            self._nano.set_pwm(mapped[0], mapped[1], duty)

    def _on_uart_appended(self, chip: str, data: str) -> None:
        if chip == self._chip:
            self._serial.append(data)
            self._nano.pulse_tx()

    def _on_can_state_changed(self, chip: str, tec: int, rec: int, state: str) -> None:
        if chip != self._chip:
            return
        bg = {"active": "#224422", "passive": "#665500", "bus-off": "#662222"}.get(state, "#222")
        fg = {"active": "#cdfac0", "passive": "#ffe089", "bus-off": "#ffb0b0"}.get(state, "#ddd")
        self._can_state_lbl.setText(f"CAN  TEC={tec} REC={rec}  {state}")
        self._can_state_lbl.setStyleSheet(
            f"QLabel {{ padding: 3px 8px; border-radius: 4px; "
            f"background: {bg}; color: {fg}; "
            f"font-family: Consolas, monospace; font-size: 9pt; }}"
        )
