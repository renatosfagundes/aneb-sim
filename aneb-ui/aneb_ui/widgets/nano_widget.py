"""
NanoWidget — stylized Arduino Nano top-down rendering.

Paints a teal-green PCB rectangle with the USB connector, the
ATmega328P chip, the reset button, the on-board status LEDs (TX / RX
/ L / PWR), and the header pin rows along the top and bottom edges.
Each header pin's logic level is rendered live as a colored dot so
the user can see firmware-driven pins toggle in real time.

Pinout matches Arduino Nano (ATmega328P) silk-screen — top row
D12..D2, RX0/TX1; bottom row D13/3V3/AREF/A0..A7/5V/RST/GND/VIN.

This is a cosmetic widget; logic state comes from SimState via
update_pin() / update_pwm() public slots.
"""
from __future__ import annotations

from PyQt6.QtCore import QPointF, QRectF, QSize, Qt
from PyQt6.QtGui  import (QBrush, QColor, QFont, QFontMetrics, QPainter, QPen,
                          QRadialGradient)
from PyQt6.QtWidgets import QSizePolicy, QWidget


# Logical board layout in board-units. The widget rescales to fit;
# coordinates here are nominal and remain stable as the widget grows.
NANO_W = 420
NANO_H = 160

# Pin labels for the two header rows. None entries mean the slot is
# present but doesn't correspond to a port pin in our model (power,
# RST, AREF, GND, VIN, etc.). Display-only.
TOP_ROW = [
    "D13", "3V3", "AREF", "A0", "A1", "A2", "A3", "A4", "A5",
    "A6",  "A7",  "5V",   "RST", "GND", "VIN",
]
BOTTOM_ROW = [
    "D12", "D11", "D10", "D9", "D8", "D7", "D6", "D5", "D4", "D3",
    "D2",  "GND", "RST", "RX0", "TX1",
]

# Map labels -> AVR port + bit (only for digital pins). Power and reset
# pins map to None and just render as a static gray dot.
def _digital_to_port(label: str) -> tuple[str, int] | None:
    if label == "D0":   return ("PD", 0)
    if label == "D1":   return ("PD", 1)
    if label.startswith("D") and label != "D13":
        try: return ("PD" if int(label[1:]) <= 7 else "PB",
                     int(label[1:]) if int(label[1:]) <= 7 else int(label[1:]) - 8)
        except ValueError: return None
    if label == "D13":  return ("PB", 5)
    if label.startswith("A") and len(label) == 2 and label[1].isdigit():
        n = int(label[1])
        if 0 <= n <= 5: return ("PC", n)
        # A6, A7 ADC-only — no digital pin to track
        return None
    if label == "RX0":  return ("PD", 0)
    if label == "TX1":  return ("PD", 1)
    return None


