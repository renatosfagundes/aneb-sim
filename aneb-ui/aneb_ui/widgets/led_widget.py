"""
LedWidget — round LED with radial gradient and an outer "glow" halo
when on. Brightness 0.0..1.0 dims linearly between off and on colors,
and proportionally scales the halo's alpha.
"""
from __future__ import annotations

from PyQt6.QtCore import QPointF, QRectF, QSize, Qt
from PyQt6.QtGui  import QBrush, QColor, QPainter, QPen, QRadialGradient
from PyQt6.QtWidgets import QSizePolicy, QWidget


class LedWidget(QWidget):
    DEFAULT_COLOR = QColor("#22cc44")     # green
    OFF_COLOR     = QColor("#0e2014")     # very dim green-black

    def __init__(self, label: str = "", color: QColor | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._label = label
        self._color = QColor(color) if color else QColor(self.DEFAULT_COLOR)
        self._brightness = 0.0
        self.setMinimumSize(QSize(32, 32))
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        self.setToolTip(label)

    def sizeHint(self) -> QSize:
        return QSize(36, 36)

    def set_brightness(self, value: float) -> None:
        if value < 0.0: value = 0.0
        if value > 1.0: value = 1.0
        if value != self._brightness:
            self._brightness = value
            self.update()

    def set_on(self, on: bool) -> None:
        self.set_brightness(1.0 if on else 0.0)

    # ----- drawing ---------------------------------------------------

    def _lerp(self, a: int, b: int, t: float) -> int:
        return int(a + (b - a) * t)

    def paintEvent(self, _evt) -> None:                # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        b = self._brightness
        on  = self._color
        off = self.OFF_COLOR

        # Body color = interpolated between off and on.
        body = QColor(
            self._lerp(off.red(),   on.red(),   b),
            self._lerp(off.green(), on.green(), b),
            self._lerp(off.blue(),  on.blue(),  b),
        )

        rect = QRectF(self.rect()).adjusted(2, 2, -2, -2)
        cx, cy = rect.center().x(), rect.center().y()

        # Outer halo (visible only when on).
        if b > 0.05:
            halo_r = max(rect.width(), rect.height()) * 0.55
            halo_rect = QRectF(cx - halo_r, cy - halo_r, halo_r * 2, halo_r * 2)
            halo = QRadialGradient(cx, cy, halo_r)
            halo_color = QColor(on)
            halo_color.setAlphaF(min(0.55, b * 0.6))
            halo.setColorAt(0.0, halo_color)
            halo_outer = QColor(on)
            halo_outer.setAlphaF(0.0)
            halo.setColorAt(1.0, halo_outer)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QBrush(halo))
            p.drawEllipse(halo_rect)

        # LED body — radial gradient for a 3D look.
        body_grad = QRadialGradient(cx - rect.width() * 0.18,
                                    cy - rect.height() * 0.18,
                                    rect.width() * 0.7)
        bright = QColor(
            min(255, body.red()   + 40 if b > 0.05 else body.red()),
            min(255, body.green() + 40 if b > 0.05 else body.green()),
            min(255, body.blue()  + 40 if b > 0.05 else body.blue()),
        )
        body_grad.setColorAt(0.0, bright)
        body_grad.setColorAt(0.7, body)
        body_grad.setColorAt(1.0, body.darker(160))
        p.setPen(QPen(QColor(15, 15, 15), 1))
        p.setBrush(QBrush(body_grad))
        p.drawEllipse(rect)

        # Specular highlight (small white blob, top-left).
        if b > 0.05:
            hr = rect.width() * 0.18
            hi = QPointF(cx - rect.width() * 0.22, cy - rect.height() * 0.22)
            highlight = QRadialGradient(hi, hr)
            hl_inner = QColor(255, 255, 255, int(180 * b))
            highlight.setColorAt(0.0, hl_inner)
            highlight.setColorAt(1.0, QColor(255, 255, 255, 0))
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QBrush(highlight))
            p.drawEllipse(QRectF(hi.x() - hr, hi.y() - hr, hr * 2, hr * 2))
