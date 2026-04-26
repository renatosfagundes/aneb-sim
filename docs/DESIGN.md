# aneb-sim — Design Rationale

This document captures the *why* behind the engineering decisions in
aneb-sim — the choices made, the alternatives considered, and the
trade-offs accepted. It is intended as a companion to the per-component
docs ([`ARCHITECTURE.md`](ARCHITECTURE.md), [`PROTOCOL.md`](PROTOCOL.md),
[`MCP2515_MODEL.md`](MCP2515_MODEL.md)) and as a reference for future
contributors who need to understand *why the code looks the way it does*
before they change it.

## 1. Project goals

aneb-sim is a high-fidelity simulator of the **Automotive Network
Evaluation Board (ANEB) v1.1** — five physical Atmel chips (4× ATmega328P
ECUs running student RTOS firmware + 1× ATmega328PB MCU acting as the
board controller) wired through MCP2515 controllers onto a CAN bus, with
GPIO/ADC/UART peripherals, buttons, LEDs, a buzzer, an LDR, and a
PWM↔ADC analog loopback path.

The original workflow at UFPE has students SSH'ing into a shared
[remote_flasher](../../remote_flasher/) host that owns the physical jig.
That works but creates contention: only one student can use the board
at a time, the lab grading bottleneck is hardware, and students cannot
debug locally. The goal of aneb-sim is to make the *same firmware* run
unmodified on a virtual jig hosted on every student's own machine.

Concrete success criteria:

- A student can clone the repo, run `bootstrap.sh`, and have a working
  build in under 30 minutes on a clean Windows 10/11 machine.
- The engine runs all five chips at ≥ 1× wallclock on commodity
  hardware, in under 1 GB of RAM total (UI + engine + child processes).
- All RTOS lab examples that depend on CAN, GPIO, UART, and ADC behave
  visibly the same as on the physical jig.
- An instructor can write a YAML scenario script that drives the
  simulator deterministically — useful for grading, demos, and
  bug-reproducer triage.

## 2. Hard constraints

| Constraint | Source | Implication |
|---|---|---|
| **Native Windows** (no WSL required) | Most students run Windows; the existing remote_flasher app is Windows-native (Java/QML) | Build under MSYS2 MINGW64; no Linux-only assumptions in the engine; no CRLF/LF surprises |
| **≤ 1 GB total RAM** | Pedagogical tooling, must coexist with browser/IDE | Single-process engine; no per-chip subprocesses; light UI (PyQt6, not Electron) |
| **Faithful enough to teach CAN errors** | M4 exit criterion: drive an ECU into bus-off and recover | Full TEC/REC/EFLG state machine; not just register storage |
| **Same firmware as hardware** | Avoid "but it worked on the simulator" gotchas | Instruction-accurate AVR core (simavr); same `.hex` files |

## 3. Top-level architecture decisions

### 3.1. Two-process design (engine + UI)

**Decision:** the simulator is split between a C engine (`aneb-sim`) and
a Python UI (`aneb-ui`, M6+). They communicate over JSON Lines on stdio.

**Why this matters most:** it is the single most consequential decision
in the project. Every other choice is downstream of it.

**Alternatives considered:**

- **Monolithic single-process app** (engine + UI in the same C++ or
  Qt binary). Simpler to debug, fewer moving parts. Rejected because:
  - UI crashes would kill the running simulation.
  - It pins the project to one UI framework forever — switching from
    PyQt6 to a browser later (a goal stated in early design discussions)
    becomes a rewrite, not a port.
  - It precludes headless operation. CI and instructor scenario
    scripts need an engine they can drive without a display.
- **Per-chip subprocesses** (e.g., one simavr instance per child
  process, IPC for CAN/UART). Maximally isolated. Rejected because:
  - Memory cost: each simavr subprocess starts at ~10–20 MB before
    any work; five chips × 20 MB + UI = exceeds the RAM budget by a
    wide margin once you add OS overhead.
  - Lockstep CAN timing becomes much harder. Same-tick contention
    across processes requires a synchronization barrier that defeats
    the parallelism it would otherwise enable.
  - Complexity: 5–6 IPC channels and a startup orchestrator vs. a
    single in-process scheduler.

**Why the chosen split works well:**

