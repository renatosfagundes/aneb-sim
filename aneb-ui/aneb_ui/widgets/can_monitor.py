"""
CanMonitor — table view of CAN frames as they cross the bus.

Live append of can_tx events with optional ID filter. Renders ID in
hex, ext/rtr flags, DLC, hex-encoded payload, source ECU, and a
per-frame timestamp (cycles).
"""
from __future__ import annotations

from typing import Any

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QHBoxLayout, QHeaderView, QLabel, QLineEdit, QPushButton,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

from ..state import SimState


COLS = ("ts", "src", "id", "ext", "rtr", "dlc", "data")
HEADERS = ("ts", "src", "id", "ext", "rtr", "dlc", "data")


class CanMonitor(QWidget):
    def __init__(self, state: SimState, parent=None) -> None:
        super().__init__(parent)
        self._state = state
        self._filter: str | None = None

        self._table = QTableWidget(0, len(COLS), self)
        self._table.setHorizontalHeaderLabels(HEADERS)
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._table.setShowGrid(False)
        hdr = self._table.horizontalHeader()
        hdr.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        hdr.setStretchLastSection(True)

        # Top toolbar: id filter + clear.
        self._filter_in = QLineEdit(self)
        self._filter_in.setPlaceholderText('filter by id (e.g. "0x123" or substring)')
        clear = QPushButton("Clear", self)
        clear.clicked.connect(self._clear)
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Filter:"))
        bar.addWidget(self._filter_in, 1)
        bar.addWidget(clear)

        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        v.addLayout(bar)
        v.addWidget(self._table, 1)

        self._filter_in.textChanged.connect(self._on_filter_text)
        state.can_tx_appended.connect(self._on_frame)

    # -----------------------------------------------------------------

    def _on_filter_text(self, txt: str) -> None:
        self._filter = txt.strip() or None
        self._refresh()

    def _refresh(self) -> None:
        self._table.setRowCount(0)
        for f in self._state.can_log():
            if self._matches(f):
                self._append_row(f)

    def _matches(self, f: dict[str, Any]) -> bool:
        if not self._filter:
            return True
        return self._filter.lower() in str(f.get("id", "")).lower()

    def _on_frame(self, evt: dict[str, Any]) -> None:
        if self._matches(evt):
            self._append_row(evt)

    def _append_row(self, f: dict[str, Any]) -> None:
        row = self._table.rowCount()
        self._table.insertRow(row)
        for col, key in enumerate(COLS):
            v = f.get(key, "")
            item = QTableWidgetItem(str(v))
            item.setTextAlignment(Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft)
            self._table.setItem(row, col, item)
        # Bound row count.
        if self._table.rowCount() > 1500:
            self._table.removeRow(0)
        self._table.scrollToBottom()

    def _clear(self) -> None:
        self._table.setRowCount(0)
