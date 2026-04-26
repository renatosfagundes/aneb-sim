#!/usr/bin/env python3
"""
M1 smoke harness — drives aneb-sim through stdio, validates the JSON wire
protocol, and exercises a representative subset of commands.

Run from the repo root:
    python tests/m1_smoke.py firmware/examples/blink.hex

Exit code 0 on pass, non-zero on failure. All assertions log to stderr.
"""
from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO  = Path(__file__).resolve().parent.parent
BIN   = REPO / "build" / "aneb-sim" / "aneb-sim.exe"
TIMEOUT_S = 10.0


def reader(proc, q):
    for line in proc.stdout:
        q.put(line.rstrip("\n"))
    q.put(None)  # sentinel


def expect(events: queue.Queue, predicate, label, timeout=TIMEOUT_S):
    """Drain events until predicate(evt) is True. Returns the matched event."""
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
            print(f"  [non-JSON line] {line!r}", file=sys.stderr)
            continue
        if predicate(evt):
            return evt
    raise AssertionError(f"{label}: timed out after {timeout}s")


def send(proc, obj):
    line = json.dumps(obj) + "\n"
    proc.stdin.write(line)
    proc.stdin.flush()


def main():
    if len(sys.argv) < 2:
        print("usage: m1_smoke.py <ecu1.hex>", file=sys.stderr)
        return 2

    hex_path = sys.argv[1]
    if not Path(hex_path).is_file():
        print(f"no such hex: {hex_path}", file=sys.stderr)
        return 2
    if not BIN.exists():
        print(f"engine not built: {BIN}", file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        [str(BIN), f"--ecu1={hex_path}"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    events = queue.Queue()
    threading.Thread(target=reader, args=(proc, events), daemon=True).start()

    try:
        # 1. Engine should announce ready with a log event.
        ready = expect(events,
                       lambda e: e.get("t") == "log" and "ready" in e.get("msg", ""),
                       "engine ready")
        print(f"  ok: ready -> {ready['msg']}", file=sys.stderr)

        # 2. ecu1 should produce a pin event from the loaded blink firmware
        #    (Arduino LED_BUILTIN = PB5).
        pin_evt = expect(events,
                         lambda e: e.get("t") == "pin"
                                   and e.get("chip") == "ecu1"
                                   and e.get("pin") == "PB5",
                         "ecu1 PB5 toggled")
        print(f"  ok: pin event -> {pin_evt}", file=sys.stderr)

        # 3. Load command targeting ecu2 with the same hex — engine should
        #    log success and then ecu2 should emit its own PB5 pin event.
        send(proc, {"v": 1, "c": "load", "chip": "ecu2", "path": hex_path})
        ecu2_evt = expect(events,
                          lambda e: e.get("t") == "pin"
                                    and e.get("chip") == "ecu2"
                                    and e.get("pin") == "PB5",
                          "ecu2 PB5 toggled after load")
        print(f"  ok: ecu2 ran after load -> {ecu2_evt}", file=sys.stderr)

        # 4. Pause / resume sanity — engine accepts and does not crash.
        send(proc, {"v": 1, "c": "pause"})
        send(proc, {"v": 1, "c": "resume"})
        print("  ok: pause/resume accepted", file=sys.stderr)

        # 5. din command on a chip that does not exist — engine should
        #    emit a warn-level log, not crash.
        send(proc, {"v": 1, "c": "din", "chip": "nope", "pin": "PD2", "val": 1})
        warn = expect(events,
                      lambda e: e.get("t") == "log"
                                and e.get("level") == "warn"
                                and "unknown chip 'nope'" in e.get("msg", ""),
                      "din unknown-chip warn")
        print(f"  ok: warn on unknown chip -> {warn['msg']}", file=sys.stderr)

        print("M1 SMOKE PASS", file=sys.stderr)
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
