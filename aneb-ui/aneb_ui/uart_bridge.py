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
# Connect to the *bridge* port (8700+i), not the flasher port (8600+i).
# The simulator treats bridge-port clients as passive subscribers: no
# DTR-pulse chip_reset on connect, no JSON-event suppression at the
# console.  Flasher port (8600+i) is reserved for avrdude/remote_flasher.
BASE_PORT    = 8700
BUFFER_SIZE  = 256

# How long to wait after the simulator kicks us before trying to reconnect.
# Long enough for a typical avrdude flash session (≈4 s on atmega328p) to
# finish before we'd grab the slot back and disrupt it.  Too short = reconnect
# kicks avrdude mid-flash; too long = the COM port partner stays closed for
# that whole gap (com0com makes any user-side serial tab raise "connection
# closed" when CNCB0 disappears) and Dashboard/Plotter feels frozen after
# every flash.  5 s is comfortable for ATmega328p; if a longer flash kicks
# us mid-write the supervisor's exponential backoff handles it.
RECONNECT_DELAY_S = 5.0


class ChipBridge:
    """Bidirectional bridge between one chip's TCP UART socket and a COM port.

    A supervisor thread tracks the bidirectional worker threads and
    auto-reconnects if they die unexpectedly — e.g. when an external
    avrdude session connects to the simulator's TCP UART and the
    multi-client UART server kicks the existing bridge client to make
    room.  Without auto-reconnect the suppress-UART-JSON flag in the
    engine flips OFF and the UI gets flooded with per-byte UART events.
    """

    def __init__(self, chip: str, tcp_port: int, com_port: str) -> None:
        self.chip     = chip
        self.tcp_port = tcp_port
        self.com_port = com_port
        self._stop    = threading.Event()
        self._sock: Optional[socket.socket] = None
        self._serial  = None
        self._t_tcp2com: Optional[threading.Thread] = None
        self._t_com2tcp: Optional[threading.Thread] = None
        self._supervisor: Optional[threading.Thread] = None

    def start(self) -> bool:
        """Open the bridge and start the supervisor.  Returns False if
        the initial open fails (so the manager can mark this chip dead);
        if a later reconnect fails, the supervisor keeps retrying in the
        background."""
        if not self._open_and_spawn():
            return False
        self._stop.clear()
        self._supervisor = threading.Thread(
            target=self._supervise, daemon=True,
            name=f"uart-bridge-{self.chip}-supervisor")
        self._supervisor.start()
        return True

    def _open_and_spawn(self) -> bool:
        """One-shot: open serial + TCP, spawn the two forwarder threads.
        Returns True if both opens and the spawn succeeded."""
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
            # 127.0.0.1 (not "localhost") so the resolver doesn't try
            # IPv6 first — on Windows the IPv6 attempt can take 1-2 s
            # to fail before falling back, which adds up across five
            # chips and stalls the UI startup.
            self._sock = socket.create_connection(("127.0.0.1", self.tcp_port),
                                                   timeout=1.0)
            self._sock.settimeout(0.05)
        except Exception as exc:
            log.error("failed to connect to localhost:%d: %s",
                      self.tcp_port, exc)
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
            return False

        self._t_tcp2com = threading.Thread(
            target=self._tcp_to_com, daemon=True,
            name=f"uart-bridge-{self.chip}-tcp2com")
        self._t_com2tcp = threading.Thread(
            target=self._com_to_tcp, daemon=True,
            name=f"uart-bridge-{self.chip}-com2tcp")
        self._t_tcp2com.start()
        self._t_com2tcp.start()
        log.info("bridge connected: %s TCP:%d <-> %s",
                 self.chip, self.tcp_port, self.com_port)
        return True

    def _supervise(self) -> None:
        """Wait for the workers to die.  If stop wasn't requested, the
        kick came from the simulator (e.g. avrdude grabbed the slot);
        wait out the typical flash session and reopen.  Repeats forever
        until stop() is called.

        Liveness is polled rather than via two sequential join()s: when
        the simulator kicks us, _tcp_to_com sees the closed socket on its
        next recv() and exits, but _com_to_tcp only exits when its
        sendall() fails — which only happens when the firmware actually
        emits a UART byte.  A silent firmware would leave _com_to_tcp
        stuck in serial.read() forever, never giving the supervisor a
        chance to reconnect.  Polling both is_alive() flags every 100 ms
        catches whichever exits first; closing I/O dislodges the other."""
        while not self._stop.is_set():
            while (self._t_tcp2com is not None
                   and self._t_com2tcp is not None
                   and self._t_tcp2com.is_alive()
                   and self._t_com2tcp.is_alive()
                   and not self._stop.is_set()):
                self._stop.wait(0.1)
            if self._stop.is_set():
                return

            log.info("bridge %s: kicked by simulator, reconnecting in %.0fs",
                     self.chip, RECONNECT_DELAY_S)
            # Closing I/O forces the still-alive worker (if any) out of
            # its blocking read.  Join with a short timeout afterwards.
            self._close_io()
            if self._t_tcp2com:
                self._t_tcp2com.join(timeout=2.0)
            if self._t_com2tcp:
                self._t_com2tcp.join(timeout=2.0)

            if self._stop.wait(RECONNECT_DELAY_S):
                return

            # Retry the open in a loop in case the slot is still held
            # (e.g. a longer flash or back-to-back flashes).  Bounded
            # backoff keeps the log quiet without giving up.
            attempt = 0
            while not self._stop.is_set():
                if self._open_and_spawn():
                    log.info("bridge %s: reconnected", self.chip)
                    break
                attempt += 1
                wait = min(RECONNECT_DELAY_S * (1 << min(attempt, 3)), 60.0)
                log.warning("bridge %s: reconnect failed (attempt %d), "
                            "retrying in %.0fs", self.chip, attempt, wait)
                if self._stop.wait(wait):
                    return

    def _close_io(self) -> None:
        """Close the socket + serial port without joining threads.
        Idempotent — safe to call from stop() and the supervisor."""
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

    def stop(self) -> None:
        self._stop.set()
        self._close_io()
        if self._supervisor:
            self._supervisor.join(timeout=3.0)
        if self._t_tcp2com:
            self._t_tcp2com.join(timeout=1.0)
        if self._t_com2tcp:
            self._t_com2tcp.join(timeout=1.0)
        log.info("bridge stopped: %s", self.chip)

    def _tcp_to_com(self) -> None:
        while not self._stop.is_set():
            sock = self._sock
            ser = self._serial
            if sock is None or ser is None:
                # _close_io() — exit quietly.
                break
            try:
                data = sock.recv(BUFFER_SIZE)
                if not data:
                    break
                ser.write(data)
            except socket.timeout:
                continue
            except Exception as exc:
                if not self._stop.is_set() and self._sock is not None:
                    log.warning("tcp→com error on %s: %s", self.chip, exc)
                break

    def _com_to_tcp(self) -> None:
        while not self._stop.is_set():
            ser = self._serial
            sock = self._sock
            if ser is None or sock is None:
                # Supervisor just called _close_io() to dislodge us after
                # the partner worker died — exit quietly, no warning.
                break
            try:
                data = ser.read(BUFFER_SIZE)
                if data:
                    sock.sendall(data)
            except Exception as exc:
                if not self._stop.is_set() and self._serial is not None:
                    log.warning("com→tcp error on %s: %s", self.chip, exc)
                break


