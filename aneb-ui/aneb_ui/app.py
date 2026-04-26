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


class MainWindow(QMainWindow):
    def __init__(self, engine_path: Path) -> None:
        super().__init__()
        self.setWindowTitle("aneb-sim — ANEB v1.1 simulator")
        self.resize(1600, 950)

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

        # ---- chip panels ------------------------------------------------
        chip_row = QHBoxLayout()
        chip_row.setSpacing(6)
        for ecu in ("ecu1", "ecu2", "ecu3", "ecu4"):
            chip_row.addWidget(EcuPanel(ecu, self._proxy, self._state, self))
        chip_row.addWidget(McuPanel(self._proxy, self._state, self))

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
