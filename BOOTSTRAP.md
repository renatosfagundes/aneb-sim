# Bootstrapping aneb-sim on Windows

This guide takes a clean Windows 10/11 machine to a working `aneb-sim` smoke test.

Total time: ~30 minutes (most of it MSYS2's first `pacman -Syu`).

## 1. Install MSYS2

1. Download the installer from <https://www.msys2.org/>.
2. Run it. Default install path is `C:\msys64` — leave it.
3. After install, **launch the "MSYS2 MINGW64" shell** from the Start menu.
   The icon is blue and labelled `MSYS2 MINGW64`. *Do not* use `MSYS2 MSYS` (purple) or `MSYS2 UCRT64` (green) — they are different toolchain environments and the build assumes MINGW64.

## 2. Update the package database

In the MINGW64 shell:

```bash
pacman -Syu
```

When prompted, **close the shell and reopen it**, then run again:

```bash
pacman -Syu
```

(Standard MSYS2 first-run dance — it has to update the package manager before it can update everything else.)

## 3. Clone the repo

```bash
mkdir -p /c/dev
cd /c/dev
git clone --recurse-submodules <repo-url> aneb-sim
cd aneb-sim
```

If you forgot `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

## 4. Bootstrap (installs build deps, builds simavr)

```bash
./scripts/bootstrap.sh
```

This installs (via `pacman`):

| Package | Why |
|---|---|
| `mingw-w64-x86_64-toolchain` | gcc, make, etc. for native Windows builds |
| `mingw-w64-x86_64-cmake` | build system |
| `mingw-w64-x86_64-ninja` | fast builder |
| `mingw-w64-x86_64-libelf` | simavr loads ELF / Intel hex |
| `mingw-w64-x86_64-avr-gcc` | AVR cross compiler (for building example firmware later) |
| `mingw-w64-x86_64-avr-libc` | AVR C library headers |
| `git`, `make` | basics |

Then it runs `make` inside `external/simavr/simavr/` to produce `libsimavr.a`.

## 5. Build aneb-sim

```bash
./scripts/build.sh
```

Outputs `build/aneb-sim/aneb-sim.exe`.

## 6. Smoke test

Copy any Arduino-built `.hex` file into `firmware/examples/blink.hex`. The
`Blink1000.ino.eightanaloginputs.hex` from your local
`JigaAppCmd4/firmware/mcu/nano/` works — it toggles `LED_BUILTIN` (pin 13, PB5)
once per second.

```bash
cp '/c/Users/renat/OneDrive/Pos/RTOS/JigaAppCmd4-20260419T021937Z-3-001/JigaAppCmd4/firmware/mcu/nano/Blink1000.ino.eightanaloginputs.hex' firmware/examples/blink.hex

./scripts/run.sh firmware/examples/blink.hex
```

Expected output (Ctrl-C to stop):

```
aneb-sim smoke test: running firmware/examples/blink.hex
PIN PB5 = 1
PIN PB5 = 0
PIN PB5 = 1
...
```

If you see pin transitions, **M0 is done.** Move on to M1.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `pacman: command not found` | Wrong shell | Use **MINGW64**, not Windows `cmd` or PowerShell |
| `cmake: command not found` after bootstrap | New PATH not picked up | Close & reopen the MINGW64 shell |
| `libelf.h: No such file` | `libelf` package missing | `pacman -S mingw-w64-x86_64-libelf` |
| `aneb-sim.exe` runs but nothing prints | Wrong hex (no pin activity) or hex didn't load | Try a known-good blink hex; confirm path with `ls -la firmware/examples/` |
| Build artifacts polluting the repo via OneDrive | Repo is inside OneDrive | Move outside OneDrive — `c:\dev\aneb-sim` is fine, `c:\Users\<you>\OneDrive\...` is not |
| Long-path errors during `git clone` | Windows long path off | `git config --system core.longpaths true` (admin) |
