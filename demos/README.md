# Scenarios — JSON-Lines replay scripts

Each `.jsonl` here is a script the engine plays back on demand: the
instructor (or a student following along) runs one of these and the
chips drive themselves through pre-canned states without anyone
typing commands by hand.

## Format

One JSON object per line.  The object is a normal engine command
(see [`docs/PROTOCOL.md`](../docs/PROTOCOL.md)) plus a leading
`"at_ms"` field that says when (in milliseconds since the script
started) to apply it:

```json
{"v": 1, "at_ms":  500, "c": "force_busoff", "chip": "ecu2"}
{"v": 1, "at_ms": 3000, "c": "can_recover",  "chip": "ecu2"}
```

Lines starting with `#` or `//` are treated as comments and skipped.
Blank lines are skipped.

## Running a scenario

**From the engine's CLI:**

```bash
./build/aneb-sim/aneb-sim.exe --script demos/busoff_recovery.jsonl
```

**From the UI:** Toolbar → `Scenario ▾` → pick one.

**From any JSON client:** push a `run_script` command on the
engine's stdin:

```json
{"v": 1, "c": "run_script", "path": "demos/busoff_recovery.jsonl"}
```

## Catalogue

| File | What it does | Setup |
|---|---|---|
| [`busoff_recovery.jsonl`](busoff_recovery.jsonl) | Force ECU2 into bus-off twice, recover after each | Flash `can_busoff_demo.hex` on ECU2 |
| [`tec_climb.jsonl`](tec_climb.jsonl) | Walk ECU3's TEC up by 16 every 500 ms past the bus-off threshold, then recover | Flash `can_busoff_demo.hex` on ECU3 |
| [`multi_node_arbitration.jsonl`](multi_node_arbitration.jsonl) | Inject three same-tick CAN frames with different IDs to illustrate the engine's attach-order delivery | Open Toolbar → `CAN monitor` first |

## Authoring new scenarios

- Use any text editor — the format is plain text.
- The engine parses each line independently; a malformed line emits
  a warning and is skipped, the rest of the script continues.
- `at_ms` is *absolute* time from script start, not relative to the
  previous line.  If `at_ms` for line N is before the wallclock when
  the runner finishes line N-1, line N fires immediately.
- A second scenario can be started while the first is still running;
  they share the same command queue and interleave naturally.
