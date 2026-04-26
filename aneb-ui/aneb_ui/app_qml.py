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
        # taskbar) — never start larger than the monitor. We aim for
        # 1700x1000 if room exists, otherwise 92% of available space.
        screen = QGuiApplication.primaryScreen()
        avail = screen.availableGeometry() if screen else None
        if avail is not None:
            # 80% of the available area leaves generous breathing room
            # around the window so the Windows taskbar / title bar /
            # borders don't push content off-screen.
            target_w = min(1400, int(avail.width()  * 0.80))
            target_h = min(820,  int(avail.height() * 0.80))
            min_w = min(1100, int(avail.width()  * 0.65))
            min_h = min(660,  int(avail.height() * 0.65))
        else:
            target_w, target_h = 1300, 780
            min_w, min_h = 1100, 660
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
        self._proxy.stop()
        super().closeEvent(evt)
