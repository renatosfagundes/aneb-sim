"""
PushButton — momentary input. Pressed = HIGH while held; released = LOW.
Wired so that pressing/releasing emits a `din` command via the proxy.

Latching mode (toggle) is also supported, useful for sticky configuration
inputs like the Mode Selector buttons on the MCU.
"""
from __future__ import annotations

from typing import Callable

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import QPushButton


class PushButton(QPushButton):
    """
    Two modes:
      - momentary (default): press_value emits on press, release_value on release
      - latching: each click toggles the value
    """

    value_changed = pyqtSignal(int)   # 0 or 1

    def __init__(
        self,
        label: str,
        press_value: int = 1,
        release_value: int = 0,
        latching: bool = False,
        parent=None,
    ) -> None:
        super().__init__(label, parent)
        self.setCheckable(latching)
        self._press   = press_value
        self._release = release_value
        self._latching = latching
        if latching:
            self.toggled.connect(self._on_toggled)
        else:
            self.pressed.connect(lambda: self.value_changed.emit(self._press))
            self.released.connect(lambda: self.value_changed.emit(self._release))

    def _on_toggled(self, checked: bool) -> None:
        self.value_changed.emit(self._press if checked else self._release)