1. **UI swappability.** The protocol boundary is the only contract;
   any UI implementation that speaks JSON Lines on stdio is a valid
   replacement. PyQt6 today, browser tomorrow, an automated grader
   the day after.
2. **Headless operation.** `aneb-sim --ecu1=foo.hex --ecu2=bar.hex`
   plus piped stdin/stdout is a complete CLI. CI uses it for
   regression smoke tests; scenario scripts use it for replayable
   demos.
3. **Multiple simultaneous UIs.** A live student session can have a
   read-only "instructor view" attached, since the engine is just
   producing a stream of events.
4. **Hard isolation.** A bug in widget code can't corrupt simulator
   state; a bug in the simulator can't lock up the UI's event loop.

### 3.2. JSON Lines as the wire format

**Decision:** every message is a single-line JSON object with a `"v":1`
version envelope. Engine output → UI input on stdout/stdin
(`{"v":1,"t":"..."}` for events, `{"v":1,"c":"..."}` for commands).
Schema is documented in [`PROTOCOL.md`](PROTOCOL.md).

**Alternatives considered:**

- **Protobuf** or **MessagePack**. More compact, schema-typed,
  forward-compatible without manual versioning. Rejected because:
  - Adds a third-party dependency on both sides (the C engine and
    every UI implementation).
  - Hides the wire format from `tee`/`grep`/`jq` debugging. Students
    and instructors should be able to read the events flying past on
    stdout without a decoder ring.
  - Schema evolution is mostly *additive* in a teaching tool — we
    add new event types, rarely change existing ones. JSON's
    "ignore unknown fields" semantics suffice.
- **Custom line protocol** (e.g. `PIN ecu1 PB5 1`). Even smaller,
  no parsing dependency. Rejected because:
  - Every new event type requires another delimiter or escape
    convention. JSON gets this right by construction.
  - The cost of cJSON in the engine (≈ 700 LOC, single-file vendor)
    is negligible.

**Versioning policy:** the envelope carries `"v":1`. New event types and
new optional fields land under v1 (additive only). A breaking change
would bump to v2 and the engine would reject mismatched-version
commands. We have not needed to bump yet.

**Hot-path note:** event emission is allocation-free —
`proto_emit_pin`, `proto_emit_uart`, `proto_emit_can_tx` etc. write
hand-rolled JSON via `fprintf` under a single output mutex. Only the
infrequent inbound *command* parsing uses cJSON. This matters because
some events fire at firmware-cycle rate (e.g., a busy GPIO pin).

### 3.3. simavr as the AVR core

