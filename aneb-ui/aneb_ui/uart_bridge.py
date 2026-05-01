"""
uart_bridge — bridges each chip's TCP UART socket to a real COM port.

The C engine exposes each chip's simulated UART on localhost:
  ecu1 → 8600   ecu2 → 8601   ecu3 → 8602   ecu4 → 8603   mcu → 8604

This module provides UartBridgeManager which optionally connects those
TCP sockets to virtual COM ports (created by com0com or any other virtual
serial port driver).

Usage (standalone):
    python -m aneb_ui.uart_bridge  COM10 COM12 COM14 COM16 COM18

    Each positional argument maps to a chip in order (ecu1..mcu).
    Pass "-" to skip a chip.

Usage (from UI):
    mgr = UartBridgeManager()
    mgr.start({"ecu1": "COM10", "ecu2": "COM12"})
    ...
    mgr.stop()

Requirements:
    pip install pyserial

COM port setup (com0com):
    Install com0com from https://sourceforge.net/projects/com0com/
    Create pairs, e.g.:  COM10 <-> COM11,  COM12 <-> COM13, ...
    The bridge connects to COM10; avrdude/remote_flasher uses COM11.
    Set "use Ports class" checkbox in com0com setup so ports appear in
    Device Manager.

Alternative — direct TCP (no COM ports needed):
    avrdude supports  -P net:localhost:8600  with  -c arduino
    This bypasses COM ports entirely and is simpler for flashing.
    The chip resets automatically when avrdude connects (reset-on-connect).
"""
from __future__ import annotations

import logging
import socket
import threading
from typing import Optional

log = logging.getLogger(__name__)

CHIP_ORDER   = ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")
BASE_PORT    = 8600
BUFFER_SIZE  = 256


class ChipBridge:
    """Bidirectional bridge between one chip's TCP UART socket and a COM port."""

    def __init__(self, chip: str, tcp_port: int, com_port: str) -> None:
        self.chip     = chip
        self.tcp_port = tcp_port
        self.com_port = com_port
        self._stop    = threading.Event()
        self._sock: Optional[socket.socket] = None
        self._serial  = None
        self._t_tcp2com: Optional[threading.Thread] = None
        self._t_com2tcp: Optional[threading.Thread] = None

    def start(self) -> bool:
        try:
            import serial  # type: ignore
        except ImportError:
            log.error("pyserial not installed — pip install pyserial")
            return False

        try:
            self._serial = serial.Serial(
                self.com_port,
                baudrate=115200,
                timeout=0.05,
            )
        except Exception as exc:
            log.error("failed to open %s: %s", self.com_port, exc)
            return False

        try:
            self._sock = socket.create_connection(("localhost", self.tcp_port),
                                                   timeout=2.0)
            self._sock.settimeout(0.05)
        except Exception as exc:
            log.error("failed to connect to localhost:%d: %s",
                      self.tcp_port, exc)
            self._serial.close()
            return False

        self._stop.clear()
        self._t_tcp2com = threading.Thread(
            target=self._tcp_to_com, daemon=True,
            name=f"uart-bridge-{self.chip}-tcp2com")
        self._t_com2tcp = threading.Thread(
            target=self._com_to_tcp, daemon=True,
            name=f"uart-bridge-{self.chip}-com2tcp")
        self._t_tcp2com.start()
        self._t_com2tcp.start()
        log.info("bridge started: %s TCP:%d <-> %s",
                 self.chip, self.tcp_port, self.com_port)
        return True

    def stop(self) -> None:
        self._stop.set()
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
        if self._t_tcp2com:
            self._t_tcp2com.join(timeout=1.0)
        if self._t_com2tcp:
            self._t_com2tcp.join(timeout=1.0)
        log.info("bridge stopped: %s", self.chip)

    def _tcp_to_com(self) -> None:
        while not self._stop.is_set():
            try:
                data = self._sock.recv(BUFFER_SIZE)
                if not data:
                    break
                self._serial.write(data)
            except socket.timeout:
                continue
            except Exception as exc:
                if not self._stop.is_set():
                    log.warning("tcp→com error on %s: %s", self.chip, exc)
                break

    def _com_to_tcp(self) -> None:
        while not self._stop.is_set():
            try:
                data = self._serial.read(BUFFER_SIZE)
                if data:
                    self._sock.sendall(data)
            except Exception as exc:
                if not self._stop.is_set():
                    log.warning("com→tcp error on %s: %s", self.chip, exc)
                break


class UartBridgeManager:
    """Manages COM port bridges for all chips."""

    def __init__(self) -> None:
        self._bridges: dict[str, ChipBridge] = {}

    def start(self, mapping: dict[str, str]) -> dict[str, bool]:
        """Start bridges for the given chip→COM mapping.
        Returns dict of chip→success."""
        self.stop()
        results: dict[str, bool] = {}
        for chip, com in mapping.items():
            if not com or com == "-":
                continue
            idx = CHIP_ORDER.index(chip) if chip in CHIP_ORDER else -1
            if idx < 0:
                log.warning("unknown chip: %s", chip)
                continue
            bridge = ChipBridge(chip, BASE_PORT + idx, com)
            ok = bridge.start()
            if ok:
                self._bridges[chip] = bridge
            results[chip] = ok
        return results

    def stop(self) -> None:
        for bridge in self._bridges.values():
            bridge.stop()
        self._bridges.clear()

    def start_chip(self, chip: str, com: str) -> bool:
        """Start one chip's bridge (replacing any existing one for that chip)."""
        if chip not in CHIP_ORDER:
            log.warning("unknown chip: %s", chip)
            return False
        if chip in self._bridges:
            self._bridges[chip].stop()
            del self._bridges[chip]
        if not com or com == "-":
            return False
        bridge = ChipBridge(chip, BASE_PORT + CHIP_ORDER.index(chip), com)
        if bridge.start():
            self._bridges[chip] = bridge
            return True
        return False

    def stop_chip(self, chip: str) -> None:
        """Stop one chip's bridge and forget it."""
        bridge = self._bridges.pop(chip, None)
        if bridge:
            bridge.stop()

    @property
    def active(self) -> dict[str, str]:
        """chip → COM port for currently running bridges."""
        return {chip: b.com_port for chip, b in self._bridges.items()}


# ---- CLI entry point -------------------------------------------------------

if __name__ == "__main__":
    import sys

    logging.basicConfig(level=logging.INFO,
                        format="%(levelname)s %(name)s: %(message)s")

    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(0)

    mapping: dict[str, str] = {}
    for i, com in enumerate(args):
        if i >= len(CHIP_ORDER):
            break
        if com != "-":
            mapping[CHIP_ORDER[i]] = com

    mgr = UartBridgeManager()
    results = mgr.start(mapping)
    for chip, ok in results.items():
        status = "OK" if ok else "FAILED"
        print(f"  {chip}: {mapping[chip]} [{status}]")

    if not any(results.values()):
        sys.exit(1)

    print("Bridges running. Press Ctrl-C to stop.")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        mgr.stop()
