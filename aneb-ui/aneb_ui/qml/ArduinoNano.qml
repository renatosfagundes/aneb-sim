// ArduinoNano.qml — PNG base + JSON-driven overlay coordinates.
//
// The static art comes from `qml_assets/arduino.png`. The position
// of every LED / header pad comes from `qml_assets/arduino-coords.json`
// (produced by `scripts/calibrate-nano.py`). The bridge reads the JSON
// at startup and exposes it as `bridge.nanoCoords` — the QML below
// just looks each item up by name.
//
// All overlay coordinates are normalised (0..1) to image dimensions,
// so the layout stays correct at any rendered size.
import QtQuick 2.15

Item {
    id: root

    property string chip: ""
    property bool   power: false
    property real   txGlow: 0.0
    property real   rxGlow: 0.0

    function pulseTx() { txGlow = 1.0; txDecay.restart() }
    function pulseRx() { rxGlow = 1.0; rxDecay.restart() }

    Timer {
        id: txDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.txGlow *= 0.55; if (root.txGlow < 0.05) { root.txGlow = 0; running = false } }
    }
    Timer {
        id: rxDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.rxGlow *= 0.55; if (root.rxGlow < 0.05) { root.rxGlow = 0; running = false } }
    }

    function level(p) {
        if (!bridge || !bridge.pinStates) return 0
        var c = bridge.pinStates[root.chip]
        return (c && c[p]) ? 1 : 0
    }
    function duty(p) {
        if (!bridge || !bridge.pwmDuties) return 0.0
        var c = bridge.pwmDuties[root.chip]
        return (c && c[p]) ? c[p] : 0.0
    }

    // Bridge-provided coordinate dict. Empty until calibration is done.
    readonly property var coords: (typeof bridge !== "undefined" && bridge && bridge.nanoCoords) ? bridge.nanoCoords : ({})

    // Pad-id -> AVR port mapping (which pin's HIGH state lights this pad).
    // Only digital pins are listed; power / GND / RST entries are absent.
    readonly property var padPort: ({
        "top.d12": "PB4", "top.d11": "PB3", "top.d10": "PB2",
        "top.d9":  "PB1", "top.d8":  "PB0", "top.d7":  "PD7",
        "top.d6":  "PD6", "top.d5":  "PD5", "top.d4":  "PD4",
        "top.d3":  "PD3", "top.d2":  "PD2",
        "top.rx0": "PD0", "top.tx1": "PD1",
        "bot.d13": "PB5",
        "bot.a0":  "PC0", "bot.a1":  "PC1", "bot.a2":  "PC2",
        "bot.a3":  "PC3", "bot.a4":  "PC4", "bot.a5":  "PC5"
    })

    // Match the source image's natural 1500x571 aspect ratio so the
    // parent layout can give the widget the right space.
    implicitWidth:  1500
    implicitHeight: 571

    // ---- The PNG, aspect-fit into the widget ---------------------
    Image {
        id: nano
        anchors.fill: parent
        source: "../qml_assets/arduino.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        antialiasing: true
        // Don't constrain sourceSize — let Qt rasterise at the
        // natural image size and downscale for display. Setting
        // sourceSize to the wrong dimensions distorts overlay math
        // because paintedWidth/Height become inconsistent with what
        // we actually see on screen.
    }

    // The actual rendered area of the PNG inside the widget. With
    // PreserveAspectFit, the image is letterboxed — overlays anchor
    // to this area, not the full widget bounds. We read it straight
    // from the Image element so it stays correct regardless of what
    // the source image's native dimensions are.
    readonly property real _imgW: nano.paintedWidth
    readonly property real _imgH: nano.paintedHeight
    readonly property real _imgX: (width  - _imgW) / 2
    readonly property real _imgY: (height - _imgH) / 2

    // ---- LED overlays --------------------------------------------
    Repeater {
        model: [
            { key: "tx",  brightness: root.txGlow,                        color: "#ff4444" },
            { key: "rx",  brightness: root.rxGlow,                        color: "#ffdd44" },
            { key: "l",   brightness: Math.max(root.level("PB5"), root.duty("PD6")), color: "#ffaa22" },
            { key: "pwr", brightness: root.power ? 1.0 : 0.0,             color: "#22cc44" }
        ]
        OnBoardLedOverlay {
            property var entry: (root.coords.leds && root.coords.leds[modelData.key])
                                ? root.coords.leds[modelData.key] : null
            xNorm: entry ? entry.x   : -1
            yNorm: entry ? entry.y   : -1
            wNorm: entry ? (entry.w !== undefined ? entry.w : 0.014) : 0.014
            hNorm: entry ? (entry.h !== undefined ? entry.h : 0.018) : 0.018
            rotation: entry ? (entry.rot !== undefined ? entry.rot : 0) : 0
            brightness: modelData.brightness
            color: modelData.color
        }
    }

    // ---- Header pad overlays -------------------------------------
    Repeater {
        model: Object.keys(root.padPort)
        PadOverlay {
            property var parts: modelData.split(".")
            property var entry: (root.coords.pads
                                 && root.coords.pads[parts[0]]
                                 && root.coords.pads[parts[0]][parts[1]])
                               ? root.coords.pads[parts[0]][parts[1]] : null
            xNorm: entry ? entry.x : -1
            yNorm: entry ? entry.y : -1
            rNorm: entry ? (entry.r !== undefined ? entry.r : 0.006) : 0.006
            pinPort: root.padPort[modelData]
        }
    }

    // ---- Components ----------------------------------------------
    component OnBoardLedOverlay: Item {
        id: lo
        property real xNorm: -1
        property real yNorm: -1
        property real wNorm: 0.014
        property real hNorm: 0.018
        property real brightness: 0
        property color color: "#ffaa22"

        visible: xNorm >= 0 && yNorm >= 0

        // Floor the *smaller* dimension at MIN_DIM and scale both
        // axes by the same factor, so the calibrated aspect ratio
        // (and therefore orientation) is preserved at every render
        // size. Earlier we floored width and height independently,
        // which flipped landscape SMD LEDs into portrait squares
        // when the panel was tight.
        readonly property real _baseW: wNorm * root._imgW
        readonly property real _baseH: hNorm * root._imgH
        readonly property real _minDim: 8
        readonly property real _scale: Math.max(1.0, _minDim / Math.max(1, Math.min(_baseW, _baseH)))
        x: root._imgX + xNorm * root._imgW - width  / 2
        y: root._imgY + yNorm * root._imgH - height / 2
        width:  _baseW * _scale
        height: _baseH * _scale
        transformOrigin: Item.Center

        // Halo (only when bright).
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 2.4
            height: parent.height * 2.0
            radius: Math.min(width, height) / 2
            color: lo.color
            opacity: 0.5 * lo.brightness
            visible: lo.brightness > 0.05
        }
        // SMD body — always visible. Dim grey when off so the
        // calibrated position is recognizable; colored at brightness
        // when on.
        Rectangle {
            anchors.fill: parent
            radius: 1.5
            color: lo.brightness > 0.05 ? lo.color : "#2a2a2a"
            opacity: lo.brightness > 0.05 ? lo.brightness : 0.55
            border.color: "#101010"; border.width: 1
            Behavior on color   { ColorAnimation  { duration: 60 } }
            Behavior on opacity { NumberAnimation { duration: 60 } }
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left
                anchors.topMargin:  parent.height * 0.10
                anchors.leftMargin: parent.width  * 0.18
                width:  parent.width  * 0.40
                height: parent.height * 0.18
                radius: 1
                color: "white"
                opacity: lo.brightness * 0.6
                visible: lo.brightness > 0.05
            }
        }
    }

    component PadOverlay: Item {
        id: po
        property real xNorm: -1
        property real yNorm: -1
        property real rNorm: 0.006
        property string pinPort: ""

        visible: xNorm >= 0 && yNorm >= 0

        function _level() {
            if (!po.pinPort) return 0
            return Math.max(root.level(po.pinPort), root.duty(po.pinPort))
        }

        // Compute the dot's pixel-space center, then snap it to the
        // nearest integer pixel before deriving x/y. Without rounding,
        // a fractional center smears the dot across two pixels via
        // antialiasing — and because Qt's rasteriser doesn't always
        // distribute that smear symmetrically, the dot can look like
        // it sits ~half a pixel off-center. Using a fixed even width
        // (so width/2 is integer) keeps left/right edges aligned to
        // the same pixel grid as the snapped center.
        // Force the size to be even, so width/2 stays an integer
        // and the bounding-box edges align to the same pixel grid as
        // the snapped center. An odd size (e.g. 5 px) would mean
        // x = _cx - 2.5, putting the rectangle on half-pixel
        // boundaries and causing Qt's antialiasing to render the
        // left/right edges with subtly different intensities — the
        // dot then *looks* offset even though its center is correct.
        readonly property real _baseSize: rNorm * root._imgW * 2
        readonly property int  _size: Math.max(4, 2 * Math.round(_baseSize / 2))
        readonly property int  _cx: Math.round(root._imgX + xNorm * root._imgW)
        readonly property int  _cy: Math.round(root._imgY + yNorm * root._imgH)
        x: _cx - _size / 2
        y: _cy - _size / 2
        width:  _size
        height: _size

        // Halo when HIGH.
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 1.8
            height: parent.height * 1.8
            radius: width / 2
            color: "#3cff5a"
            opacity: po._level() * 0.5
            visible: po._level() > 0.05
        }
        // Pad dot — always visible. Faint when LOW so you can see
        // every calibrated pad even before any firmware drives a pin;
        // bright green when the firmware drives the pin HIGH (matches
        // the breakout-LED convention used elsewhere in the panel).
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: po._level() > 0.05 ? "#3cff5a" : "#1a1a1a"
            opacity: po._level() > 0.05
                ? Math.max(0.95 * po._level(), 0.4)
                : 0.35
            border.color: "#0a0a0a"; border.width: 0.5
            Behavior on color   { ColorAnimation  { duration: 60 } }
            Behavior on opacity { NumberAnimation { duration: 60 } }
        }
    }
}
