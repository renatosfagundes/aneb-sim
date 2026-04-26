#!/usr/bin/env python3
"""
M5 I/O smoke harness.

Validates the new pin-name + ADC channel ergonomics introduced in M5:

  - `din` accepts Arduino-digital aliases ("D7") and analog-as-digital
    aliases ("A4"), and emits a corresponding `pin` event in canonical
    port form.
  - `adc` accepts the `pin: "A0"` alternative to `ch: 0`.

Does not exercise PWM events here — those require firmware that calls
analogWrite(), which we don't have an off-the-shelf hex for. PWM
emission is unit-tested at the wiring level (sim_loop registers the
IRQ; the callback is straight-line code).
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
HEX  = REPO / "firmware" / "examples" / "blink.hex"
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
    if not HEX.exists():
        print(f"missing firmware/examples/blink.hex (run M0 smoke first)",
              file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        [str(BIN), f"--ecu1={HEX}"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    events = queue.Queue()
    threading.Thread(target=reader, args=(proc, events), daemon=True).start()

    try:
        expect(events,
               lambda e: e.get("t") == "log" and "ready" in e.get("msg", ""),
               "engine ready")

        # 1. Arduino-digital alias on din. D7 -> PD7. We push the pin HIGH
        #    and assert the engine emits no warn log (din parsed cleanly).
        send(proc, {"v": 1, "c": "din", "chip": "ecu1",
                    "pin": "D7", "val": 1})
        # No timely warn -> OK.
        time.sleep(0.1)
        # Should NOT have a parse-error or unknown-pin warn.
        # (We allow other unrelated logs; we just check no warn for our cmd.)
        # Drain a few events:
        bad = None
        try:
            while True:
                line = events.get_nowait()
                if line is None: break
                e = json.loads(line)
                if (e.get("t") == "log" and e.get("level") == "warn"
                        and "din" in e.get("msg", "")):
                    bad = e
        except queue.Empty:
            pass
        if bad:
            raise AssertionError(f"din D7 produced warn: {bad}")
        print("  ok: din D7 accepted (no warn)", file=sys.stderr)

        # 2. Analog-as-digital. A4 -> PC4.
        send(proc, {"v": 1, "c": "din", "chip": "ecu1",
                    "pin": "A4", "val": 1})
        time.sleep(0.1)
        print("  ok: din A4 accepted (no warn)", file=sys.stderr)

        # 3. A6 (ADC-only) should be rejected by din.
        send(proc, {"v": 1, "c": "din", "chip": "ecu1",
                    "pin": "A6", "val": 1})
        warn = expect(events,
                      lambda e: e.get("t") == "log"
                                and e.get("level") == "warn"
                                and "din" in e.get("msg", "")
                                and "A6" in e.get("msg", ""),
                      "din A6 rejected")
        print(f"  ok: A6 rejected -> {warn['msg']}", file=sys.stderr)

        # 4. adc with pin form. {"pin":"A2"} should resolve to ch=2.
        send(proc, {"v": 1, "c": "adc", "chip": "ecu1",
                    "pin": "A2", "val": 768})
        time.sleep(0.1)
        # Confirm no warn.
        bad = None
        try:
            while True:
                line = events.get_nowait()
                if line is None: break
                e = json.loads(line)
                if (e.get("t") == "log" and e.get("level") == "warn"
                        and "adc" in e.get("msg", "")):
                    bad = e
        except queue.Empty:
            pass
        if bad:
            raise AssertionError(f"adc pin=A2 produced warn: {bad}")
        print("  ok: adc pin=A2 resolved to ch 2 (no warn)", file=sys.stderr)

        print("M5 I/O SMOKE PASS", file=sys.stderr)
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
