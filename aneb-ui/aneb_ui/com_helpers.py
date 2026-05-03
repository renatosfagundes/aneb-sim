"""
com_helpers — discover aneb-sim COM ports without relying on
serial.tools.list_ports.

Pete Batard's signed com0com INF (the only one trusted by Windows
10/11 out of the box) registers the driver in the private CNCPorts
device class, not the standard Ports class.  pyserial's
`list_ports.comports()` filters strictly to Ports, so com0com
devices don't appear there even though they're fully usable serial
ports.  WMI's `Win32_SerialPort` is broader and does enumerate them,
so we use that instead.

Typical use from remote_flasher or similar:

    from aneb_ui.com_helpers import find_aneb_ports

    ports = find_aneb_ports()
    # {"ecu1": "COM11", "ecu2": "COM13", ..., "mcu": "COM19"}

    import serial
    s = serial.Serial(ports["ecu1"], 115200)
    s.write(b"hello\\n")
"""
from __future__ import annotations

import logging
import re
import sys

log = logging.getLogger(__name__)

# Order in which chips were assigned by scripts/setup_com.ps1 — matches
# qml_bridge.py's _CHIP_COM_USER mapping.
ANEB_CHIPS = ("ecu1", "ecu2", "ecu3", "ecu4", "mcu")


def _wmi_serial_ports() -> list[tuple[str, str]]:
    """Return [(device_id, description), ...] for every Win32_SerialPort.
    Empty list on non-Windows or if WMI access fails."""
    if sys.platform != "win32":
        return []
    try:
        import wmi  # type: ignore
    except ImportError:
        log.warning("wmi package not installed; pip install wmi")
        return []

    try:
        ports = []
        for p in wmi.WMI().Win32_SerialPort():
            ports.append((p.DeviceID or "", p.Description or p.Caption or ""))
        return ports
    except Exception as exc:
        log.warning("WMI query failed: %s", exc)
        return []


def _registry_friendly_names() -> dict[str, str]:
    """Read FriendlyName for each com0com port directly from the
    registry (Win32_SerialPort.Description doesn't pick up the
    FriendlyName override our setup script writes — it shows the
    INF's generic 'com0com - serial port emulator' instead).

    Returns {com_port: friendly_name}, e.g. {'COM11': 'ECU1 (aneb-sim)'}.
    """
    if sys.platform != "win32":
        return {}
    try:
        import winreg  # type: ignore
    except ImportError:
        return {}

    out: dict[str, str] = {}
    base = r"SYSTEM\CurrentControlSet\Enum\COM0COM\PORT"
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base) as root:
            i = 0
            while True:
                try:
                    name = winreg.EnumKey(root, i)
                except OSError:
                    break
                i += 1
                try:
                    with winreg.OpenKey(root, name) as port:
                        friendly, _ = winreg.QueryValueEx(port, "FriendlyName")
                        # The actual COM number is in a Device Parameters
                        # subkey, NOT next to FriendlyName.
                        try:
                            with winreg.OpenKey(port, "Device Parameters") as dp:
                                com, _ = winreg.QueryValueEx(dp, "PortName")
                                out[com] = friendly
                        except OSError:
                            pass
                except OSError:
                    continue
    except OSError:
        pass
    return out


def find_aneb_ports() -> dict[str, str]:
    """Map chip name -> user-side COM port for every aneb-sim port
    currently installed via scripts/setup_com.ps1.

    Returns e.g. ``{"ecu1": "COM11", "ecu2": "COM13", ...}``.
    Empty dict if none are installed, if WMI/winreg are unavailable,
    or if the FriendlyName override hasn't been applied (e.g. the
    setup script was never run).

    Bridge-side ports (CNCAn / "(aneb-sim bridge)") are deliberately
    excluded — those are the ports the aneb-sim UI itself opens, not
    the ones a user-facing tool should attach to.
    """
    out: dict[str, str] = {}
    friendlies = _registry_friendly_names()
    for com, name in friendlies.items():
        # Match "ECU1 (aneb-sim)" but NOT "ECU1 (aneb-sim bridge)".
        m = re.match(r"^([A-Za-z0-9]+)\s+\(aneb-sim\)\s*$", name)
        if m:
            out[m.group(1).lower()] = com
    return out


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO,
                        format="%(levelname)s %(name)s: %(message)s")
    ports = find_aneb_ports()
    if not ports:
        print("No aneb-sim COM ports found.")
        print("Run scripts\\setup_com.bat (or the 'Setup COM ports...' button")
        print("in the aneb-sim UI toolbar) to install them.")
        sys.exit(1)
    for chip in ANEB_CHIPS:
        com = ports.get(chip, "(missing)")
        print(f"  {chip.upper():5} -> {com}")
