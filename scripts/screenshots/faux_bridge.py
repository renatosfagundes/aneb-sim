"""
faux_bridge.py — minimal stand-in for QmlBridge that the screenshot
scenes can use as a context property.

Real QmlBridge is heavyweight (state store, sub-processes, plot
buffers).  For static screenshots we just need the panes to bind to
*something* with the right shape: fire one or two signals, return
plausible data from the lookup methods, and let the QML render.
"""
from __future__ import annotations

import math

from PyQt6.QtCore import QObject, pyqtSignal, pyqtSlot, pyqtProperty


class FauxBridge(QObject):
    # ---- Console pane -----------------------------------------------
    uartAppended         = pyqtSignal(str, str)
    uartSent             = pyqtSignal(str)

    # ---- Plotter pane -----------------------------------------------
    plotSeqChanged       = pyqtSignal()

    # ---- Avrdude pane -----------------------------------------------
    avrdudeOutput        = pyqtSignal(str, str)
    avrdudeStateChanged  = pyqtSignal(str, bool)

    # ---- Chip-info sidebar (EcuPanel) -------------------------------
    chipStatChanged      = pyqtSignal(str)
    pinSeqChanged        = pyqtSignal()
    pwmSeqChanged        = pyqtSignal()
    canStateSeqChanged   = pyqtSignal()
    lcdLinesChanged      = pyqtSignal()
    uartBridgeChanged    = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._engine_running = True

    # ---- Properties read by EcuPanel + chip badges ------------------

    @pyqtProperty(bool, constant=True)
    def engineRunning(self) -> bool:
        return self._engine_running

    @pyqtProperty("QVariantMap", constant=True)
    def userComPorts(self) -> dict:
        return {"ecu1": "COM11", "ecu2": "COM13", "ecu3": "COM15",
                "ecu4": "COM17", "mcu":  "COM19"}

    @pyqtProperty("QVariantMap", constant=True)
    def uartPorts(self) -> dict:
        return {"ecu1": 8600, "ecu2": 8601, "ecu3": 8602,
                "ecu4": 8603, "mcu":  8604}

    @pyqtProperty("QVariantMap", constant=True)
    def lcdLines(self) -> dict:
        return {c: ["", ""] for c in
                ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")}

    # ---- Slots invoked by panes (no-ops for screenshots) ------------

    @pyqtSlot(str, str)
    def sendUart(self, chip: str, text: str) -> None:
        pass

    @pyqtSlot(str)
    def flashChipAvrdude(self, chip: str) -> None:
        pass

    @pyqtSlot(str)
    def unloadChip(self, chip: str) -> None:
        pass

    @pyqtSlot(str, str, int)
    def setDin(self, chip: str, pin: str, val: int) -> None:
        pass

    @pyqtSlot(str, int, int)
    def setAdc(self, chip: str, ch: int, val: int) -> None:
        pass

    @pyqtSlot()
    def installComPorts(self) -> None:
        pass

    @pyqtSlot()
    def removeComPorts(self) -> None:
        pass

    # ---- Per-chip data accessors used by EcuPanel + Plotter ---------

    @pyqtSlot(str, result="QVariantMap")
    def chipStat(self, chip: str) -> dict:
        # Plausible per-chip stats so the Hex/Free RAM lines render.
        if chip == "mcu":
            return {"hex_name": "MCU_Firmware_v08.hex",
                    "hex_path": "C:/dev/aneb-sim/firmware/mcu/MCU_Firmware_v08.hex",
                    "free_ram": 1620, "ram_size": 2048, "sp": 1956}
        return {"hex_name": "dashboard_full.ino.hex",
                "hex_path": ("C:/Users/renat/OneDrive/Pos/RTOS/remote_flasher/"
                             "test_firmwares/dashboard_full/build/"
                             "dashboard_full.ino.hex"),
                "free_ram": 1832, "ram_size": 2048, "sp": 2168}

    @pyqtSlot(str, result="QVariantMap")
    def canStateOf(self, chip: str) -> dict:
        return {"tec": 0, "rec": 0, "state": "active"}

    @pyqtSlot(str, str, result=int)
    def pinOf(self, chip: str, pin: str) -> int:
        return 0

    @pyqtSlot(str, str, result=float)
    def pwmOf(self, chip: str, pin: str) -> float:
        return 0.0

    @pyqtSlot(str, int, result=int)
    def adcOf(self, chip: str, channel: int) -> int:
        return 512

    # ---- Plotter signal catalogue + sample data ---------------------

    @pyqtSlot(result="QVariantList")
    def plotSignals(self) -> list:
        return [
            {"key": "adc:0", "label": "AIN0",   "axis": "adc"},
            {"key": "adc:1", "label": "AIN1",   "axis": "adc"},
            {"key": "adc:2", "label": "AIN2",   "axis": "adc"},
            {"key": "adc:3", "label": "AIN3",   "axis": "adc"},
            {"key": "pwm:PD3", "label": "DOUT0 PWM", "axis": "pwm"},
            {"key": "pwm:PD6", "label": "LDR PWM",   "axis": "pwm"},
        ]

    @pyqtSlot(str, str, result="QVariantList")
    def plotSeries(self, chip: str, key: str) -> list:
        # 200 samples spanning the last 10 s, with a key-dependent
        # waveform so each enabled trace looks distinct.
        n = 200
        out = []
        for i in range(n):
            t = -10.0 * (1 - i / (n - 1))   # x in [-10, 0]
            if key == "adc:0":
                y = 512 + 380 * math.sin(0.6 * (t + 10))
            elif key == "adc:1":
                y = 512 + 200 * math.sin(0.9 * (t + 10) + 1.2)
            elif key == "adc:2":
                y = 200 + 120 * math.cos(0.4 * (t + 10))
            elif key == "adc:3":
                y = 700 + 80  * math.sin(1.4 * (t + 10) + 2.0)
            elif key == "pwm:PD3":
                y = 0.5 + 0.45 * math.sin(0.5 * (t + 10))
            elif key == "pwm:PD6":
                y = 0.3 + 0.25 * math.sin(0.7 * (t + 10) + 1.5)
            else:
                y = 0
            out.append({"x": t, "y": y})
        return out
