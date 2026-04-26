# Wire protocol

> *Status: M1. v1 schema. The shape may grow (more event types, more
> command fields) but versioned as `v:1` until breaking changes land.*

JSON Lines (UTF-8, one object per line, terminated `\n`) over **stdio**:

- engine **stdout** → UI input  (events)
- UI **stdin**     → engine input (commands)

Every message carries `"v":1` as the first field. Mismatched versions are
rejected by the engine with a `parse error` log.

---

## Events (engine → UI)

All events carry `"t":"<type>"`. Common fields:

| Field | Type | Meaning |
|---|---|---|
| `v`     | int    | Always `1`. |
| `t`     | string | Event type (see below). |
| `chip`  | string | `"ecu1"…"ecu4"`, `"mcu"`. Omitted for engine-global events. |
| `ts`    | int    | Cycles since start of the chip's most recent firmware load. Monotonic per-chip. |

### `pin` — GPIO digital state change

```json
{"v":1,"t":"pin","chip":"ecu1","pin":"PB5","val":1,"ts":12345}
```

| Field | Type | Notes |
|---|---|---|
| `pin` | string | Always emitted in canonical port form (`"PB5"`, etc.). Inbound commands accept Arduino aliases too — see below. |
| `val` | int    | `0` or `1`. |

### `pwm` — PWM duty cycle change

```json
{"v":1,"t":"pwm","chip":"ecu1","pin":"PD6","duty":0.42,"ts":12345}
```

`duty` is normalized 0.0–1.0 from the underlying timer's `OCR / TOP`.

Hooked PWM channels (per ECU; the MCU doesn't have any peripheral
wiring requiring PWM):