class NanoWidget(QWidget):
    PCB_COLOR        = QColor(20,  90,  90)     # teal
    PCB_BORDER       = QColor( 5,  35,  35)
    SILK             = QColor(220, 235, 230)    # off-white silk
    PIN_HOLE         = QColor( 22,  22,  22)
    PIN_HOLE_BORDER  = QColor(170, 175, 175)
    HIGH_COLOR       = QColor("#ffd24a")        # amber when pin is HIGH
    LOW_COLOR        = QColor( 60,  70,  80)
    BUILTIN_COLOR    = QColor("#ffaa22")
    TX_COLOR         = QColor("#ff6644")
    RX_COLOR         = QColor("#ffdd44")
    PWR_COLOR        = QColor("#44ff66")

    def __init__(self, label: str = "ECU", parent=None) -> None:
        super().__init__(parent)
        self._label = label

        # Per-port-pin logic level cache. Keyed as ("PB", 5) -> 0/1.
        self._pin_states: dict[tuple[str, int], int] = {}
        # PWM duty 0..1 for select pins (D6 = LDR_LED on ECU1; D5 = LOOP).
        self._pwm: dict[tuple[str, int], float] = {}

        # On-board LED levels (built-in 'L' = D13, TX/RX UART activity).
        self._builtin_on = False
        self._tx_active  = 0.0
        self._rx_active  = 0.0

        self.setMinimumSize(QSize(360, 140))
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)

    def sizeHint(self) -> QSize:
        return QSize(NANO_W, NANO_H)

    # ----- public state inputs ---------------------------------------

    def set_pin_state(self, port: str, bit: int, value: int) -> None:
        self._pin_states[(port, bit)] = int(bool(value))
        if port == "PB" and bit == 5:
            self._builtin_on = bool(value)
        self.update()

    def set_pwm(self, port: str, bit: int, duty: float) -> None:
        self._pwm[(port, bit)] = max(0.0, min(1.0, duty))
        self.update()

    def pulse_tx(self) -> None:
        """Brief LED flash on UART TX activity (decays via timer in panel)."""
        self._tx_active = 1.0
        self.update()

    def pulse_rx(self) -> None:
        self._rx_active = 1.0
        self.update()

    def decay_uart(self, factor: float = 0.6) -> None:
        """Called from a periodic timer to fade the TX/RX activity LEDs."""
        self._tx_active *= factor
        self._rx_active *= factor
        if self._tx_active < 0.05: self._tx_active = 0.0
        if self._rx_active < 0.05: self._rx_active = 0.0
        self.update()

    # ----- drawing ---------------------------------------------------

    def paintEvent(self, _evt) -> None:                # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Scale logical board to the widget's actual size while keeping
        # aspect ratio (letterbox vertically if needed).
        w = self.width()
        h = self.height()
        scale = min(w / NANO_W, h / NANO_H)
        bw    = NANO_W * scale
        bh    = NANO_H * scale
        ox    = (w - bw) / 2
        oy    = (h - bh) / 2
        p.translate(ox, oy)
        p.scale(scale, scale)

        # PCB body with rounded corners.
        body = QRectF(0, 0, NANO_W, NANO_H)
        p.setPen(QPen(self.PCB_BORDER, 2))
        p.setBrush(QBrush(self.PCB_COLOR))
        p.drawRoundedRect(body, 8, 8)

        # USB connector on the left edge — a silver block protruding
        # slightly off the PCB.
        usb = QRectF(-12, NANO_H/2 - 22, 38, 44)
        p.setPen(QPen(QColor(80, 85, 90), 1))
        usb_grad = QRadialGradient(usb.center(), usb.width())
        usb_grad.setColorAt(0.0, QColor(220, 222, 226))
        usb_grad.setColorAt(1.0, QColor(120, 124, 130))
        p.setBrush(QBrush(usb_grad))
        p.drawRoundedRect(usb, 3, 3)

        # ATmega328P central QFP package.
        chip = QRectF(NANO_W * 0.42, NANO_H * 0.35, NANO_W * 0.18, NANO_H * 0.36)
        p.setPen(QPen(QColor(15, 15, 15), 1))
        p.setBrush(QBrush(QColor(28, 28, 30)))
        p.drawRoundedRect(chip, 2, 2)
        # Pin-1 dot.
        p.setBrush(QBrush(QColor(160, 160, 160)))
        p.drawEllipse(QPointF(chip.x() + 6, chip.y() + 6), 1.5, 1.5)
        # Chip text.
        p.setPen(QPen(QColor(180, 180, 180), 1))
        f = QFont("Consolas")
        f.setPixelSize(8)
        p.setFont(f)
        p.drawText(chip.adjusted(2, 2, -2, -2),
                   Qt.AlignmentFlag.AlignCenter,
                   "ATmega\n328P")

        # Crystal oscillator — small rectangle next to the chip.
        xtal = QRectF(chip.right() + 6, chip.center().y() - 6, 18, 12)
        p.setPen(QPen(QColor(100, 100, 105), 1))
        p.setBrush(QBrush(QColor(180, 180, 180)))
        p.drawRoundedRect(xtal, 2, 2)

        # Reset button — small black circle.
        rst = QPointF(chip.right() + 35, chip.center().y() - 2)
        p.setPen(QPen(QColor(15, 15, 15), 1))
        p.setBrush(QBrush(QColor(40, 40, 40)))
        p.drawEllipse(rst, 5, 5)

        # On-board status LEDs (PWR, L=D13, TX, RX) clustered near chip.
        self._draw_led(p, QPointF(chip.x() - 8,        chip.y() + 6),  self.PWR_COLOR, 1.0)
        self._draw_led(p, QPointF(chip.x() - 8,        chip.y() + 18), self.BUILTIN_COLOR, 1.0 if self._builtin_on else 0.0)
        self._draw_led(p, QPointF(chip.x() - 8,        chip.y() + 30), self.TX_COLOR, self._tx_active)
        self._draw_led(p, QPointF(chip.x() - 8,        chip.y() + 42), self.RX_COLOR, self._rx_active)
        # Tiny labels next to the LEDs.
        p.setPen(QPen(QColor(180, 220, 200), 1))
        f.setPixelSize(7)
        p.setFont(f)
        for txt, dy in (("PWR", 6), ("L", 18), ("TX", 30), ("RX", 42)):
            p.drawText(QPointF(chip.x() - 28, chip.y() + dy + 2), txt)

        # Header rows — top (further from the USB) and bottom (closer to the
        # USB on the photo, but rendered along the bottom edge here).
        self._draw_header_row(p, BOTTOM_ROW, y=NANO_H - 18, top=False)
        self._draw_header_row(p, TOP_ROW,    y=18,         top=True)

        # Big chip-name label off the board (drawn by the parent panel
        # outside the Nano body — no need here).

    def _draw_led(self, p: QPainter, c: QPointF, color: QColor, brightness: float) -> None:
        # Outer halo when bright.
        if brightness > 0.05:
            halo = QRadialGradient(c, 7)
            hc = QColor(color); hc.setAlphaF(0.45 * brightness)
            halo.setColorAt(0.0, hc)
            halo.setColorAt(1.0, QColor(0, 0, 0, 0))
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QBrush(halo))
            p.drawEllipse(c, 8, 8)
        # LED itself.
        body = QColor(color) if brightness > 0.05 else QColor(40, 50, 50)
        p.setPen(QPen(QColor(15, 15, 15), 0.5))
        p.setBrush(QBrush(body))
        p.drawEllipse(c, 3, 3)

    def _draw_header_row(self, p: QPainter, labels: list[str], y: float, top: bool) -> None:
        n  = len(labels)
        x0 = 12
        x1 = NANO_W - 12
        spacing = (x1 - x0) / (n - 1) if n > 1 else 0
        f = QFont("Consolas")
        f.setPixelSize(7)
        f.setBold(True)
        p.setFont(f)

        for i, lbl in enumerate(labels):
            cx = x0 + spacing * i
            cy = y

            # Pin hole.
            p.setPen(QPen(self.PIN_HOLE_BORDER, 0.8))
            p.setBrush(QBrush(self.PIN_HOLE))
            p.drawEllipse(QPointF(cx, cy), 3.4, 3.4)

            # Live state dot inside the hole if we know the pin.
            mapped = _digital_to_port(lbl)
            if mapped:
                state = self._pin_states.get(mapped, 0)
                duty  = self._pwm.get(mapped, 0.0)
                level = float(state) if duty == 0 else duty
                if level > 0.0:
                    halo = QRadialGradient(QPointF(cx, cy), 8)
                    hc = QColor(self.HIGH_COLOR); hc.setAlphaF(0.35 * level)
                    halo.setColorAt(0.0, hc)
                    halo.setColorAt(1.0, QColor(0, 0, 0, 0))
                    p.setPen(Qt.PenStyle.NoPen)
                    p.setBrush(QBrush(halo))
                    p.drawEllipse(QPointF(cx, cy), 8, 8)
                    p.setBrush(QBrush(self.HIGH_COLOR))
                    p.drawEllipse(QPointF(cx, cy), 1.6, 1.6)

            # Label below or above.
            p.setPen(QPen(self.SILK, 1))
            tx = cx
            if top:
                ty = cy + 12
            else:
                ty = cy - 6
            fm = QFontMetrics(f)
            tw = fm.horizontalAdvance(lbl)
            p.drawText(QPointF(tx - tw / 2, ty), lbl)
