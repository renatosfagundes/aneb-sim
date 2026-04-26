# Prompt — improve the Arduino Nano SVG (v2)

> Paste everything below the `---` line into your AI of choice
> (Gemini Pro, Claude, GPT, etc.) along with a top-down product
> photograph of an Arduino Nano. Save the result to
> `aneb-ui/aneb_ui/qml_assets/arduino-nano.svg`.

---

# Improve the Arduino Nano SVG to look like a real product photo

I have two earlier attempts at a top-down Arduino Nano SVG. Both are
unusable for different reasons:

## Attempt 1 — `arduino-vector.svg` (~280 KB)

A detailed vector illustration with 15 named gradients and a single
giant `<g>` of paths. Visually nice, but **doesn't render** in
Qt's QtSvg — almost certainly because of a **nested `<svg>` element**
right after the outer one. Qt silently drops the whole tree.

Failures to avoid:
- No nested `<svg>` elements. **Single root `<svg>`** only.
- No clipPath that swallows the entire visible content.
- No `transform="scale(0.146...)"` on the root content (this
  attempt did that and Qt clipped most of the board off-screen).

## Attempt 2 — `arduino-nano-deepseek.svg` (~15 KB)

Has clean structure (named `<g id="pin_D13">` groups, simple
gradients, viewBox `0 0 400 260`) but the aesthetics are wrong:

| Failure | What I want instead |
|---|---|
| White / cream background `#f8f9fa` covering the whole canvas | Empty (alpha=0) outside the PCB body |
| Big "ARDUINO NANO" title text in the top margin | Move it to small italic silkscreen in the bottom-right corner of the PCB itself |
| Pin labels duplicated: top row says `... D12 3V3 AREF D12 D11 D10 ...`; bottom says `... A4 A5 A6 A7 T 103 RST` | Use the exact labels from the photo (top: `D12 D11 D10 D9 D8 D7 D6 D5 D4 D3 D2 GND RST RX0 TX1`, bottom: `D13 3V3 AREF A0 A1 A2 A3 A4 A5 A6 A7 5V RST GND VIN`) |
| Big yellow circle marked "RESET" | Tiny black tactile-switch square — the real reset button is barely visible in photos |
| "CH340" chip rectangle next to the USB | A standard ATmega328P Nano doesn't have CH340 visible on top; remove |
| Bare component rectangles | SMD packages with subtle bevels and shading |
| PCB color `#008090` (washed out cyan) | Deeper Arduino teal: `#0a5f50` to `#003a30` gradient |
| 14-pin top row (off-by-one) | Exactly 15 pins per row |
| Cartoon-style flat fills everywhere | Subtle radial / linear gradients on every metallic part |

## What I want (positive spec)

A single self-contained SVG that visually resembles **the attached
top-down product photograph of an Arduino Nano**:
- Teal-green PCB with vertical gradient, rounded corners, 1px dark
  border. Empty / transparent everywhere outside the PCB.
- Mini-USB connector (left edge, protruding off the PCB) with
  silver gradient shielding, visible inner port slot.
- ATmega328P TQFP-32 chip in the center: black body with subtle
  bevel highlight, fine pin legs visible on all four sides
  extending into the chip footprint. White silkscreen "ATMEGA
  328P" centered. Pin-1 dot.
- 16 MHz silver crystal can to the right of the chip with "16M"
  silkscreen label below.
- Two tan decoupling caps below the crystal.
- Small black tactile reset button with "RST" silkscreen below.
- 6-pin ICSP header on the right edge: 2x3 grid of gold-plated
  through-holes inside a small black plastic frame.
- 4 SMD status LEDs in a vertical strip between the chip and the
  ICSP, with text labels TX (red body), RX (yellow), L or RX/L
  (orange), PWR or LPWR (green) — match exactly what's in the
  attached photo.
- Header pads: 15 top + 15 bottom, drawn as gold-plated rings
  (gold outer disc ~10px, dark center ~5px). Tiny white
  silkscreen labels above each top pad and below each bottom pad,
  exactly: top row `D12 D11 D10 D9 D8 D7 D6 D5 D4 D3 D2 GND RST
  RX0 TX1`, bottom row `D13 3V3 AREF A0 A1 A2 A3 A4 A5 A6 A7 5V
  RST GND VIN`.
- Scattered small dark / tan rectangles around the PCB
  (resistors, caps, etc.) to suggest hardware density.
- "Arduino Nano" silkscreen text in italic in the bottom-right
  corner of the PCB.

## Hard structural requirements

These let my QML overlay reference the right spots without
re-tuning every time:

1. **Single root `<svg>`** — no nesting, no clipPath that
   contains the whole visible content.
2. **`viewBox="0 0 400 260"`** (matches the Deepseek attempt,
   which our QML already has calibration constants for).
3. **No background rectangle outside the PCB.** Alpha = 0 outside
   the board. The PCB itself is the only opaque shape outside its
   bounds.
4. **Named `id` attributes** on every dynamic element. The QML
   overlay reads these by id to position live state markers:

   On-board LEDs (use one `id` per LED on the SMD body's
   `<rect>` / `<circle>`):
       `led_tx`, `led_rx`, `led_l`, `led_pwr`

   Header pad through-holes (one `id` per inner dark circle):
       Top row, left to right:
         `pad_top_d12`, `pad_top_d11`, `pad_top_d10`, `pad_top_d9`,
         `pad_top_d8`,  `pad_top_d7`,  `pad_top_d6`,  `pad_top_d5`,
         `pad_top_d4`,  `pad_top_d3`,  `pad_top_d2`,  `pad_top_gnd`,
         `pad_top_rst`, `pad_top_rx0`, `pad_top_tx1`
       Bottom row, left to right:
         `pad_bot_d13`, `pad_bot_3v3`, `pad_bot_aref`,
         `pad_bot_a0`,  `pad_bot_a1`,  `pad_bot_a2`,  `pad_bot_a3`,
         `pad_bot_a4`,  `pad_bot_a5`,  `pad_bot_a6`,  `pad_bot_a7`,
         `pad_bot_5v`,  `pad_bot_rst`, `pad_bot_gnd`, `pad_bot_vin`

5. **No `<style>` blocks, no CSS classes** for the dynamic state
   (the previous attempt added `.led-on .led-core` etc.). My QML
   handles state externally — those CSS classes do nothing useful
   here.
6. **No `<filter>` definitions** unless they're applied
   STATICALLY to a static element. Drop the glow filter from the
   previous attempt — the QML adds glow on top.
7. **No `<script>`, no `<animate>`, no `<animateTransform>`.**
   Pure static art.

## Output

A single fenced ```xml``` code block containing the complete SVG
file. Nothing outside the block — paste-ready into a `.svg` file.

Keep the file under 50 KB if possible. Avoid generating thousands
of tiny path segments for textures; use gradients and a few
shapes instead.
