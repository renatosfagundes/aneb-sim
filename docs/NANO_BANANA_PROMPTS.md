# Nano Banana asset-generation prompts

Generate these PNGs and drop them into
`aneb-ui/aneb_ui/qml_assets/`.

After dropping a new render, **always run** the asset-cleaner to
convert near-white background pixels to true alpha=0:

```bash
.venv/Scripts/python.exe aneb-ui/aneb_ui/qml_assets/process_assets.py
```

## Currently-used assets

### `arduino.png`

```
Top-down product photograph of an Arduino Nano (ATmega328P, 30-pin),
teal-green PCB, mini-USB connector at the left edge, central QFP-32
ATmega328P chip with a small reset button and crystal oscillator
beside it, four small on-board LEDs (PWR / L / TX / RX) clustered
near the chip, gold-plated header pins along top and bottom edges
with white silkscreen labels. Fully transparent background — no
checkerboard, no white fill, alpha=0 everywhere outside the board.
2:1 aspect ratio, 1680x800, neutral white-balance lighting, no
shadow, sharp focus.
```

### `background.png`

```
Top-down photograph of a pale-cyan FR4 PCB substrate, glossy solder
mask, subtle texture, no components, even lighting, white silkscreen
markings around the edge for orientation, photorealistic, 2x retina
resolution. 16:10 or wider aspect. No white fill outside the PCB —
the substrate fills the frame edge-to-edge.
```

## Not currently rendered (TrimPot + PushButton are pure QML)

The TrimPot widget draws itself in QML primitives (a static blue
circle with a brass center and a rotating dark slot) and PushButton
likewise (gradient cap with a bevel that flips on press). No images
needed. If you ever want photo-real versions, the structured
two-layer prompts (body + rotating screw / frame + state-dependent
cap) would be the way — they avoid the diamond-rotation problem
that hits when you spin a single square asset around its center.

## Workflow

1. Copy a prompt above into Nano Banana.
2. Save as the indicated filename in `aneb-ui/aneb_ui/qml_assets/`.
3. Run `python aneb-ui/aneb_ui/qml_assets/process_assets.py`.
4. Add the new filename to that script's `TARGETS` list if not
   already there.
5. Relaunch `./scripts/run-ui.sh`.
