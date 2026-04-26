"""
PotWidget — slider for a single ADC channel (0..1023). Emits value_changed
on user drag; the parent panel maps that to a `adc` command.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QSlider, QWidget


class PotWidget(QWidget):
    value_changed = pyqtSignal(int)

    def __init__(self, label: str, parent=None) -> None:
        super().__init__(parent)
        self._slider = QSlider(Qt.Orientation.Horizontal, self)
        self._slider.setRange(0, 1023)
        self._slider.setValue(0)
        self._slider.setMinimumWidth(80)

        self._readout = QLabel("0", self)
        self._readout.setMinimumWidth(36)
        self._readout.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(QLabel(label, self))
        layout.addWidget(self._slider, 1)
        layout.addWidget(self._readout)

        self._slider.valueChanged.connect(self._on_changed)

    def _on_changed(self, v: int) -> None:
        self._readout.setText(str(v))
        self.value_changed.emit(v)