class UartBridgeManager:
    """Manages COM port bridges for all chips."""

    def __init__(self) -> None:
        self._bridges: dict[str, ChipBridge] = {}

    def start(self, mapping: dict[str, str]) -> dict[str, bool]:
        """Start bridges for the given chip→COM mapping in parallel.
        Returns dict of chip→success.

        Each ChipBridge.start() opens a serial port + a TCP socket; on
        Windows the serial open can take ~200 ms even for a virtual
        com0com port, and on a slow machine the TCP connect adds more.
        Doing five sequentially would block the GUI thread for several
        seconds at app launch.  Running them in a thread pool drops the
        wall-clock cost to roughly the slowest single bridge."""
        from concurrent.futures import ThreadPoolExecutor

        self.stop()
        # Build the (chip, ChipBridge) work list up-front so we never
        # mutate self._bridges concurrently from worker threads.
        work: list[tuple[str, ChipBridge]] = []
        for chip, com in mapping.items():
            if not com or com == "-":
                continue
            if chip not in CHIP_ORDER:
                log.warning("unknown chip: %s", chip)
                continue
            idx = CHIP_ORDER.index(chip)
            work.append((chip, ChipBridge(chip, BASE_PORT + idx, com)))

        results: dict[str, bool] = {}
        if not work:
            return results

        with ThreadPoolExecutor(max_workers=len(work)) as pool:
            futures = {pool.submit(b.start): (chip, b) for chip, b in work}
            for fut in futures:
                chip, bridge = futures[fut]
                try:
                    ok = fut.result()
                except Exception as exc:
                    log.warning("bridge %s start raised: %s", chip, exc)
                    ok = False
                results[chip] = ok
                if ok:
                    self._bridges[chip] = bridge
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
