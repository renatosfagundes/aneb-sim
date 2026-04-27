"""
Pure-logic tests for the plotter's rolling-window time-series buffer.
No Qt event loop — the buffer's `tick()` method is plain-Python and
takes its timestamp as a parameter.
"""
from __future__ import annotations

from aneb_ui.plot_buffers import (BUF_LEN, PlotBuffers, SAMPLE_HZ,
                                  Signal, WINDOW_S)


# ----- helpers -----------------------------------------------------------

class FakeState:
    """SimState shape, just the bits the buffer reads."""
    def __init__(self) -> None:
        self._pins: dict[str, dict[str, int]] = {}
        self._pwm:  dict[str, dict[str, float]] = {}
        self._adc:  dict[str, dict[int, int]] = {}


# ----- bounded buffer ----------------------------------------------------

def test_default_constants_match_design():
    assert SAMPLE_HZ == 20
    assert WINDOW_S  == 10.0
    assert BUF_LEN   == 200


def test_buffer_capacity_is_bounded():
    buf = PlotBuffers(sample_hz=10, window_s=2.0)   # 20 samples max
    state = FakeState()
    state._adc["ecu1"] = {0: 100}
    sig = Signal("adc", "0")
    for i in range(500):
        buf.tick(state, [("ecu1", sig)], now=float(i) * 0.05)
    series = buf.series("ecu1", sig)
    assert len(series) == 20, "buffer should clamp to capacity"


def test_rolling_window_uses_relative_timestamps():
    buf = PlotBuffers(sample_hz=10, window_s=2.0)
    state = FakeState()
    state._adc["ecu1"] = {0: 42}
    sig = Signal("adc", "0")
    for i in range(20):
        buf.tick(state, [("ecu1", sig)], now=float(i) * 0.1)
    series = buf.series("ecu1", sig)
    # Most recent sample should be at t=0 (relative to last tick).
    assert series[-1][0] == 0.0
    # Oldest retained sample is one window back, ~ -1.9 s.
    assert -2.0 <= series[0][0] <= -1.8
    assert all(v == 42.0 for (_, v) in series)


# ----- multi-chip / multi-signal isolation -------------------------------

def test_chips_and_signals_isolated():
    buf = PlotBuffers()
    state = FakeState()
    state._pwm["ecu1"] = {"PD6": 0.50}
    state._pwm["ecu2"] = {"PD6": 0.10}
    state._pwm["ecu1"]["PD3"] = 0.99
    sig_pd6 = Signal("pwm", "PD6")
    sig_pd3 = Signal("pwm", "PD3")
    buf.tick(state, [
        ("ecu1", sig_pd6),
        ("ecu2", sig_pd6),
        ("ecu1", sig_pd3),
    ], now=1.0)
    assert buf.latest("ecu1", sig_pd6) == 0.50
    assert buf.latest("ecu2", sig_pd6) == 0.10
    assert buf.latest("ecu1", sig_pd3) == 0.99
    # ecu2 has no PD3 entry — should read as missing -> 0.0.
    assert buf.latest("ecu2", sig_pd3) is None
    # Until tick'd; tick it now.
    buf.tick(state, [("ecu2", sig_pd3)], now=1.05)
    assert buf.latest("ecu2", sig_pd3) == 0.0


# ----- read-from-state semantics -----------------------------------------

def test_pin_signal_reads_from_pins_dict():
    buf = PlotBuffers()
    state = FakeState()
    state._pins["ecu1"] = {"PB5": 1}
    buf.tick(state, [("ecu1", Signal("pin", "PB5"))], now=0.0)
    assert buf.latest("ecu1", Signal("pin", "PB5")) == 1.0


def test_pwm_signal_reads_from_pwm_dict():
    buf = PlotBuffers()
    state = FakeState()
    state._pwm["ecu1"] = {"PD3": 0.42}
    buf.tick(state, [("ecu1", Signal("pwm", "PD3"))], now=0.0)
    assert buf.latest("ecu1", Signal("pwm", "PD3")) == 0.42


def test_adc_signal_reads_from_adc_dict_by_channel():
    buf = PlotBuffers()
    state = FakeState()
    state._adc["ecu1"] = {0: 768, 3: 121}
    buf.tick(state, [
        ("ecu1", Signal("adc", "0")),
        ("ecu1", Signal("adc", "3")),
    ], now=0.0)
    assert buf.latest("ecu1", Signal("adc", "0")) == 768.0
    assert buf.latest("ecu1", Signal("adc", "3")) == 121.0


def test_missing_signal_yields_zero_not_crash():
    """Subscribing to something the chip has never reported is fine."""
    buf = PlotBuffers()
    state = FakeState()      # totally empty
    sig = Signal("pwm", "PD6")
    buf.tick(state, [("ecu3", sig)], now=0.0)
    assert buf.latest("ecu3", sig) == 0.0


# ----- export shape ------------------------------------------------------

def test_series_returns_pairs_in_chronological_order():
    buf = PlotBuffers(sample_hz=10, window_s=1.0)
    state = FakeState()
    for i, v in enumerate([10, 20, 30, 40, 50]):
        state._adc["ecu1"] = {0: v}
        buf.tick(state, [("ecu1", Signal("adc", "0"))], now=float(i) * 0.1)
    series = buf.series("ecu1", Signal("adc", "0"))
    assert [v for (_, v) in series] == [10.0, 20.0, 30.0, 40.0, 50.0]
    # Strictly non-decreasing relative timestamps (most recent at 0).
    times = [t for (t, _) in series]
    assert times == sorted(times)
    assert times[-1] == 0.0


def test_series_empty_when_unsubscribed():
    buf = PlotBuffers()
    assert buf.series("ecu1", Signal("adc", "0")) == []


# ----- clear -------------------------------------------------------------

def test_clear_drops_all_buffers():
    buf = PlotBuffers()
    state = FakeState()
    state._adc["ecu1"] = {0: 1}; state._adc["ecu2"] = {0: 2}
    sig = Signal("adc", "0")
    buf.tick(state, [("ecu1", sig), ("ecu2", sig)], now=0.0)
    buf.clear()
    assert buf.series("ecu1", sig) == []
    assert buf.series("ecu2", sig) == []


def test_clear_per_chip_only_drops_that_chip():
    buf = PlotBuffers()
    state = FakeState()
    state._adc["ecu1"] = {0: 1}; state._adc["ecu2"] = {0: 2}
    sig = Signal("adc", "0")
    buf.tick(state, [("ecu1", sig), ("ecu2", sig)], now=0.0)
    buf.clear(chip="ecu1")
    assert buf.series("ecu1", sig) == []
    assert buf.latest("ecu2", sig) == 2.0
