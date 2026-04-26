"""
CanInject — small form for sending arbitrary CAN frames onto the bus.
Mirrors the can_inject JSON command.
"""
from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox, QFormLayout, QGroupBox, QHBoxLayout, QLineEdit,
    QPushButton, QSpinBox, QVBoxLayout, QWidget,
)

from ..sim_proxy import SimProxy


class CanInject(QGroupBox):
    def __init__(self, proxy: SimProxy, parent=None) -> None:
        super().__init__("CAN inject", parent)
        self._proxy = proxy

        self._id   = QLineEdit("0x123", self)
        self._ext  = QCheckBox("ext", self)
        self._rtr  = QCheckBox("rtr", self)
        self._dlc  = QSpinBox(self); self._dlc.setRange(0, 8); self._dlc.setValue(0)
        self._data = QLineEdit("", self)
        self._data.setPlaceholderText("hex bytes, e.g. DEADBEEF (omit for RTR)")

        send = QPushButton("Send", self)
        send.clicked.connect(self._on_send)

        flags = QHBoxLayout()
        flags.addWidget(self._ext)
        flags.addWidget(self._rtr)
        flags.addStretch(1)

        form = QFormLayout()
        form.addRow("ID",   self._id)
        form.addRow("flags", flags)
        form.addRow("DLC",  self._dlc)
        form.addRow("data", self._data)

        v = QVBoxLayout(self)
        v.addLayout(form)
        v.addWidget(send)

    def _on_send(self) -> None:
        try:
            id_str = self._id.text().strip()
            frame_id = int(id_str, 0)
        except ValueError:
            return
        data = self._data.text().strip().replace(" ", "").upper()
        # Strip any 0x prefix some users may type by mistake.
        if data.startswith("0X"):
            data = data[2:]
        self._proxy.cmd_can_inject(
            frame_id, data,
            ext=self._ext.isChecked(),
            rtr=self._rtr.isChecked(),
            dlc=self._dlc.value(),
        )
