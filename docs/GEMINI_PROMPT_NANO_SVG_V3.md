# Prompt — improve the Arduino Nano SVG (v3)

> Paste everything below the `---` line into your AI of choice along
> with the current SVG (`aneb-ui/aneb_ui/resources/arduino-nano-deepseek.svg`)
> AND a top-down product photo of an Arduino Nano. Save the result
> to `aneb-ui/aneb_ui/qml_assets/arduino-nano.svg`.

---

# Refine the existing Arduino Nano SVG

The current `arduino-nano-deepseek.svg` is structurally correct
(viewBox `0 0 400 260`, 4 named LED ids, 30 named pad ids, 15-pin
rows with the right labels) and renders cleanly. It needs one more
polish pass to look closer to a real product photo.

## Things to KEEP exactly as they are

- `viewBox="0 0 400 260"` — do not change.
- The complete set of named ids:
    `led_tx`, `led_rx`, `led_l`, `led_pwr`,
    `pad_top_d12 ... pad_top_tx1` (15 ids),
    `pad_bot_d13 ... pad_bot_vin` (15 ids).
- Pad coordinates: top row `cy=40`, bottom row `cy=215`,
  `cx = 45 + index*22` for index 0..14.
- LED rect coordinates: x=305, w=6, h=12;
  `y = 90, 110, 130, 150` for TX/RX/L/PWR.
- Single root `<svg>`, no nesting, viewBox-relative coords only.
- The italic "Arduino Nano" silkscreen in the bottom-right
  corner (small and faint — looks great).
- The 15+15 gold-pad rings with white silkscreen labels above
  top pads / below bottom pads. The labels and pad styling are
  good.
- The 6-pin ICSP header on the right edge. Keep as-is.
- The `<g id="usb_connector">` and `<g id="icsp_header">`
  semantic groups.

## Things to FIX (specific issues visible in the current render)

1. **Remove the dashed "bounding box" guides.** The current SVG
   has two `<rect ... stroke-dasharray="3 3">` rectangles around
   the chip and crystal:
   ```
   <rect x="155" y="65" width="90" height="90" rx="2"
         fill="none" stroke="#a0b0a8" stroke-dasharray="3 3" .../>
   <rect x="240" y="70" width="42" height="34" rx="2"
         fill="none" stroke="#a0b0a8" stroke-dasharray="3 3" .../>
   ```
   These are component-placement guides and they do not appear
   on a real PCB photo. **Delete them.**

2. **Drop the `<filter id="pcbShadow">`.** Qt's QtSvg renders
   `<feDropShadow>` inconsistently and the drop shadow doesn't
   add value here (the simulator shows the Nano flat against
   a UI panel, not floating in a void). Remove the filter
   definition AND the `filter="url(#pcbShadow)"` on the PCB
   `<rect>`.

3. **Make the USB connector look like a mini-USB plug instead
   of a tall metal bar.** Currently the metal shield is
   `x="5" y="75" width="32" height="90"` — a 32x90 vertical
   block. Real Nano mini-USB:
   - The shield is roughly square-ish in top-down view, around
     30-36 wide and 36-44 tall (NOT 90).
   - Position it centered on the PCB's left edge, e.g.
     `y = 110, height = 44` so it spans y=110..154.
   - Inside the shield: a darker port cavity centered, with
     a tiny tongue / connector tab visible.
   - Mounting tabs at the four corners (the current 8x4
     rectangles are fine, just match the new shield bounds).
   The asymmetric vertical bar of the current render reads as
   "battery" or "header" rather than "USB port".

4. **Make the reset button more recognizable.** The current
   small black square labeled "RST" works but could look more
   like a real tactile switch:
   - 8x8 or 10x10 dark grey body with rounded corners.
   - A small lighter circle inside (the actuator).
   - Position it so the silkscreen "RST" sits below the
     button, not floating in space.

5. **Add visible PCB traces.** Right now the PCB looks blank
   between the chip and the headers. Add a few subtle
   horizontal/vertical thin paths in `#0e7a68` or
   `#1a8678` at ~0.4 opacity. Just suggest hardware density;
   no need for a real netlist.

6. **Brighter gold pads.** The current `goldPadGrad` from
   `#ffe480 → #d4af37 → #996515` reads slightly muted.
   Try `#ffeb88 → #d6b94a → #a87e1e` and ensure each pad has
   a 1px dark border so it pops against the teal PCB.

7. **Improve the chip pin legs.** Currently the dashed
   bounding box around the chip is the only thing suggesting
   pin placement. Add explicit small `<rect>` slivers around
   the four sides of the ATmega body (~8 per side, 1.5x4 each),
   colored with the existing `chipPinGrad`. Make them look
   like surface-mount TQFP legs extending outward 3-4px from
   the body.

8. **Crystal proportions.** The current SVG draws the 16M
   crystal at `x=240..282, y=70..104` (42x34) which is too
   square. Real 16MHz crystals on a Nano are oval / pill-shaped,
   ~24x10. Resize the crystal to ~`x=242 y=85 width=24 height=10`,
   put "16M" silkscreen below at y=100.

9. **Two odd orange rectangles flanking the chip.** Current
   render shows two thin orange/tan rectangles at the top-left
   and top-right of the chip footprint that look misplaced.
   They appear to be SMD components but their position reads
   as awkward. Either:
   - Remove them, or
   - Move them to plausible passive-component spots (next to
     the crystal, between the chip and the ICSP, etc.) and
     make them match the cap color (`#b39d89`).

10. **Don't add the title text back.** The earlier attempt had
    "ARDUINO NANO" as a big title above the PCB. Keep only
    the small italic "Arduino Nano" silkscreen in the
    bottom-right corner (which is correct in the current SVG).

## Output

A single fenced ```xml``` code block containing the complete SVG
file. Nothing outside the block. Under 50 KB total.

Take the **current** `arduino-nano-deepseek.svg` as your starting
point — keep everything that's already correct, only modify the
specific issues called out above. Don't regenerate from scratch.
