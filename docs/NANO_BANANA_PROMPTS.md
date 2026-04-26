# Nano Banana asset-generation prompts

Generate these PNGs and drop them into
`aneb-ui/aneb_ui/qml_assets/`. Each one targets a specific QML
layer; the structure (separate body + screw, separate frame +
cap-up + cap-down) is what makes the rotation and press-state
animations clean instead of fighting with rotated square corners.

After generation, **always run** the asset-cleaner to convert the
near-white background pixels to true alpha=0:

```bash
.venv/Scripts/python.exe aneb-ui/aneb_ui/qml_assets/process_assets.py
```

(Or update its `TARGETS` list to include any new files.)

---

## TrimPot — two layers

### `trimpot_body.png` — static body

```
Top-down product photograph of a single 10kΩ horizontal trim potentiometer
(Bourns 3362 single-turn style), viewed from directly above (90°
perpendicular axis, orthographic projection, like a CAD top view).
Square cyan-blue plastic body. Three short brass solder leads protruding
from one edge. CRITICAL: the center adjustment screw is NOT present in
this image — leave a clean circular hole in the middle of the blue body
where the brass disc would normally sit, and that hole must be fully
transparent (alpha=0). The blue body itself has its outer corners
rounded slightly. No PCB beneath, no traces, no silkscreen labels, no
frame, no surrounding decoration. Fully transparent background.
Square 1:1, 512x512, soft top lighting, no drop shadow.
```

### `trimpot_screw.png` — rotating brass screw

```
Top-down close-up photograph of just the brass slotted screw of a trim
potentiometer, perfectly centered. Round brass disc, single horizontal
slot across the diameter for a flathead screwdriver. Slight bevel on the
edge, fine concentric machining marks visible. Nothing else — no blue
body, no leads, no PCB. Fully transparent background. Square 1:1, 512x512,
soft top lighting, orthographic projection.
```

---

## PushButton — three images (frame + two cap states)

### `button_frame.png` — static metal frame

```
Top-down product photograph of a 12mm tactile-switch metal frame, viewed
from directly above. Square stainless-steel collar with rounded corners,
four short solder pins protruding diagonally from the four corners. The
center is a clean circular hole (alpha=0 transparent) where the cap
sits. NO black plastic cap — leave the center hole open. No PCB beneath,
no traces. Fully transparent background. Square 1:1, 512x512, neutral
lighting, no drop shadow.
```

### `button_cap_up.png` — released state

```
Top-down close-up of a tactile-switch black plastic cap in its raised
position. Slightly domed top with a small specular highlight on the
upper-left, soft rounded corners. The cap is matte black with a hint
of warm-grey shading on the bevel edges. Fully transparent background,
nothing surrounds the cap. Square 1:1, 512x512.
```

### `button_cap_down.png` — pressed state

```
Same matte-black tactile-switch cap as `button_cap_up.png`, but pressed
in: the cap appears recessed, the bevel inverted (shadow on top-left,
highlight on bottom-right), and the cap face slightly darker overall to
suggest it's been pushed down into the frame. Fully transparent
background, square 1:1, 512x512.
```

---

## Already-correct assets (don't regenerate unless you want to retune)

- `arduino.png` — Arduino Nano top-down. Already cleaned by
  `process_assets.py`.
- `background.png` — pale-cyan FR4 PCB substrate. Used as the main
  window backdrop.

If you do regenerate `arduino.png` and want crisper results, the
prompt is:

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

---

## Workflow checklist

1. Copy a prompt above into Nano Banana.
2. Save the result as the indicated filename in
   `aneb-ui/aneb_ui/qml_assets/`.
3. Run `python aneb-ui/aneb_ui/qml_assets/process_assets.py` to
   chroma-key any opaque white background to alpha=0.
4. Add the new filename to that script's `TARGETS` list if it's not
   already there.
5. Relaunch `./scripts/run-ui.sh` — the QML auto-loads any
   asset that has been added.

The widgets fall back to drawn QML primitives if an asset is missing,
so the UI works at every step of the regeneration cycle.
