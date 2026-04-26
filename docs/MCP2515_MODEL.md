# MCP2515 model fidelity

> *Status: M2 complete (loopback-only). M3 adds bus interaction.
> M4 adds error counters, error frames, and bus-off transitions.*

The model is implemented as a pure-logic module under
[`aneb-sim/src/mcp2515/`](../aneb-sim/src/mcp2515/) with no `simavr`
dependency — it is driven by three external stimuli (CS edges, SPI
bytes, and inbound bus frames) and exposes two outputs via callbacks
(INT pin level changes, outbound bus frames). The simavr glue lives
in [`aneb-sim/src/sim_loop.c`](../aneb-sim/src/sim_loop.c).

Tests link the pure module directly without simavr; see
[`aneb-sim/test/`](../aneb-sim/test/).

## Implemented (M2)

- Full SPI command set:
  - `RESET` (0xC0)
  - `READ` (0x03) with auto-incrementing address
  - `WRITE` (0x02) with auto-incrementing address
  - `BIT MODIFY` (0x05)
  - `READ STATUS` (0xA0) — repeats indefinitely while CS held low
  - `RX STATUS` (0xB0)
  - `LOAD TX BUFFER` (0x40–0x45) — both full and data-only variants
  - `READ RX BUFFER` (0x90, 0x92, 0x94, 0x96) — with auto-clear of RXnIF
  - `RTS` (0x80–0x87) — TX request for any combination of TXB0/1/2
- Register file with reset values and read-only protection (CANSTAT,
  TEC, REC).
- Mode transitions via CANCTRL.REQOP: Configuration / Normal /
  Loopback / Listen-only / Sleep.
- TX/RX buffer state, including RXB0CTRL.BUKT rollover semantics.
- Acceptance filters (RXF0..RXF5) with masks (RXM0, RXM1) for both
  standard and extended frames. Per-buffer RXM modes (filtered /
  std-only / ext-only / receive-any).
- Loopback delivery: TX buffer → filters → RX buffer → CANINTF →
  INT pin.
- Interrupt enable/flag wiring (CANINTE × CANINTF → INT pin).

## Deferred to M3 (CAN bus model)

- `on_tx` callback delivers TX'd frames to a shared bus model.
- Bus arbitration (lower ID wins) and inter-frame spacing.

## Deferred to M4 (errors)

- TEC / REC counters
- EFLG bits and error-frame propagation
- Mode transitions error-active → error-passive → bus-off
- ERRIF / MERRF flags
- Bus-off recovery (128 × 11 recessive bits or mode toggle)

## Out of scope (v1, may add later if a lab requires)

- Bit-timing fidelity (CNF1 / CNF2 / CNF3 are accepted and round-tripped
  but not enforced — frame delivery is logical, not bit-timed)
- One-shot mode
- CLKOUT / SOF pin
- RXnBF / TXnRTS pins
- Wake-up from sleep on bus activity (sleep currently implemented as
  "no TX, no RX"; does not auto-wake on bus traffic)

## Verifying

The test binary `test_mcp2515` runs ~25 unit tests across four suites
(registers, SPI commands, filters, loopback) and is wired into `ctest`:

```bash
./scripts/build.sh
ctest --test-dir build --output-on-failure
```

CI runs the same on every push.
