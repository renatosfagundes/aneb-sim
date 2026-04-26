#!/usr/bin/env python3
"""
M4 bus-off smoke harness.

Drives the engine through the new error commands:
  - force_busoff -> can_state {state: bus-off}
  - can_recover  -> can_state {state: active, tec: 0}
  - can_errors   -> can_state with non-zero counters and matching state

No firmware needed; this exercises the model + JSON wiring.
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
        expect(events,
               lambda e: e.get("t") == "log" and "ready" in e.get("msg", ""),
               "engine ready")

        # 1. Force ECU2 into bus-off; expect a can_state event.
        send(proc, {"v": 1, "c": "force_busoff", "chip": "ecu2"})
        st = expect(events,
                    lambda e: e.get("t") == "can_state"
                              and e.get("chip") == "ecu2",
                    "can_state after force_busoff")
        assert st["state"] == "bus-off", f"expected bus-off, got {st!r}"
        assert st["tec"] == 255, f"expected TEC=255, got {st!r}"
        print(f"  ok: bus-off -> {st}", file=sys.stderr)

        # 2. Recover. State should return to active with TEC=0.
        send(proc, {"v": 1, "c": "can_recover", "chip": "ecu2"})
        st = expect(events,
                    lambda e: e.get("t") == "can_state"
                              and e.get("chip") == "ecu2",
                    "can_state after can_recover")
        assert st["state"] == "active", f"expected active, got {st!r}"
        assert st["tec"] == 0 and st["rec"] == 0, f"counters not cleared: {st!r}"
        print(f"  ok: recovered -> {st}", file=sys.stderr)

        # 3. Inject 12 TX errors (TEC = 96 -> warning level, but state
        #    should still be active since 96 < 128).
        send(proc, {"v": 1, "c": "can_errors", "chip": "ecu1", "tx": 12})
        st = expect(events,
                    lambda e: e.get("t") == "can_state"
                              and e.get("chip") == "ecu1",
                    "can_state after can_errors warning")
        assert st["state"] == "active", f"warning level should stay active, got {st!r}"
        assert st["tec"] == 96, f"expected TEC=96, got {st!r}"
        print(f"  ok: warning -> {st}", file=sys.stderr)

        # 4. Bump 4 more TX errors (+32 = TEC 128) -> error-passive.
        send(proc, {"v": 1, "c": "can_errors", "chip": "ecu1", "tx": 4})
        st = expect(events,
                    lambda e: e.get("t") == "can_state"
                              and e.get("chip") == "ecu1",
                    "can_state after error-passive crossing")
        assert st["state"] == "passive", f"expected passive, got {st!r}"
        assert st["tec"] == 128, f"expected TEC=128, got {st!r}"
        print(f"  ok: error-passive -> {st}", file=sys.stderr)

        # 5. Force_busoff on a chip without an MCP2515 (mcu) — warn, no crash.
        send(proc, {"v": 1, "c": "force_busoff", "chip": "mcu"})
        warn = expect(events,
                      lambda e: e.get("t") == "log"
                                and e.get("level") == "warn"
                                and "no MCP2515" in e.get("msg", ""),
                      "warn on mcu force_busoff")
        print(f"  ok: warn on mcu -> {warn['msg']}", file=sys.stderr)

        print("M4 BUS-OFF SMOKE PASS", file=sys.stderr)
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