| Pin   | Timer  | Use on the ANEB v1.1 board |
|-------|--------|----------------------------|
| `PD6` (D6) | Timer 0 OC0A | LDR_LED PWM (ECU1's optical loop) |
| `PD5` (D5) | Timer 0 OC0B | LOOP PWM (ECU1's analog loop to ECU2) |
| `PB1` (D9) | Timer 1 OC1A | free for student use |
| `PD3` (D3) | Timer 2 OC2B | DOUT0 (dimmable LED) |

Other PWM-capable pins on the 328P are intentionally NOT hooked because
they conflict with our peripheral wiring — Timer 1 OC1B (PB2) is the
MCP2515 chip-select, and Timer 2 OC2A (PB3) is SPI MOSI.

`duty` assumes fast-PWM mode with `TOP=255` (the default
`analogWrite()` configuration). Other PWM modes will still report a
`duty` based on this assumption; for true bit-accurate duty across all
modes, sample `pin` events instead and average over a window.

### `uart` — UART byte stream from firmware

```json
{"v":1,"t":"uart","chip":"ecu1","data":"hello\n","ts":12345}
```

`data` is a JSON-escaped string. Bytes ≥ 0x80 are passed through verbatim
(invalid UTF-8 may appear if firmware emits raw binary).

### `can_tx` — CAN frame transmitted onto the bus

```json
{"v":1,"t":"can_tx","bus":"can1","src":"ecu1","id":"0x123","ext":false,"rtr":false,"dlc":2,"data":"CAFE","ts":12345}
```

| Field | Type | Notes |
|---|---|---|
| `bus`  | string | Bus name; currently always `"can1"`. |
| `src`  | string | Source chip id (`"ecu1"`–`"ecu4"`). |
| `id`   | string | Hex-formatted identifier (11- or 29-bit, LSB-aligned). |
| `ext`  | bool   | Extended (29-bit) frame. |
| `rtr`  | bool   | Remote-transmission-request. |
| `dlc`  | int    | Data length 0–8. |
| `data` | string | Hex-encoded payload, exactly `dlc * 2` chars. |

The event fires at the moment a controller's TXREQ is acknowledged
("transmission complete" in our model). Self-deliveries from a peer's
filtered RX are visible only as `pin` events on its INT line — not as
new `can_tx` events.

### `log` — engine log message

```json
{"v":1,"t":"log","level":"info","msg":"chip ecu1: loaded 5120 bytes from ..."}
```

`level` is one of `info`, `warn`, `error`. Logs do not carry `chip` or `ts`.

### `can_state` — CAN error counters / state snapshot

```json
{"v":1,"t":"can_state","chip":"ecu2","tec":40,"rec":0,"state":"active","ts":12345}
```

| Field | Type | Notes |
|---|---|---|
| `tec`   | int    | Transmit Error Counter, 0–255. |
| `rec`   | int    | Receive Error Counter, 0–255. |
| `state` | string | One of `"active"` / `"passive"` / `"bus-off"`. Derived from EFLG. |

Emitted whenever a `force_busoff`, `can_errors`, or `can_recover` command
runs against the chip. Future expansion: emit on every threshold crossing
without a UI poke.

---

## Commands (UI → engine)

All commands carry `"c":"<type>"`. Common fields:

| Field | Type | Meaning |
|---|---|---|
| `v`    | int    | Always `1`. |
| `c`    | string | Command type. |
| `chip` | string | Target chip id. Required by most commands. |

Engine response is asynchronous — commands take effect on the next tick.
Failures emit a `log` event with `level:"warn"` and a descriptive message.

### `din` — set digital-input pin level

```json
{"v":1,"c":"din","chip":"ecu1","pin":"PD2","val":1}
```

Drives the external pin level. The firmware sees the new state next time
it reads `PINx`. Subsequent transitions will emit `pin` events as usual.

The `pin` field accepts three forms:

- **Port form**: `"PB0"…"PD7"`. Direct AVR port + bit.
- **Arduino digital**: `"D0"…"D13"`. Mapped per the ATmega328P pinout
  (D0–D7 → PD0–PD7; D8–D13 → PB0–PB5).
- **Arduino analog as digital**: `"A0"…"A5"` → PC0–PC5. (`A6` and `A7`
  are ADC-only and are rejected by `din`.)

### `adc` — set ADC channel value

```json
{"v":1,"c":"adc","chip":"ecu1","ch":0,"val":512}
```

| Field | Type | Notes |
|---|---|---|
| `ch`  | int  | ADC channel `0`–`7`. |
| `pin` | string | Optional alternative to `ch`: `"A0"`…`"A7"`. |
| `val` | int  | `0`–`1023`. Out-of-range is clamped. |

`{"pin":"A6"}` and `{"ch":6}` are equivalent.

### `uart` — push bytes into a chip's UART RX

```json
{"v":1,"c":"uart","chip":"ecu1","data":"hello\n"}
```

### `load` — load `.hex` into a chip

```json
{"v":1,"c":"load","chip":"ecu2","path":"firmware/examples/blink.hex"}
```

Replaces firmware on a running chip (acts as load + reset).

### `reset` — hard reset a chip

```json
{"v":1,"c":"reset","chip":"ecu1"}
```

### `speed` — wallclock multiplier

```json
{"v":1,"c":"speed","speed":1.0}
```

`1.0` = real time (default in M5+). M1 ignores this — engine runs flat-out.

### `pause` / `resume`

```json
{"v":1,"c":"pause"}
{"v":1,"c":"resume"}
```

Affects all chips simultaneously.

### `step` — run N cycles then pause

```json
{"v":1,"c":"step","cycles":1000}
```

M1 implements this as a coarse resume-tick-pause loop (granularity
= `SIM_CYCLES_PER_TICK`). Single-cycle stepping arrives in M5.

### `force_busoff` — drive an ECU's MCP2515 into bus-off

```json
{"v":1,"c":"force_busoff","chip":"ecu1"}
```

Sets TEC = 255 + EFLG.TXBO; raises ERRIF and MERRF; emits a `can_state`
event so the UI can reflect the change. Pedagogical lever — instructors
use this in scenario scripts to demonstrate fault recovery.

### `can_errors` — inject error increments

```json
{"v":1,"c":"can_errors","chip":"ecu1","tx":4,"rx":0}
```

| Field | Type | Notes |
|---|---|---|
| `tx`  | int  | TX-error increments. Each adds 8 to TEC. |
| `rx`  | int  | RX-error increments. Each adds 1 to REC. |

After applying, the engine recomputes EFLG and the error state, then
emits a `can_state` event.

### `can_recover` — clear bus-off + reset counters

```json
{"v":1,"c":"can_recover","chip":"ecu1"}
```

Models the firmware-driven recovery path (mode toggle through
Configuration). After the call, TEC = REC = 0 and the state is `active`
again.

### `can_inject` — inject a CAN frame onto the bus

```json
{"v":1,"c":"can_inject","bus":"can1","id":"0x123","ext":false,"rtr":false,"dlc":2,"data":"CAFE"}
```

| Field | Type | Notes |
|---|---|---|
| `bus`  | string | Bus name, optional. Defaults to the only bus (`"can1"`). |
| `id`   | int or string | Identifier. Numbers and `"0x..."` strings both accepted. |
| `ext`  | bool   | Extended (29-bit). Default `false`. |
| `rtr`  | bool   | Remote-transmission-request. Default `false`. |
| `dlc`  | int    | Optional. If omitted, inferred from the length of `data`. |
| `data` | string | Hex-encoded payload. Optional for RTR frames. |

The frame is delivered to every attached controller (subject to that
controller's filters and mode). Used by the UI ("send a frame from
outside the sim") and the scenario player.

---

## Future events / commands (post-M1)

Added in their respective milestones, all under `v:1`:

- `can_tx`, `can_state`, `can_err`            (M3 / M4)
- `can_inject`, `can_corrupt`, `force_busoff` (M3 / M4)
- ADC events (currently UI-driven only)        (M5)
- `pwm` widening to include all PWM-capable pins (M5)
