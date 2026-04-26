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
| `pin` | string | Port-letter form: `"PB0"…"PD7"`. Arduino aliases (`"D7"`, `"A4"`) land in M5. |
| `val` | int    | `0` or `1`. |

### `pwm` — PWM duty cycle change

```json
{"v":1,"t":"pwm","chip":"ecu1","pin":"PD6","duty":0.42,"ts":12345}
```

`duty` is normalized 0.0–1.0. Lands in M5.

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

### `adc` — set ADC channel value

```json
{"v":1,"c":"adc","chip":"ecu1","ch":0,"val":512}
```

| Field | Type | Notes |
|---|---|---|
| `ch`  | int  | ADC channel `0`–`7`. |
| `val` | int  | `0`–`1023`. Out-of-range is clamped. |

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
