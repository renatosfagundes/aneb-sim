"""
app.py — main window assembly.

Layout (kept simple for M6 — photo overlay is a future polish step):

  ┌────────────────────────────────────────────────────────────────┐
  │ [Load…] [Pause] [Resume] [Reset] [Engine: running]             │ toolbar
  ├──────────────┬──────────────┬──────────────┬──────────────┬────┤
  │ ECU1         │ ECU2         │ ECU3         │ ECU4         │MCU │
  │ leds buttons │ leds buttons │ leds buttons │ leds buttons │... │
  │ pots can srl │ pots can srl │ pots can srl │ pots can srl │    │
  ├──────────────┴──────────────┴──────────────┴──────────────┴────┤
  │ CAN monitor                                       │ CAN inject │
  └────────────────────────────────────────────────────┴───────────┘
"""
from __future__ import annotations

import logging
from pathlib import Path

from PyQt6.QtCore import Qt
from PyQt6.QtGui  import QGuiApplication
from PyQt6.QtWidgets import (
    QFileDialog, QHBoxLayout, QLabel, QMainWindow, QMenuBar,
    QPushButton, QSplitter, QStatusBar, QToolBar, QVBoxLayout, QWidget,
)

from .sim_proxy import SimProxy
from .state import SimState, CHIPS
from .widgets.ecu_panel import EcuPanel
from .widgets.mcu_panel import McuPanel
from .widgets.can_monitor import CanMonitor
from .widgets.can_inject import CanInject


log = logging.getLogger(__name__)


# Global QSS theme — PCB-green dark palette so the Nano illustrations
# and live LEDs feel like they're sitting on a real board.
QSS_THEME = """
QMainWindow, QWidget { background: #0a1a14; color: #cdfac0; }

QGroupBox {
    background: #102a1c;
    border: 1px solid #3e6b4d;
    border-radius: 6px;
    margin-top: 12px;
    font-weight: bold;
    color: #b8e0a8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 2px 8px;
    background: #1a3a26;
    border: 1px solid #3e6b4d;
    border-radius: 3px;
    color: #cdfac0;
}

QLabel { color: #cdfac0; }

QPlainTextEdit, QLineEdit, QSpinBox {
    background: #061410;
    color: #cdfac0;
    border: 1px solid #3e6b4d;
    border-radius: 3px;
    selection-background-color: #4a8a5d;
}

QPushButton {
    background: #1c2a20;
    color: #cdfac0;
    border: 1px solid #4a8a5d;
    padding: 5px 12px;
    border-radius: 3px;
}
QPushButton:hover    { background: #284033; }
QPushButton:pressed  { background: #122018; }

QToolBar {
    background: #0a1a14;
    border-bottom: 1px solid #3e6b4d;
    spacing: 6px;
    padding: 4px;
}
QToolBar QPushButton, QToolBar QLabel {
    margin: 0 2px;
}

QSplitter::handle { background: #1a3a26; }
QSplitter::handle:horizontal { width: 4px; }
QSplitter::handle:vertical   { height: 4px; }

QTableWidget, QHeaderView::section {
    background: #061410;
    color: #cdfac0;
    border: 1px solid #3e6b4d;
}
QHeaderView::section { padding: 4px; font-weight: bold; }

QStatusBar { background: #0a1a14; color: #b8e0a8; }
"""


