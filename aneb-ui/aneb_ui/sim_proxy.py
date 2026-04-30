"""
sim_proxy — bridge between the C engine subprocess and the Qt UI.

Spawns aneb-sim, reads JSON-Lines events from its stdout via QProcess
(integrates with the Qt event loop, no extra threads), and emits a
typed Qt signal per event type. UI code calls send_command() to push
JSON commands the other way.
"""
from __future__ import annotations

import json
import logging
import shlex
from pathlib import Path
from typing import Any

from PyQt6.QtCore import QObject, QProcess, QByteArray, pyqtSignal


log = logging.getLogger(__name__)


class SimProxy(QObject):
    # One signal per event type the engine emits. Listeners get the parsed
    # event dict and can trust the structure documented in PROTOCOL.md.
    pin_event       = pyqtSignal(dict)   # {"chip", "pin", "val", "ts"}
    pwm_event       = pyqtSignal(dict)   # {"chip", "pin", "duty", "ts"}
    uart_event      = pyqtSignal(dict)   # {"chip", "data", "ts"}
    can_tx_event    = pyqtSignal(dict)   # {"bus", "src", "id", "ext", "rtr", "dlc", "data", "ts"}
    can_state_event = pyqtSignal(dict)   # {"chip", "tec", "rec", "state", "ts"}
    lcd_event       = pyqtSignal(dict)   # {"chip", "line0", "line1", "ts"}
    log_event       = pyqtSignal(dict)   # {"level", "msg"}
    raw_event       = pyqtSignal(dict)   # everything (for diagnostic panels)
    parse_error     = pyqtSignal(str)    # malformed line received

    # Lifecycle.
    started      = pyqtSignal()
    stopped      = pyqtSignal(int)        # exit code

    def __init__(self, engine_path: Path, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._engine_path = Path(engine_path)
        self._buffer = bytearray()
        self._proc = QProcess(self)
        self._proc.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self._proc.readyReadStandardOutput.connect(self._on_stdout)
        self._proc.readyReadStandardError.connect(self._on_stderr)
        self._proc.started.connect(self.started.emit)
        self._proc.finished.connect(self._on_finished)

    # ---------- subprocess control ------------------------------------

    def start(self, args: list[str] | None = None) -> None:
        """Launch the engine. `args` are passed verbatim to aneb-sim."""
        if not self._engine_path.exists():
            raise FileNotFoundError(f"engine not found: {self._engine_path}")
        cmd = [str(self._engine_path), *(args or [])]
        log.info("spawning engine: %s", shlex.join(cmd))
        self._proc.setProgram(cmd[0])
        self._proc.setArguments(cmd[1:])
        self._proc.start()

    def stop(self) -> None:
        if self._proc.state() != QProcess.ProcessState.NotRunning:
            self._proc.terminate()
            if not self._proc.waitForFinished(2000):
                self._proc.kill()

    def is_running(self) -> bool:
        return self._proc.state() == QProcess.ProcessState.Running

    # ---------- outbound commands -------------------------------------

    def send_command(self, cmd: dict[str, Any]) -> None:
        """Serialize and send a single JSON-Lines command."""
        if "v" not in cmd:
            cmd = {"v": 1, **cmd}
        line = (json.dumps(cmd) + "\n").encode("utf-8")
        n = self._proc.write(QByteArray(line))
        if n != len(line):
            log.warning("partial write: %d/%d", n, len(line))

    # convenience wrappers — match the JSON command names in PROTOCOL.md.
    def cmd_load   (self, chip: str, path: str) -> None: self.send_command({"c":"load",   "chip": chip, "path": path})
    def cmd_unload (self, chip: str)            -> None: self.send_command({"c":"unload", "chip": chip})
    def cmd_reset  (self, chip: str)            -> None: self.send_command({"c":"reset",  "chip": chip})
    def cmd_speed  (self, factor: float)        -> None: self.send_command({"c":"speed",  "speed": float(factor)})
    def cmd_din    (self, chip: str, pin: str, val: int) -> None:
        self.send_command({"c":"din", "chip": chip, "pin": pin, "val": int(bool(val))})
    def cmd_adc    (self, chip: str, ch: int, val: int) -> None:
        self.send_command({"c":"adc", "chip": chip, "ch": ch, "val": val})
    def cmd_pause  (self) -> None: self.send_command({"c":"pause"})
    def cmd_resume (self) -> None: self.send_command({"c":"resume"})
    def cmd_can_inject(self, frame_id: int, data_hex: str, *,
                       ext: bool=False, rtr: bool=False, dlc: int=0) -> None:
        self.send_command({
            "c": "can_inject",
            "id": frame_id, "ext": ext, "rtr": rtr,
            "dlc": dlc, "data": data_hex,
        })
    def cmd_force_busoff (self, chip: str) -> None:
        self.send_command({"c":"force_busoff", "chip": chip})
    def cmd_can_recover  (self, chip: str) -> None:
        self.send_command({"c":"can_recover",  "chip": chip})
    def cmd_can_errors   (self, chip: str, *, tx: int=0, rx: int=0) -> None:
        self.send_command({"c":"can_errors", "chip": chip, "tx": tx, "rx": rx})

    # ---------- internal slots ----------------------------------------

    def _on_stdout(self) -> None:
        chunk = bytes(self._proc.readAllStandardOutput())
        self._buffer.extend(chunk)
        # Process complete lines.
        while True:
            nl = self._buffer.find(b"\n")
            if nl < 0:
                break
            line = bytes(self._buffer[:nl]).decode("utf-8", errors="replace")
            del self._buffer[: nl + 1]
            self._dispatch(line)

    def _on_stderr(self) -> None:
        # Engine stderr is human-readable diagnostics; mirror to our log.
        chunk = bytes(self._proc.readAllStandardError()).decode("utf-8", "replace")
        for line in chunk.splitlines():
            if line:
                log.info("engine: %s", line)

    def _dispatch(self, line: str) -> None:
        if not line.strip():
            return
        try:
            evt = json.loads(line)
        except json.JSONDecodeError as exc:
            self.parse_error.emit(f"{exc}: {line!r}")
            return
        if not isinstance(evt, dict):
            self.parse_error.emit(f"non-object event: {line!r}")
            return

        self.raw_event.emit(evt)
        t = evt.get("t")
        if   t == "pin":       self.pin_event.emit(evt)
        elif t == "pwm":       self.pwm_event.emit(evt)
        elif t == "uart":      self.uart_event.emit(evt)
        elif t == "can_tx":    self.can_tx_event.emit(evt)
        elif t == "can_state": self.can_state_event.emit(evt)
        elif t == "lcd":       self.lcd_event.emit(evt)
        elif t == "log":       self.log_event.emit(evt)

    def _on_finished(self, exit_code: int, _exit_status) -> None:
        # Flush any partial buffer.
        if self._buffer:
            log.warning("engine exited with %d bytes unparsed", len(self._buffer))
            self._buffer.clear()
        self.stopped.emit(exit_code)
