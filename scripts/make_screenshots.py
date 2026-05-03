"""Generate documentation screenshots for aneb-sim.

Boots the QML UI offscreen (WA_DontShowOnScreen), injects synthetic
state into the bridge / state store, and saves PNGs into
docs/manual/images/. The simulator engine subprocess is replaced by a
no-op stub so we don't depend on real chip activity — every visible
value comes from a pose function that pokes the same SimState slots
the real engine would feed.

Run:
    .venv\\Scripts\\python scripts\\make_screenshots.py

Output: docs/manual/images/*.png

The script is deterministic: same inputs, same pixels.  Re-run
whenever the UI changes and commit the refreshed PNGs alongside it.

Mirrors the pattern in remote_flasher's scripts/make_screenshots.py.
The big difference is that aneb-sim's UI hosts QML inside a
QQuickWidget; QWidget.grab() captures the QQuickWidget framebuffer as
long as the window has actually been shown (hence WA_DontShowOnScreen
rather than skipping show() entirely).
"""

from __future__ import annotations

import math
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Force the bundled Basic style — the headless PyQt6 wheel doesn't ship
# qtquickcontrols2windowsstyleimplplugin.dll, and without an override
# Qt fails to load any QtQuick.Controls component (TextArea, ComboBox,
# Menu) and the QML scene refuses to load at all.  Must be set BEFORE
# the QGuiApplication is constructed.
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")

# Stub engine path BEFORE the Python imports — SimProxy spawns whatever
# we hand it, so we point at a 2-line script that exits immediately
# instead of the real C engine.  The bridge then has nothing to push
# at our SimState; we feed it ourselves.
_TMP = Path(tempfile.mkdtemp(prefix="aneb_shots_"))
_STUB = _TMP / "stub_engine.bat"
_STUB.write_text("@echo off\r\nrem aneb-sim screenshot stub\r\nexit /b 0\r\n",
                 encoding="ascii")

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "aneb-ui"))

from PyQt6.QtCore import Qt, QObject, QTimer  # noqa: E402
from PyQt6.QtGui import QGuiApplication        # noqa: E402
from PyQt6.QtWidgets import QApplication        # noqa: E402

from aneb_ui.app_qml import QmlMainWindow      # noqa: E402

OUT = ROOT / "docs" / "manual" / "images"
OUT.mkdir(parents=True, exist_ok=True)

CHIPS = ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")


# ─────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────

def _pump(app: QApplication, n: int = 3) -> None:
    """Let Qt layout / style / paint events settle before grabbing."""
    for _ in range(n):
        app.processEvents()


def grab(widget, out_path: Path) -> None:
    pix = widget.grab()
    if pix.isNull():
        print(f"  WARN: empty grab for {out_path.name}", flush=True)
        return
    pix.save(str(out_path))
    print(f"  saved {out_path.name}  {pix.width()}x{pix.height()}", flush=True)


def _find_qml_objects_with_property(root: QObject, prop: str, value):
    """Walk the QObject tree under `root` and return every child whose
    property `prop` matches `value`.  QQuickItem inherits QObject, so
    this works for QML object trees too."""
    out = []
    for child in root.findChildren(QObject):
        try:
            v = child.property(prop)
        except Exception:
            continue
        if v == value:
            out.append(child)
    return out


def _find_qml_by_class(root, prefix: str, chip: str | None = None):
    """Walk root.findChildren and return the first QObject whose class
    name starts with `prefix`, optionally also matching a `chip`
    property.  Returns None if not found."""
    if root is None:
        return None
    for child in root.findChildren(QObject):
        try:
            cn = child.metaObject().className()
        except Exception:
            continue
        if not cn.startswith(prefix):
            continue
        if chip is not None:
            try:
                if child.property("chip") != chip:
                    continue
            except Exception:
                continue
        return child
    return None


def _qml_scene_rect(item):
    """Return the (x, y, w, h) of a QQuickItem in QQuickWindow scene
    coords, or None if the item isn't laid out yet.

    We read geometry through the QML `width`/`height` properties (which
    every QQuickItem exposes) and walk parents via `parentItem()` to
    accumulate the absolute scene position — that's more robust than
    boundingRect()/mapToScene() across the offscreen render boundary,
    where some intermediate Item types report a zero bounding rect."""
    try:
        wpx = float(item.property("width")  or 0)
        hpx = float(item.property("height") or 0)
        if wpx <= 0 or hpx <= 0:
            return None
        x = 0.0
        y = 0.0
        cur = item
        while cur is not None:
            try:
                cx = float(cur.property("x") or 0)
                cy = float(cur.property("y") or 0)
            except Exception:
                cx = cy = 0.0
            x += cx
            y += cy
            try:
                cur = cur.parentItem()    # QQuickItem-only; QObject lacks it
            except Exception:
                cur = None
        return (x, y, wpx, hpx)
    except Exception:
        return None


