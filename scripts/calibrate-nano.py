"""
calibrate-nano.py — interactive tool to mark LED + pad positions on
the Arduino Nano image.

Run:
    .venv/Scripts/python.exe scripts/calibrate-nano.py

UI:
    - Left  : the arduino.png. Click anywhere to place the current
              marker. Already-placed markers are shown as colored dots.
    - Right : item list. Click an item to make it active (its dot
              moves on the next click). A check appears next to
              placed items.
    - Bottom: Load (re-read JSON), Save, Reset.

Output:
    aneb-ui/aneb_ui/qml_assets/arduino-coords.json — every coordinate
    is normalised to image dimensions (0..1) so the QML overlay
    positions correctly at any rendered size.

Why a dedicated tool: the Nano image's exact pixel layout varies per
asset; calibrating once by clicking is faster and more accurate than
hand-tuning constants in QML.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

from PyQt6.QtCore    import QPointF, Qt
from PyQt6.QtGui     import (QBrush, QColor, QImage, QPainter, QPen, QPixmap)
from PyQt6.QtWidgets import (
    QApplication, QHBoxLayout, QLabel, QListWidget, QListWidgetItem,
    QMainWindow, QMessageBox, QPushButton, QVBoxLayout, QWidget,
)


REPO        = Path(__file__).resolve().parent.parent
IMAGE_PATH  = REPO / "aneb-ui" / "aneb_ui" / "qml_assets" / "arduino.png"
COORDS_PATH = REPO / "aneb-ui" / "aneb_ui" / "qml_assets" / "arduino-coords.json"


# Items to calibrate, in click-order. Naming matches the wire-protocol
# pin mapping the bridge already uses, so the QML overlay can look up
# `coords["leds"]["tx"]`, `coords["pads"]["top"]["d12"]`, etc.
ITEMS: list[tuple[str, str, str]] = []
ITEMS += [(f"leds.{n}", "leds", n) for n in ("tx", "rx", "l", "pwr")]
TOP_PINS = ["d12","d11","d10","d9","d8","d7","d6","d5","d4","d3","d2",
            "gnd","rst","rx0","tx1"]
BOT_PINS = ["d13","3v3","aref","a0","a1","a2","a3","a4","a5","a6","a7",
            "5v","rst","gnd","vin"]
ITEMS += [(f"pads.top.{p}", "pads.top", p) for p in TOP_PINS]
ITEMS += [(f"pads.bot.{p}", "pads.bot", p) for p in BOT_PINS]


COLOR_BY_GROUP = {
    "leds":     QColor("#ffd24a"),
    "pads.top": QColor("#22cc44"),
    "pads.bot": QColor("#3aaaff"),
}


class ImageCanvas(QLabel):
    """Image display + click-to-place. Emits coordinates via owner."""

    def __init__(self, owner: "CalibrateWindow") -> None:
        super().__init__()
        self.owner   = owner
        self.image   = QImage(str(IMAGE_PATH))
        if self.image.isNull():
            QMessageBox.critical(None, "calibrate-nano",
                                 f"Cannot open image: {IMAGE_PATH}")
            sys.exit(2)
        self.setMinimumSize(self.image.size())
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMouseTracking(True)

    def paintEvent(self, _evt) -> None:    # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        # Draw the image at native size, top-left aligned.
        p.drawImage(0, 0, self.image)

        # Draw markers for every placed coordinate.
        coords = self.owner.coords
        for key, group, name in ITEMS:
            entry = self._lookup(coords, key)
            if entry is None:
                continue
            x = entry["x"] * self.image.width()
            y = entry["y"] * self.image.height()
            color = COLOR_BY_GROUP[group]
            self._draw_marker(p, x, y, color, key == self.owner.current_key())

        p.end()

    def _draw_marker(self, p: QPainter, x: float, y: float,
                     color: QColor, active: bool) -> None:
        if active:
            # Larger ring for the current item.
            p.setPen(QPen(color.lighter(150), 2))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawEllipse(QPointF(x, y), 9, 9)
        p.setPen(QPen(QColor("#202020"), 1))
        p.setBrush(QBrush(color))
        p.drawEllipse(QPointF(x, y), 4, 4)

    @staticmethod
    def _lookup(coords: dict, dotted_key: str):
        node = coords
        for k in dotted_key.split("."):
            if not isinstance(node, dict) or k not in node:
                return None
            node = node[k]
        return node

    def mousePressEvent(self, evt) -> None:    # noqa: N802
        if evt.button() != Qt.MouseButton.LeftButton:
            return
        x = evt.position().x()
        y = evt.position().y()
        if x < 0 or y < 0 or x >= self.image.width() or y >= self.image.height():
            return
        self.owner.set_current_position(x, y)


class CalibrateWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(f"Calibrate Nano coordinates  —  {IMAGE_PATH.name}")

        # State.
        self.coords: dict = {}
        self.current_index: int = 0
        self._load_existing()

        # Layout: image (left) | list (right).
        self.canvas = ImageCanvas(self)

        self.list = QListWidget()
        for key, _group, _name in ITEMS:
            self.list.addItem(QListWidgetItem(key))
        self.list.itemClicked.connect(self._on_list_clicked)
        self._refresh_list()

        save_btn  = QPushButton("Save");  save_btn.clicked.connect(self.save)
        reset_btn = QPushButton("Reset"); reset_btn.clicked.connect(self.reset)
        load_btn  = QPushButton("Reload from disk"); load_btn.clicked.connect(self._reload)

        right = QVBoxLayout()
        hint = QLabel("Click a row to make it active.\n"
                      "Click on the image to place the marker.\n"
                      "After placing, the next item activates automatically.")
        hint.setStyleSheet("QLabel { color: #555; }")
        right.addWidget(hint)
        right.addWidget(self.list, 1)
        right.addWidget(load_btn)
        right.addWidget(save_btn)
        right.addWidget(reset_btn)

        layout = QHBoxLayout()
        layout.addWidget(self.canvas, 0)
        right_w = QWidget(); right_w.setLayout(right)
        right_w.setMinimumWidth(220)
        layout.addWidget(right_w, 1)

        central = QWidget(); central.setLayout(layout)
        self.setCentralWidget(central)

        # Match the image aspect.
        self.resize(self.canvas.image.width() + 240,
                    max(420, self.canvas.image.height() + 40))

    # ------ state helpers ----------------------------------------------

    def current_key(self) -> str | None:
        if self.current_index < 0 or self.current_index >= len(ITEMS):
            return None
        return ITEMS[self.current_index][0]

    def set_current_position(self, x: float, y: float) -> None:
        key = self.current_key()
        if key is None:
            return
        # Insert into nested dict.
        node = self.coords
        parts = key.split(".")
        for k in parts[:-1]:
            node = node.setdefault(k, {})
        node[parts[-1]] = {
            "x": round(x / self.canvas.image.width(),  4),
            "y": round(y / self.canvas.image.height(), 4),
        }
        # Advance to the next unplaced item.
        self._advance()
        self._refresh_list()
        self.canvas.update()

    def _advance(self) -> None:
        for i in range(self.current_index + 1, len(ITEMS)):
            key = ITEMS[i][0]
            if ImageCanvas._lookup(self.coords, key) is None:
                self.current_index = i
                self.list.setCurrentRow(i)
                return
        # Wrap around — go back to first unplaced.
        for i in range(len(ITEMS)):
            key = ITEMS[i][0]
            if ImageCanvas._lookup(self.coords, key) is None:
                self.current_index = i
                self.list.setCurrentRow(i)
                return
        # Everything placed.
        self.current_index = len(ITEMS)

    def _refresh_list(self) -> None:
        for i, (key, _group, _name) in enumerate(ITEMS):
            item = self.list.item(i)
            placed = ImageCanvas._lookup(self.coords, key) is not None
            prefix = "[x] " if placed else "[ ] "
            item.setText(prefix + key)
        self.list.setCurrentRow(self.current_index)

    # ------ buttons ----------------------------------------------------

    def _on_list_clicked(self, item: QListWidgetItem) -> None:
        self.current_index = self.list.row(item)
        self.canvas.update()

    def save(self) -> None:
        payload = {
            "image": IMAGE_PATH.name,
            "image_size": [self.canvas.image.width(), self.canvas.image.height()],
            "coords": self.coords,
        }
        COORDS_PATH.parent.mkdir(parents=True, exist_ok=True)
        COORDS_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        placed = sum(
            1 for key, _g, _n in ITEMS
            if ImageCanvas._lookup(self.coords, key) is not None
        )
        QMessageBox.information(
            self, "Saved",
            f"Saved {placed} / {len(ITEMS)} coordinates to:\n{COORDS_PATH}"
        )

    def reset(self) -> None:
        if QMessageBox.question(self, "Reset",
                                "Clear all placed coordinates?") \
                != QMessageBox.StandardButton.Yes:
            return
        self.coords = {}
        self.current_index = 0
        self._refresh_list()
        self.canvas.update()

    def _reload(self) -> None:
        self._load_existing()
        self._refresh_list()
        self.canvas.update()

    def _load_existing(self) -> None:
        if not COORDS_PATH.exists():
            return
        try:
            data = json.loads(COORDS_PATH.read_text(encoding="utf-8"))
            self.coords = data.get("coords") or {}
        except Exception:
            self.coords = {}
        self._advance()


def main() -> int:
    app = QApplication(sys.argv)
    win = CalibrateWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
