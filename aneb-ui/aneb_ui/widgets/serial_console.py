"""
SerialConsole — a read-mostly text view for a chip's UART output, plus
a single-line input that pushes typed characters back via `uart` command.

Performance: appends are O(1) via QPlainTextEdit's appendPlainText path;
ANSI escapes are stripped (we don't render colors).
"""
from __future__ import annotations

import re

from PyQt6.QtCore import pyqtSignal
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import QHBoxLayout, QLineEdit, QPlainTextEdit, QPushButton, QVBoxLayout, QWidget


_ANSI_RE = re.compile(r"\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])")


class SerialConsole(QWidget):
    text_to_send = pyqtSignal(str)   # what the user typed + Enter

    def __init__(self, title: str, parent=None) -> None:
        super().__init__(parent)
        self._view = QPlainTextEdit(self)
        self._view.setReadOnly(True)
        self._view.setMaximumBlockCount(2000)
        f = QFont("Consolas")
        f.setStyleHint(QFont.StyleHint.Monospace)
        self._view.setFont(f)

        self._input = QLineEdit(self)
        self._input.setPlaceholderText(f"send to {title} UART (Enter to send)")
        self._send = QPushButton("Send", self)

        bar = QHBoxLayout()
        bar.setContentsMargins(0, 0, 0, 0)
        bar.addWidget(self._input, 1)
        bar.addWidget(self._send)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._view, 1)
        layout.addLayout(bar)

        self._input.returnPressed.connect(self._do_send)
        self._send.clicked.connect(self._do_send)

    def append(self, text: str) -> None:
        clean = _ANSI_RE.sub("", text)
        # Preserve trailing newline behavior — we don't always get whole lines.
        cursor = self._view.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        cursor.insertText(clean)
        self._view.setTextCursor(cursor)
        self._view.ensureCursorVisible()

    def _do_send(self) -> None:
        text = self._input.text()
        if not text:
            return
        # Append a newline by default — most firmware reads line-buffered.
        self.text_to_send.emit(text + "\n")
        self._input.clear()