def _crop_qml_widgets(w, source_label: str) -> None:
    """Grab the main window once, then for each (file, qml_class[, chip])
    target crop the corresponding QQuickItem out of the pixmap and save
    it.  HiDPI scaling on Windows multiplies the captured pixmap by
    the display scale factor, so we compute the scale dynamically from
    the pixmap-vs-widget ratio."""
    from PyQt6.QtCore import QRect
    pix = w.grab()
    if pix.isNull():
        print(f"  WARN: empty grab when cropping from {source_label}",
              flush=True)
        return
    view = w._view
    scale_x = pix.width()  / max(1, view.width())
    scale_y = pix.height() / max(1, view.height())
    root = view.rootObject()

    targets = [
        ("can_monitor.png",   "CanMonitor",    None),
        ("can_inject.png",    "CanInject",     None),
        ("ecu1_panel.png",    "EcuPanel",      "ecu1"),
        ("mcu_panel.png",     "Mcu",           None),
        ("ecu1_lcd.png",      "LcdWidget",     "ecu1"),
    ]
    for filename, cls, chip in targets:
        item = _find_qml_by_class(root, cls, chip)
        if item is None:
            print(f"  could not locate {cls}"
                  + (f" for {chip}" if chip else ""), flush=True)
            continue
        sr = _qml_scene_rect(item)
        if sr is None or sr[2] <= 0 or sr[3] <= 0:
            print(f"  no layout for {cls}"
                  + (f" for {chip}" if chip else ""), flush=True)
            continue
        x  = int(sr[0] * scale_x)
        y  = int(sr[1] * scale_y)
        cw = int(sr[2] * scale_x)
        ch = int(sr[3] * scale_y)
        # Clip to the pixmap bounds — items can extend past the
        # visible area when a layout hasn't fully settled.
        if x < 0: cw += x; x = 0
        if y < 0: ch += y; y = 0
        if x + cw > pix.width():  cw = pix.width()  - x
        if y + ch > pix.height(): ch = pix.height() - y
        if cw <= 0 or ch <= 0:
            print(f"  zero-area crop for {cls}", flush=True)
            continue
        crop = pix.copy(QRect(x, y, cw, ch))
        if crop.isNull():
            print(f"  crop returned null for {cls}", flush=True)
            continue
        crop.save(str(OUT / filename))
        print(f"  saved {filename}  {crop.width()}x{crop.height()}",
              flush=True)


def _toggle_window(view, chip: str, kind: str, visible: bool):
    """Find the QQuickWindow descendant of the EcuPanel for `chip` whose
    class name matches `kind` (one of "console" / "plotter" / "avrdude")
    and show/hide it via the underlying QWindow API (not QML's
    `visible` property — under WA_DontShowOnScreen the QML binding
    doesn't always trigger the real show/expose events that the layout
    engine waits on).  Returns the QQuickWindow on success."""
    root = view.rootObject()
    if root is None:
        return None
    needle = {
        "console": "SerialConsoleWindow",
        "plotter": "PlotterWindow",
        "avrdude": "AvrdudeWindow",
    }[kind]
    for child in root.findChildren(QObject):
        try:
            cn = child.metaObject().className()
        except Exception:
            continue
        if not cn.startswith(needle):
            continue
        try:
            if child.property("chip") != chip:
                continue
        except Exception:
            continue
        # QQuickWindow inherits QWindow — show() / hide() trigger the
        # real expose+resize events the QML scene needs.
        if visible:
            try:
                child.show()
            except Exception:
                child.setProperty("visible", True)
        else:
            try:
                child.hide()
            except Exception:
                child.setProperty("visible", False)
        return child
    return None


# ─────────────────────────────────────────────────────────────────────
# Pose functions — each writes one bit of state into the SimState.
# The bridge mirrors SimState changes as QML signals, so panels redraw.
# ─────────────────────────────────────────────────────────────────────

