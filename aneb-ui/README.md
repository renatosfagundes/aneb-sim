# aneb-ui — PyQt6 UI for aneb-sim

Desktop interface for the ANEB v1.1 simulator. Spawns the C engine as a
child process, parses JSON-Lines events, and renders five chips' worth
of LEDs, buttons, potentiometers, serial consoles, plus a CAN bus
monitor and a frame-injection form.

## Install

The recommended path is `../scripts/bootstrap.sh` from the repo root,
which detects Python 3.11 (via `py -3.11`), creates `../.venv/`, and
runs `pip install -e .` into it.

Manual install if you prefer:

```bash
py -3.11 -m venv .venv                  # Python 3.11 specifically
source .venv/Scripts/activate           # MSYS2 / Git Bash
# or:  .venv\Scripts\activate.bat       # cmd.exe
pip install -e .
```

We target Python 3.11 (not the most-recent 3.13/3.14) for stability
with PyQt6 wheels.

## Run

```bash
# Engine must be built first (see ../BOOTSTRAP.md).
../scripts/run-ui.sh
# or, equivalently, with the venv active:
python -m aneb_ui
```

By default the UI looks for `../build/aneb-sim/aneb-sim.exe` relative
to the package. Override with `--engine PATH`.

## Architecture

```
                 ┌──────────────────┐
   stdin (cmd) ──▶  aneb-sim.exe   │  engine (C, simavr-driven)
                 │  (subprocess)   │
   stdout (evt)◀──                 │
                 └──────────────────┘
                          │  JSON Lines
                 ┌──────────────────┐
                 │  sim_proxy.py    │  Qt signals per event type
                 └──────────────────┘
                          │
                 ┌──────────────────┐
                 │  state.py        │  observable store
                 └──────────────────┘
                          │
                 ┌──────────────────┐
                 │  widgets/*.py    │  dumb renderers
                 └──────────────────┘
```

The state store is the single source of truth. Widgets observe it via
Qt signals and never mutate it directly — UI input flows through
`sim_proxy.send_command()` instead, which puts the engine in charge of
state transitions.
