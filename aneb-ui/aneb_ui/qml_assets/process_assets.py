"""
process_assets.py — clean up the Gemini-generated assets.

The renders come out with an opaque near-white "checkerboard" outside
the actual subject (the image-viewer convention for transparency was
baked into pixels rather than the alpha channel). This script keys out
the pale-grey/white background to true alpha=0 so the QML compositing
on the dark PCB-green panels reads cleanly.

Run once after dropping new renders into this directory:

    python aneb-ui/aneb_ui/qml_assets/process_assets.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from PyQt6.QtGui import QImage, qRgba


HERE = Path(__file__).resolve().parent

# (filename, threshold). A pixel with R, G, B all >= threshold is
# treated as background and gets alpha=0.
TARGETS = [
    ("arduino.png",   220),
    ("trimpot.png",   220),
    ("buttons.png",   220),
    # background.png keeps its solid color — it IS the PCB.
]


def chroma_key(path: Path, threshold: int) -> int:
    img = QImage(str(path)).convertToFormat(QImage.Format.Format_ARGB32)
    if img.isNull():
        print(f"  skip {path.name}: failed to load")
        return 0

    w, h = img.width(), img.height()
    keyed = 0
    # Vectorise via raw bits buffer for speed; the assets are 2k+ wide.
    ptr = img.bits()
    ptr.setsize(img.sizeInBytes())
    buf = memoryview(ptr).cast("B")
    # ARGB32 is little-endian B,G,R,A in memory.
    for i in range(0, w * h * 4, 4):
        b, g, r, a = buf[i], buf[i+1], buf[i+2], buf[i+3]
        if a == 0:
            continue
        if r >= threshold and g >= threshold and b >= threshold:
            buf[i+3] = 0
            keyed += 1

    if keyed > 0:
        img.save(str(path))
    return keyed


def main() -> int:
    rc = 0
    for fname, thr in TARGETS:
        p = HERE / fname
        if not p.exists():
            print(f"  missing: {fname}")
            rc = 1
            continue
        n = chroma_key(p, thr)
        print(f"  {fname}: keyed {n:>9} pixels  (threshold {thr})")
    return rc


if __name__ == "__main__":
    sys.exit(main())