def pose_default_chip_stats(w: QmlMainWindow) -> None:
    """Sidebar (Hex / Free RAM / COM) values for every chip."""
    pose = {
        "ecu1": ("dashboard_full.ino.hex",
                 r"C:/Users/renato/Codigos/Pos/RTOS/examples/13_dashboard_full/build/dashboard_full.ino.hex",
                 1832, 2168),
        "ecu2": ("Optiboot (built-in)", "",       2042, 2298),
        "ecu3": ("Optiboot (built-in)", "",       2042, 2298),
        "ecu4": ("Optiboot (built-in)", "",       2042, 2298),
        "mcu":  ("MCU_Firmware_v08.hex",
                 r"C:/dev/aneb-sim/firmware/mcu/MCU_Firmware_v08.hex",
                 1620, 1956),
    }
    for chip, (name, path, free, sp) in pose.items():
        w._state.update_chip_stat({
            "chip": chip,
            "hex_name": name,
            "hex_path": path,
            "free_ram": free,
            "ram_size": 2048,
            "sp":       sp,
        })


def pose_running_pins(w: QmlMainWindow) -> None:
    """Light up a few LEDs / pin states so the board doesn't look dead."""
    state = w._state
    # ECU1: DOUT0 on (PD3), L on (PB5), turn-left arrow blinking high
    for pin, val in [("PB5", 1), ("PD3", 1), ("PD7", 1)]:
        state.update_pin({"chip": "ecu1", "pin": pin, "val": val})
    # ECU2: a different combo
    for pin, val in [("PB5", 0), ("PD3", 1)]:
        state.update_pin({"chip": "ecu2", "pin": pin, "val": val})
    # ECU3 / ECU4: idle (no extra pins set)


def pose_pwm(w: QmlMainWindow) -> None:
    """Plausible PWM duty-cycles for the dimmable LEDs."""
    state = w._state
    state.update_pwm({"chip": "ecu1", "pin": "PD3", "duty": 0.65})
    state.update_pwm({"chip": "ecu1", "pin": "PD6", "duty": 0.40})
    state.update_pwm({"chip": "ecu2", "pin": "PD3", "duty": 0.20})


def pose_can_states(w: QmlMainWindow) -> None:
    """One ECU error-passive, the rest active — exercises the pill colors."""
    state = w._state
    for chip in ("ecu1", "ecu3", "ecu4"):
        state.update_can_state({"chip": chip, "tec": 0, "rec": 0,
                                "state": "active"})
    state.update_can_state({"chip": "ecu2", "tec": 142, "rec": 38,
                            "state": "passive"})


def pose_lcd(w: QmlMainWindow) -> None:
    """Fill the 16x2 LCDs with sample lines."""
    state = w._state
    state.update_lcd({"chip": "ecu1", "line0": "SPEED  72 km/h",
                                       "line1": "RPM    3870   "})
    state.update_lcd({"chip": "mcu",  "line0": "MODE: ELECTRIC ",
                                       "line1": "BAT 68%  ECO ON"})


def pose_serial_uart(w: QmlMainWindow) -> None:
    """Push sample UART output so the per-chip Console window has text."""
    state = w._state
    sample = "\n".join([
        "[boot] dashboard_full v1.4",
        "[ecu] ADC0=812 ADC1=540 ADC2=210 ADC3=703",
        "lights:1,seatbeltUnbuckled:0,turnLeft:1,turnRight:0,cruiseActive:1,serviceDue:0,tirePressureLow:0",
        "lights:1,seatbeltUnbuckled:0,turnLeft:1,turnRight:0,cruiseActive:1,serviceDue:0,tirePressureLow:0",
        "[can] tx id=0x123 dlc=2 data=CAFE",
        "lights:1,seatbeltUnbuckled:0,turnLeft:0,turnRight:1,cruiseActive:1,serviceDue:0,tirePressureLow:0",
        "[adc] hi=703 lo=210",
        "lights:1,seatbeltUnbuckled:1,turnLeft:0,turnRight:0,cruiseActive:0,serviceDue:0,tirePressureLow:0",
        "",
    ])
    state.update_uart({"chip": "ecu1", "data": sample})


