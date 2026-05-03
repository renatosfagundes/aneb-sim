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

# Hard cap on the live console buffer (characters).  Plotter sketches at
# full UART speed produce ~11 KB/s; without this cap the QPlainTextEdit
# document grows without bound when the data has no newlines, eventually
# making each insertText slow enough to freeze the GUI thread.
#
# Trim policy is watermark-based: grow to MAX + SLACK, then drop SLACK
# from the front in one shot.  Trimming on every append after MAX would
# call removeSelectedText() ~100 times/sec, which is itself O(n) on the
# document and reintroduces the freeze.
UART_VIEW_MAX   = 200_000
UART_VIEW_SLACK = 50_000


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
        if not clean:
            return
        # setMaximumBlockCount only evicts when whole BLOCKS are appended,
        # so a stream without newlines (e.g. continuous Plotter output
        # before the first "\n") will grow the document forever via
        # cursor.insertText.  Hard-cap the document by character count
        # too, trimming from the front when we'd grow past UART_VIEW_MAX.
        cursor = self._view.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        cursor.insertText(clean)

        doc = self._view.document()
        if doc.characterCount() > UART_VIEW_MAX + UART_VIEW_SLACK:
            trim = doc.characterCount() - UART_VIEW_MAX
            cursor.movePosition(cursor.MoveOperation.Start)
            cursor.movePosition(cursor.MoveOperation.NextCharacter,
                                cursor.MoveMode.KeepAnchor, trim)
            cursor.removeSelectedText()
            cursor.movePosition(cursor.MoveOperation.End)

        self._view.setTextCursor(cursor)
        self._view.ensureCursorVisible()

    def _do_send(self) -> None:
        text = self._input.text()
        if not text:
            return
        # Append a newline by default — most firmware reads line-buffered.
        self.text_to_send.emit(text + "\n")
        self._input.clear()
