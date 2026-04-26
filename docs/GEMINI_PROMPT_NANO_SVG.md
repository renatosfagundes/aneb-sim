# Gemini prompt — generate `arduino_nano.svg`

> Paste everything below the `---` line into Gemini. Save the resulting
> SVG as `aneb-ui/aneb_ui/qml_assets/arduino_nano.svg`. The QML overlay
> will pick it up automatically.

---

# Generate an SVG of an Arduino Nano (top-down)

I need a single self-contained `.svg` file that I'll save as
`aneb-ui/aneb_ui/qml_assets/arduino_nano.svg` and use as the static
base layer of a QML widget. The widget overlays small QML rectangles
on top to drive live LED states and pin-HIGH indicators, so the SVG
should NOT animate or carry script — it's purely the static board art.

## Hard requirements

1. **Pure SVG, no embedded raster**, no `<image>` tags, no `<script>`.
   Use only `<rect>`, `<circle>`, `<ellipse>`, `<path>`, `<text>`,
   `<g>`, and `<linearGradient>` / `<radialGradient>`.
2. **`viewBox="0 0 420 160"`** so the whole asset fits a 420x160
   logical board (matches my QML widget's coordinate system).
3. **No checkerboard, no white box**, no transparent-rendering
   indicators baked into pixels. Backgrounds outside the PCB stay
   genuinely empty (alpha 0).
4. The board's body must occupy roughly **x=12..408, y=8..152** so
   the surrounding margin is transparent.
5. **Named element ids** for the dynamic spots, exactly as listed
   below. The QML overlay needs to reference these positions; if
   the ids match, my code does no calibration. Each id must be on
   a `<circle>` or small `<rect>` element marking the *center* of
   the live indicator, drawn in its OFF state (small dark body,
   suitable for the QML overlay to tint when active).

   On-board LEDs (4):
       `led_tx`, `led_rx`, `led_l`, `led_pwr`

   Header pad rings (30):
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

6. The four LED bodies should be small DARK rectangles in their
   resting (OFF) state — the QML overlay tints them with the live
   color when active, so the SVG shows them as black SMD bodies
   with at most a subtle border.
7. Each header pad must be a gold-plated RING: gold outer disc
   ~8px wide, dark center ~4px wide. The id-tagged element should
   be the inner dark circle (which the QML overlay highlights when
   the firmware drives that pin HIGH).

## Layout coordinates to preserve exactly

These are pixel coordinates in the 420x160 viewBox. The QML overlay
assumes them — keep them stable.

    led_tx    center @ (332, 56)
    led_rx    center @ (332, 70)
    led_l     center @ (332, 84)
    led_pwr   center @ (332, 98)

    Header pads:
        Top row    centers at  y = 7
        Bottom row centers at  y = 143
        Both rows  centers at  x = 31 + index*26  for index 0..14

    USB connector:    x =   0..36,  y = 64..96
    ATmega328P chip:  x = 180..248, y = 53..107
    Crystal:          x = 260..284, y = 58..72
    Decoupling caps:  x = 260..284, y = 80..89   (two caps side-by-side)
    Reset button:     x = 290..304, y = 56..70
    ICSP header box:  x = 358..394, y = 60..98

## What the SVG should depict (top-down product photo style)

- **PCB body**: teal-green with a vertical gradient `#00695C → #003a30`,
  rounded corners (~6px radius), 1px dark-teal border. Subtle
  horizontal trace lines visible across the empty regions.
- **USB connector**: silver metallic shielding around y=64..96 with
  an inner black port slot, sticking out off the left edge.
- **ATmega328P chip**: TQFP-32 footprint. Dark body with a subtle
  bevel highlight on the top edge, 8 fine pin legs visible on each
  of the four sides extending into the chip footprint.
  "ATMEGA 328P" silkscreen text centered, small light-grey pin-1
  dot in the corner.
- **16 MHz crystal**: oval silver "can" with "16M" silkscreen
  below it.
- **Decoupling caps**: two small tan rectangles below the crystal.
- **Reset button**: small dark square with "RST" silkscreen below.
- **6-pin ICSP header**: 2x3 grid of gold-plated through-holes
  inside a small black plastic frame.
- **On-board status LEDs**: 4 small SMD bodies in a vertical strip
  with their text labels (TX, RX, L, PWR) immediately to the right
  in white silkscreen.
- **Header pads**: 15 top + 15 bottom gold-plated rings with black
  plated through-holes; tiny white silkscreen text labels above
  each top pad and below each bottom pad (D12, D11, ..., GND, VIN).
- **Other SMD components**: scatter small dark rectangles
  (representing additional resistors / capacitors) to suggest
  hardware density without inventing a real schematic.
- **"Arduino Nano"** silkscreen text in the bottom-right corner in
  small italic white type.

The board's overall character should resemble a real Arduino Nano
top-down product photograph, not a circuit-diagram cartoon.

## Output format

A single fenced `xml` code block containing the complete SVG file —
nothing outside the block. No prose, no commentary, no preamble.
Paste-ready into a `.svg` file.