**Decision:** the AVR cores are emulated using
[simavr](https://github.com/buserror/simavr), pinned at v1.7-211-gde71ca2,
vendored as a git submodule under `external/simavr/`.

**Alternatives considered:**

- **Hand-written instruction-level emulator.** Rejected because the
  ATmega328P has hundreds of instructions plus interrupt handling, and
  a custom emulator would be a multi-month detour from the lab tooling
  that is the actual goal.
- **Compile firmware to native.** Recompile student `.cpp` files
  with a host-target HAL stub instead of avr-gcc. Faster, but breaks
  the "same firmware as hardware" guarantee — bugs that depend on
  AVR-specific behavior (interrupt latency, register quirks) would not
  reproduce in the sim.
- **QEMU's AVR target.** Less mature than simavr for ATmega328P, less
  actively maintained for this MCU family, and harder to embed
  (designed as a standalone process, not a library).
- **Other AVR libraries** (simulavr, MCUSim). Less mature or less
  actively maintained.

**Why simavr won:**

- **Correct.** Instruction-accurate, with peripheral models for GPIO
  ports, UART, SPI, ADC, EEPROM, timers — all of which we use.
- **Embeddable.** Builds as a static library; we drive it from our
  scheduler in-process.
- **IRQ-based peripheral hooks.** We can register callbacks on every
  GPIO pin transition and every UART byte. That's the foundation of
  both event emission and our MCP2515 SPI bridge.
- **Modest memory.** Each core in our config is ~10–20 MB resident.
  Five cores fits well under the budget.
- **328PB support.** The MCU controller chip is an ATmega328PB, a
  close cousin of the 328P. simavr handles both with the same core
  registration mechanism (verified at M0).

**Caveats accepted:**

- simavr's GDB-stub code drags in BSD socket calls. On Windows we
  link Winsock2 (`ws2_32`) unconditionally; we don't actually use the
  GDB stub.
- simavr's Makefile, not CMake. We shell out to `make RELEASE=1` from
  `bootstrap.sh` and link against the resulting `libsimavr.a`. Simpler
  than rewriting their build under CMake.
- simavr's headers have a zero-size array (`symbol[0]`) that trips
  GCC's `-Wpedantic`. We mark its include directory as `SYSTEM` so the
  warning is suppressed for third-party code without disabling it
  for our own.
- libsimavr.a lives under `external/simavr/simavr/obj-<host-triple>/`
  (modern simavr layout, not the legacy top-level). Our CMake globs
  for it.

### 3.4. Pure-logic separation for peripherals

**Decision:** the MCP2515 model
([`aneb-sim/src/mcp2515/`](../aneb-sim/src/mcp2515/)) and the CAN bus
model ([`aneb-sim/src/can_bus/`](../aneb-sim/src/can_bus/)) are
implemented as pure-logic modules with no simavr dependency. They are
driven by external stimuli (CS edges, SPI bytes, inbound frames) and
expose outputs via callbacks (INT pin level, outbound frames). The
simavr glue lives separately in `sim_loop.c`.

**Why this matters:**

1. **Unit-testable in isolation.** The 48-test suite under
   [`aneb-sim/test/`](../aneb-sim/test/) instantiates `mcp2515_t`s and
   `can_bus_t`s directly, asserts on their internal state, and runs in
   under 50 ms. No AVR cycles, no firmware, no flakiness.
2. **Pluggable transport.** When M3 added a real CAN bus, the only
   change in the MCP2515 module was wiring the `on_tx` callback —
   the SPI logic, register file, filter machine were already done.
3. **Clear contract.** The pure module documents *the MCP2515's
   behavior* in its API comments. The glue layer documents *the
   board's wiring* (CS=PB2, INT=PD2). They evolve independently.

**Cost:** the engine has to maintain a small glue layer in `sim_loop.c`
that translates between simavr's IRQ-based world and the pure module's
synchronous-call world. ≈ 60 lines, isolated, no business logic.

## 4. Fidelity decisions

The MCP2515 datasheet is ~140 pages. The CAN spec is hundreds more.
We made deliberate fidelity calls per feature.

### 4.1. What we model in full

| Feature | Why fully modeled |
|---|---|
| All 9 SPI commands (RESET, READ, WRITE, BIT MODIFY, RTS, READ STATUS, RX STATUS, LOAD TX BUFFER, READ RX BUFFER variants) | Drivers like `MCP_CAN_lib` (used by the lab firmware) exercise all of them. Missing one would silently break a class of student code. |
| Register file with reset values + read-only protection (CANSTAT/TEC/REC) | Drivers verify reset values during `begin()`. Read-only enforcement matches firmware expectations. |
| Acceptance filters (RXF0–5) and masks (RXM0/1) for both std and ext IDs | Lab examples use filtering for selective reception. Per-buffer RXM modes (filtered / std-only / ext-only / receive-any) ditto. |
| Modes: Configuration / Normal / Loopback / Listen-only / Sleep | Loopback in particular is the M2 exit criterion (firmware self-test). |
| TEC/REC with auto-decrement on success, EFLG bits, mode transitions | M4 exit criterion: drive an ECU into bus-off, observe EFLG, recover. The full state machine is required to teach CAN error behavior. |
| Bus-off TX gating + RX gating | Without this, "bus-off" would just be a register bit, not a behavior. |
| Firmware-driven recovery (CANCTRL mode toggle clears bus-off) | The path real drivers use. |

### 4.2. What we deliberately do not model

| Feature | Why deferred |
|---|---|
| Bit-time fidelity (CNF1/2/3) | Frame delivery is logical, not bit-timed. CNF registers are accepted and round-tripped but not enforced. Modeling per-bit would multiply complexity by a large factor for negligible pedagogical benefit — students don't tune bit timing in lab work. |
| 128 × 11 recessive-bit auto-recovery from bus-off | Requires per-bit time modeling. The firmware-driven recovery path (mode toggle) is supported and is what real drivers use. The auto-recovery path can be added if a future lab specifically teaches it. |
| One-shot mode | Not used by `MCP_CAN_lib`. Add when needed. |
| CLKOUT / SOF pin | Lab does not use these. |
| RXnBF / TXnRTS pins | Hardware handshake outputs; lab uses interrupt-driven drivers. |
| Wake-up from sleep on bus activity | Sleep is implemented as "no TX, no RX". Auto-wake requires bus event monitoring during sleep, not relevant to current labs. |
| LIN bus | Board has it (ECU3/ECU4); lab focus is CAN. Deferral is explicit. |
| Two CAN buses (CAN1 + CAN2) | Original board has both; lab wiring puts every ECU on CAN1. Multi-bus support is an additive change to `can_bus.c`. |
| AVR sleep modes | Not exercised by RTOS firmware; deferral is non-load-bearing. |
| Bootloader behavior | We load `.hex` directly, bypassing avrdude. Faster, deterministic, but precludes testing the bootloader itself — which the lab also does not exercise. |

### 4.3. CAN bus arbitration

**Decision:** the bus model delivers frames in attach order when
multiple controllers TX in the same tick. Real CAN arbitrates by ID
(lower ID wins via dominant bit OR-ing).

**Why:** in our scheduler, each AVR core advances synchronously by N
cycles per tick. Two TXs landing in the same tick is rare in normal
firmware but possible. True ID-based arbitration would require:
1. Buffering all same-tick TXs.
2. Resolving by ID at end-of-tick.
3. Replaying losers as still-pending TXREQs.

The bookkeeping is non-trivial and the pedagogical payoff is small —
students rarely write code that intentionally exploits CAN arbitration
(it's a corner case). If a future lab requires it, the bus model has a
clean place to add the resolver (in `can_bus_broadcast`), and the
controller's `on_tx` callback is already wired correctly.

### 4.4. Error injection model

**Decision:** errors are injected via explicit API
(`mcp2515_inject_tx_errors`, `mcp2515_inject_rx_errors`,
`mcp2515_force_busoff`) rather than emerging from bit-level simulation.
The injection API maps 1:1 to JSON commands (`can_errors`,
`force_busoff`, `can_recover`).

**Why:**

- Real CAN errors come from physical-layer events (bit errors, stuff
  errors, CRC mismatches, missing ACKs). We don't model the physical
  layer.
- For a teaching tool, *deterministic* error injection is more useful
  than *realistic* error generation. Instructors can write a scenario:
  "at t=2s, bump ECU1's TEC by 16 to push it into error-passive". The
  result is reproducible across student environments.
- Students learn the *consequences* of errors — how the firmware
  reacts to EFLG changes, how recovery sequences work. They do not need
  to learn how a CAN transceiver detects a stuff error.

The trade-off accepted: a student cannot explore "what happens if a
peer drops out" without explicit injection. We document this in
[`MCP2515_MODEL.md`](MCP2515_MODEL.md) and provide UI controls for the
common cases.

## 5. UI choice — PyQt6 first, browser later

**Decision:** ship a PyQt6 desktop UI (M6) first. The protocol
boundary is designed so a browser UI can be added later without
touching the engine.

**Alternatives considered:**

- **Browser-first.** Examples 09–13 are dashboards with gauges,
  doors, and warning lights — those are the web's natural domain
  (SVG/CSS). Examples 15–16 are real-time plots, also web-native.
- **PyQt6-only forever.** Simpler distribution model (one Python
  install). Fixed UI choice.

**Why PyQt6 first:**

1. The user is comfortable with QML/Qt — the existing
   [remote_flasher](../../remote_flasher/) UI is Qt-based.
2. Faster path to a working dev loop. PyQt6 + pyqtgraph + a board
   photo overlay gets us to a usable UI in days, not weeks.
3. No HTTP server, no WebSocket plumbing, no JS toolchain on top of
   the engine.

**Why browser later, not never:**

- Zero student install. Open `localhost:8080` instead of
  `pip install PyQt6` failing on someone's Python 3.13 / wrong arch /
  corporate proxy.
- Future-proofs the remote-lab story. A hosted aneb-sim with a
  browser UI is the original SSH-replacement, just without SSH.
- Better dashboard / plotter visuals.

**Migration cost estimate** (with the protocol boundary in place):

- 3–5 days if the engine boundary stays clean from day one (current
  plan).
- ~weeks if PyQt6 widgets bind directly to engine state via Qt
  signals/slots (which we are explicitly avoiding by routing
  everything through a single `state.py` store).

## 6. Test strategy

aneb-sim has three distinct test layers, each catching a different
class of bug.

### 6.1. Pure-logic unit tests (C, ctest)

48 tests across five suites:

- `registers` (8): RESET semantics, READ/WRITE auto-increment, BIT
  MODIFY, mode transitions via CANCTRL, SPI RESET command, CANSTAT
  read-only protection.
- `spi cmds` (6): READ STATUS / RX STATUS encoding, RTS, LOAD TX
  BUFFER variants, READ RX BUFFER auto-clear.
- `filters` (7): exact-match std and ext, mask=0 wildcards, std-vs-ext
  disagreement, RXM=any bypass, RXM=std/ext gating, FILHIT recording.
- `loopback` (4): TX self-RX round trip, INT pin assert/release,
  TX0IF setting, filtered-out frames still complete TX.
- `can_bus` (7): attach (with double-attach refusal), broadcast skips
  source, broadcast to multiple peers, inject delivers everywhere,
  filter end-to-end, mode gating, NORMAL TX through callback into bus.
- `errors` (16): initial state, inject TEC/REC, EWARN/TXEP/RXEP/TXBO
  thresholds, force_busoff/force_error_passive helpers, bus-off TX/RX
  gating, recovery via helper and via CANCTRL mode toggle, successful
  TX/RX counter decrements, sticky overflow bits.

These run in milliseconds and are wired into `ctest`. CI runs them on
every push.

### 6.2. Python integration smokes (engine + JSON proto)

Three smokes under [`tests/`](../tests/):

- `m1_smoke.py`: load a hex on ECU1, observe pin events, dynamically
  load on ECU2 via JSON, verify pause/resume, verify warn-on-unknown.
- `m3_can_smoke.py`: bus init log, can_inject with valid + malformed +
  unknown-bus payloads.
- `m4_busoff_smoke.py`: force_busoff produces can_state, recover
  resets counters, can_errors crosses threshold, target without an
  MCP2515 yields warn.

These exercise the JSON wire format, the command queue, the stdin
reader thread, and end-to-end command → state-change → event paths.
They will be wired into CI once the GitHub-hosted MSYS2 runner reliably
runs Python harnesses.

### 6.3. Firmware-level integration (deferred)

The eventual highest-fidelity test: load real CAN-using firmware
(currently `05_can_basic/can_tx.cpp` + `can_rx.cpp` from
remote_flasher) on two ECUs and assert that frames cross. This requires
producing `.hex` files from the Trampoline-RTOS source — outside our
direct toolchain. The plan is to wire this in once we have a CAN-using
sketch built with `arduino-cli` + `MCP_CAN_lib` (or via the user's
existing Trampoline build setup).

## 7. Decisions explicitly considered and rejected

| Considered | Rejected because |
|---|---|
| Vendor cJSON instead of submodule | cJSON is two files; vendoring with a clear README + LICENSE is simpler than submodule overhead for such a small dependency. (Done — vendored.) |
| Use cJSON for event emission too | The hot path benefits from allocation-free `fprintf`. cJSON is parser-only on our side. |
| Implement the MCP2515 inside simavr's peripheral system | Would tie the model to simavr forever. The pure-logic split is more valuable than the small ergonomic win. |
| One CMakeLists at the root only | Two-level CMake (root + `aneb-sim/`) keeps the test binary, engine binary, and library targets logically grouped. |
| Run unit tests by spawning each suite as a separate ctest entry | One binary with all suites is faster to build and easier to run by hand. The current `add_test(NAME unit ...)` wraps the whole binary. Granular re-runnability is a future quality-of-life nicety, not load-bearing. |
| Use `gh repo create` without asking | Public repo creation is hard to undo and visible to others. We confirmed visibility before pushing. |
| Vendor avr-gcc / avr-libc | Lots of GBs and breaks the "students just run bootstrap" promise. Pacman packages are stable. |

## 8. Build system

**CMake** for our code (engine + tests + libraries), **`make`** for
simavr (its own Makefile). `bootstrap.sh` bridges the two by running
simavr's make first, then handing off to CMake. **Ninja** is the CMake
generator (faster builds, parallel by default).

Why CMake despite a build complexity that makes pure Make tempting:

- Out-of-tree builds (`build/` is git-ignored, easy to nuke).
- Cross-platform potential (Linux/Mac builds need maybe an afternoon's
  work — paths and Winsock guards).
- ctest integration.
- IDE support (VSCode CMake Tools, CLion, etc.) without bespoke plugins.

## 9. CI

GitHub Actions runs on `windows-latest` with `msys2/setup-msys2@v2`,
installs the same packages `bootstrap.sh` does (so CI is the same as
local), builds simavr, builds aneb-sim, runs ctest. ~5–7 min total.
Runs on every push and PR to `main`.

The Python smoke harnesses are not yet in CI — they require the
engine binary plus a known-good firmware hex, both of which are
available locally but not yet bundled into the runner. Wiring them in
is on the M9 polish list.

## 10. Repository conventions

- **LF line endings everywhere** in the repo, enforced via
  `.gitattributes`. Build scripts are `eol=lf` so MSYS2 bash will
  execute them; `.bat`/`.cmd` files are `eol=crlf` for Windows
  compatibility (none currently exist, but the rule is in place).
- **Logical commits with Conventional Commits prefixes** (`feat`,
  `fix`, `test`, `docs`, `build`, `ci`, `chore`). Each milestone is a
  series of self-contained commits, not one mega-commit. This makes
  `git bisect` useful and per-milestone code review tractable.
- **Milestone tags** (`m0`, `m1`, ..., `m4` so far). Each tag points at
  the commit where the milestone's exit criterion was demonstrably met.
- **`Co-Authored-By` for AI-assisted commits**, surfacing pair-coding
  attribution in the git log.
- **Build artifacts not in OneDrive**. The repo lives at
  `c:\dev\aneb-sim`, not under `OneDrive\Pos\RTOS\`. OneDrive's sync
  fights the build (file locks, race conditions on .o files); putting
  the repo outside OneDrive avoids the entire problem class.

## 11. What is intentionally not in this document

- **API references.** Each module has header-file comments that
  document its public API. This document is about *why* the modules
  exist, not *what* their functions do.
- **Step-by-step build instructions.** Those are in
  [`BOOTSTRAP.md`](../BOOTSTRAP.md) and `scripts/`.
- **Per-event JSON schemas.** Those are in
  [`PROTOCOL.md`](PROTOCOL.md).
- **Per-register MCP2515 details.** Those are in
  [`MCP2515_MODEL.md`](MCP2515_MODEL.md) and the datasheet.

## 12. Glossary

| Term | Meaning |
|---|---|
| **ANEB** | Automotive Network Evaluation Board (v1.1). The physical lab jig with 4 Arduino Nanos + 1 ATmega328PB MCU + MCP2515 transceivers. |
| **ECU** | Electronic Control Unit. The four ATmega328P chips that students program. |
| **MCU (in this repo)** | The board-controller ATmega328PB chip — distinct from the four ECUs. Runs `MCU_Firmware_v08`. |
| **TEC / REC** | Transmit Error Counter / Receive Error Counter (CAN error-state inputs). |
| **EFLG** | Error Flag register on the MCP2515 (EWARN/TXWAR/RXWAR/TXEP/RXEP/TXBO/RXnOVR). |
| **Bus-off** | The CAN error state where TEC ≥ 256, in which the controller is taken offline until recovery. |
| **Loopback (MCP2515 mode)** | Mode where TX'd frames are routed back through the controller's RX path without touching the bus. Used for self-test. |
| **Pure-logic module** | A module with no simavr / no AVR / no I/O dependencies. Driven by callable inputs and callback outputs. Unit-testable in isolation. |
| **Lockstep scheduler** | The single-thread loop that advances all five AVR cores by N cycles per tick before letting peripherals tick. Source of timing reproducibility. |
