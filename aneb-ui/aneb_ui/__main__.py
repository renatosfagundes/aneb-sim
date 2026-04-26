"""Entry point. `python -m aneb_ui` or the `aneb-ui` console script.

Two UI variants are available:

  --use-qml   QML-based view (qml/Board.qml + qml_assets/*.png)
              Cleaner, board-photo-style appearance. Default in 0.1+.

  --use-widgets
              Original PyQt6 QtWidgets layout. Kept as a fallback in case
              QML assets are missing or QML rendering misbehaves.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PyQt6.QtWidgets import QApplication


DEFAULT_ENGINE = Path(__file__).resolve().parent.parent.parent / "build" / "aneb-sim" / "aneb-sim.exe"


def main() -> int:
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    parser = argparse.ArgumentParser(prog="aneb-ui")
    parser.add_argument(
        "--engine", type=Path, default=DEFAULT_ENGINE,
        help=f"path to aneb-sim engine binary (default: {DEFAULT_ENGINE})",
    )
    ui_group = parser.add_mutually_exclusive_group()
    ui_group.add_argument(
        "--use-qml", action="store_true", default=True,
        help="render with QML (default)",
    )
    ui_group.add_argument(
        "--use-widgets", action="store_true",
        help="render with the original QtWidgets layout (fallback)",
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("aneb-ui")
    app.setApplicationDisplayName("ANEB Simulator")

    if args.use_widgets:
        from .app import MainWindow
        win = MainWindow(args.engine)
    else:
        from .app_qml import QmlMainWindow
        win = QmlMainWindow(args.engine)

    win.show()
    win.start_engine()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
