"""
snap.py — render aneb-sim's QML panes off-screen with sample data and
save them as PNGs for the manual.

How it works:
    1. Builds a faux QmlBridge (FauxBridge) that exposes the same
       signals/slots/properties the panes bind to, populated with
       plausible sample data.
    2. For each scene file in scripts/screenshots/scenes/, loads it
       into a QQuickView, sets `bridge` as a context property, waits
       for the root component to be Ready, calls grabToImage() and
       saves the resulting PNG under docs/images/.
    3. No subprocess, no real engine, no live UI.  The render runs
       headless via Qt's threaded scene-graph; on Windows the QML
       items still need a QQuickView to anchor to but it doesn't
       have to be visible (size > 0 is enough).

Add new screenshots by dropping a `*.qml` file under
scripts/screenshots/scenes/ that wraps the target component and
optionally injects sample data via Component.onCompleted.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

# Force the bundled Basic style.  The Windows native style relies on a
# qtquickcontrols2windowsstyleimplplugin.dll that isn't shipped with
# the headless PyQt6 wheel, and without an override Qt fails to load
# any QtQuick.Controls component (TextArea, ComboBox, …).  Setting
# this BEFORE QGuiApplication is constructed picks Basic instead.
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")

from PyQt6.QtCore import QEventLoop, QObject, QTimer, QUrl
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtQml import QQmlComponent
from PyQt6.QtQuick import QQuickItem, QQuickView

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))

from faux_bridge import FauxBridge   # noqa: E402


def _wait_until_ready(view: QQuickView, timeout_ms: int = 5000) -> bool:
    """Block on Qt's event loop until the view's status is Ready (or
    Error).  Required because setSource() is asynchronous when the
    QML imports network / plugin items."""
    if view.status() == QQuickView.Status.Ready:
        return True
    loop = QEventLoop()
    timer = QTimer()
    timer.setSingleShot(True)
    timer.timeout.connect(loop.quit)

    def _on_status(s):
        if s in (QQuickView.Status.Ready, QQuickView.Status.Error):
            loop.quit()

    view.statusChanged.connect(_on_status)
    timer.start(timeout_ms)
    loop.exec()
    return view.status() == QQuickView.Status.Ready


def _grab_async(item: QQuickItem, out: Path,
                size_w: int = 0, size_h: int = 0,
                timeout_ms: int = 5000) -> bool:
    """grabToImage is asynchronous — block on the event loop until the
    grab result fires `ready`.  Optionally pass a target size for
    super-sampled grabs."""
    from PyQt6.QtCore import QSize
    if size_w and size_h:
        result = item.grabToImage(QSize(size_w, size_h))
    else:
        result = item.grabToImage()
    if result is None:
        print(f"[snap] grabToImage returned None for {out.name}")
        return False

    loop = QEventLoop()
    timer = QTimer()
    timer.setSingleShot(True)
    timer.timeout.connect(loop.quit)
    result.ready.connect(loop.quit)
    timer.start(timeout_ms)
    loop.exec()

    img = result.image()
    if img.isNull():
        print(f"[snap] grab result null for {out.name}")
        return False
    out.parent.mkdir(parents=True, exist_ok=True)
    if img.save(str(out)):
        print(f"[snap] {out.relative_to(REPO)}  {img.width()}x{img.height()}")
        return True
    print(f"[snap] FAILED to save {out}")
    return False


def render_scene(app: QGuiApplication, bridge: QObject,
                 scene_path: Path, out_path: Path) -> bool:
    """Load scene_path into a QQuickView, render and grab to out_path."""
    view = QQuickView()
    view.setResizeMode(QQuickView.ResizeMode.SizeRootObjectToView)
    view.setColor(view.color())  # keep theme bg
    # The faux bridge has to be available BEFORE the QML loads — every
    # binding to `bridge` resolves at component-init time.
    view.rootContext().setContextProperty("bridge", bridge)
    view.setSource(QUrl.fromLocalFile(str(scene_path)))

    if not _wait_until_ready(view):
        print(f"[snap] scene not ready: {scene_path.name}")
        for err in view.errors():
            print(f"        {err.toString()}")
        return False

    root = view.rootObject()
    if root is None:
        print(f"[snap] no root object for {scene_path.name}")
        return False

    # The view itself is offscreen; force a size so layouts run.
    w = int(root.property("width") or root.implicitWidth() or 600)
    h = int(root.property("height") or root.implicitHeight() or 400)
    view.resize(w, h)
    # Show offscreen so the scene graph actually renders the items.
    # On Windows the QQuickView needs to be exposed for grabToImage
    # to produce a non-empty result; setting Hidden visibility means
    # nothing pops on screen.
    view.setVisibility(QQuickView.Visibility.Hidden)
    view.show()
    # Pump events for a moment so timers / Component.onCompleted run.
    end = QTimer()
    end.setSingleShot(True)
    loop = QEventLoop()
    end.timeout.connect(loop.quit)
    end.start(200)
    loop.exec()

    ok = _grab_async(root, out_path)
    view.hide()
    return ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out",    default="docs/images")
    ap.add_argument("--scenes", default=None,
                    help="optional comma-separated list of scene names "
                         "(without .qml) to render; default = all")
    ap.add_argument("--no-isolate", action="store_true",
                    help="render all scenes in this process (default: each "
                         "scene runs in its own subprocess so a crash in "
                         "one doesn't lose the whole batch — QtCharts in "
                         "the plotter scene segfaults during teardown).")
    ap.add_argument("--one", action="store_true",
                    help="internal: render exactly one scene named by --scenes "
                         "in this process. Used by the per-scene subprocess.")
    args = ap.parse_args()

    out_dir    = REPO / args.out
    scenes_dir = HERE / "scenes"
    out_dir.mkdir(parents=True, exist_ok=True)

    targets = sorted(scenes_dir.glob("*.qml"))
    if args.scenes:
        wanted = {s.strip() for s in args.scenes.split(",")}
        targets = [t for t in targets if t.stem in wanted]
    if not targets:
        print(f"[snap] no scenes found in {scenes_dir}")
        return 1

    if args.one or args.no_isolate:
        app = QGuiApplication(sys.argv)
        bridge = FauxBridge()
        failures = 0
        for scene in targets:
            out = out_dir / f"{scene.stem}.png"
            if not render_scene(app, bridge, scene, out):
                failures += 1
        # Skip the QApplication teardown chain that triggers QtCharts'
        # segfault — we already saved the PNGs.  os._exit is a hard
        # exit; the OS reclaims the resources.
        os._exit(0 if failures == 0 else 1)

    # Default: spawn one subprocess per scene so a crash in one (looking
    # at you, plotter_demo with QtCharts) doesn't poison the others.
    import subprocess
    failures = 0
    for scene in targets:
        rc = subprocess.run(
            [sys.executable, str(Path(__file__)), "--one",
             "--scenes", scene.stem, "--out", args.out],
            cwd=str(REPO),
        ).returncode
        if rc != 0:
            print(f"[snap] {scene.stem}: subprocess exit {rc}")
            failures += 1
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
