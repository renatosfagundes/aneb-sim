# aneb-sim

Instruction-accurate simulator of the **Automotive Network Evaluation Board v1.1** (ANEB) — five chips on one process: 4× ATmega328P (ECU1–ECU4) + 1× ATmega328PB (board MCU controller), wired through a faithful MCP2515 model on a single CAN bus.

Built so students can run the same firmware they would flash to hardware, on their own machines, without SSH'ing into a shared jig.

## Status

**M0–M7 done, M9 in progress.** Engine runs all 5 chips, JSON wire protocol, MCP2515 model with full bus + error state machine, bus-off + recovery, full PyQt6/QML UI with per-chip Nano illustrations, LCD, plotter, dashboard.  External avrdude flashing supported via embedded Optiboot + dual-port TCP UART (flasher port `8600+i`, bridge port `8700+i`).  48 unit tests + 3 Python smokes.

- [`docs/DESIGN.md`](docs/DESIGN.md) — architectural rationale (why the code looks the way it does)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — milestone roadmap + roster
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — JSON wire schema
- [`docs/MCP2515_MODEL.md`](docs/MCP2515_MODEL.md) — controller fidelity
- [`BOOTSTRAP.md`](BOOTSTRAP.md) — set up your environment

## Quick start (after MSYS2 is installed — see BOOTSTRAP.md)

```bash
git clone --recurse-submodules <repo-url> /c/dev/aneb-sim
cd /c/dev/aneb-sim
./scripts/bootstrap.sh    # installs deps, builds simavr
./scripts/build.sh        # builds aneb-sim
./scripts/run-ui.sh       # runs the engine + Qt UI
```

For external avrdude flashing (e.g. from `remote_flasher`), run
`scripts/setup_com.bat` once to install com0com virtual COM-port pairs
named `ECU1 (aneb-sim)` … `MCU (aneb-sim)`.  Tools open the user-side
port (e.g. COM11 for ECU1) and the aneb-sim UI's bridge forwards bytes
to/from the simulator's UART socket on `127.0.0.1:8700+i`.

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
