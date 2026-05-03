# Screenshot tool

Renders aneb-sim's QML panes off-screen with sample data and saves
them as PNGs under `docs/images/` for the manual.

## Usage

```bash
# Activate the same venv run-ui.sh uses:
PATH="/c/msys64/mingw64/bin:$PATH" .venv/Scripts/python.exe scripts/screenshots/snap.py
```

Or to render a single scene:

```bash
.venv/Scripts/python.exe scripts/screenshots/snap.py --scenes serial_demo
```

## How it works

1. `faux_bridge.py` defines a `FauxBridge` `QObject` with the same
   signals/slots/properties as the real `QmlBridge`, populated with
   plausible sample data (chip stats, UART catalogue, plot waveforms).
2. For each `*.qml` file under `scenes/`, the driver:
   - creates a `QQuickView`,
   - sets `bridge` as a context property to `FauxBridge`,
   - loads the scene,
   - waits for `Status.Ready`,
   - calls `grabToImage()` (async — pumps the event loop until the
     `ready` signal fires),
   - saves the resulting `QImage` as PNG.
3. Each scene runs in its own subprocess (`--no-isolate` to disable)
   so a crash in one (looking at you, QtCharts) doesn't lose the batch.

The `Basic` QtQuick.Controls style is forced via `QT_QUICK_CONTROLS_STYLE`
because the bundled PyQt6 wheel doesn't ship the Windows-native style
plugin DLL that the live UI happens to find at runtime.

## Adding a scene

Drop a `*.qml` under `scenes/` that imports the target component from
`../../../aneb-ui/aneb_ui/qml`, sets `width` / `height`, and optionally
populates state via `Component.onCompleted`.  Example:

```qml
import QtQuick 2.15
import "../../../aneb-ui/aneb_ui/qml" as Aneb

Rectangle {
    width: 600; height: 380
    color: "#0a1a14"
    Aneb.SerialConsole {
        anchors.fill: parent
        chip: "ecu1"; label: "ECU 1"
    }
    Component.onCompleted: bridge.uartAppended("ecu1", "hello\n")
}
```

## Known limits — manual screenshots needed for these

- **Plotter** (`Plotter.qml`): QtCharts' `ChartView` segfaults during
  teardown when hosted in a non-main `QQuickView` on Windows under
  PyQt6 (the threaded scene-graph + GPU teardown path).  Software
  backend (`QT_QUICK_BACKEND=software`) doesn't help.  Capture from
  the live app via Snipping Tool / Print Screen.
- **EcuPanel**: the panel embeds `PlotterWindow` inline, which loads
  ChartView eagerly even when invisible — so the same crash hits.
- **Main board view**: relies on the engine being live to populate
  per-chip state.  Easier to grab from a running `./scripts/run-ui.sh`.

The `scenes/` folder is the contract for what we render programmatically;
anything else is a Snipping Tool job.
