# MCP2515 model fidelity

> *Status: M2 + M3 + M4 complete (loopback, multi-node bus, and
> bus-off / error frames). Bit-timing fidelity is the remaining
> deferral, not currently planned for v1.*

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

## Implemented (M3)

- Multi-node single-bus fan-out via `aneb-sim/src/can_bus/`.
- `on_tx` callback wired in NORMAL mode delivers frames to peer
  controllers; `mcp2515_rx_frame` filters and routes inbound.
- Frame counters on the bus (broadcast / delivered / injected).
- External injection via `can_bus_inject` (no source skipped) — used
  by the UI / scenario player and the `can_inject` JSON command.

## Implemented (M4)

- TEC / REC counters with auto-decrement on successful TX/RX.
- EFLG bit derivation: EWARN, RXWAR, TXWAR, RXEP, TXEP, TXBO, with
  RX0OVR / RX1OVR sticky across recomputations.
- Error states: error-active → error-passive (TEC ≥ 128 || REC ≥ 128)
  → bus-off (TEC ≥ 256, modeled via the saturated value 255).
- ERRIF + MERRF flag handling; INT pin re-evaluates after every
  error event.
- Bus-off TX gating (TXREQ stays pending) and RX gating (no inbound
  frames accepted while bus-off).
- Bus-off recovery via firmware-driven CANCTRL mode toggle (clears
  TEC / REC / TXBO automatically).
- Pedagogical helpers: `mcp2515_inject_tx_errors`,
  `mcp2515_inject_rx_errors`, `mcp2515_force_busoff`,
  `mcp2515_force_error_passive`, `mcp2515_recover_busoff`.

## Out of scope (v1, may add later if a lab requires)

The 128 × 11 recessive-bit auto-recovery requires per-bit time
modeling, which we do not do (frame delivery is logical, not
bit-timed). The firmware-driven recovery path (mode toggle through
Configuration) is fully supported and is the path drivers like
`MCP_CAN_lib` use anyway.

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