def pose_adc(w: QmlMainWindow) -> None:
    """Spin the four trim-pots on each ECU to non-zero positions so the
    Plotter has real ADC values and the trim-pot illustrations on the
    panels rotate to a non-default angle."""
    state = w._state
    presets = {
        "ecu1": [820, 540, 210, 703],
        "ecu2": [180, 760, 410, 880],
        "ecu3": [512, 320, 600, 240],
        "ecu4": [430, 870, 150, 660],
    }
    for chip, vals in presets.items():
        for ch, v in enumerate(vals):
            state.update_adc(chip, ch, v)


def pose_can_traffic(w: QmlMainWindow) -> None:
    """Push a handful of CAN frames into the global log so the bottom-
    right CAN monitor panel has scrolled traffic to display."""
    state = w._state
    sample = [
        ("ecu1", "0x101", 8, "01 02 03 04 05 06 07 08"),
        ("ecu2", "0x102", 4, "DE AD BE EF"),
        ("ecu1", "0x100", 2, "CA FE"),
        ("ecu3", "0x103", 8, "12 34 56 78 9A BC DE F0"),
        ("ecu4", "0x104", 3, "AA BB CC"),
        ("ecu1", "0x101", 8, "11 22 33 44 55 66 77 88"),
        ("ecu2", "0x123", 1, "42"),
        ("ecu3", "0x103", 8, "00 11 22 33 44 55 66 77"),
        ("ecu4", "0x200", 6, "01 02 03 04 05 06"),
        ("ecu1", "0x100", 2, "BA BE"),
    ]
    for src, fid, dlc, data in sample:
        state.update_can_tx({
            "bus": "can1", "src": src,
            "id": fid, "ext": False, "rtr": False,
            "dlc": dlc, "data": data.replace(" ", ""),
            "ts": 0,
        })


def pose_din_pressed(w: QmlMainWindow) -> None:
    """Hold DIN1 (PC4) and DIN3 (PB1) pressed on ECU1 so the colored
    push-buttons render with their lit-up appearance."""
    state = w._state
    state.update_pin({"chip": "ecu1", "pin": "PC4", "val": 0})  # pressed=LOW
    state.update_pin({"chip": "ecu1", "pin": "PB1", "val": 0})


def pose_avrdude_log(w: QmlMainWindow, chip: str = "ecu1") -> None:
    """Drive the avrdude pane via the bridge's avrdudeOutput +
    avrdudeStateChanged signals — that's how the real flash flow
    populates the log, so the rendering is identical."""
    bridge = w._bridge
    bridge.avrdudeStateChanged.emit(chip, True)
    for line in [
        "Avrdude.EXE version 8.1",
        "Using port            : net:127.0.0.1:8600",
        "Using programmer      : arduino",
        "Setting baud rate     : 115200",
        "AVR device initialized and ready to accept instructions",
        "Device signature = 1E 95 0F (ATmega328P, ATA6614Q, LGT8F328P)",
        "Reading 13104 bytes for flash from input file dashboard_full.hex",
        "Writing 13104 bytes to flash",
        "Writing | ################################################## | 100% 1.65s",
        "Reading | ################################################## | 100% 1.50s",
        "13104 bytes of flash verified",
        "Avrdude.EXE done.  Thank you.",
    ]:
        bridge.avrdudeOutput.emit(chip, line)
    bridge.avrdudeStateChanged.emit(chip, False)


# ─────────────────────────────────────────────────────────────────────
# Main shoot
# ─────────────────────────────────────────────────────────────────────

