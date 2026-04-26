"""Entry point. `python -m aneb_ui` or the `aneb-ui` console script."""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PyQt6.QtWidgets import QApplication

from .app import MainWindow


DEFAULT_ENGINE = Path(__file__).resolve().parent.parent.parent / "build" / "aneb-sim" / "aneb-sim.exe"


def main() -> int:
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    parser = argparse.ArgumentParser(prog="aneb-ui")
    parser.add_argument(
        "--engine", type=Path, default=DEFAULT_ENGINE,
        help=f"path to aneb-sim engine binary (default: {DEFAULT_ENGINE})",
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("aneb-ui")
    app.setApplicationDisplayName("ANEB Simulator")

    win = MainWindow(args.engine)
    win.show()
    win.start_engine()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
