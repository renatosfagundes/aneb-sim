#!/usr/bin/env python3
"""
M3 CAN smoke harness — drives aneb-sim through stdio, exercises the
can_inject command and verifies that the engine accepts the frame, logs
the bus init, and survives a few injections without crashing.

This does NOT require any CAN-using firmware to be loaded. The exit
criterion for *firmware-level* CAN traffic (an ECU running a TX sketch
talking to another ECU running an RX sketch) requires CAN-using hex
files; once those are available, a separate M3 firmware-smoke can
extend this harness.

Run from the repo root:
    python tests/m3_can_smoke.py
"""
from __future__ import annotations

import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BIN  = REPO / "build" / "aneb-sim" / "aneb-sim.exe"
TIMEOUT_S = 10.0


def reader(proc, q):
    for line in proc.stdout:
        q.put(line.rstrip("\n"))
    q.put(None)


def expect(events, predicate, label, timeout=TIMEOUT_S):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            line = events.get(timeout=0.5)
        except queue.Empty:
            continue
        if line is None:
            raise AssertionError(f"{label}: engine closed stdout")
        try:
            evt = json.loads(line)
        except json.JSONDecodeError:
            continue
        if predicate(evt):
            return evt
    raise AssertionError(f"{label}: timed out after {timeout}s")


def send(proc, obj):
    proc.stdin.write(json.dumps(obj) + "\n")
    proc.stdin.flush()


def main():
    if not BIN.exists():
        print(f"engine not built: {BIN}", file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        [str(BIN)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    events = queue.Queue()
    threading.Thread(target=reader, args=(proc, events), daemon=True).start()

    try:
        # 1. Engine starts and announces the bus.
        ready = expect(events,
                       lambda e: e.get("t") == "log"
                                 and "bus 'can1'" in e.get("msg", ""),
                       "bus initialized")
        print(f"  ok: bus initialized -> {ready['msg']}", file=sys.stderr)

        # 2. Inject a frame. Engine must accept silently (or at most a
        #    log) and not crash.
        send(proc, {
            "v": 1, "c": "can_inject",
            "bus": "can1",
            "id": "0x123",
            "ext": False, "rtr": False,
            "dlc": 2, "data": "CAFE",
        })
        time.sleep(0.1)

        # 3. Inject malformed hex — should produce a warn log, not crash.
        send(proc, {
            "v": 1, "c": "can_inject",
            "id": "0x456",
            "data": "DEADZZ",
        })
        warn = expect(events,
                      lambda e: e.get("t") == "log"
                                and e.get("level") == "warn"
                                and "can_inject" in e.get("msg", ""),
                      "malformed hex warn")
        print(f"  ok: malformed hex rejected -> {warn['msg']}", file=sys.stderr)

        # 4. Inject targeting a nonexistent bus.
        send(proc, {
            "v": 1, "c": "can_inject", "bus": "imaginary",
            "id": 0x100, "dlc": 0,
        })
        warn = expect(events,
                      lambda e: e.get("t") == "log"
                                and e.get("level") == "warn"
                                and "unknown bus" in e.get("msg", ""),
                      "unknown bus warn")
        print(f"  ok: unknown bus rejected -> {warn['msg']}", file=sys.stderr)

        print("M3 CAN SMOKE PASS", file=sys.stderr)
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
