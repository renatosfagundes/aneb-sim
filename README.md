# aneb-sim

Instruction-accurate simulator of the **Automotive Network Evaluation Board v1.1** (ANEB) — five chips on one process: 4× ATmega328P (ECU1–ECU4) + 1× ATmega328PB (board MCU controller), wired through a faithful MCP2515 model on a single CAN bus.

Built so students can run the same firmware they would flash to hardware, on their own machines, without SSH'ing into a shared jig.

## Status

**M0 — bootstrap & toolchain.** Loads a `.hex` into one core and traces GPIO activity. No CAN, no UI yet.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design, [`BOOTSTRAP.md`](BOOTSTRAP.md) to set up your environment.

## Quick start (after MSYS2 is installed — see BOOTSTRAP.md)

```bash
git clone --recurse-submodules <repo-url> /c/dev/aneb-sim
cd /c/dev/aneb-sim
./scripts/bootstrap.sh    # installs deps, builds simavr
./scripts/build.sh        # builds aneb-sim
./scripts/run.sh firmware/examples/blink.hex
```

You should see GPIO pin transitions printed to stdout.

## Layout

| Path | What |
|---|---|
| `aneb-sim/` | C engine (simavr cores + MCP2515 + CAN bus) |
| `aneb-ui/` | PyQt6 UI (added in M6) |
| `external/simavr/` | simavr submodule, pinned commit |
| `firmware/` | `.hex` files, copied/symlinked at bootstrap (not committed) |
| `scripts/` | `bootstrap.sh`, `build.sh`, `run.sh` |
| `docs/` | architecture, protocol, MCP2515 model notes, lab authoring |

## License

TBD.
