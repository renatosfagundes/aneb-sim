"""
PushButton — tactile-style momentary or latching input.

Visually tries to look like the small black tactile pushbuttons on the
ANEB board: rounded square cap, depressed shadow when pressed, dark
metallic ring when released.
"""
from __future__ import annotations

from PyQt6.QtCore import QSize, Qt, pyqtSignal
from PyQt6.QtGui  import QBrush, QColor, QPainter, QPen, QRadialGradient
from PyQt6.QtWidgets import QSizePolicy, QWidget


class PushButton(QWidget):
    """Custom-painted tactile button.

    momentary mode (default): pressed = press_value, released = release_value.
    latching  mode: each click toggles the state.
    """

    value_changed = pyqtSignal(int)
    clicked       = pyqtSignal()

    def __init__(
        self,
        label: str,
        press_value: int = 1,
        release_value: int = 0,
        latching: bool = False,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self._label    = label
        self._press    = press_value
        self._release  = release_value
        self._latching = latching
        self._down     = False
        self._latched  = False
        self.setMinimumSize(QSize(56, 56))
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def sizeHint(self) -> QSize:
        return QSize(64, 64)

    # ----- drawing ---------------------------------------------------

    def paintEvent(self, _evt) -> None:                # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        rect = self.rect().adjusted(3, 3, -3, -3)
        cx, cy = rect.center().x(), rect.center().y()

        pressed = self._down or (self._latching and self._latched)

        # Outer ring (the metal collar around the cap).
        ring = QRadialGradient(cx, cy, rect.width() / 2)
        ring.setColorAt(0.0, QColor(80, 88, 96))
        ring.setColorAt(1.0, QColor(28, 34, 40))
        p.setPen(QPen(QColor(20, 25, 30), 1))
        p.setBrush(QBrush(ring))
        p.drawRoundedRect(rect, 8, 8)

        # Inner cap — slightly smaller, with a fake bevel that flips when
        # pressed.
        cap = rect.adjusted(6, 6, -6, -6)
        cap_grad = QRadialGradient(cx, cy, cap.width() * 0.7)
        if pressed:
            cap_grad.setColorAt(0.0, QColor(40, 50, 56))
            cap_grad.setColorAt(1.0, QColor(15, 20, 25))
        else:
            cap_grad.setColorAt(0.0, QColor(70, 80, 90))
            cap_grad.setColorAt(1.0, QColor(20, 25, 30))
        p.setPen(QPen(QColor(10, 14, 18), 1))
        p.setBrush(QBrush(cap_grad))
        p.drawRoundedRect(cap, 5, 5)

        # Label, two lines centered.
        p.setPen(QPen(QColor("#cdfac0"), 1))
        f = p.font()
        f.setPointSize(7)
        f.setBold(True)
        p.setFont(f)
        p.drawText(rect, Qt.AlignmentFlag.AlignCenter, self._label)

    # ----- input -----------------------------------------------------

    def mousePressEvent(self, evt) -> None:            # noqa: N802
        if evt.button() != Qt.MouseButton.LeftButton:
            return
        self._down = True
        self.update()
        if self._latching:
            self._latched = not self._latched
            self.value_changed.emit(self._press if self._latched else self._release)
        else:
            self.value_changed.emit(self._press)
        self.clicked.emit()

    def mouseReleaseEvent(self, evt) -> None:          # noqa: N802
        if evt.button() != Qt.MouseButton.LeftButton:
            return
        self._down = False
        self.update()
        if not self._latching:
            self.value_changed.emit(self._release)

    def is_latched(self) -> bool:
        return self._latched