def main() -> int:
    # Hi-DPI off so every PNG has the same pixel dimensions.
    if hasattr(Qt, "AA_DisableHighDpiScaling"):
        QApplication.setAttribute(Qt.ApplicationAttribute.AA_DisableHighDpiScaling)
    app = QApplication(sys.argv)
    app.setApplicationName("aneb-sim screenshot tool")

    # Construct MainWindow against the stub engine.  SimProxy will
    # launch the .bat which exits immediately; we don't get any real
    # events from it.  The bridge / state objects exist normally and
    # we feed them by hand.
    w = QmlMainWindow(_STUB)
    w.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    w.resize(1500, 850)
    w.show()
    _pump(app, 10)

    # First wave of state — static stuff that every shot needs.
    pose_default_chip_stats(w)
    pose_running_pins(w)
    pose_pwm(w)
    pose_can_states(w)
    pose_lcd(w)
    _pump(app, 6)

    # 1) Full board view (relatively quiet — chips up, LCD populated,
    # but no live CAN traffic / button activity).
    grab(w, OUT / "board_main.png")

    # 2) "Active" board — same window, but with ADC spinners turned,
    # buttons held down, and CAN traffic in the right-side monitor.
    # This is the shot the manual uses to show what a running session
    # looks like with everything exercised.
    pose_adc(w)
    pose_din_pressed(w)
    pose_can_traffic(w)
    _pump(app, 6)
    grab(w, OUT / "board_active.png")

    # Crop a few useful close-ups out of board_active so the manual
    # has readable per-feature shots even in a small print column.
    # We find each target QQuickItem in the QML tree and use its
    # mapToScene geometry; that auto-adjusts to whatever HiDPI scale
    # Windows applies to the captured pixmap.
    try:
        _crop_qml_widgets(w, "board_active.png")
    except Exception as e:
        import traceback
        print(f"  crop helper failed: {e}", flush=True)
        traceback.print_exc()

    # DEBUG: dump the QML object tree so we can find the EcuPanels.
    root = w._view.rootObject()
    print(f"  root: {root}", flush=True)
    if root is not None:
        kids = root.findChildren(QObject)
        print(f"  total descendants: {len(kids)}", flush=True)
        # Show items that have a `chip` property — those are our EcuPanels.
        chips_seen = []
        for k in kids:
            try:
                v = k.property("chip")
            except Exception:
                continue
            if v not in (None, "", "ecu0"):
                chips_seen.append((k.metaObject().className(), v))
        print(f"  items with `chip` prop: {chips_seen[:20]}", flush=True)

    # Sizes for each pane window — the popup Windows in QML default to
    # whatever the host platform decides if we don't pin them, which on
    # Windows ends up much smaller than the design and clips layouts.
    WIN_SIZE = {
        "console": (640, 380),
        "plotter": (800, 460),
        "avrdude": (680, 400),
    }

    def _grab_subwindow(name: str, kind: str, settle: int = 8,
                        before_grab=None):
        sub = _toggle_window(w._view, "ecu1", kind, True)
        if sub is None:
            print(f"  could not locate {kind} window for ecu1 — check the "
                  f"QML object tree", flush=True)
            return
        # Force a sane size BEFORE pumping so the QML scene lays itself
        # out at the right dimensions.  The popup Window otherwise picks
        # something near the implicit default and PaneHeader/inputs can
        # overlap.  We use QWindow.resize() (QQuickWindow inherits
        # QWindow) — setProperty("width", ...) on the QML side doesn't
        # propagate cleanly when the Window has explicit width/height
        # bindings declared in QML.
        ww, hh = WIN_SIZE[kind]
        try:
            sub.resize(ww, hh)
        except Exception:
            sub.setProperty("width",  ww)
            sub.setProperty("height", hh)
        _pump(app, settle)
        if before_grab:
            before_grab()
            _pump(app, settle)
        try:
            img = sub.grabWindow()
        except Exception as e:
            print(f"  {name} grabWindow exception: {e}", flush=True)
            img = None
        if img is None or img.isNull():
            print(f"  {name} grabWindow returned empty", flush=True)
        else:
            img.save(str(OUT / f"{name}.png"))
            print(f"  saved {name}.png  {img.width()}x{img.height()}",
                  flush=True)
        _toggle_window(w._view, "ecu1", kind, False)
        _pump(app, 4)

    # 2) Console pane — open the window FIRST, pump for layout, THEN
    # inject UART so the text-area's onUartAppended slot runs against
    # an already-laid-out scene.
    _grab_subwindow("ecu1_console", "console", settle=12,
                    before_grab=lambda: pose_serial_uart(w))

    # 3) Plotter pane — QtCharts needs a few extra paints before it's
    # ready, hence the larger settle.
    _grab_subwindow("ecu1_plotter", "plotter", settle=20)

    # 4) Avrdude pane — open first, then push log lines, then grab.
    _grab_subwindow("ecu1_avrdude", "avrdude", settle=10,
                    before_grab=lambda: pose_avrdude_log(w, "ecu1"))

    pngs = sorted(OUT.glob("*.png"))
    print(f"\nDone. {len(pngs)} PNGs in {OUT.relative_to(ROOT)}")
    for p in pngs:
        print(f"  {p.name}")

    # Hard exit so QtCharts' teardown can't take us out (same workaround
    # we use in scripts/screenshots/snap.py).
    os._exit(0)


if __name__ == "__main__":
    main()
