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
        self.resize(1700, 1000)
        # Below this size the layout can't fit four ECU panels + the
        # MCU column without overlap; clamp instead of letting Qt clip.
        self.setMinimumSize(1280, 760)

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
