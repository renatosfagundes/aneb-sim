"""
LedWidget — round colored circle that brightens when its pin is HIGH or
its PWM duty is non-zero. Stateless renderer; observes SimState.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QColor, QPainter, QBrush, QPen
from PyQt6.QtWidgets import QWidget


class LedWidget(QWidget):
    DEFAULT_COLOR = QColor("#22cc44")    # green
    OFF_COLOR     = QColor("#1a3a22")    # dim green

    def __init__(self, label: str = "", color: QColor | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._label = label
        self._color = color or self.DEFAULT_COLOR
        self._brightness = 0.0   # 0.0 = off, 1.0 = full
        self.setMinimumSize(QSize(28, 28))
        self.setToolTip(label)

    def sizeHint(self) -> QSize:
        return QSize(34, 34)

    def set_brightness(self, value: float) -> None:
        """0.0..1.0; 0 = off, anything > 0 lights up proportionally."""
        if value < 0.0: value = 0.0
        if value > 1.0: value = 1.0
        if value != self._brightness:
            self._brightness = value
            self.update()

    def set_on(self, on: bool) -> None:
        self.set_brightness(1.0 if on else 0.0)

    def paintEvent(self, _evt) -> None:    # noqa: N802 (Qt signature)
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = self.rect().adjusted(2, 2, -2, -2)
        # Interpolate between off and full color by brightness.
        on  = self._color
        off = self.OFF_COLOR
        b = self._brightness
        c = QColor(
            int(off.red()   + (on.red()   - off.red())   * b),
            int(off.green() + (on.green() - off.green()) * b),
            int(off.blue()  + (on.blue()  - off.blue())  * b),
        )
        p.setBrush(QBrush(c))
        p.setPen(QPen(QColor("#222"), 1))
        p.drawEllipse(rect)
