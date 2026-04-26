# Wire protocol

> *Status: stub. Becomes the source of truth in M1.*

JSON Lines (UTF-8, one object per line, terminated `\n`) over stdio between
`aneb-sim` (engine) and `aneb-ui` (UI).

## Direction: engine → UI (events)

| `t` | Meaning |
|---|---|
| `pin`         | GPIO digital state change |
| `pwm`         | PWM duty cycle change (sampled) |
| `uart`        | UART byte stream |
| `can_tx`      | CAN frame transmitted on the bus |
| `can_state`   | TEC/REC counters + active/passive/bus-off mode |
| `can_err`     | Error frame or injected-error notification |
| `log`         | Engine log message |

## Direction: UI → engine (commands)

| `c` | Meaning |
|---|---|
| `din`           | Set digital-input pin level |
| `adc`           | Set ADC channel value (0–1023) |
| `uart`          | Write bytes into a chip's UART |
| `can_inject`    | Inject a CAN frame onto the bus |
| `can_corrupt`   | Corrupt the next frame on the bus |
| `force_busoff`  | Force an ECU into bus-off |
| `reset`         | Hard reset a chip |
| `load`          | Load a new `.hex` into a chip |
| `speed`         | Wallclock multiplier (1.0 = real time) |
| `pause`, `resume`, `step` | Control execution |

The full schema (with envelope `{"v":1,...}` versioning, exact field types,
and validation rules) lands in M1.
