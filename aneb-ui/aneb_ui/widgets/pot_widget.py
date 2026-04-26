"""
PotWidget — circular trim-pot lookalike for a single ADC channel
(0..1023).

The user drags vertically to rotate the knob; up = increase, down =
decrease. Click + drag is preferred over scroll-wheel because the
real board's blue trim pots respond to mechanical rotation and we want
the muscle memory to carry over.
"""
from __future__ import annotations

import math

from PyQt6.QtCore import QPointF, QRectF, QSize, Qt, pyqtSignal
from PyQt6.QtGui  import QBrush, QColor, QConicalGradient, QPainter, QPen, QRadialGradient
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QSizePolicy, QVBoxLayout, QWidget


# Knob sweeps 270° from -135° (min, lower-left) clockwise to +135° (max,
# lower-right), leaving a 90° gap at the bottom where the indicator
# never points — matches typical rotary-pot aesthetics.
ANGLE_MIN = -135.0
ANGLE_MAX =  135.0


class _Knob(QWidget):
    value_changed = pyqtSignal(int)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._value = 0           # 0..1023
        self._dragging = False
        self._drag_y = 0
        self._drag_start_value = 0
        self.setFixedSize(QSize(70, 70))
        self.setCursor(Qt.CursorShape.SizeVerCursor)

    def value(self) -> int:
        return self._value

    def set_value(self, v: int) -> None:
        v = max(0, min(1023, int(v)))
        if v != self._value:
            self._value = v
            self.update()
            self.value_changed.emit(v)

    # ----- drawing ---------------------------------------------------

    def paintEvent(self, _evt) -> None:                # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        rect  = QRectF(self.rect()).adjusted(4, 4, -4, -4)
        cx    = rect.center().x()
        cy    = rect.center().y()
        r     = min(rect.width(), rect.height()) / 2

        # Outer ring (the metal collar).
        outer_pen = QPen(QColor(38, 50, 56), 2)
        outer = QRadialGradient(cx, cy, r)
        outer.setColorAt(0.0, QColor(80, 120, 160))
        outer.setColorAt(1.0, QColor(20, 40, 60))
        p.setPen(outer_pen)
        p.setBrush(QBrush(outer))
        p.drawEllipse(rect)

        # Inner cap (the blue plastic top of a real trim pot).
        inner_r = r * 0.78
        inner_rect = QRectF(cx - inner_r, cy - inner_r, inner_r * 2, inner_r * 2)
        inner = QRadialGradient(cx - r * 0.25, cy - r * 0.25, inner_r * 1.5)
        inner.setColorAt(0.0, QColor(120, 180, 220))
        inner.setColorAt(0.6, QColor(40,  90, 140))
        inner.setColorAt(1.0, QColor(20,  50,  90))
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QBrush(inner))
        p.drawEllipse(inner_rect)

        # Tick / pointer.
        frac  = self._value / 1023.0
        angle = ANGLE_MIN + (ANGLE_MAX - ANGLE_MIN) * frac
        rad   = math.radians(angle - 90)   # -90 so 0° points up
        tip = QPointF(cx + math.cos(rad) * (inner_r * 0.85),
                      cy + math.sin(rad) * (inner_r * 0.85))
        base = QPointF(cx + math.cos(rad) * (inner_r * 0.35),
                       cy + math.sin(rad) * (inner_r * 0.35))
        p.setPen(QPen(QColor(220, 240, 255), 3, Qt.PenStyle.SolidLine,
                      Qt.PenCapStyle.RoundCap))
        p.drawLine(base, tip)

    # ----- input -----------------------------------------------------

    def mousePressEvent(self, evt) -> None:            # noqa: N802
        if evt.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self._drag_y = evt.position().y()
            self._drag_start_value = self._value
            evt.accept()

    def mouseReleaseEvent(self, evt) -> None:          # noqa: N802
        self._dragging = False
        evt.accept()

    def mouseMoveEvent(self, evt) -> None:             # noqa: N802
        if not self._dragging:
            return
        dy = self._drag_y - evt.position().y()
        # 200 px drag = full 0..1023 sweep.
        delta = int(dy * (1024.0 / 200.0))
        self.set_value(self._drag_start_value + delta)
        evt.accept()

    def wheelEvent(self, evt) -> None:                 # noqa: N802
        steps = evt.angleDelta().y() / 120.0
        self.set_value(self._value + int(steps * 32))
        evt.accept()


class PotWidget(QWidget):
    """Pot widget = label + knob + numeric readout, vertical layout."""

    value_changed = pyqtSignal(int)

    def __init__(self, label: str, parent=None) -> None:
        super().__init__(parent)
        self._knob = _Knob(self)
        self._readout = QLabel("0", self)
        self._readout.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        self._readout.setStyleSheet(
            "QLabel { color: #cdfac0; font-family: Consolas, monospace; font-size: 10pt; }"
        )

        lbl = QLabel(label, self)
        lbl.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        lbl.setStyleSheet("QLabel { color: #cdfac0; font-size: 8pt; }")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(1)
        layout.addWidget(lbl,            alignment=Qt.AlignmentFlag.AlignHCenter)
        layout.addWidget(self._knob,     alignment=Qt.AlignmentFlag.AlignHCenter)
        layout.addWidget(self._readout,  alignment=Qt.AlignmentFlag.AlignHCenter)

        self._knob.value_changed.connect(self._on_changed)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)

    def _on_changed(self, v: int) -> None:
        self._readout.setText(str(v))
        self.value_changed.emit(v)
