"""
process_assets.py — chroma-key out the background of asset images.

The earlier threshold version was unsafe — it keyed every near-white
pixel including the silkscreen labels in the middle of the board.
This version flood-fills from the four image corners through any
near-white neighbors, so only background pixels actually CONNECTED
to a corner become transparent. Silkscreen text inside the PCB
stays intact.

Run after dropping new renders into this directory:

    python aneb-ui/aneb_ui/qml_assets/process_assets.py
"""
from __future__ import annotations

import sys
from collections import deque
from pathlib import Path

from PyQt6.QtGui import QImage


HERE = Path(__file__).resolve().parent

# (filename, threshold). A pixel with R, G, B all >= threshold AND
# reachable from a corner via similar pixels gets alpha=0.
TARGETS = [
    ("arduino.png",   240),
    ("trimpot.png",   240),
    ("buttons.png",   240),
]


def chroma_key_flood(path: Path, threshold: int) -> int:
    img = QImage(str(path)).convertToFormat(QImage.Format.Format_ARGB32)
    if img.isNull():
        print(f"  skip {path.name}: failed to load")
        return 0

    w, h = img.width(), img.height()
    ptr = img.bits()
    ptr.setsize(img.sizeInBytes())
    buf = memoryview(ptr).cast("B")  # B G R A per pixel

    def is_bg(i: int) -> bool:
        # Already alpha=0 → not background, already done.
        return (buf[i + 3] != 0
                and buf[i + 0] >= threshold
                and buf[i + 1] >= threshold
                and buf[i + 2] >= threshold)

    visited = bytearray(w * h)        # 0 = unvisited, 1 = visited
    queue: deque[tuple[int, int]] = deque()

    def seed(x: int, y: int) -> None:
        idx = y * w + x
        if visited[idx]:
            return
        i = idx * 4
        if is_bg(i):
            visited[idx] = 1
            queue.append((x, y))

    # Seed from every corner.
    for cx, cy in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        seed(cx, cy)

    keyed = 0
    while queue:
        x, y = queue.popleft()
        idx = y * w + x
        i = idx * 4
        # Mark transparent.
        buf[i + 3] = 0
        keyed += 1
        # Expand to 4-connected neighbors.
        if x > 0:     seed(x - 1, y)
        if x < w - 1: seed(x + 1, y)
        if y > 0:     seed(x, y - 1)
        if y < h - 1: seed(x, y + 1)

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
        n = chroma_key_flood(p, thr)
        print(f"  {fname}: keyed {n:>9} pixels via flood-fill  (threshold {thr})")
    return rc


if __name__ == "__main__":
    sys.exit(main())