class MainWindow(QMainWindow):
    def __init__(self, engine_path: Path) -> None:
        super().__init__()
        self.setWindowTitle("aneb-sim — ANEB v1.1 simulator")
        # Fit to the screen's available area (taskbar excluded) — never
        # open larger than the actual monitor.
        screen = QGuiApplication.primaryScreen()
        avail = screen.availableGeometry() if screen else None
        if avail is not None:
            self.resize(min(1600, int(avail.width()  * 0.95)),
                        min(950,  int(avail.height() * 0.95)))
            self.move(
                avail.left() + (avail.width()  - self.width())  // 2,
                avail.top()  + (avail.height() - self.height()) // 2,
            )
        else:
            self.resize(1400, 800)
        self.setStyleSheet(QSS_THEME)

        self._state = SimState(self)
        self._proxy = SimProxy(engine_path, self)

        # ---- wire engine events into the state store -----------------
        self._proxy.pin_event       .connect(self._state.update_pin)
        self._proxy.pwm_event       .connect(self._state.update_pwm)
        self._proxy.uart_event      .connect(self._state.update_uart)
        self._proxy.can_tx_event    .connect(self._state.update_can_tx)
        self._proxy.can_state_event .connect(self._state.update_can_state)
        self._proxy.log_event       .connect(self._state.update_log)
        self._proxy.parse_error     .connect(lambda m: log.warning("proto: %s", m))
        self._proxy.started         .connect(lambda: self._set_engine_status("running"))
        self._proxy.stopped         .connect(self._on_engine_stopped)

        # ---- chip panels: 2x2 ECU grid + MCU on the right ---------------
        # Mirrors the physical board: ECU1 top-left, ECU2 top-right,
        # ECU4 bottom-left, ECU3 bottom-right (matches the photo).
        from PyQt6.QtWidgets import QGridLayout
        ecu_grid = QGridLayout()
        ecu_grid.setSpacing(8)
        ecu_grid.addWidget(EcuPanel("ecu1", self._proxy, self._state, self), 0, 0)
        ecu_grid.addWidget(EcuPanel("ecu2", self._proxy, self._state, self), 0, 1)
        ecu_grid.addWidget(EcuPanel("ecu4", self._proxy, self._state, self), 1, 0)
        ecu_grid.addWidget(EcuPanel("ecu3", self._proxy, self._state, self), 1, 1)
        ecu_widget = QWidget(self)
        ecu_widget.setLayout(ecu_grid)

        # The MCU panel sits beside the ECU grid, full-height.
        mcu_panel = McuPanel(self._proxy, self._state, self)

        chip_row = QHBoxLayout()
        chip_row.setSpacing(8)
        chip_row.addWidget(ecu_widget, 4)
        chip_row.addWidget(mcu_panel, 1)

        chip_widget = QWidget(self)
        chip_widget.setLayout(chip_row)

        # ---- bottom split: can monitor + can inject ---------------------
        self._can_monitor = CanMonitor(self._state, self)
        self._can_inject  = CanInject(self._proxy, self)
        bottom = QSplitter(Qt.Orientation.Horizontal, self)
        bottom.addWidget(self._can_monitor)
        bottom.addWidget(self._can_inject)
        bottom.setStretchFactor(0, 4)
        bottom.setStretchFactor(1, 1)

        # ---- main split: chip panels above, can+inject below ------------
        main_split = QSplitter(Qt.Orientation.Vertical, self)
        main_split.addWidget(chip_widget)
        main_split.addWidget(bottom)
        main_split.setStretchFactor(0, 3)
        main_split.setStretchFactor(1, 1)
        self.setCentralWidget(main_split)

        # ---- toolbar ----------------------------------------------------
        tb = QToolBar("Engine controls", self)
        tb.setMovable(False)
        self._engine_status = QLabel("Engine: stopped", self)
        for ecu in ("ecu1", "ecu2", "ecu3", "ecu4"):
            btn = QPushButton(f"Load {ecu.upper()}…", self)
            btn.clicked.connect(lambda _, c=ecu: self._on_load_clicked(c))
            tb.addWidget(btn)
        load_mcu = QPushButton("Load MCU…", self)
        load_mcu.clicked.connect(lambda: self._on_load_clicked("mcu"))
        tb.addWidget(load_mcu)
        tb.addSeparator()
        for label, fn in (("Pause", self._proxy.cmd_pause),
                          ("Resume", self._proxy.cmd_resume)):
            b = QPushButton(label, self)
            b.clicked.connect(lambda _, f=fn: f())
            tb.addWidget(b)
        tb.addSeparator()
        tb.addWidget(self._engine_status)
        self.addToolBar(tb)

        # ---- status bar -------------------------------------------------
        sb = QStatusBar(self)
        self.setStatusBar(sb)
        sb.showMessage("Ready.")
        self._state.log_appended.connect(self._on_log_event)

    # -------- toolbar handlers ------------------------------------------

    def _on_load_clicked(self, chip: str) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, f"Load firmware for {chip}", "", "Intel hex (*.hex);;All (*.*)"
        )
        if path:
            self._proxy.cmd_load(chip, path)

    def _on_engine_stopped(self, exit_code: int) -> None:
        self._set_engine_status(f"stopped (exit {exit_code})")

    def _set_engine_status(self, text: str) -> None:
        self._engine_status.setText(f"Engine: {text}")

    def _on_log_event(self, evt: dict) -> None:
        self.statusBar().showMessage(f"[{evt.get('level','?')}] {evt.get('msg','')}", 5000)

    # -------- lifecycle -------------------------------------------------

    def start_engine(self) -> None:
        self._proxy.start()

    def closeEvent(self, evt) -> None:    # noqa: N802 (Qt signature)
        self._proxy.stop()
        super().closeEvent(evt)
