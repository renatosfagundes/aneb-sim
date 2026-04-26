"""
calibrate-nano.py — interactive tool to mark LED + pad positions,
sizes, and rotations on the Arduino Nano image.

Run:
    .venv/Scripts/python.exe scripts/calibrate-nano.py

UI:
    Left   : the arduino.png, scaled to fit the window. Click on the
             image to place the currently-selected marker at that
             position. Already-placed markers render in their actual
             rectangle/circle shape (rotated for LEDs) so you can see
             what they will look like in the running sim.
    Right  : list of items, plus a property editor for the selected
             item (position, size, rotation in normalised 0..1 image
             coords). Edit any field to nudge the marker.

Keyboard shortcuts (focus the canvas first by clicking on it):
    Arrow keys      nudge position by 1 pixel
    Shift+arrows    nudge by 5 pixels
    + / -           scale up / down
    [ / ]           rotate (LEDs only) by 5 degrees
    Tab / Shift+Tab next / previous item

Output JSON (`aneb-ui/aneb_ui/qml_assets/arduino-coords.json`):

    {
      "image":      "arduino.png",
      "image_size": [W, H],
      "coords": {
        "leds":  { "tx":  {"x", "y", "w", "h", "rot"}, ... },
        "pads": {
          "top": { "d12": {"x", "y", "r"}, ... },
          "bot": { "d13": {"x", "y", "r"}, ... }
        }
      }
    }

All x/y/w/h/r values are normalised 0..1 against the image's native
pixel dimensions; the QML overlay multiplies them back at runtime so
the layout stays correct at any rendered size.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

from PyQt6.QtCore    import QPointF, QRectF, QSize, Qt
from PyQt6.QtGui     import (QBrush, QColor, QImage, QPainter, QPen,
                             QPolygonF, QTransform)
from PyQt6.QtWidgets import (
    QApplication, QDoubleSpinBox, QFormLayout, QHBoxLayout, QLabel,
    QListWidget, QListWidgetItem, QMainWindow, QMessageBox, QPushButton,
    QSizePolicy, QVBoxLayout, QWidget,
)


REPO        = Path(__file__).resolve().parent.parent
IMAGE_PATH  = REPO / "aneb-ui" / "aneb_ui" / "qml_assets" / "arduino.png"
COORDS_PATH = REPO / "aneb-ui" / "aneb_ui" / "qml_assets" / "arduino-coords.json"

# Maximum displayed dimensions — large source images get downsampled
# for the UI but click coords are recorded against the native pixel
# grid (then normalised 0..1 in the JSON).
MAX_DISPLAY_W = 1400
MAX_DISPLAY_H = 760

# Default sizes for first-time placement. Tunable per-item afterwards.
LED_DEFAULT_W   = 0.014    # ~12 px on a 1500-wide image
LED_DEFAULT_H   = 0.018    # taller than wide; user can flip via rotation
LED_DEFAULT_ROT = 0.0
PAD_DEFAULT_R   = 0.006    # ~9 px


# Items, in click-order. Each entry: (dotted_key, type) where type is
# "led" or "pad".
def _build_items() -> list[tuple[str, str]]:
    items = [(f"leds.{n}", "led") for n in ("tx", "rx", "l", "pwr")]
    top = ["d12","d11","d10","d9","d8","d7","d6","d5","d4","d3","d2",
           "gnd","rst","rx0","tx1"]
    bot = ["d13","3v3","aref","a0","a1","a2","a3","a4","a5","a6","a7",
           "5v","rst","gnd","vin"]
    items += [(f"pads.top.{p}", "pad") for p in top]
    items += [(f"pads.bot.{p}", "pad") for p in bot]
    return items

ITEMS: list[tuple[str, str]] = _build_items()


COLOR_BY_TYPE_AND_GROUP = {
    ("led", "leds"):     QColor("#ffd24a"),
    ("pad", "pads.top"): QColor("#22cc44"),
    ("pad", "pads.bot"): QColor("#3aaaff"),
}


def _lookup(coords: dict, dotted_key: str):
    node = coords
    for k in dotted_key.split("."):
        if not isinstance(node, dict) or k not in node:
            return None
        node = node[k]
    return node


def _set(coords: dict, dotted_key: str, value: dict) -> None:
    node = coords
    parts = dotted_key.split(".")
    for k in parts[:-1]:
        node = node.setdefault(k, {})
    node[parts[-1]] = value


def _group_of(dotted_key: str) -> str:
    """e.g. 'leds.tx' -> 'leds', 'pads.top.d12' -> 'pads.top'."""
    parts = dotted_key.split(".")
    return ".".join(parts[:-1])


# ---------------------------------------------------------------- canvas

class ImageCanvas(QLabel):
    """Image display + click-to-place + keyboard shortcuts."""

    def __init__(self, owner: "CalibrateWindow") -> None:
        super().__init__()
        self.owner = owner
        self.image = QImage(str(IMAGE_PATH))
        if self.image.isNull():
            QMessageBox.critical(None, "calibrate-nano",
                                 f"Cannot open image: {IMAGE_PATH}")
            sys.exit(2)

        sw = MAX_DISPLAY_W / self.image.width()
        sh = MAX_DISPLAY_H / self.image.height()
        self.disp_scale = min(1.0, sw, sh)
        self.disp_w = int(round(self.image.width()  * self.disp_scale))
        self.disp_h = int(round(self.image.height() * self.disp_scale))

        self.setFixedSize(QSize(self.disp_w, self.disp_h))
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMouseTracking(True)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    # --- drawing ---------------------------------------------------

    def paintEvent(self, _evt) -> None:    # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        p.drawImage(self.rect(), self.image)

        coords = self.owner.coords
        active_key = self.owner.current_key()

        for key, kind in ITEMS:
            entry = _lookup(coords, key)
            if entry is None:
                continue
            color = COLOR_BY_TYPE_AND_GROUP.get((kind, _group_of(key)),
                                                QColor("#cccccc"))
            self._draw_marker(p, key, kind, entry, color, key == active_key)
        p.end()

    def _draw_marker(self, p: QPainter, key: str, kind: str,
                     entry: dict, color: QColor, active: bool) -> None:
        cx = entry["x"] * self.disp_w
        cy = entry["y"] * self.disp_h

        if kind == "led":
            w  = entry.get("w",   LED_DEFAULT_W)   * self.disp_w
            h  = entry.get("h",   LED_DEFAULT_H)   * self.disp_h
            rot = entry.get("rot", LED_DEFAULT_ROT)
            p.save()
            p.translate(cx, cy)
            p.rotate(rot)
            rect = QRectF(-w / 2, -h / 2, w, h)
            if active:
                p.setPen(QPen(color.lighter(150), 2))
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.drawRoundedRect(rect.adjusted(-3, -3, 3, 3), 2, 2)
            p.setPen(QPen(QColor("#202020"), 1))
            p.setBrush(QBrush(color))
            p.drawRoundedRect(rect, 1, 1)
            p.restore()
        else:  # pad
            r = entry.get("r", PAD_DEFAULT_R) * self.disp_w
            if active:
                p.setPen(QPen(color.lighter(150), 2))
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.drawEllipse(QPointF(cx, cy), r + 4, r + 4)
            p.setPen(QPen(QColor("#202020"), 1))
            p.setBrush(QBrush(color))
            p.drawEllipse(QPointF(cx, cy), r, r)

    # --- input -----------------------------------------------------

    def mousePressEvent(self, evt) -> None:    # noqa: N802
        if evt.button() != Qt.MouseButton.LeftButton:
            return
        self.setFocus()
        x = evt.position().x()
        y = evt.position().y()
        if x < 0 or y < 0 or x >= self.disp_w or y >= self.disp_h:
            return
        self.owner.place_at(x / self.disp_w, y / self.disp_h)

    def keyPressEvent(self, evt) -> None:    # noqa: N802
        owner = self.owner
        key = owner.current_key()
        if key is None:
            return
        entry = _lookup(owner.coords, key)
        if entry is None:
            return

        step = 5 if (evt.modifiers() & Qt.KeyboardModifier.ShiftModifier) else 1
        # Convert pixel step into normalised-coord step.
        dx_n = step / self.image.width()
        dy_n = step / self.image.height()

        if evt.key() == Qt.Key.Key_Left:
            entry["x"] = round(entry["x"] - dx_n, 5)
        elif evt.key() == Qt.Key.Key_Right:
            entry["x"] = round(entry["x"] + dx_n, 5)
        elif evt.key() == Qt.Key.Key_Up:
            entry["y"] = round(entry["y"] - dy_n, 5)
        elif evt.key() == Qt.Key.Key_Down:
            entry["y"] = round(entry["y"] + dy_n, 5)
        elif evt.key() in (Qt.Key.Key_Plus, Qt.Key.Key_Equal):
            self._scale(entry, 1.1)
        elif evt.key() == Qt.Key.Key_Minus:
            self._scale(entry, 1 / 1.1)
        elif evt.key() == Qt.Key.Key_BracketLeft and "rot" in entry:
            entry["rot"] = round(entry["rot"] - 5, 1)
        elif evt.key() == Qt.Key.Key_BracketRight and "rot" in entry:
            entry["rot"] = round(entry["rot"] + 5, 1)
        elif evt.key() == Qt.Key.Key_Tab:
            owner.cycle_item(+1 if not (evt.modifiers() & Qt.KeyboardModifier.ShiftModifier) else -1)
            return
        else:
            super().keyPressEvent(evt)
            return

        owner.refresh_property_editor()
        self.update()

    @staticmethod
    def _scale(entry: dict, factor: float) -> None:
        if "w" in entry: entry["w"] = round(entry["w"] * factor, 5)
        if "h" in entry: entry["h"] = round(entry["h"] * factor, 5)
        if "r" in entry: entry["r"] = round(entry["r"] * factor, 5)


# ---------------------------------------------------------------- main window

class CalibrateWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(f"Calibrate Nano coordinates  —  {IMAGE_PATH.name}")

        self.coords: dict = {}
        self.current_index: int = 0
        self._load_existing()

        self.canvas = ImageCanvas(self)

        self.list = QListWidget()
        for key, _kind in ITEMS:
            self.list.addItem(QListWidgetItem(key))
        self.list.itemClicked.connect(self._on_list_clicked)
        self._refresh_list()

        # Property editor.
        self._spin_x = self._make_spin(0, 1, 4, 0.001)
        self._spin_y = self._make_spin(0, 1, 4, 0.001)
        self._spin_w = self._make_spin(0, 1, 4, 0.001)
        self._spin_h = self._make_spin(0, 1, 4, 0.001)
        self._spin_r = self._make_spin(0, 0.1, 4, 0.001)
        self._spin_rot = self._make_spin(-180, 180, 1, 1.0)

        for sp in (self._spin_x, self._spin_y, self._spin_w, self._spin_h,
                   self._spin_r, self._spin_rot):
            sp.valueChanged.connect(self._on_property_changed)

        prop_form = QFormLayout()
        prop_form.addRow("X (norm)",   self._spin_x)
        prop_form.addRow("Y (norm)",   self._spin_y)
        prop_form.addRow("W (LED)",    self._spin_w)
        prop_form.addRow("H (LED)",    self._spin_h)
        prop_form.addRow("Rot (deg)",  self._spin_rot)
        prop_form.addRow("R (pad)",    self._spin_r)
        prop_box = QWidget()
        prop_box.setLayout(prop_form)

        save_btn  = QPushButton("Save");  save_btn.clicked.connect(self.save)
        reset_btn = QPushButton("Reset"); reset_btn.clicked.connect(self.reset)
        load_btn  = QPushButton("Reload"); load_btn.clicked.connect(self._reload)

        info = QLabel(
            f"Image: {self.canvas.image.width()} x {self.canvas.image.height()} px\n"
            f"Display: {self.canvas.disp_w} x {self.canvas.disp_h} px "
            f"(scale {self.canvas.disp_scale:.2f})\n"
            f"Click image to place. Click row to select.\n"
            f"Arrows = nudge   +/- = scale   [ ] = rotate   Tab = next item"
        )
        info.setStyleSheet("QLabel { color: #555; }")
        info.setWordWrap(True)

        right = QVBoxLayout()
        right.addWidget(info)
        right.addWidget(self.list, 1)
        right.addWidget(prop_box)
        right.addWidget(load_btn)
        right.addWidget(save_btn)
        right.addWidget(reset_btn)

        layout = QHBoxLayout()
        layout.addWidget(self.canvas, 0)
        right_w = QWidget(); right_w.setLayout(right)
        right_w.setMinimumWidth(280)
        layout.addWidget(right_w, 1)

        central = QWidget(); central.setLayout(layout)
        self.setCentralWidget(central)

        self.resize(self.canvas.disp_w + 320, max(560, self.canvas.disp_h + 60))
        self.refresh_property_editor()

    @staticmethod
    def _make_spin(lo: float, hi: float, decimals: int, step: float) -> QDoubleSpinBox:
        sp = QDoubleSpinBox()
        sp.setRange(lo, hi)
        sp.setDecimals(decimals)
        sp.setSingleStep(step)
        return sp

    # --- state helpers --------------------------------------------

    def current_key(self) -> str | None:
        if self.current_index < 0 or self.current_index >= len(ITEMS):
            return None
        return ITEMS[self.current_index][0]

    def current_kind(self) -> str | None:
        if self.current_index < 0 or self.current_index >= len(ITEMS):
            return None
        return ITEMS[self.current_index][1]

    def place_at(self, xnorm: float, ynorm: float) -> None:
        key = self.current_key()
        kind = self.current_kind()
        if key is None:
            return
        existing = _lookup(self.coords, key) or {}
        if kind == "led":
            entry = {
                "x":   round(xnorm, 5),
                "y":   round(ynorm, 5),
                "w":   existing.get("w",   self._last_led_w()),
                "h":   existing.get("h",   self._last_led_h()),
                "rot": existing.get("rot", self._last_led_rot()),
            }
        else:
            entry = {
                "x": round(xnorm, 5),
                "y": round(ynorm, 5),
                "r": existing.get("r", self._last_pad_r()),
            }
        _set(self.coords, key, entry)
        self._advance()
        self._refresh_list()
        self.refresh_property_editor()
        self.canvas.update()

    def cycle_item(self, direction: int) -> None:
        n = len(ITEMS)
        self.current_index = (self.current_index + direction) % n
        self.list.setCurrentRow(self.current_index)
        self.refresh_property_editor()
        self.canvas.update()

    def _last_led_w(self) -> float:
        for k, kind in ITEMS:
            if kind != "led": continue
            e = _lookup(self.coords, k)
            if e and "w" in e:
                return e["w"]
        return LED_DEFAULT_W

    def _last_led_h(self) -> float:
        for k, kind in ITEMS:
            if kind != "led": continue
            e = _lookup(self.coords, k)
            if e and "h" in e:
                return e["h"]
        return LED_DEFAULT_H

    def _last_led_rot(self) -> float:
        for k, kind in ITEMS:
            if kind != "led": continue
            e = _lookup(self.coords, k)
            if e and "rot" in e:
                return e["rot"]
        return LED_DEFAULT_ROT

    def _last_pad_r(self) -> float:
        for k, kind in ITEMS:
            if kind != "pad": continue
            e = _lookup(self.coords, k)
            if e and "r" in e:
                return e["r"]
        return PAD_DEFAULT_R

    def _advance(self) -> None:
        for i in range(self.current_index + 1, len(ITEMS)):
            if _lookup(self.coords, ITEMS[i][0]) is None:
                self.current_index = i; self.list.setCurrentRow(i); return
        for i in range(len(ITEMS)):
            if _lookup(self.coords, ITEMS[i][0]) is None:
                self.current_index = i; self.list.setCurrentRow(i); return
        self.current_index = len(ITEMS)

    def _refresh_list(self) -> None:
        for i, (key, _kind) in enumerate(ITEMS):
            item = self.list.item(i)
            placed = _lookup(self.coords, key) is not None
            item.setText(("[x] " if placed else "[ ] ") + key)
        self.list.setCurrentRow(self.current_index)

    # --- property editor ------------------------------------------

    def refresh_property_editor(self) -> None:
        key  = self.current_key()
        kind = self.current_kind()
        entry = _lookup(self.coords, key) if key else None

        # Disconnect signals during refresh to avoid feedback loops.
        for sp in (self._spin_x, self._spin_y, self._spin_w, self._spin_h,
                   self._spin_r, self._spin_rot):
            sp.blockSignals(True)

        if entry is None:
            for sp in (self._spin_x, self._spin_y, self._spin_w, self._spin_h,
                       self._spin_r, self._spin_rot):
                sp.setValue(0)
                sp.setEnabled(False)
        else:
            self._spin_x.setEnabled(True); self._spin_x.setValue(entry.get("x", 0))
            self._spin_y.setEnabled(True); self._spin_y.setValue(entry.get("y", 0))
            led = (kind == "led")
            self._spin_w.setEnabled(led);   self._spin_w.setValue(entry.get("w",   LED_DEFAULT_W))
            self._spin_h.setEnabled(led);   self._spin_h.setValue(entry.get("h",   LED_DEFAULT_H))
            self._spin_rot.setEnabled(led); self._spin_rot.setValue(entry.get("rot", LED_DEFAULT_ROT))
            self._spin_r.setEnabled(not led); self._spin_r.setValue(entry.get("r", PAD_DEFAULT_R))

        for sp in (self._spin_x, self._spin_y, self._spin_w, self._spin_h,
                   self._spin_r, self._spin_rot):
            sp.blockSignals(False)

    def _on_property_changed(self, _value: float) -> None:
        key  = self.current_key()
        kind = self.current_kind()
        entry = _lookup(self.coords, key) if key else None
        if entry is None or kind is None:
            return
        entry["x"] = round(self._spin_x.value(), 5)
        entry["y"] = round(self._spin_y.value(), 5)
        if kind == "led":
            entry["w"]   = round(self._spin_w.value(), 5)
            entry["h"]   = round(self._spin_h.value(), 5)
            entry["rot"] = round(self._spin_rot.value(), 1)
        else:
            entry["r"] = round(self._spin_r.value(), 5)
        self.canvas.update()

    # --- buttons --------------------------------------------------

    def _on_list_clicked(self, item: QListWidgetItem) -> None:
        self.current_index = self.list.row(item)
        self.refresh_property_editor()
        self.canvas.update()

    def save(self) -> None:
        payload = {
            "image":      IMAGE_PATH.name,
            "image_size": [self.canvas.image.width(), self.canvas.image.height()],
            "coords":     self.coords,
        }
        COORDS_PATH.parent.mkdir(parents=True, exist_ok=True)
        COORDS_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        placed = sum(
            1 for key, _ in ITEMS if _lookup(self.coords, key) is not None
        )
        QMessageBox.information(
            self, "Saved",
            f"Saved {placed} / {len(ITEMS)} items to:\n{COORDS_PATH}"
        )

    def reset(self) -> None:
        if QMessageBox.question(self, "Reset",
                                "Clear all placed coordinates?") \
                != QMessageBox.StandardButton.Yes:
            return
        self.coords = {}
        self.current_index = 0
        self._refresh_list()
        self.refresh_property_editor()
        self.canvas.update()

    def _reload(self) -> None:
        self._load_existing()
        self._refresh_list()
        self.refresh_property_editor()
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
