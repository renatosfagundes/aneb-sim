# MCP2515 model fidelity

> *Status: stub. Filled in during M2.*

## In scope (M2–M4)

- Full register map (~70 registers)
- SPI command set: RESET, READ, WRITE, BIT MODIFY, RTS, READ STATUS,
  RX STATUS, READ RX BUFFER, LOAD TX BUFFER
- TX buffers (3) and RX buffers (2) with priority and rollover
- RXFn/RXMn acceptance filters and masks
- Modes: Configuration, Normal, Loopback, Listen-only, Sleep
- INT pin (active-low), wired to AVR INT0
- TEC/REC error counters, EFLG register
- Mode transitions: error-active → error-passive (TEC/REC ≥ 128) → bus-off (TEC ≥ 256)
- Interrupt flags: ERRIF, MERRF, RX0IF, RX1IF, TX0IF, TX1IF, TX2IF
- Error injection via UI commands (CRC, form, stuff, ack)
- Bus-off recovery (128 × 11 recessive bits, or mode toggle)

## Out of scope (v1)

- Bit-time fidelity (CNF1 / CNF2 / CNF3 are accepted but not enforced —
  frames are delivered based on logical ordering, not physical bit timing)
- One-shot mode
- CLKOUT / SOF pin
- RXnBF / TXnRTS pins (can add if a lab needs them)
- Wake-up from sleep on bus activity (sleep is implemented as
  "no TX, no RX" but does not auto-wake on bus traffic)
