"""
plot_buffers — bounded rolling-window time-series store for the plotter.

Lives between SimState and the Qt UI. A periodic tick samples the
latest known value of each subscribed signal from a SimState snapshot
and appends it to a per-(chip, signal) ring buffer. The plotter QML
reads the buffer via QmlBridge.plotSeries(chip, signal).

This file is the data spine. It does NOT depend on Qt at runtime —
the sampling tick is a plain method (`tick()`). The Qt integration
layer in qml_bridge.py wraps it in a QTimer.

Design notes:

- Sampling, not event-driven. Events fire 100s-1000s/sec when
  firmware is busy; sampling at a fixed cadence (20 Hz here) gives
  bounded UI work regardless of firmware activity. This is the same
  decoupling that fixed the M6 pin-event flood.
- Buffer length is fixed (`WINDOW_S * SAMPLE_HZ` points/trace), so
  memory is bounded.
- Timestamps are seconds relative to the time of the LATEST tick,
  so QML can plot t-now on the X axis and the window slides left
  naturally.
"""
from __future__ import annotations

import time
from collections import deque
from typing import Any, Iterable, Optional


# Default plotter parameters — tuned for "teaching plotter" use, not
# for catching microsecond glitches.
SAMPLE_HZ = 20             # tick frequency
WINDOW_S  = 10.0           # rolling window length in seconds
BUF_LEN   = int(SAMPLE_HZ * WINDOW_S)   # 200 points / trace


# ---- signal descriptors ---------------------------------------------------
#
# A Signal is a (kind, name) pair that the buffer looks up from a
# SimState-shaped object. Kinds:
#
#   "pin": digital level, 0 or 1, looked up via state._pins[chip][name]
#   "pwm": duty 0.0..1.0,           state._pwm[chip][name]
#   "adc": ADC counts 0..1023,      state._adc[chip][int(name)]
#
# `name` for "pin"/"pwm" is the canonical port form ("PB5", "PD3"). For
# "adc" it is the channel index as a string ("0".."7"); int(name) is
# used when reading the dict.

class Signal:
    __slots__ = ("kind", "name")

    def __init__(self, kind: str, name: str) -> None:
        self.kind = kind
        self.name = name

    def __eq__(self, other: object) -> bool:
        return (isinstance(other, Signal)
                and self.kind == other.kind
                and self.name == other.name)

    def __hash__(self) -> int:
        return hash((self.kind, self.name))

    def __repr__(self) -> str:
        return f"Signal({self.kind!r}, {self.name!r})"

    @property
    def key(self) -> str:
        """Stable string key for QML / serialization (e.g. "pwm:PD6")."""
        return f"{self.kind}:{self.name}"


# Light protocol the buffer expects; SimState satisfies this naturally
# but we don't `isinstance`-check, so tests can pass a fake.
class _StateLike:
    _pins: dict[str, dict[str, int]]
    _pwm:  dict[str, dict[str, float]]
    _adc:  dict[str, dict[int, int]]


# ---- the buffer ----------------------------------------------------------

class PlotBuffers:
    """Per-(chip, signal) rolling time-series buffers.

    All methods are pure-Python; no Qt. Wrap `tick()` in a QTimer for
    live use (see qml_bridge.QmlBridge), or call it directly from
    tests.
    """

    def __init__(self,
                 sample_hz: int = SAMPLE_HZ,
                 window_s:  float = WINDOW_S) -> None:
        self.sample_hz = sample_hz
        self.window_s  = window_s
        self.buf_len   = int(sample_hz * window_s)
        # _data[chip][signal_key] -> deque[(t_seconds, value)]
        self._data: dict[str, dict[str, deque]] = {}
        # When the chart wants relative time, we hand it (t - now). The
        # tick() method records `now` at the end so series() can use it.
        self._last_tick_t: float = 0.0
        # Time origin chosen at construction so timestamps are small.
        self._t0: float = time.monotonic()

    # ---- subscription ---------------------------------------------------

    def _slot(self, chip: str, sig: Signal) -> deque:
        chip_d = self._data.setdefault(chip, {})
        slot = chip_d.get(sig.key)
        if slot is None:
            slot = deque(maxlen=self.buf_len)
            chip_d[sig.key] = slot
        return slot

    # ---- sampling -------------------------------------------------------

    def tick(self, state: _StateLike, signals: Iterable[tuple[str, Signal]],
             now: Optional[float] = None) -> None:
        """Take one snapshot of every (chip, signal) in `signals`.

        `signals` is an iterable of (chip, Signal) pairs. The same
        signal can be requested for multiple chips; chips outside the
        roster show up as missing in `state` and yield 0.0.

        `now` lets tests inject a deterministic timestamp; production
        callers leave it None (uses monotonic clock).
        """
        t = (now if now is not None else time.monotonic()) - self._t0
        for chip, sig in signals:
            v = self._read(state, chip, sig)
            self._slot(chip, sig).append((t, v))
        self._last_tick_t = t

    @staticmethod
    def _read(state: _StateLike, chip: str, sig: Signal) -> float:
        if sig.kind == "pin":
            return float(getattr(state, "_pins", {}).get(chip, {}).get(sig.name, 0))
        if sig.kind == "pwm":
            return float(getattr(state, "_pwm",  {}).get(chip, {}).get(sig.name, 0.0))
        if sig.kind == "adc":
            try:
                ch = int(sig.name)
            except ValueError:
                return 0.0
            return float(getattr(state, "_adc",  {}).get(chip, {}).get(ch, 0))
        return 0.0

    # ---- export ---------------------------------------------------------

    def series(self, chip: str, sig: Signal) -> list[tuple[float, float]]:
        """Return the current buffer as a copy of (t_relative, value).

        `t_relative` is `sample_t - last_tick_t`, so the most recent
        sample sits at 0 and older samples are negative — feeding
        directly into a left-sliding chart.
        """
        slot = self._data.get(chip, {}).get(sig.key)
        if not slot:
            return []
        ref = self._last_tick_t
        return [(t - ref, v) for (t, v) in slot]

    def latest(self, chip: str, sig: Signal) -> Optional[float]:
        """Most recent sample value, or None if no samples yet."""
        slot = self._data.get(chip, {}).get(sig.key)
        if not slot:
            return None
        return slot[-1][1]

    def clear(self, chip: Optional[str] = None) -> None:
        """Drop all stored samples, optionally only for one chip."""
        if chip is None:
            self._data.clear()
        else:
            self._data.pop(chip, None)
