"""
app_qml — MainWindow that hosts the Board.qml view inside a QQuickWidget,
spawns the engine via SimProxy, and routes events through SimState +
QmlBridge.

This is the QML alternative to app.py (the QtWidgets variant). The
engine, SimProxy, and SimState are reused unchanged — only the UI layer
swaps.
"""
from __future__ import annotations

import logging
from pathlib import Path

from PyQt6.QtCore         import QUrl
from PyQt6.QtGui          import QGuiApplication
from PyQt6.QtQuickWidgets import QQuickWidget
from PyQt6.QtWidgets      import QMainWindow

from .qml_bridge import QmlBridge
from .sim_proxy  import SimProxy
from .state      import SimState


log = logging.getLogger(__name__)

QML_BOARD = Path(__file__).resolve().parent / "qml" / "Board.qml"


class QmlMainWindow(QMainWindow):
    def __init__(self, engine_path: Path) -> None:
        super().__init__()
        self.setWindowTitle("aneb-sim — ANEB v1.1 simulator")

        # Fit the window to the screen's available area (excludes the
        # taskbar) — never start larger than the monitor. The minimum
        # size needs to be generous enough that each ECU panel keeps
        # the title row on one line, the LCD visible, and the I/O row
        # of LEDs+pots+buttons fully on-screen — empirically that's
        # around 1300×800 for a 2x2 grid + right column.
        screen = QGuiApplication.primaryScreen()
        avail = screen.availableGeometry() if screen else None
        if avail is not None:
            target_w = min(1500, int(avail.width()  * 0.85))
            target_h = min(900,  int(avail.height() * 0.85))
            min_w = min(1300, int(avail.width()  * 0.70))
            min_h = min(800,  int(avail.height() * 0.70))
        else:
            target_w, target_h = 1500, 900
            min_w, min_h = 1300, 800
        self.setMinimumSize(min_w, min_h)
        self.resize(target_w, target_h)
        # Center on the available area.
        if avail is not None:
            self.move(
                avail.left() + (avail.width()  - self.width())  // 2,
                avail.top()  + (avail.height() - self.height()) // 2,
            )

        # Engine + state pipeline (identical to app.py).
        self._state = SimState(self)
        self._proxy = SimProxy(engine_path, self)
        self._proxy.pin_event       .connect(self._state.update_pin)
        self._proxy.pwm_event       .connect(self._state.update_pwm)
        self._proxy.uart_event      .connect(self._state.update_uart)
        self._proxy.can_tx_event    .connect(self._state.update_can_tx)
        self._proxy.can_state_event .connect(self._state.update_can_state)
        self._proxy.lcd_event       .connect(self._state.update_lcd)
        self._proxy.log_event       .connect(self._state.update_log)

        # Bridge sits between SimState and QML.
        self._bridge = QmlBridge(self._state, self._proxy, self)

        # Embed QML.
        self._view = QQuickWidget(self)
        self._view.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        # `bridge` is the root context property every QML file uses.
        self._view.rootContext().setContextProperty("bridge", self._bridge)
        self._view.setSource(QUrl.fromLocalFile(str(QML_BOARD)))
        if self._view.status() == QQuickWidget.Status.Error:
            for err in self._view.errors():
                log.error("QML: %s", err.toString())
        self.setCentralWidget(self._view)

    def start_engine(self) -> None:
        self._proxy.start()

    def closeEvent(self, evt) -> None:    # noqa: N802 (Qt signature)
        self._bridge.stop_uart_bridges()
        self._proxy.stop()
        super().closeEvent(evt)
